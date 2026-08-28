include_guard()

find_package(ICU REQUIRED COMPONENTS uc i18n data)

add_library(ll::icu INTERFACE IMPORTED)
target_link_libraries(ll::icu INTERFACE ICU::uc ICU::i18n ICU::data)

# The C API is the whole of what we use. Saying so keeps ICU's C++ classes out
# of the headers -- their ABI is compiler- and version-specific, which a static
# link makes our problem.
target_compile_definitions(ll::icu INTERFACE U_SHOW_CPLUSPLUS_API=0)

# Without this the headers declare every entry point __declspec(dllimport) and
# the link looks for symbols a static ICU does not carry.
get_target_property(_icu_uc_type ICU::uc TYPE)
if(_icu_uc_type STREQUAL "STATIC_LIBRARY")
    target_compile_definitions(ll::icu INTERFACE U_STATIC_IMPLEMENTATION)
endif()
unset(_icu_uc_type)
