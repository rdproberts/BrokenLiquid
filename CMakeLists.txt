cmake_minimum_required(VERSION 3.27)

project(BrokenLiquid
    VERSION 0.0.1
    DESCRIPTION "Free, local, parametric 3D CAD"
    HOMEPAGE_URL "https://github.com/"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(BL_BUILD_TESTS  "Build unit tests"              ON)
option(BL_BUILD_SPIKES "Build Phase 0 spike programs"  ON)

# --- Dependencies (provided by vcpkg, see vcpkg.json) ------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Network Svg Concurrent)
# QZipReader/QZipWriter (QtCore private API) for the .blpart container (ADR 0008).
find_package(Qt6 REQUIRED COMPONENTS CorePrivate)
find_package(OpenCASCADE CONFIG REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

qt_standard_project_setup()

# --- Build id (short git hash, or "dev") ----------------------------------------------------------
set(BL_BUILD_ID "dev")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _bl_git_hash OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_bl_git_hash)
        set(BL_BUILD_ID ${_bl_git_hash})
    endif()
endif()

# --- Common compile options -----------------------------------------------------------------------
add_library(bl_options INTERFACE)
add_library(bl::options ALIAS bl_options)
if(MSVC)
    target_compile_options(bl_options INTERFACE
        /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /MP
        /external:anglebrackets /external:W0
        /wd4251)  # DLL-interface warnings from third-party headers
    target_compile_definitions(bl_options INTERFACE NOMINMAX _USE_MATH_DEFINES UNICODE _UNICODE)
else()
    target_compile_options(bl_options INTERFACE -Wall -Wextra -Wpedantic)
endif()

# OCCT: imported targets carry their include dirs in recent versions; add the variable as a fallback.
add_library(bl_occt INTERFACE)
add_library(bl::occt ALIAS bl_occt)
target_link_libraries(bl_occt INTERFACE ${OpenCASCADE_LIBRARIES})
target_include_directories(bl_occt SYSTEM INTERFACE ${OpenCASCADE_INCLUDE_DIR})

add_subdirectory(src)

if(BL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BL_BUILD_SPIKES)
    add_subdirectory(spikes)
endif()
