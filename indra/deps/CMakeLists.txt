
project(deps)

include(00-Common)
include(FetchContent)
include(UI)

set(CMAKE_FOLDER "Third Party")
set(CMAKE_POLICY_DEFAULT_CMP0063 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

set(deps_SOURCE_FILES
    CMakeLists.txt
    )

set_source_files_properties(${deps_SOURCE_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

add_library(deps
            ../cmake/cmake_dummy.cpp
            ${deps_SOURCE_FILES}
            )

FetchContent_Declare(
  meshoptimizer
  GIT_REPOSITORY https://git.alchemyviewer.org/alchemy/mirrors/meshoptimizer.git
  GIT_TAG        v0.18
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE
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
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE
  )
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://git.alchemyviewer.org/alchemy/mirrors/modernjson.git
  GIT_TAG        v3.11.2
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE
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

if(USE_TRACY)
  FetchContent_Declare (
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG v0.9
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    )
  set(TRACY_ON_DEMAND ON)
  set(TRACY_ONLY_LOCALHOST ON)
  set(TRACY_FIBERS ON)
  FetchContent_MakeAvailable(tracy)
endif()

if(USE_NFD)
  FetchContent_Declare(
    nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG        v1.0.3
    )

  set(NFD_PORTAL ON)
  FetchContent_MakeAvailable(nfd)
endif()

unset(CMAKE_FOLDER)
