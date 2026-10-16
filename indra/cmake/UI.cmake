# -*- cmake -*-
include_guard()

include(DBUS)

add_library(ll::uilibraries INTERFACE IMPORTED)

if(LINUX)
  # The window is SDL's and GL is EGL on Wayland and X11 alike, so the viewer
  # itself needs neither wayland-client nor X11: no headers, no defines.
  target_link_libraries(ll::uilibraries INTERFACE ll::fontconfig ll::freetype ll::dbus)
elseif(DARWIN)
  target_link_libraries(ll::uilibraries INTERFACE ${CARBON_LIBRARY})
elseif(WINDOWS)
  target_link_libraries(
    ll::uilibraries
    INTERFACE
      UxTheme
      Dwmapi
      Shcore
      comdlg32 # Common Dialogs for ChooseColor
      ole32
      dxgi
      d3d9
      dinput8
      dxguid
      opengl32
      kernel32
      oleaut32
      shell32
      shlwapi
      wer
      winspool
      imm32
  )
endif()
