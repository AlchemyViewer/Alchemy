# -*- cmake -*-
include(Prebuilt)

set(USE_NVAPI OFF CACHE BOOL "Use NVAPI library.")

if (INSTALL_PROPRIETARY)
  set(USE_NVAPI ON CACHE BOOL "" FORCE)
endif (INSTALL_PROPRIETARY)

if (USE_NVAPI)
  if (WINDOWS)
    add_library( ll::nvapi INTERFACE IMPORTED )
    use_prebuilt_binary(nvapi)
    if (ADDRESS_SIZE EQUAL 32)
      target_link_libraries( ll::nvapi INTERFACE ${ARCH_PREBUILT_DIRS_RELEASE}/nvapi.lib)
    elseif (ADDRESS_SIZE EQUAL 64)
      target_link_libraries( ll::nvapi INTERFACE ${ARCH_PREBUILT_DIRS_RELEASE}/nvapi64.lib)
    endif (ADDRESS_SIZE EQUAL 32)
    target_compile_definitions( ll::nvapi INTERFACE LL_NVAPI=1)
  endif (WINDOWS)
endif (USE_NVAPI)

