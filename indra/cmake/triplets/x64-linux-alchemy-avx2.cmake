set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

set(VCPKG_C_FLAGS "-march=x86-64-v3")
set(VCPKG_CXX_FLAGS "-march=x86-64-v3")

if(PORT MATCHES "^webrtc$")
    set(VCPKG_BUILD_TYPE release)
endif()
