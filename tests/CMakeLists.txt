find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(bl_tests
    test_settings.cpp
    test_paths.cpp
    test_kernel.cpp
    test_units.cpp
    test_undo.cpp
    test_measure.cpp
    test_document.cpp
    test_sketch_solver.cpp
    test_sketch_operations.cpp
    test_features.cpp
    test_naming.cpp)
target_link_libraries(bl_tests PRIVATE bl::kernel bl::doc bl::sketch GTest::gtest GTest::gtest_main)
set_target_properties(bl_tests PROPERTIES AUTOMOC OFF)

# DISCOVERY_MODE PRE_TEST: the exe needs OCCT DLLs next to it, which vcpkg copies post-build.
gtest_discover_tests(bl_tests DISCOVERY_MODE PRE_TEST)
