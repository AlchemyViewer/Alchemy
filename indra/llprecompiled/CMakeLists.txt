# -*- cmake -*-

set(llprecompiled_SOURCE_FILES
    cmake_dummy.cpp
    )

add_library (llprecompiled STATIC ${llprecompiled_SOURCE_FILES})

target_link_libraries(llprecompiled
        llcommon
        ll::apr
        ll::zlib-ng
        ll::boost
        ll::oslibraries
        ll::tracy
        ll::sse2neon
        Threads::Threads
    )

target_precompile_headers(llprecompiled PRIVATE
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

add_executable(llprecompiled_exe ${llprecompiled_SOURCE_FILES})

target_link_libraries(llprecompiled_exe
        llcommon
        ll::apr
        ll::zlib-ng
        ll::boost
        ll::oslibraries
        ll::tracy
        ll::sse2neon
        Threads::Threads
    )

target_precompile_headers(llprecompiled_exe PRIVATE
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
