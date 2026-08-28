# -*- cmake -*-
include_guard()

# Defines unofficial::cef::cef along with CEF_INCLUDE_DIR, CEF_RESOURCE_DIR,
# CEF_BINARY_DIR_RELEASE/_DEBUG and CEF_FRAMEWORK_DIR.
find_package(unofficial-cef CONFIG REQUIRED)

add_library(ll::cef INTERFACE IMPORTED)
target_link_libraries(ll::cef INTERFACE unofficial::cef::cef)

if(DARWIN)
    find_library(APPKIT_LIBRARY AppKit REQUIRED)
    target_link_libraries(ll::cef INTERFACE ${APPKIT_LIBRARY})
endif()
