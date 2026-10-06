# -*- cmake -*-
include_guard()
add_library(ll::tracy INTERFACE IMPORTED)

if(AL_USE_TRACY)
  find_package(Tracy CONFIG REQUIRED)

  if(AL_ENABLE_TRACY_GPU AND NOT DARWIN) # Tracy OpenGL mode is incompatible with macOS/iOS
    target_compile_definitions(ll::tracy INTERFACE LL_PROFILER_ENABLE_TRACY_OPENGL=1)
  endif()

  # See: indra/llcommon/llprofiler.h
  # Additionally we only want tracy support in RelWithDebInfo and Release builds.
  target_compile_definitions(
    ll::tracy
    INTERFACE
      $<$<CONFIG:RelWithDebInfo,Release>:LL_PROFILER_CONFIGURATION=3>
      $<$<CONFIG:Debug,OptDebug>:LL_PROFILER_CONFIGURATION=1>
  )
  target_link_libraries(ll::tracy INTERFACE $<$<CONFIG:RelWithDebInfo,Release>:Tracy::TracyClient>)

  # Tracy starts its profiler thread from a static initializer, and the
  # thread's first act, the broadcast saying where it listens, resolves an
  # address with getaddrinfo, which on macOS dlopens libnetwork and the
  # hundreds of images under it. Done while dyld is still finishing the
  # process's start on the main thread, that now and then crashes dyld
  # (dyld4::prepare, in generateAtlas): a test, or the viewer, dead before
  # main. Linked, libnetwork is loaded with the process, before any
  # initializer runs, and the dlopen finds it already there. -needed, since
  # nothing calls into it and -dead_strip_dylibs would drop it.
  if(DARWIN)
    target_link_options(
      ll::tracy
      INTERFACE $<$<CONFIG:RelWithDebInfo,Release>:LINKER:-needed-lnetwork>
    )
  endif()
else()
  # See: indra/llcommon/llprofiler.h
  target_compile_definitions(ll::tracy INTERFACE LL_PROFILER_CONFIGURATION=1)
endif()
