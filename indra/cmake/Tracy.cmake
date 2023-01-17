# -*- cmake -*-
include(Prebuilt)

set(USE_TRACY OFF CACHE BOOL "Use Tracy profiler.")

if (USE_TRACY)
  # use_prebuilt_binary(tracy)

  # See: indra/llcommon/llprofiler.h
  add_definitions(-DLL_PROFILER_CONFIGURATION=3)
  set(TRACY_LIBRARY TracyClient)
else (USE_TRACY)
  # Tracy.cmake should not set LLCOMMON_INCLUDE_DIRS, let LLCommon.cmake do that
  set(TRACY_INCLUDE_DIR "")
  set(TRACY_LIBRARY "")
endif (USE_TRACY)

