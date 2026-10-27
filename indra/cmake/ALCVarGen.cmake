# -*- cmake -*-
#
# The settings schema, compiled: alcvargen runs over a schema's TOML files at
# configure time, so that a fresh checkout's IDE sees the headers before the
# first build, and again in the build whenever a file of the schema, its lock
# or the generator changes. It writes a header only when its content changes,
# and each header holds keys and types alone, so a changed default, range or
# help text recompiles the area's one .cpp and nothing that reads the setting.
#
# The lock, committed beside the schema, records what each setting's stored
# values have meant, a step a line. A new setting's first step, which changes
# nothing stored, a local configure or build records itself and goes on. A
# change of a type, a unit or choices, or a migrate of a new step, fails the
# build until the lock's target records it, folding the steps a setting gained
# since HEAD's lock into one; a configure only warns, generating the step the
# change will be, so that the tree it writes can build that target.
# AL_CVARS_STRICT, for CI, records nothing and fails the configure on any line
# the lock does not carry. The generator's messages are a compiler's, at each
# entry's path, line and column, in Visual Studio's form under MSVC.
# doc/CVARS.md says how each change is made.
include_guard()

# The generator's own configure's -G, -A and -T, for the machine the build
# runs on rather than the one it builds for: a Visual Studio build for ARM64
# on an x64 machine builds an x64 generator, and an arm64 build on an Intel
# Mac an x86_64 one. Its tree takes nothing of the build's flags, ISA tier
# included, so it runs on any machine that builds.
function(_al_cvargen_host_generator_args out_var)
  set(args -G "${CMAKE_GENERATOR}")
  string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" processor)
  if(processor MATCHES "^(arm64|aarch64)$")
    set(arm TRUE)
  else()
    set(arm FALSE)
  endif()
  if(CMAKE_GENERATOR MATCHES "^Visual Studio")
    if(arm)
      list(APPEND args -A ARM64)
    else()
      list(APPEND args -A x64)
    endif()
  endif()
  if(CMAKE_GENERATOR_TOOLSET)
    list(APPEND args -T "${CMAKE_GENERATOR_TOOLSET}")
  endif()
  if(CMAKE_HOST_APPLE)
    if(arm)
      list(APPEND args -DCMAKE_OSX_ARCHITECTURES=arm64)
    else()
      list(APPEND args -DCMAKE_OSX_ARCHITECTURES=x86_64)
    endif()
  endif()
  set(${out_var} "${args}" PARENT_SCOPE)
endfunction()

# The generator for the machine the build runs on, built at configure time in
# a tree of its own under the build's, and again only when its sources do.
# The build runs this one too: one built for the target may not run here.
function(_al_cvargen_host out_var)
  set(host_dir "${CMAKE_BINARY_DIR}/alcvargen-host")
  set(exe "${host_dir}/bin/alcvargen${CMAKE_EXECUTABLE_SUFFIX}")
  file(
    GLOB sources
    "${INDRA_SOURCE_DIR}/cvars/gen/*.cpp"
    "${INDRA_SOURCE_DIR}/cvars/gen/*.h"
    "${INDRA_SOURCE_DIR}/cvars/gen/CMakeLists.txt"
  )
  list(
    APPEND sources
    "${INDRA_SOURCE_DIR}/alcvar/alcvartype.h"
    "${INDRA_SOURCE_DIR}/alcvar/alcvarconvertible.h"
  )
  # Of its sources and of how it is configured: a tree configured otherwise
  # before is begun again, as a generator's cache refuses another platform.
  _al_cvargen_host_generator_args(generator_args)
  set(digest "${generator_args}")
  foreach(source IN LISTS sources)
    file(SHA256 "${source}" hash)
    string(APPEND digest "${hash}")
  endforeach()
  string(SHA256 digest "${digest}")
  set(built "")
  if(EXISTS "${host_dir}/sources.sha256")
    file(READ "${host_dir}/sources.sha256" built)
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${sources})

  if(NOT EXISTS "${exe}" OR NOT built STREQUAL digest)
    get_target_property(toml_include tomlplusplus::tomlplusplus INTERFACE_INCLUDE_DIRECTORIES)
    file(REMOVE_RECURSE "${host_dir}")
    message(STATUS "Building alcvargen for the build machine")
    execute_process(
      COMMAND
        ${CMAKE_COMMAND} -S "${INDRA_SOURCE_DIR}/cvars/gen" -B "${host_dir}" ${generator_args}
        -DCMAKE_BUILD_TYPE=Release "-DALCVARGEN_INDRA=${INDRA_SOURCE_DIR}"
        "-DALCVARGEN_TOML_INCLUDE=${toml_include}"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE output
      ERROR_VARIABLE output
    )
    if(result EQUAL 0)
      execute_process(
        COMMAND ${CMAKE_COMMAND} --build "${host_dir}" --config Release
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
      )
    endif()
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "alcvargen could not be built for the build machine:\n${output}")
    endif()
    file(WRITE "${host_dir}/sources.sha256" "${digest}")
  endif()
  set(${out_var} "${exe}" PARENT_SCOPE)
