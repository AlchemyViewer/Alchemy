  include_guard()

  find_package(harfbuzz CONFIG REQUIRED)

  add_library(ll::harfbuzz INTERFACE IMPORTED)

  target_link_libraries(ll::harfbuzz INTERFACE harfbuzz::harfbuzz)
