configure_file(core/Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/bl/Version.h @ONLY)

# --- core: Qt-free foundation (logging, settings, paths, crash handling) ----------------------------
add_library(bl_core STATIC
    core/Log.cpp
    core/Paths.cpp
    core/Settings.cpp
    core/CrashHandler.cpp
    core/Units.cpp           core/Units.h
    core/UndoHistory.h)
add_library(bl::core ALIAS bl_core)
target_include_directories(bl_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(bl_core
    PUBLIC  bl::options spdlog::spdlog nlohmann_json::nlohmann_json
    PRIVATE $<$<PLATFORM_ID:Windows>:dbghelp>)
set_target_properties(bl_core PROPERTIES AUTOMOC OFF)

# --- sketch: 2D sketch model and constraint solver (Qt- and OCCT-free) --------------------------------
add_library(bl_sketch STATIC
    sketch/Sketch.cpp        sketch/Sketch.h
    sketch/SketchJson.cpp
    sketch/Solver.cpp        sketch/Solver.h
    sketch/Operations.cpp sketch/Operations.h
    sketch/Geometry.cpp      sketch/Geometry.h)
add_library(bl::sketch ALIAS bl_sketch)
target_link_libraries(bl_sketch PUBLIC bl::core Eigen3::Eigen)
set_target_properties(bl_sketch PROPERTIES AUTOMOC OFF)

# --- kernel: OCCT services (import/export, later naming + shape cache) ----------------------------
add_library(bl_kernel STATIC
    kernel/StepIO.cpp
    kernel/ShapeInfo.cpp
    kernel/Measure.cpp       kernel/Measure.h
    kernel/Naming.cpp        kernel/Naming.h
    kernel/Features.cpp      kernel/Features.h)
add_library(bl::kernel ALIAS bl_kernel)
target_link_libraries(bl_kernel PUBLIC bl::core bl::occt bl::sketch)
set_target_properties(bl_kernel PROPERTIES AUTOMOC OFF)

# --- doc: documents, their models and undo history ---------------------------------------------------
add_library(bl_doc STATIC
    doc/Document.cpp         doc/Document.h
    doc/Rebuild.cpp          doc/Rebuild.h
    doc/PartFile.cpp         doc/PartFile.h
    doc/PartModel.h)
add_library(bl::doc ALIAS bl_doc)
target_link_libraries(bl_doc PUBLIC bl::kernel bl::sketch Qt6::Core Qt6::Gui PRIVATE Qt6::CorePrivate)

# --- render: OCCT viewer inside Qt ------------------------------------------------------------------
add_library(bl_render STATIC
    render/OcctViewer.cpp
    render/NativeWindow.cpp  render/NativeWindow.h
    render/OcctViewer.h
    render/SketchPresentation.cpp render/SketchPresentation.h)
add_library(bl::render ALIAS bl_render)
target_link_libraries(bl_render
    PUBLIC  bl::kernel bl::doc Qt6::Widgets Qt6::OpenGLWidgets
    PRIVATE Qt6::OpenGL $<$<PLATFORM_ID:Windows>:opengl32>)

# --- ui: main window and panels ---------------------------------------------------------------------
add_library(bl_ui STATIC
    ui/CommandRegistry.cpp   ui/CommandRegistry.h
    ui/Icons.cpp             ui/Icons.h
    ui/Theme.cpp             ui/Theme.h
    ui/CommandBar.cpp        ui/CommandBar.h
    ui/FeatureTreePanel.cpp  ui/FeatureTreePanel.h
    ui/ResourcePane.cpp      ui/ResourcePane.h
    ui/ViewHud.cpp           ui/ViewHud.h
    ui/PropertyPanel.cpp     ui/PropertyPanel.h
    ui/ConfirmationCorner.cpp ui/ConfirmationCorner.h
    ui/InteractiveCommand.h
    ui/DocumentView.cpp      ui/DocumentView.h
    ui/MeasureCommand.cpp    ui/MeasureCommand.h
    ui/SectionCommand.cpp    ui/SectionCommand.h
    ui/SketchEditor.cpp      ui/SketchEditor.h
    ui/ExtrudeCommand.cpp    ui/ExtrudeCommand.h
    ui/RevolveCommand.cpp    ui/RevolveCommand.h
    ui/EdgeFeatureCommand.cpp ui/EdgeFeatureCommand.h
    ui/HomePage.cpp          ui/HomePage.h
    ui/OptionsDialog.cpp     ui/OptionsDialog.h
    ui/MainWindow.cpp        ui/MainWindow.h)
add_library(bl::ui ALIAS bl_ui)
target_link_libraries(bl_ui PUBLIC bl::render Qt6::Widgets Qt6::Svg Qt6::Concurrent)

# --- app: executable --------------------------------------------------------------------------------
file(GLOB BL_ICON_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/resources/icons/*.svg)
set(BL_APP_ICON "${CMAKE_SOURCE_DIR}/resources/app/BrokenLiquid.ico")
string(REPLACE "/" "\\\\" BL_APP_ICON "${BL_APP_ICON}")
configure_file(app/BrokenLiquid.rc.in ${CMAKE_CURRENT_BINARY_DIR}/generated/BrokenLiquid.rc @ONLY)

qt_add_executable(BrokenLiquid WIN32
    app/main.cpp
    app/SplashScreen.cpp     app/SplashScreen.h
    app/SingleInstance.cpp   app/SingleInstance.h
    ${CMAKE_CURRENT_BINARY_DIR}/generated/BrokenLiquid.rc)
qt_add_resources(BrokenLiquid "bl_resources"
    PREFIX "/"
    BASE ${CMAKE_SOURCE_DIR}/resources
    FILES
        ${CMAKE_SOURCE_DIR}/resources/themes/light.qss
        ${CMAKE_SOURCE_DIR}/resources/themes/dark.qss
        ${BL_ICON_FILES})
target_link_libraries(BrokenLiquid PRIVATE bl::ui Qt6::Network)
set_target_properties(BrokenLiquid PROPERTIES OUTPUT_NAME "BrokenLiquid")

# Deploy Qt runtime (DLLs + platform/style/imageformat plugins) next to the exe after each build.
# vcpkg's app-local step already copies OCCT and other non-Qt DLLs.
if(WIN32 AND TARGET Qt6::windeployqt)
    add_custom_command(TARGET BrokenLiquid POST_BUILD
        COMMAND Qt6::windeployqt
            --no-translations --no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler --verbose 0
            $<TARGET_FILE:BrokenLiquid>
        COMMENT "Deploying Qt runtime")
endif()
