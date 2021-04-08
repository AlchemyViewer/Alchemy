project(deps)

include(00-Common)
include(FetchContent)

set(CMAKE_FOLDER "Third Party")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(deps_SOURCE_FILES
    CMakeLists.txt
    ../cmake/cmake_dummy.cpp
    )

set_source_files_properties(${cmake_SOURCE_FILES} ${deps_SOURCE_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

add_library(deps
            ${deps_SOURCE_FILES}
            )


if(${CMAKE_VERSION} VERSION_LESS 3.14)
    macro(FetchContent_MakeAvailable NAME)
        FetchContent_GetProperties(${NAME})
        if(NOT ${NAME}_POPULATED)
            FetchContent_Populate(${NAME})
            add_subdirectory(${${NAME}_SOURCE_DIR} ${${NAME}_BINARY_DIR})
        endif()
    endmacro()
endif()

FetchContent_Declare(
  meshoptimizer
  GIT_REPOSITORY https://git.alchemyviewer.org/alchemy/mirrors/meshoptimizer.git
  GIT_TAG        v0.16
  )
# FetchContent_Declare(
#   Catch2
#   GIT_REPOSITORY https://github.com/catchorg/Catch2.git
#   GIT_TAG        v2.11.0
#   )
# FetchContent_Declare(
#   fmt
#   GIT_REPOSITORY https://github.com/fmtlib/fmt.git
#   GIT_TAG        6.1.2
#   )
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://git.alchemyviewer.org/alchemy/mirrors/modernjson.git
  GIT_TAG        v3.9.1
  )
FetchContent_Declare(
  absl
  GIT_REPOSITORY https://git.alchemyviewer.org/alchemy/mirrors/abseil-cpp.git
  GIT_TAG        3b4a16abad2c2ddc494371cc39a2946e36d35d11
  )

# This is a hack because absl has dumb cmake
set(OLD_BUILD_TEST ${BUILD_TESTING})
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(absl)
set(BUILD_TESTING ${OLD_BUILD_TEST})

# Supress warnings inside abseil under MSVC
if(WINDOWS)
  target_compile_options(absl_strings PRIVATE /wd4018)
  target_compile_options(absl_str_format_internal PRIVATE /wd4018)
  target_compile_options(absl_flags_usage_internal PRIVATE /wd4018)
  target_compile_options(absl_random_internal_distribution_test_util PRIVATE /wd4723)
elseif(LINUX) 
  target_compile_options(absl_flags PRIVATE -Wno-non-virtual-dtor)
  target_compile_options(absl_flags_commandlineflag PRIVATE -Wno-non-virtual-dtor)
  target_compile_options(absl_flags_internal PRIVATE -Wno-non-virtual-dtor)
  target_compile_options(absl_flags_parse PRIVATE -Wno-non-virtual-dtor)
  target_compile_options(absl_flags_private_handle_accessor PRIVATE -Wno-non-virtual-dtor)
  target_compile_options(absl_flags_reflection PRIVATE -Wno-non-virtual-dtor)
  target_compile_options(absl_flags_usage PRIVATE -Wno-non-virtual-dtor)
  target_compile_options(absl_flags_usage_internal PRIVATE -Wno-non-virtual-dtor)
endif()

# if (BUILD_TESTING)
#   FetchContent_MakeAvailable(Catch2)
# endif()

# #Download the rest of the libraries
# FetchContent_MakeAvailable(fmt)
FetchContent_MakeAvailable(meshoptimizer)

# # Typically you don't care so much for a third party library's tests to be
# # run from your own project's code.
# set(JSON_BuildTests OFF CACHE INTERNAL "")

# If you only include this third party in PRIVATE source files, you do not
# need to install it when your main project gets installed.
set(JSON_Install OFF CACHE INTERNAL "")
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

unset(CMAKE_FOLDER)
unset(CMAKE_POSITION_INDEPENDENT_CODE)
