# -*- cmake -*-
#
# The symbol store: every binary the viewer ships with its debug information
# beside it, which is what Sentry symbolicates crash reports with and what a
# debugger needs to read a shipped build. Run by the `symbols` target as
# `cmake -D... -P ViewerSymbols.cmake`, over the tree the viewer's staging
# step installed, with:
#
#   AL_SYMBOLS_FORMAT    elf, macho or pe
#   AL_SYMBOLS_MANIFEST  the staging install's manifest: the files that ship
#   AL_SYMBOLS_VIEWER    the viewer executable, which the manifest leaves out:
#                        the viewer links into the staged tree, and the
#                        install records nothing it finds already in place
#   AL_SYMBOLS_PREFIX    the prefix the staging installed to
#   AL_SYMBOLS_DIR       the store, made again from nothing on every run
#   AL_SYMBOLS_OBJCOPY   elf: objcopy, to split the debug information off
#   AL_SYMBOLS_READELF   elf: readelf, for build IDs and section lists
#   AL_SYMBOLS_DSYMUTIL  macho: dsymutil
#   AL_SYMBOLS_DUMPBIN   pe: dumpbin, for the PDB each binary names
#   AL_SYMBOLS_VCPKG     pe: the vcpkg installed tree, whose PDBs are found
#                        by name when the path the binary names is gone
#
# The store per format:
#   elf    by build ID, as gdb, debuginfod and distribution debug packages
#          lay one out: .build-id/xx/yyyy is the binary without its debug
#          information, .build-id/xx/yyyy.debug the debug information alone
#          with its sections compressed with zstd.
#   macho  the shipped tree's Mach-O files at their paths in it, each with
#          a dSYM beside it where it has a debug map to make one from.
#   pe     the shipped tree's executables and DLLs at their paths in it,
#          each with the PDB it names beside it.
#
# Mach-O and PE binaries are hard links into the staged tree, so they cost
# the store nothing. Binaries without debug information still go in: their
# symbol tables and unwind information let a crash report walk through them.

foreach(
  var
  AL_SYMBOLS_FORMAT
  AL_SYMBOLS_MANIFEST
  AL_SYMBOLS_VIEWER
  AL_SYMBOLS_PREFIX
  AL_SYMBOLS_DIR
)
  if("${${var}}" STREQUAL "")
    message(FATAL_ERROR "ViewerSymbols.cmake needs ${var}")
  endif()
endforeach()
if(AL_SYMBOLS_FORMAT STREQUAL "elf")
  set(tools AL_SYMBOLS_OBJCOPY AL_SYMBOLS_READELF)
elseif(AL_SYMBOLS_FORMAT STREQUAL "macho")
  set(tools AL_SYMBOLS_DSYMUTIL)
elseif(AL_SYMBOLS_FORMAT STREQUAL "pe")
  set(tools AL_SYMBOLS_DUMPBIN)
else()
  message(FATAL_ERROR "AL_SYMBOLS_FORMAT is elf, macho or pe, not ${AL_SYMBOLS_FORMAT}")
endif()
foreach(var IN LISTS tools)
  if(NOT EXISTS "${${var}}")
    message(FATAL_ERROR "ViewerSymbols.cmake needs ${var} for ${AL_SYMBOLS_FORMAT}: '${${var}}'")
  endif()
endforeach()
if(NOT EXISTS "${AL_SYMBOLS_MANIFEST}")
  message(FATAL_ERROR "No staging manifest at ${AL_SYMBOLS_MANIFEST}: build the viewer first")
endif()

file(REMOVE_RECURSE "${AL_SYMBOLS_DIR}")
file(MAKE_DIRECTORY "${AL_SYMBOLS_DIR}")
file(STRINGS "${AL_SYMBOLS_MANIFEST}" files)
list(APPEND files "${AL_SYMBOLS_VIEWER}")
list(REMOVE_DUPLICATES files)

# Runs a tool and fails with its output when the tool fails.
function(al_symbols_run)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
  )
  if(result)
    list(JOIN ARGN " " command)
    message(FATAL_ERROR "${command} failed (${result}):\n${output}")
  endif()
endfunction()

# Links a file into the store, or copies it where a link cannot be made.
function(al_symbols_link path destination)
  cmake_path(GET destination PARENT_PATH directory)
  file(MAKE_DIRECTORY "${directory}")
  file(CREATE_LINK "${path}" "${destination}" COPY_ON_ERROR)
endfunction()

# The PDBs of the prebuilt libraries, by lower-case file name. Not debug/:
# its PDBs have the same names as the release ones.
if(AL_SYMBOLS_FORMAT STREQUAL "pe" AND AL_SYMBOLS_VCPKG)
  file(GLOB vcpkg_pdbs "${AL_SYMBOLS_VCPKG}/bin/*.pdb")
  file(GLOB_RECURSE vcpkg_tool_pdbs "${AL_SYMBOLS_VCPKG}/tools/*.pdb")
  foreach(pdb IN LISTS vcpkg_pdbs vcpkg_tool_pdbs)
    cmake_path(GET pdb FILENAME name)
    string(TOLOWER "${name}" name)
    string(MAKE_C_IDENTIFIER "${name}" key)
    set("vcpkg_pdb_${key}" "${pdb}")
  endforeach()
endif()

set(compression zstd)
set(binary_count 0)
set(debug_count 0)

