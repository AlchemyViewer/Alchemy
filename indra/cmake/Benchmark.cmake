# -*- cmake -*-

include_guard()

if(NOT BUILD_BENCHMARKS)
  return()
endif()

find_package(benchmark CONFIG REQUIRED)

add_library(ll::benchmark INTERFACE IMPORTED)
target_link_libraries(ll::benchmark INTERFACE benchmark::benchmark)

add_library(ll::benchmark_main INTERFACE IMPORTED)
target_link_libraries(ll::benchmark_main INTERFACE benchmark::benchmark_main)

function(_RD_BENCHMARK_SOURCE_FILES OUTVAR)
  set(benchmark_sources)

  foreach(source IN LISTS ARGN)
    get_filename_component(source_name "${source}" NAME_WE)
    get_filename_component(source_extension "${source}" EXT)
    get_filename_component(source_directory "${source}" DIRECTORY)

    if(source_directory)
      set(benchmark_source
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/${source_directory}/${source_name}_benchmark${source_extension}")
    else()
      set(benchmark_source
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/${source_name}_benchmark${source_extension}")
    endif()

    if(NOT EXISTS "${benchmark_source}")
      message(FATAL_ERROR
        "RD_ADD_BENCHMARKS expected benchmark source ${benchmark_source} for ${source}")
    endif()

    list(APPEND benchmark_sources "${benchmark_source}")
  endforeach()

  set(${OUTVAR} "${benchmark_sources}" PARENT_SCOPE)
endfunction()

function(RD_ADD_BENCHMARKS project sources)
  if(ARGC LESS 2)
    message(FATAL_ERROR "RD_ADD_BENCHMARKS requires a project and source list")
  endif()

  if(NOT project)
    message(FATAL_ERROR "RD_ADD_BENCHMARKS requires a project")
  endif()

  # An empty source list is a valid no-op while a module is being migrated.
  if(NOT sources)
    return()
  endif()

  _RD_BENCHMARK_SOURCE_FILES(benchmark_sources ${sources})

  set(benchmark_target "${project}_benchmarks")
  add_executable(${benchmark_target} ${benchmark_sources})
  target_link_libraries(${benchmark_target}
    PRIVATE
    ll::benchmark_main
    ${project}
  )

  set_target_properties(${benchmark_target}
    PROPERTIES
    FOLDER "Benchmarks"
    RUNTIME_OUTPUT_DIRECTORY "${EXE_STAGING_DIR}"
  )

  if(WINDOWS)
    target_link_options(${benchmark_target}
      PRIVATE
      $<$<CONFIG:Release>:/DEBUG:NONE>
    )
  elseif(DARWIN)
    set_target_properties(${benchmark_target}
      PROPERTIES
      BUILD_WITH_INSTALL_RPATH 1
      INSTALL_RPATH "@executable_path/Frameworks"
      XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
    )
  elseif(LINUX)
    set_property(TARGET ${benchmark_target} APPEND PROPERTY
      BUILD_RPATH "${SHARED_LIB_STAGING_DIR}")
  endif()

  if(TARGET stage_third_party_libs)
    add_dependencies(${benchmark_target} stage_third_party_libs)
  endif()
endfunction()
