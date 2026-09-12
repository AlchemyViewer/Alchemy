# -*- cmake -*-

# The shared precompiled header, built once as a library and once as an
# executable. Both are needed: position-independent code is -fPIC for a
# library and -fPIE for an executable, and REUSE_FROM requires the provider
# and the consumer to match exactly.
set(llprecompiled_HEADERS
    [["linden_common.h"]]
    [["llwin32headers.h"]]
    [["llprofiler.h"]]
    <boost/bind.hpp>
    <boost/fiber/fiber.hpp>
    <boost/fiber/fss.hpp>
    <boost/fiber/future/promise.hpp>
    <boost/fiber/future/future.hpp>
    <boost/function.hpp>
    <boost/function_types/function_arity.hpp>
    <boost/iterator/transform_iterator.hpp>
    <boost/iterator/indirect_iterator.hpp>
    <boost/range/iterator_range.hpp>
    <boost/signals2.hpp>
    )

set(llprecompiled_LIBRARIES
    llcommon
    ll::apr
    ll::zlib-ng
    ll::boost
    ll::oslibraries
    ll::tracy
    ll::sse2neon
    Threads::Threads
    )

add_library(llprecompiled STATIC)
target_sources(llprecompiled PRIVATE cmake_dummy.cpp)
target_link_libraries(llprecompiled PRIVATE al::flags PUBLIC ${llprecompiled_LIBRARIES})
target_precompile_headers(llprecompiled PRIVATE ${llprecompiled_HEADERS})

add_executable(llprecompiled_exe)
target_sources(llprecompiled_exe PRIVATE cmake_dummy.cpp)
target_link_libraries(llprecompiled_exe PRIVATE al::flags ${llprecompiled_LIBRARIES})
target_precompile_headers(llprecompiled_exe PRIVATE ${llprecompiled_HEADERS})
