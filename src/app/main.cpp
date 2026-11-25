// BrokenLiquid entry point: splash -> settings/logging -> kernel -> main window (PLAN.md 8.3).

#include "app/SingleInstance.h"
#include "app/SplashScreen.h"
#include "bl/Version.h"
#include "core/CrashHandler.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <Standard_Version.hxx>

#include <spdlog/spdlog.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QIcon>
#include <QTimer>

namespace {

// Routes OCCT kernel messages into our log.
class SpdlogPrinter : public Message_Printer {
    DEFINE_STANDARD_RTTI_INLINE(SpdlogPrinter, Message_Printer)
protected:
    void send(const TCollection_AsciiString& text, const Message_Gravity gravity) const override {
        switch (gravity) {
        case Message_Trace: spdlog::trace("[occt] {}", text.ToCString()); break;
        case Message_Info: spdlog::debug("[occt] {}", text.ToCString()); break;
        case Message_Warning: spdlog::warn("[occt] {}", text.ToCString()); break;
        default: spdlog::error("[occt] {}", text.ToCString()); break;
        }
    }
};

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QString::fromLatin1(bl::kAppName));
    QApplication::setOrganizationName(QString::fromLatin1(bl::kOrgName));
    QApplication::setApplicationVersion(QString::fromLatin1(bl::kVersion));

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("BrokenLiquid - free, local, parametric 3D CAD"));
    cli.addHelpOption();
    cli.addVersionOption();
    const QCommandLineOption noSplash(QStringLiteral("nosplash"), QStringLiteral("Start without the splash screen."));
    const QCommandLineOption smoke(QStringLiteral("smoke-test"),
                                   QStringLiteral("Open, render, save a window screenshot to <png> and exit."),
                                   QStringLiteral("png"));
    const QCommandLineOption newInstance(QStringLiteral("new-instance"),
                                         QStringLiteral("Do not hand files to an already running window."));
    const QCommandLineOption splashShot(QStringLiteral("splash-shot"),
                                        QStringLiteral("Show only the splash screen, save it to <png> and exit."),
                                        QStringLiteral("png"));
    cli.addOptions({noSplash, smoke, newInstance, splashShot});
    cli.addPositionalArgument(QStringLiteral("files"), QStringLiteral("STEP files to open."), QStringLiteral("[files...]"));
    cli.process(app);

    QStringList files;
    for (const QString& f : cli.positionalArguments()) {
        files << QFileInfo(f).absoluteFilePath();
    }
    const bool smokeTest = cli.isSet(smoke);

    // Splash preview (design checks and docs): render it mid-startup, save, exit.
    if (cli.isSet(splashShot)) {
        auto* shot = new bl::app::SplashScreen();
        shot->showAnimated();
        shot->setStatus(QStringLiteral("Loading geometry kernel…"), 0.45);
        const QString png = cli.value(splashShot);
        QTimer::singleShot(1200, shot, [shot, png] {
            const bool ok = shot->grab().save(png);
            QCoreApplication::exit(ok ? 0 : 2);
        });
        return app.exec();
    }

    // 1. Single instance: hand files to a running window and exit.
    bl::app::SingleInstance instance;
    if (!smokeTest && !cli.isSet(newInstance) && instance.forwardToRunningInstance(files)) {
        return 0;
    }

    // 2. Splash as early as possible.
    QElapsedTimer startup;
    startup.start();
    bl::Settings& settings = bl::Settings::global();
    QPointer<bl::app::SplashScreen> splash;
    if (!cli.isSet(noSplash) && !smokeTest) {
        splash = new bl::app::SplashScreen();
        splash->showAnimated();
        splash->setStatus(QStringLiteral("Starting…"), 0.05);
    }
    auto status = [&](const QString& text, double progress) {
        if (splash) {
            splash->setStatus(text, progress);
        }
    };

    // 3. Settings, logging, crash handler.
    status(QStringLiteral("Loading settings…"), 0.15);
    bl::log::init(bl::paths::logDir());
    bl::crash::install(bl::paths::crashDumpDir());
    settings.load(bl::paths::settingsFile());

    // 4. Geometry kernel.
    status(QStringLiteral("Loading geometry kernel…"), 0.35);
    Message::DefaultMessenger()->RemovePrinters(STANDARD_TYPE(Message_Printer));
    Message::DefaultMessenger()->AddPrinter(new SpdlogPrinter());
    spdlog::info("Open CASCADE {} · Qt {}", OCC_VERSION_COMPLETE, qVersion());

    // 5. User interface.
    status(QStringLiteral("Building workspace…"), 0.6);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));
    auto* window = new bl::ui::MainWindow();
    window->setAttribute(Qt::WA_DeleteOnClose);

    instance.listen();
    QObject::connect(&instance, &bl::app::SingleInstance::filesReceived, window, [window](const QStringList& received) {
        window->showNormal();
        window->raise();
        window->activateWindow();
        if (!received.isEmpty()) {
            window->openFile(received.front());
        }
    });

    status(QStringLiteral("Starting 3D view…"), 0.85);
    if (smokeTest) {
        window->runSmokeTest(cli.value(smoke));
    }

    // 6. Show the window; keep the splash up at least ~700 ms so it never just flashes.
    const qint64 minimumMs = 700;
    const qint64 wait = splash ? std::max<qint64>(0, minimumMs - startup.elapsed()) : 0;
    QTimer::singleShot(wait, window, [window, splash, files, status] {
        status(QStringLiteral("Ready"), 1.0);
        window->show();
        if (splash) {
            splash->finish();
        }
        if (!files.isEmpty()) {
            window->openFile(files.front());
        }
        spdlog::info("main window shown");
    });

    const int rc = app.exec();
    settings.save();
    bl::log::shutdown();
    return rc;
}