foreach(path IN LISTS files)
  if(IS_SYMLINK "${path}" OR NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
    continue()
  endif()
  file(RELATIVE_PATH relative "${AL_SYMBOLS_PREFIX}" "${path}")

  if(AL_SYMBOLS_FORMAT STREQUAL "elf")
    file(READ "${path}" magic LIMIT 4 HEX)
    if(NOT magic STREQUAL "7f454c46")
      continue()
    endif()
    execute_process(
      COMMAND "${AL_SYMBOLS_READELF}" --notes --section-headers --wide "${path}"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE headers
      ERROR_VARIABLE headers
    )
    if(result)
      message(FATAL_ERROR "${AL_SYMBOLS_READELF} failed (${result}) on ${path}:\n${headers}")
    endif()
    if(NOT headers MATCHES "Build ID: ([0-9a-f]+)")
      message(WARNING "${relative} has no build ID to file it under; it is not in the store")
      continue()
    endif()
    string(SUBSTRING "${CMAKE_MATCH_1}" 0 2 head)
    string(SUBSTRING "${CMAKE_MATCH_1}" 2 -1 tail)
    set(destination "${AL_SYMBOLS_DIR}/.build-id/${head}/${tail}")
    # The same binary installed under two names is filed once.
    if(EXISTS "${destination}")
      continue()
    endif()
    file(MAKE_DIRECTORY "${AL_SYMBOLS_DIR}/.build-id/${head}")
    math(EXPR binary_count "${binary_count} + 1")
    if(NOT headers MATCHES "[ \t]\\.z?debug_info[ \t]")
      al_symbols_link("${path}" "${destination}")
      continue()
    endif()
    # zstd where this objcopy was built with it, zlib where it was not.
    execute_process(
      COMMAND
        "${AL_SYMBOLS_OBJCOPY}" --only-keep-debug --compress-debug-sections=${compression} "${path}"
        "${destination}.debug"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE output
      ERROR_VARIABLE output
    )
    if(result AND compression STREQUAL "zstd")
      message(
        WARNING
        "${AL_SYMBOLS_OBJCOPY} cannot compress with zstd; the store uses zlib:\n${output}"
      )
      set(compression zlib)
      al_symbols_run(
        "${AL_SYMBOLS_OBJCOPY}"
        --only-keep-debug
        --compress-debug-sections=zlib
        "${path}"
        "${destination}.debug"
      )
    elseif(result)
      message(FATAL_ERROR "${AL_SYMBOLS_OBJCOPY} failed (${result}) on ${path}:\n${output}")
    endif()
    al_symbols_run("${AL_SYMBOLS_OBJCOPY}" --strip-debug "${path}" "${destination}")
    math(EXPR debug_count "${debug_count} + 1")
  elseif(AL_SYMBOLS_FORMAT STREQUAL "macho")
    # Thin 64-bit Mach-O, or universal.
    file(READ "${path}" magic LIMIT 4 HEX)
    if(NOT magic MATCHES "^(cffaedfe|cafebabe)$")
      continue()
    endif()
    set(destination "${AL_SYMBOLS_DIR}/${relative}")
    al_symbols_link("${path}" "${destination}")
    math(EXPR binary_count "${binary_count} + 1")
    # dsymutil reads the debug map the linker left, which points at the
    # object files. A binary built elsewhere has none, and dsymutil says so.
    execute_process(
      COMMAND "${AL_SYMBOLS_DSYMUTIL}" -o "${destination}.dSYM" "${path}"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE output
      ERROR_VARIABLE output
    )
    if(output MATCHES "no debug symbols in executable")
      file(REMOVE_RECURSE "${destination}.dSYM")
    elseif(result)
      message(FATAL_ERROR "${AL_SYMBOLS_DSYMUTIL} failed (${result}) on ${path}:\n${output}")
    else()
      string(REGEX MATCHALL "unable to open object file" missing "${output}")
      list(LENGTH missing missing)
      if(missing)
        message(
          WARNING
          "The dSYM of ${relative} lacks ${missing} object files dsymutil could not open"
        )
      endif()
      math(EXPR debug_count "${debug_count} + 1")
    endif()
  else()
    string(TOLOWER "${path}" lower)
    if(NOT lower MATCHES "\\.(exe|dll)$")
      continue()
    endif()
    set(destination "${AL_SYMBOLS_DIR}/${relative}")
    al_symbols_link("${path}" "${destination}")
    math(EXPR binary_count "${binary_count} + 1")
    # The CodeView record names the PDB the linker wrote. A prebuilt
    # library's names the tree it was built in, so it is found by name.
    execute_process(
      COMMAND "${AL_SYMBOLS_DUMPBIN}" /nologo /headers "${path}"
      RESULT_VARIABLE result
      OUTPUT_VARIABLE headers
      ERROR_VARIABLE headers
    )
    if(result)
      message(FATAL_ERROR "${AL_SYMBOLS_DUMPBIN} failed (${result}) on ${path}:\n${headers}")
    endif()
    if(NOT headers MATCHES "Format: RSDS, {[^}]*}, [0-9]+, ([^\r\n]+)")
      continue()
    endif()
    string(STRIP "${CMAKE_MATCH_1}" pdb)
    file(TO_CMAKE_PATH "${pdb}" pdb)
    cmake_path(GET pdb FILENAME pdb_name)
    if(NOT EXISTS "${pdb}")
      string(TOLOWER "${pdb_name}" key)
      string(MAKE_C_IDENTIFIER "${key}" key)
      if(NOT DEFINED "vcpkg_pdb_${key}")
        continue()
      endif()
      set(pdb "${vcpkg_pdb_${key}}")
    endif()
    cmake_path(GET destination PARENT_PATH directory)
    if(NOT EXISTS "${directory}/${pdb_name}")
      al_symbols_link("${pdb}" "${directory}/${pdb_name}")
      math(EXPR debug_count "${debug_count} + 1")
    endif()
  endif()
endforeach()

message(
  STATUS
  "Symbol store ${AL_SYMBOLS_DIR}: ${binary_count} binaries, ${debug_count} with debug files"
)
