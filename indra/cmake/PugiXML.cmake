# -*- cmake -*-
include_guard()

add_library(ll::pugixml INTERFACE IMPORTED)

find_package(pugixml CONFIG REQUIRED)
target_link_libraries(ll::pugixml INTERFACE pugixml::pugixml)
