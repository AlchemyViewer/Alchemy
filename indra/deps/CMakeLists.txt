project(deps)

include(00-Common)
include(FetchContent)

set(CMAKE_FOLDER "Third Party")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(deps_SOURCE_FILES
    CMakeLists.txt
    )

set_source_files_properties(${deps_SOURCE_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

add_library(deps
            ../cmake/cmake_dummy.cpp
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
  GIT_TAG        v0.18
  )
# FetchContent_Declare(
#   Catch2
#   GIT_REPOSITORY https://github.com/catchorg/Catch2.git
#   GIT_TAG        v2.13.7
#   )
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG        9.1.0
  )
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://git.alchemyviewer.org/alchemy/mirrors/modernjson.git
  GIT_TAG        v3.11.2
  )

# #Download the rest of the libraries
FetchContent_MakeAvailable(fmt)

set(MESHOPT_BUILD_SHARED_LIBS OFF)
FetchContent_MakeAvailable(meshoptimizer)

# If you only include this third party in PRIVATE source files, you do not
# need to install it when your main project gets installed.
set(JSON_Install OFF CACHE INTERNAL "")
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

unset(CMAKE_FOLDER)
unset(CMAKE_POSITION_INDEPENDENT_CODE)
