# -*- cmake -*-
include_guard()
include(ZLIBNG)

# newview links minizip directly as well, so it keeps a target of its own.
find_package(minizip CONFIG REQUIRED)

add_library(ll::minizip INTERFACE IMPORTED)
target_link_libraries(ll::minizip INTERFACE MINIZIP::minizip ll::zlib-ng)

# Defines unofficial::collada-dom::collada14dom, which pulls in libxml2 and
# minizip itself.
find_package(unofficial-collada-dom CONFIG REQUIRED)

add_library(ll::colladadom INTERFACE IMPORTED)
target_link_libraries(ll::colladadom INTERFACE unofficial::collada-dom::collada14dom ll::minizip)
