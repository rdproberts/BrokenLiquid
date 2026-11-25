find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(bl_tests
    test_settings.cpp
    test_paths.cpp
    test_kernel.cpp)
target_link_libraries(bl_tests PRIVATE bl::kernel GTest::gtest GTest::gtest_main)
set_target_properties(bl_tests PROPERTIES AUTOMOC OFF)

# DISCOVERY_MODE PRE_TEST: the exe needs OCCT DLLs next to it, which vcpkg copies post-build.
gtest_discover_tests(bl_tests DISCOVERY_MODE PRE_TEST)
