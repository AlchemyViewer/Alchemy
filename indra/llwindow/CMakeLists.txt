# -*- cmake -*-
#
# Compared to other libraries, compiling this one is a mess.  The
# reason is that we have several source files that have two different
# sets of behaviour, depending on whether they're intended to be part
# of the viewer or the map server.
#
# Unfortunately, the affected code is a rat's nest of #ifdefs, so it's
# easier to play compilation tricks than to actually fix the problem.

include(UI)

set(llwindow_BASE_SOURCE_FILES
  llcursortypes.cpp
  llkeyboard.cpp
  llkeyboardheadless.cpp
  llmousehandler.cpp
  llwindow.cpp
  llwindowheadless.cpp
  llwindowcallbacks.cpp
)

set(llwindow_BASE_HEADER_FILES
  llcursortypes.h
  llkeyboard.h
  llkeyboardheadless.h
  llmousehandler.h
  llscrolldelta.h
  llwindow.h
  llwindowheadless.h
  llwindowcallbacks.h
)

set(llwindow_BASE_LINK_LIBRARIES
  llcommon
  llimage
  llmath
  llfilesystem
  llxml
  ll::glm
)

add_library(llwindow STATIC)

target_sources(llwindow
  PRIVATE
    ${llwindow_BASE_SOURCE_FILES}
  PUBLIC
    ${llwindow_BASE_HEADER_FILES}
    llpreeditor.h
)

target_link_libraries(llwindow llrender ${llwindow_BASE_LINK_LIBRARIES} ll::uilibraries)
target_include_directories(llwindow INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_precompile_headers(llwindow REUSE_FROM llprecompiled)

if(USE_SDL_WINDOW)
  target_sources(llwindow
    PRIVATE
      llsdl.cpp
      llkeyboardsdl.cpp
      llwindowsdl.cpp
    PUBLIC
      llsdl.h
      llkeyboardsdl.h
      llwindowsdl.h
  )

  target_compile_definitions(llwindow PUBLIC LL_SDL_WINDOW=1)

  if(DARWIN)
    # macOS-only Objective-C++ helper that fixes up SDL's auto-created Cocoa
    # menu bar (strips the Cmd+W shortcut off its default Window > Close item).
    target_sources(llwindow
      PRIVATE
        llsdl_macos.mm
      PUBLIC
        llsdl_macos.h
    )

    # llwindow reuses llprecompiled's C++ PCH; that PCH has no Objective-C++
    # variant, so skip it for the .mm or CMake fails to resolve a cmake_pch.objcxx
    # header it expects this target to provide.
    set_source_files_properties(llsdl_macos.mm PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
  endif()

  target_link_libraries(llwindow
    ll::SDL3
  )
endif()

if(DARWIN)
  # IOKit deployment-target shim, consumed by newview's llmachineid.cpp.
  target_sources(llwindow
    PUBLIC
      llwindowmacosx_iokit.h
  )
endif(DARWIN)

if(WINDOWS)
  # lldxhardware (GPU/driver/VRAM queries) is used regardless of window
  # backend — the SDL backend calls LLDXHardware::updateVRAMBudgetFromDXGI()
  # and llappviewer queries WMI driver versions.
  target_sources(llwindow
    PRIVATE
      lldxhardware.cpp
    PUBLIC
      lldxhardware.h
  )

  # The native Win32 backend is only built when SDL isn't driving the window.
  # llkeyboardwin32 in particular can't compile under LL_SDL_WINDOW (the
  # NATIVE_KEY_TYPE typedef switches to the SDL flavor), and the factory never
  # instantiates LLWindowWin32 on an SDL build anyway. Headers stay listed so
  # llviewerwindow.cpp's (guarded) include still resolves.
  if(NOT USE_SDL_WINDOW)
    target_sources(llwindow
      PRIVATE
        llwindowwin32.cpp
        llkeyboardwin32.cpp
        lldragdropwin32.cpp
    )
  endif()
  target_sources(llwindow
    PUBLIC
      llwindowwin32.h
      llkeyboardwin32.h
      lldragdropwin32.h
  )
endif(WINDOWS)

if(BUILD_HEADLESS)
  include(Mesa)
  add_library(llwindowheadless STATIC)

  target_sources(llwindowheadless
    PRIVATE
      ${llwindow_BASE_SOURCE_FILES}
      llwindowmesaheadless.cpp
    PUBLIC
      ${llwindow_BASE_HEADER_FILES}
      llwindowmesaheadless.h
  )

  target_compile_definitions(llwindowheadless PUBLIC LL_MESA_HEADLESS=1)

  target_link_libraries(llwindowheadless
    llrenderheadless
    ${llwindow_BASE_LINK_LIBRARIES}
    ll::osmesa
  )
  target_include_directories(llwindowheadless INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
  target_precompile_headers(llwindowheadless REUSE_FROM llprecompiled)
endif(BUILD_HEADLESS)
