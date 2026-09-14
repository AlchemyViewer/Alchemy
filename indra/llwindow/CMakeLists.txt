# -*- cmake -*-

include(UI)

set(
  llwindow_BASE_SOURCES
  alwindowsdlheadless.cpp
  llcursortypes.cpp
  llkeyboard.cpp
  llkeyboardheadless.cpp
  llmousehandler.cpp
  llsdl.cpp
  llwindow.cpp
  llwindowheadless.cpp
  llwindowcallbacks.cpp
)

set(
  llwindow_BASE_HEADERS
  alwindowsdlheadless.h
  llcursortypes.h
  llkeyboard.h
  llkeyboardheadless.h
  llmousehandler.h
  llscrolldelta.h
  llsdl.h
  llwindow.h
  llwindowheadless.h
  llwindowcallbacks.h
)

set(
  llwindow_BASE_LINK_LIBRARIES
  llcommon
  llimage
  llmath
  llfilesystem
  llxml
  ll::glm
)

add_library(llwindow STATIC)

target_sources(
  llwindow
  PRIVATE ${llwindow_BASE_SOURCES}
  PUBLIC FILE_SET HEADERS FILES ${llwindow_BASE_HEADERS} llpreeditor.h
)

# SDL's video and GL bring-up (llsdl) is part of every build: the worker
# threads' shared contexts are made through it, and so is the hidden window a
# headless run renders on. The SDL window backend below is the part that is
# optional.
target_link_libraries(
  llwindow
  PRIVATE al::flags
  PUBLIC llrender ${llwindow_BASE_LINK_LIBRARIES} ll::uilibraries ll::SDL3core
)
target_precompile_headers(llwindow REUSE_FROM llprecompiled)

if(AL_USE_SDL_WINDOW)
  target_sources(
    llwindow
    PRIVATE llkeyboardsdl.cpp llwindowsdl.cpp
    PUBLIC FILE_SET HEADERS FILES llkeyboardsdl.h llwindowsdl.h
  )

  target_compile_definitions(llwindow PUBLIC LL_SDL_WINDOW=1)

  target_link_libraries(llwindow PUBLIC ll::SDL3)
endif()

if(DARWIN)
  # Objective-C++ helper that fixes up SDL's auto-created Cocoa menu bar
  # (strips the Cmd+W shortcut off its default Window > Close item); init_sdl
  # calls it.
  target_sources(llwindow PRIVATE llsdl_macos.mm PUBLIC FILE_SET HEADERS FILES llsdl_macos.h)

  # llwindow reuses llprecompiled's C++ PCH; that PCH has no Objective-C++
  # variant, so skip it for the .mm or CMake fails to resolve a cmake_pch.objcxx
  # header it expects this target to provide.
  set_source_files_properties(llsdl_macos.mm PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

  # IOKit deployment-target shim, consumed by newview's llmachineid.cpp.
  target_sources(llwindow PUBLIC FILE_SET HEADERS FILES llwindowmacosx_iokit.h)
endif()

if(WINDOWS)
  # lldxhardware (GPU/driver/VRAM queries) is used regardless of window
  # backend — the SDL backend calls LLDXHardware::updateVRAMBudgetFromDXGI()
  # and llappviewer queries WMI driver versions.
  target_sources(llwindow PRIVATE lldxhardware.cpp PUBLIC FILE_SET HEADERS FILES lldxhardware.h)

  # The native Win32 backend is only built when SDL isn't driving the window.
  # llkeyboardwin32 in particular can't compile under LL_SDL_WINDOW (the
  # NATIVE_KEY_TYPE typedef switches to the SDL flavor), and the factory never
  # instantiates LLWindowWin32 on an SDL build anyway. Headers stay listed so
  # llviewerwindow.cpp's (guarded) include still resolves.
  if(NOT AL_USE_SDL_WINDOW)
    target_sources(llwindow PRIVATE llwindowwin32.cpp llkeyboardwin32.cpp lldragdropwin32.cpp)
  endif()
  target_sources(
    llwindow
    PUBLIC FILE_SET HEADERS FILES llwindowwin32.h llkeyboardwin32.h lldragdropwin32.h
  )
endif()

if(AL_BUILD_HEADLESS)
  include(Mesa)
  add_library(llwindowheadless STATIC)

  target_sources(
    llwindowheadless
    PRIVATE ${llwindow_BASE_SOURCES} llwindowmesaheadless.cpp
    PUBLIC FILE_SET HEADERS FILES ${llwindow_BASE_HEADERS} llwindowmesaheadless.h
  )

  target_compile_definitions(llwindowheadless PUBLIC LL_MESA_HEADLESS=1)

  target_link_libraries(
    llwindowheadless
    PRIVATE al::flags
    PUBLIC llrenderheadless ${llwindow_BASE_LINK_LIBRARIES} ll::osmesa
  )
  target_precompile_headers(llwindowheadless REUSE_FROM llprecompiled)
endif()
