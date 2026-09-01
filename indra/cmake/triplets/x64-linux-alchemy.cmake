set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

if(PORT MATCHES "^webrtc$")
    set(VCPKG_BUILD_TYPE release)
endif()

# SDL3's upstream vcpkg port introduced a regression which explicitly disables libdecor, causing GNOME Wayland
# windows to have no decorations. Append ON after the portfile's OFF so cmake's
# last-value-wins rule applies. Remove once upstream vcpkg regression is fixed.
if(PORT STREQUAL "sdl3")
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS "-DSDL_WAYLAND_LIBDECOR=ON")
endif()
