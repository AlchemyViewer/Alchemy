# -*- cmake -*-
include_guard()
add_library(ll::libcurl INTERFACE IMPORTED)

# The port exports no CMake config, so the probe for one FindCURL makes first
# is a search that fails every configure.
set(CURL_NO_CURL_CMAKE ON)
find_package(CURL REQUIRED)
target_link_libraries(ll::libcurl INTERFACE CURL::libcurl)

# The curl port exports no CMake config, so FindCURL builds CURL::libcurl
# from the library alone and the static build's HTTP/2 dependency has to be
# named here; OpenSSL and zlib reach the link through their own ll:: targets.
find_library(NGHTTP2_LIBRARIES nghttp2 REQUIRED)
target_link_libraries(ll::libcurl INTERFACE ${NGHTTP2_LIBRARIES})
