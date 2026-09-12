set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_C_FLAGS "/arch:AVX2")
set(VCPKG_CXX_FLAGS "/arch:AVX2 /std:c++20 /Zc:__cplusplus")

if(PORT MATCHES "faudio")
    set(VCPKG_LIBRARY_LINKAGE static)
endif()

if(PORT MATCHES "openal-soft")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()

if(PORT MATCHES "^webrtc$")
    set(VCPKG_BUILD_TYPE release)
endif()
