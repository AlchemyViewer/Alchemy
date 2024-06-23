# -*- cmake -*-
include(Prebuilt)

include_guard()
add_library( ll::sdbus-cpp INTERFACE IMPORTED )

if(LINUX)
  use_system_binary(sdbus-cpp)
  use_prebuilt_binary(sdbus-cpp)

  target_compile_definitions( ll::sdbus-cpp INTERFACE LL_DBUS_ENABLED=1 )

  target_link_libraries(ll::sdbus-cpp INTERFACE sdbus-c++)

  target_include_directories( ll::sdbus-cpp SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include/)
endif()
