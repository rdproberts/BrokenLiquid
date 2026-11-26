#include "doc/PartFile.h"

#include "bl/Version.h"

#include <BinTools.hxx>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <sstream>

namespace bl::doc {
namespace {

using nlohmann::json;

std::string str(const QString& s) { return s.toStdString(); }
QString qstr(const json& j, const char* key, const QString& fallback = {}) {
    return j.contains(key) ? QString::fromStdString(j.at(key).get<std::string>()) : fallback;
}
std::string idOf(const QUuid& id) { return id.toString(QUuid::WithoutBraces).toStdString(); }
QUuid uuid(const json& j, const char* key) {
    return j.contains(key) ? QUuid::fromString(QString::fromStdString(j.at(key).get<std::string>())) : QUuid();
}

json xyz(const gp_XYZ& v) { return json::array({v.X(), v.Y(), v.Z()}); }
gp_XYZ toXyz(const json& j) { return gp_XYZ(j.at(0).get<double>(), j.at(1).get<double>(), j.at(2).get<double>()); }

const char* endName(EndCondition e) {
    switch (e) {
    case EndCondition::Blind: return "blind";
    case EndCondition::ThroughAll: return "throughAll";
    case EndCondition::MidPlane: return "midPlane";
    }
    return "blind";
}
EndCondition endFrom(const std::string& s) {
    if (s == "throughAll") {
        return EndCondition::ThroughAll;
    }
    if (s == "midPlane") {
        return EndCondition::MidPlane;
    }
    return EndCondition::Blind;
}

const char* statusKey(sketch::SketchStatus s) {
    switch (s) {
    case sketch::SketchStatus::UnderDefined: return "under";
    case sketch::SketchStatus::FullyDefined: return "full";
    case sketch::SketchStatus::OverDefined: return "over";
    }
    return "under";
}
sketch::SketchStatus statusFrom(const std::string& s) {
    if (s == "full") {
        return sketch::SketchStatus::FullyDefined;
    }
    if (s == "over") {
        return sketch::SketchStatus::OverDefined;
    }
    return sketch::SketchStatus::UnderDefined;
}

json faceRefJson(const kernel::FaceRef& r) {
    const kernel::FaceSignature& s = r.signature;
    return {{"name", r.name},
            {"surface", s.surface},
            {"centroid", xyz(s.centroid.XYZ())},
            {"normal", xyz(s.normal.XYZ())},
            {"planar", s.planar},
            {"area", s.area}};
}
kernel::FaceRef faceRefFrom(const json& j) {
    kernel::FaceRef r;
    r.name = j.value("name", std::string());
    r.signature.surface = j.value("surface", -1);
    r.signature.centroid = gp_Pnt(toXyz(j.at("centroid")));
    r.signature.normal = gp_Dir(toXyz(j.at("normal")));
    r.signature.planar = j.value("planar", false);
    r.signature.area = j.value("area", 0.0);
    return r;
}

json edgeRefJson(const kernel::EdgeRef& r) {
    const kernel::EdgeSignature& s = r.signature;
    return {{"faces", json::array({r.face1, r.face2})},
            {"curve", s.curve},
            {"middle", xyz(s.middle.XYZ())},
            {"direction", xyz(s.direction.XYZ())},
            {"straight", s.straight},
            {"length", s.length}};
}
kernel::EdgeRef edgeRefFrom(const json& j) {
    kernel::EdgeRef r;
    const json& faces = j.at("faces");
    r.face1 = faces.at(0).get<std::string>();
    r.face2 = faces.at(1).get<std::string>();
    r.signature.curve = j.value("curve", -1);
    r.signature.middle = gp_Pnt(toXyz(j.at("middle")));
    r.signature.direction = gp_Dir(toXyz(j.at("direction")));
    r.signature.straight = j.value("straight", false);
    r.signature.length = j.value("length", 0.0);
    return r;
}
json edgesJson(const std::vector<kernel::EdgeRef>& edges) {
    json out = json::array();
    for (const kernel::EdgeRef& e : edges) {
        out.push_back(edgeRefJson(e));
    }
    return out;
}
std::vector<kernel::EdgeRef> edgesFrom(const json& j) {
    std::vector<kernel::EdgeRef> out;
    if (j.contains("edges")) {
        for (const json& e : j.at("edges")) {
            out.push_back(edgeRefFrom(e));
        }
    }
    return out;
}

QByteArray shapeBytes(const TopoDS_Shape& shape) {
    std::ostringstream out(std::ios::binary);
    BinTools::Write(shape, out);
    const std::string s = out.str();
    return QByteArray(s.data(), static_cast<qsizetype>(s.size()));
}
TopoDS_Shape shapeFrom(const QByteArray& bytes) {
    std::istringstream in(std::string(bytes.constData(), static_cast<size_t>(bytes.size())), std::ios::binary);
    TopoDS_Shape shape;
    BinTools::Read(shape, in);
    return shape;
}

// Brings model.json up to the current schema (one step per version). Nothing to do yet.
void migrate(json& /*model*/, int /*fromSchema*/) {}

} // namespace

bool savePart(const PartModel& m, const QString& path, QString* error) {
    auto fail = [error](const QString& text) {
        if (error) {
            *error = text;
        }
        return false;
    };
    json model;
    model["units"] = {{"system", units::systemName(m.units.system)},
                      {"decimals", m.units.decimals},
                      {"angleDecimals", m.units.angleDecimals}};
    model["counters"] = {{"import", m.importCount},   {"sketch", m.sketchCount},
                         {"extrude", m.extrudeCount}, {"cut", m.cutCount},
                         {"revolve", m.revolveCount}, {"revolveCut", m.revolveCutCount},
                         {"fillet", m.filletCount},   {"chamfer", m.chamferCount}};
    model["rollback"] = m.rollback;

    std::vector<std::pair<QString, QByteArray>> shapes;
    json features = json::array();
    for (const Feature& f : m.features) {
        json j{{"id", idOf(f.id)}, {"name", str(f.name)}, {"suppressed", f.suppressed}};
        if (!f.body.isNull()) {
            j["body"] = idOf(f.body);
        }
        switch (f.type) {
        case FeatureType::Import: {
            const QString file = QStringLiteral("shapes/%1.brep").arg(f.id.toString(QUuid::WithoutBraces));
            shapes.emplace_back(file, shapeBytes(f.import.shape));
            j["type"] = "import";
            j["shape"] = str(file);
            j["sourceFile"] = str(f.import.sourceFile);
            j["bodyName"] = str(f.import.bodyName);
            break;
        }
        case FeatureType::Sketch: j["type"] = "sketch"; break;
        case FeatureType::Extrude: {
            const ExtrudeParams& p = f.extrude;
            j["type"] = "extrude";
            j["sketch"] = idOf(p.sketch);
            j["cut"] = p.cut;
            j["end"] = endName(p.end);
            j["depth"] = p.depth;
            j["reverse"] = p.reverse;
            j["direction2"] = p.direction2;
            j["depth2"] = p.depth2;
            j["merge"] = p.merge;
            break;
        }
        case FeatureType::Revolve: {
            const RevolveParams& p = f.revolve;
            j["type"] = "revolve";
            j["sketch"] = idOf(p.sketch);
            j["cut"] = p.cut;
            j["axis"] = p.axis;
            j["angle"] = p.angle;
            j["reverse"] = p.reverse;
            j["midPlane"] = p.midPlane;
            j["merge"] = p.merge;
            break;
        }
        case FeatureType::Fillet:
            j["type"] = "fillet";
            j["radius"] = f.fillet.radius;
            j["edges"] = edgesJson(f.fillet.edges);
            break;
        case FeatureType::Chamfer: {
            const ChamferParams& p = f.chamfer;
            j["type"] = "chamfer";
            j["chamferType"] = p.type == ChamferType::TwoDistances ? "twoDistances" : "distance";
            j["distance"] = p.distance;
            j["distance2"] = p.distance2;
            j["flip"] = p.flip;
            j["edges"] = edgesJson(p.edges);
            break;
        }
        }
        features.push_back(j);
    }
    model["features"] = features;

    json sketches = json::array();
    for (const SketchFeature& s : m.sketches) {
        json j{{"id", idOf(s.id)},
               {"name", str(s.name)},
               {"planeName", str(s.planeName)},
               {"plane",
                {{"origin", xyz(s.plane.Location().XYZ())},
                 {"normal", xyz(s.plane.Direction().XYZ())},
                 {"x", xyz(s.plane.XDirection().XYZ())}}},
               {"visible", s.visible},
               {"status", statusKey(s.status)},
               {"sketch", s.sketch.toJson()}};
        if (s.face) {
            j["face"] = faceRefJson(*s.face);
        }
        sketches.push_back(j);
    }
    model["sketches"] = sketches;

    json bodies = json::array();
    for (const Body& b : m.bodies) {
        json j{{"id", idOf(b.id)}, {"name", str(b.name)}, {"visible", b.visible}};
        if (b.color) {
            j["color"] = str(b.color->name(QColor::HexRgb));
        }
        bodies.push_back(j);
    }
    model["bodies"] = bodies;

    const json manifest{{"format", "BrokenLiquid"},
                        {"type", "part"},
                        {"schema", kPartSchema},
                        {"app", kVersion},
                        {"build", kBuildId},
                        {"saved", str(QDateTime::currentDateTimeUtc().toString(Qt::ISODate))}};

    const QString temp = path + QStringLiteral(".saving");
    {
        QZipWriter zip(temp);
        if (zip.status() != QZipWriter::NoError) {
            return fail(QObject::tr("Cannot write %1.").arg(temp));
        }
        zip.setCompressionPolicy(QZipWriter::AutoCompress);
        zip.addFile(QStringLiteral("manifest.json"), QByteArray::fromStdString(manifest.dump(2)));
        zip.addFile(QStringLiteral("model.json"), QByteArray::fromStdString(model.dump(1)));
        for (const auto& [name, bytes] : shapes) {
            zip.addFile(name, bytes);
        }
        zip.close();
        if (zip.status() != QZipWriter::NoError) {
            QFile::remove(temp);
            return fail(QObject::tr("Writing %1 failed.").arg(temp));
        }
    }
    const QString backup = path + QStringLiteral(".bak");
    const bool replacing = QFileInfo::exists(path);
    if (replacing) {
        QFile::remove(backup);
        if (!QFile::rename(path, backup)) {
            QFile::remove(temp);
            return fail(QObject::tr("Cannot replace %1 (is it open elsewhere?).").arg(path));
        }
    }
    if (!QFile::rename(temp, path)) {
        if (replacing) {
            QFile::rename(backup, path); // put the previous version back
        }
        return fail(QObject::tr("Cannot move the saved file to %1; it is kept as %2.").arg(path, temp));
    }
    spdlog::info("saved part {} ({} features)", path.toStdString(), m.features.size());
    return true;
}

LoadedPart loadPart(const QString& path) {
    LoadedPart out;
    QZipReader zip(path);
    if (!zip.isReadable() || zip.status() != QZipReader::NoError) {
        out.error = QObject::tr("%1 is not a BrokenLiquid part file.").arg(QFileInfo(path).fileName());
        return out;
    }
    try {
        const json manifest = json::parse(zip.fileData(QStringLiteral("manifest.json")).toStdString());
        if (manifest.value("format", std::string()) != "BrokenLiquid" || manifest.value("type", std::string()) != "part") {
            out.error = QObject::tr("%1 is not a BrokenLiquid part file.").arg(QFileInfo(path).fileName());
            return out;
        }
        const int schema = manifest.value("schema", 0);
        if (schema > kPartSchema) {
            out.error = QObject::tr("%1 was made with a newer version of BrokenLiquid (%2). Update to open it.")
                            .arg(QFileInfo(path).fileName(), QString::fromStdString(manifest.value("app", std::string("?"))));
            return out;
        }
        json model = json::parse(zip.fileData(QStringLiteral("model.json")).toStdString());
        migrate(model, schema);

        PartModel& m = out.model;
        const json& u = model.at("units");
        m.units.system = units::systemFromName(u.value("system", std::string("mm"))).value_or(UnitSystem::Millimetre);
        m.units.decimals = u.value("decimals", 2);
        m.units.angleDecimals = u.value("angleDecimals", 2);
        const json& c = model.at("counters");
        m.importCount = c.value("import", 0);
        m.sketchCount = c.value("sketch", 0);
        m.extrudeCount = c.value("extrude", 0);
        m.cutCount = c.value("cut", 0);
        m.revolveCount = c.value("revolve", 0);
        m.revolveCutCount = c.value("revolveCut", 0);
        m.filletCount = c.value("fillet", 0);
        m.chamferCount = c.value("chamfer", 0);
        m.rollback = model.value("rollback", -1);

        for (const json& j : model.at("features")) {
            Feature f;
            f.id = uuid(j, "id");
            f.name = qstr(j, "name");
            f.suppressed = j.value("suppressed", false);
            f.body = uuid(j, "body");
            const std::string type = j.at("type").get<std::string>();
            if (type == "import") {
                f.type = FeatureType::Import;
                f.import.shape = shapeFrom(zip.fileData(qstr(j, "shape")));
                f.import.sourceFile = qstr(j, "sourceFile");
                f.import.bodyName = qstr(j, "bodyName");
                if (f.import.shape.IsNull()) {
                    throw std::runtime_error("the shape of " + j.value("name", std::string()) + " is missing");
                }
            } else if (type == "sketch") {
                f.type = FeatureType::Sketch;
            } else if (type == "extrude") {
                f.type = FeatureType::Extrude;
                ExtrudeParams& p = f.extrude;
                p.sketch = uuid(j, "sketch");
                p.cut = j.value("cut", false);
                p.end = endFrom(j.value("end", std::string("blind")));
                p.depth = j.value("depth", 10.0);
                p.reverse = j.value("reverse", false);
                p.direction2 = j.value("direction2", false);
                p.depth2 = j.value("depth2", 10.0);
                p.merge = j.value("merge", true);
            } else if (type == "revolve") {
                f.type = FeatureType::Revolve;
                RevolveParams& p = f.revolve;
                p.sketch = uuid(j, "sketch");
                p.cut = j.value("cut", false);
                p.axis = j.value("axis", sketch::Id{0});
                p.angle = j.value("angle", 360.0);
                p.reverse = j.value("reverse", false);
                p.midPlane = j.value("midPlane", false);
                p.merge = j.value("merge", true);
            } else if (type == "fillet") {
                f.type = FeatureType::Fillet;
                f.fillet.radius = j.value("radius", 2.0);
                f.fillet.edges = edgesFrom(j);
            } else if (type == "chamfer") {
                f.type = FeatureType::Chamfer;
                ChamferParams& p = f.chamfer;
                p.type = j.value("chamferType", std::string("distance")) == "twoDistances" ? ChamferType::TwoDistances
                                                                                         : ChamferType::Distance;
                p.distance = j.value("distance", 1.0);
                p.distance2 = j.value("distance2", 1.0);
                p.flip = j.value("flip", false);
                p.edges = edgesFrom(j);
            } else {
                throw std::runtime_error("unknown feature type " + type);
            }
            m.features.push_back(std::move(f));
        }
        for (const json& j : model.at("sketches")) {
            SketchFeature s;
            s.id = uuid(j, "id");
            s.name = qstr(j, "name");
            s.planeName = qstr(j, "planeName");
            const json& pl = j.at("plane");
            s.plane = gp_Ax3(gp_Pnt(toXyz(pl.at("origin"))), gp_Dir(toXyz(pl.at("normal"))), gp_Dir(toXyz(pl.at("x"))));
            if (j.contains("face")) {
                s.face = faceRefFrom(j.at("face"));
            }
            s.visible = j.value("visible", true);
            s.status = statusFrom(j.value("status", std::string("under")));
            std::string error;
            if (!sketch::Sketch::fromJson(j.at("sketch"), s.sketch, &error)) {
                throw std::runtime_error(str(s.name) + ": " + error);
            }
            m.sketches.push_back(std::move(s));
        }
        for (const json& j : model.at("bodies")) {
            Body b;
            b.id = uuid(j, "id");
            b.name = qstr(j, "name");
            b.visible = j.value("visible", true);
            if (j.contains("color")) {
                b.color = QColor(qstr(j, "color"));
            }
            m.bodies.push_back(std::move(b)); // names and colours; the rebuild adds the shapes
        }
        out.ok = true;
    } catch (const std::exception& e) {
        out.error = QObject::tr("%1 could not be read: %2").arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what()));
        out.model = {};
    }
    return out;
}

} // namespace bl::doc
