# -*- cmake -*-

include(UI)

set(
  llwindow_BASE_SOURCES
  llcursortypes.cpp
  llkeyboard.cpp
  llkeyboardheadless.cpp
  llmousehandler.cpp
  llwindow.cpp
  llwindowheadless.cpp
  llwindowcallbacks.cpp
)

set(
  llwindow_BASE_HEADERS
  llcursortypes.h
  llkeyboard.h
  llkeyboardheadless.h
  llmousehandler.h
  llscrolldelta.h
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

target_link_libraries(
  llwindow
  PRIVATE al::flags
  PUBLIC llrender ${llwindow_BASE_LINK_LIBRARIES} ll::uilibraries
)
target_precompile_headers(llwindow REUSE_FROM llprecompiled)

if(AL_USE_SDL_WINDOW)
  target_sources(
    llwindow
    PRIVATE llsdl.cpp llkeyboardsdl.cpp llwindowsdl.cpp
    PUBLIC FILE_SET HEADERS FILES llsdl.h llkeyboardsdl.h llwindowsdl.h
  )

  target_compile_definitions(llwindow PUBLIC LL_SDL_WINDOW=1)

  if(DARWIN)
    # macOS-only Objective-C++ helper that fixes up SDL's auto-created Cocoa
    # menu bar (strips the Cmd+W shortcut off its default Window > Close item).
    target_sources(llwindow PRIVATE llsdl_macos.mm PUBLIC FILE_SET HEADERS FILES llsdl_macos.h)

    # llwindow reuses llprecompiled's C++ PCH; that PCH has no Objective-C++
    # variant, so skip it for the .mm or CMake fails to resolve a cmake_pch.objcxx
    # header it expects this target to provide.
    set_source_files_properties(llsdl_macos.mm PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
  endif()

  target_link_libraries(llwindow PUBLIC ll::SDL3)
endif()

if(DARWIN)
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
