# -*- cmake -*-
include(Prebuilt)

set(USE_NVAPI OFF CACHE BOOL "Use NVAPI.")

if (USE_NVAPI)
  if (WINDOWS)
    use_prebuilt_binary(nvapi)
    if (ADDRESS_SIZE EQUAL 32)
      set(NVAPI_LIBRARY ${ARCH_PREBUILT_DIRS_RELEASE}/nvapi.lib)
    elseif (ADDRESS_SIZE EQUAL 64)
      set(NVAPI_LIBRARY ${ARCH_PREBUILT_DIRS_RELEASE}/nvapi64.lib)
    endif (ADDRESS_SIZE EQUAL 32)	
  else (WINDOWS)
    set(NVAPI_LIBRARY "")
  endif (WINDOWS)
else (USE_NVAPI)
  set(NVAPI_LIBRARY "")
endif (USE_NVAPI)

