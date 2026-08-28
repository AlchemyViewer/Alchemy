include_guard()

find_package(ICU REQUIRED COMPONENTS uc i18n data)

add_library(ll::icu INTERFACE IMPORTED)
target_link_libraries(ll::icu INTERFACE ICU::uc ICU::i18n ICU::data)

# The C API is the whole of what we use, and saying so keeps ICU's C++ headers
# -- which are not light -- out of every translation unit that wants a category
# lookup. Nothing we need is C++-only: break iteration, collation, case mapping,
# normalization and sort keys all have C entry points, and the RAII those
# classes would bring is forty lines we already have.
#
# Not an ABI argument. ICU's C++ ABI is unstable between versions, but that
# only matters across a dynamic boundary; we link a pinned ICU statically with
# our own toolchain, so there is no boundary for it to matter across. Flipping
# this to 1 is a one-line change if a C++-only API ever earns it.
target_compile_definitions(ll::icu INTERFACE U_SHOW_CPLUSPLUS_API=0)

# Without this the headers declare every entry point __declspec(dllimport) and
# the link looks for symbols a static ICU does not carry.
get_target_property(_icu_uc_type ICU::uc TYPE)
if(_icu_uc_type STREQUAL "STATIC_LIBRARY")
    target_compile_definitions(ll::icu INTERFACE U_STATIC_IMPLEMENTATION)
endif()
unset(_icu_uc_type)
