# -*- cmake -*-
include_guard()
add_library(ll::ndof INTERFACE IMPORTED)

if (USE_NDOF)
  target_compile_definitions(ll::ndof INTERFACE LIB_NDOF=1)

  # libndofdev on Windows and macOS, open-libndofdev on Linux; both ship the
  # same package and target.
  find_package(unofficial-libndofdev CONFIG REQUIRED)
  target_link_libraries(ll::ndof INTERFACE unofficial::libndofdev::ndofdev)

  if (LINUX)
    include(SDL3)
    target_link_libraries(ll::ndof INTERFACE ll::SDL3)
  endif()
endif (USE_NDOF)