endfunction()

# al_generate_cvars(<target> ROOT <namespace> INCLUDE_DIR <dir> OUTPUT_DIR <dir>
#                   LOCK <cvars.lock> [LOCK_TARGET <name>] [SCHEMA <file.toml>...]
#                   [WORDS <file>])
#
# Generates the schema's code into OUTPUT_DIR and compiles it into <target>:
# the areas' descriptor tables and the registry as sources, the headers'
# directory as a PUBLIC include path, and the debugger's formatter of each
# handle in the PDB of whatever links <target>. LOCK_TARGET, <target>_lock
# unless named, records the schema's changes in LOCK; it builds nothing else.
# Paths in the schema's descriptors, and in what --where says, are the
# repository's.
function(al_generate_cvars target)
  cmake_parse_arguments(
    PARSE_ARGV 1
    arg
    ""
    "ROOT;INCLUDE_DIR;OUTPUT_DIR;LOCK;LOCK_TARGET;WORDS"
    "SCHEMA"
  )
  foreach(var ROOT INCLUDE_DIR OUTPUT_DIR LOCK)
    if(NOT arg_${var})
      message(FATAL_ERROR "al_generate_cvars(${target}): ${var} is required")
    endif()
  endforeach()

  get_filename_component(repository "${INDRA_SOURCE_DIR}/.." ABSOLUTE)
  set(inputs "")
  foreach(file IN LISTS arg_SCHEMA)
    file(RELATIVE_PATH relative "${repository}" "${file}")
    list(APPEND inputs "${relative}")
  endforeach()
  set(
    args
    --out
    "${arg_OUTPUT_DIR}"
    --root
    ${arg_ROOT}
    --include-dir
    ${arg_INCLUDE_DIR}
  )
  if(NOT arg_LOCK_TARGET)
    set(arg_LOCK_TARGET ${target}_lock)
  endif()
  file(RELATIVE_PATH lock "${repository}" "${arg_LOCK}")
  list(
    APPEND args
    --lock
    "${lock}"
    --lock-command
    "cmake --build ${CMAKE_BINARY_DIR} --target ${arg_LOCK_TARGET}"
  )
  # A schema's first lock is written by its target.
  set(depends ${arg_SCHEMA})
  if(EXISTS "${arg_LOCK}")
    list(APPEND depends "${arg_LOCK}")
  endif()
  if(arg_WORDS)
    list(APPEND args --words "${arg_WORDS}")
    list(APPEND depends "${arg_WORDS}")
  endif()

  if(MSVC)
    list(APPEND args --diagnostics msvc)
  endif()
  # The configure's and the build's own runs: a strict one records nothing.
  set(provisional --provisional)
  set(record --record-new)
  if(AL_CVARS_STRICT)
    set(provisional "")
    set(record "")
  endif()

  _al_cvargen_host(host_alcvargen)
  execute_process(
    COMMAND "${host_alcvargen}" ${provisional} ${record} ${args} ${inputs}
    WORKING_DIRECTORY "${repository}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
  )
  # The generator's own lines first, each at its entry as an IDE reads it,
  # and only then CMake's word on them.
  string(STRIP "${output}" output)
  string(STRIP "${errors}" errors)
  if(output)
    message(NOTICE "${output}")
  endif()
  if(errors)
    message(NOTICE "${errors}")
  endif()
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "The settings schema has errors, each above at its entry")
  endif()
  if(errors)
    message(
      WARNING
      "The settings schema does not build as its lock stands: each warning above says what to do"
    )
  endif()

  # What the generator wrote, by kind. A run that writes a different set --
  # a group or an area added or gone -- changes the list, and the next build
  # configures again to take it.
  set(manifest "${arg_OUTPUT_DIR}/cvars.outputs")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${manifest}")
  file(STRINGS "${manifest}" entries)
  set(sources "")
  set(byproducts "")
  set(natvis "")
  foreach(entry IN LISTS entries)
    if(entry MATCHES "^([a-z]+) (.+)$")
      set(path "${arg_OUTPUT_DIR}/${CMAKE_MATCH_2}")
      list(APPEND byproducts "${path}")
      if(CMAKE_MATCH_1 STREQUAL "source")
        list(APPEND sources "${path}")
      elseif(CMAKE_MATCH_1 STREQUAL "natvis")
        set(natvis "${path}")
      endif()
    endif()
  endforeach()

  # The build machine's generator, which a configure builds again whenever
  # its sources change, before the build reaches this rule.
  set(stamp "${arg_OUTPUT_DIR}/cvars.stamp")
  add_custom_command(
    OUTPUT "${stamp}"
    COMMAND "${host_alcvargen}" ${record} ${args} ${inputs}
    COMMAND ${CMAKE_COMMAND} -E touch "${stamp}"
    BYPRODUCTS ${byproducts}
    DEPENDS "${host_alcvargen}" ${depends}
    WORKING_DIRECTORY "${repository}"
    COMMENT "Generating the settings schema ${arg_ROOT}"
    VERBATIM
  )
  target_sources(${target} PRIVATE ${sources} "${stamp}")
  set_source_files_properties(${sources} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
  target_include_directories(${target} PUBLIC "${arg_OUTPUT_DIR}/include")
  # A source of whatever links <target>, which the linker embeds in its
  # PDB, as Boost's are; a /NATVIS link option the Visual Studio generator
  # turns into a link setting that MSBuild ignores.
  if(natvis AND MSVC)
    target_sources(${target} INTERFACE "${natvis}")
  endif()
  source_group("Generated" FILES ${sources} "${stamp}")

  # Of the generator and the schema alone, so that it builds while the rule
  # above fails. The lock as HEAD holds it, read through git, folds the steps
  # a setting gained since into one; with no git there is none, and each
  # recording is a step of its own.
  find_package(Git QUIET)
  set(committed "${CMAKE_CURRENT_BINARY_DIR}/${arg_LOCK_TARGET}.committed")
  add_custom_target(
    ${arg_LOCK_TARGET}
    COMMAND
      ${CMAKE_COMMAND} "-DGIT=${GIT_EXECUTABLE}" "-DREPOSITORY=${repository}" "-DLOCK=${lock}"
      "-DOUT=${committed}" -P "${INDRA_SOURCE_DIR}/cmake/ALCVarCommittedLock.cmake"
    COMMAND "${host_alcvargen}" --write-lock --committed "${committed}" ${args} ${inputs}
    WORKING_DIRECTORY "${repository}"
    COMMENT "Recording the settings schema ${arg_ROOT} in ${lock}"
    VERBATIM
  )
  set_target_properties(${arg_LOCK_TARGET} PROPERTIES FOLDER "Tools")
endfunction()
