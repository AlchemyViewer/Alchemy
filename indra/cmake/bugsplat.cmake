# -*- cmake -*-
include_guard()
add_library(ll::bugsplat INTERFACE IMPORTED)

# AL_USE_BUGSPLAT holds only on Windows and macOS with a database named; the
# root derives it.
if(AL_USE_BUGSPLAT)
  # bugsplat on Windows, bugsplat-apple on macOS; both ship the same package
  # and target.
  find_package(unofficial-bugsplat CONFIG REQUIRED)
  target_link_libraries(ll::bugsplat INTERFACE unofficial::bugsplat::bugsplat)
  target_compile_definitions(ll::bugsplat INTERFACE LL_BUGSPLAT=1)
endif()
