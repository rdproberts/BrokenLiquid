# Release-only, dynamically linked dependencies.
# Dynamic linking keeps Qt (LGPL-3) and OCCT (LGPL-2.1) replaceable by users.
# Release-only halves dependency build time; BrokenLiquid dev builds use RelWithDebInfo
# so the MSVC runtime (/MD) matches. See docs/adr/0002-dependencies-via-vcpkg.md.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
