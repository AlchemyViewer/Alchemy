# -*- cmake -*-

if (WINDOWS)
  # On Windows, explicitly avoid Cygwin Python.

  if (DEFINED ENV{VIRTUAL_ENV})
    find_program(Python3_EXECUTABLE
      NAMES python3.exe python.exe
      PATHS
      "$ENV{VIRTUAL_ENV}\\scripts"
      NO_DEFAULT_PATH
      )
  else()
    find_program(Python3_EXECUTABLE
      NAMES python3.exe python.exe
      NO_DEFAULT_PATH # added so that cmake does not find cygwin python
      PATHS
      [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\3.9\\InstallPath]
      [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\3.8\\InstallPath]
      [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\3.7\\InstallPath]
      [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\3.6\\InstallPath]
      [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\3.5\\InstallPath]
      [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\3.4\\InstallPath]
      [HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore\\3.9\\InstallPath]
      [HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore\\3.8\\InstallPath]
      [HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore\\3.7\\InstallPath]
      [HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore\\3.6\\InstallPath]
      [HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore\\3.5\\InstallPath]
      [HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore\\3.4\\InstallPath]
      )
  endif()

  if (NOT Python3_EXECUTABLE)
    message(FATAL_ERROR "No Python interpreter found")
  endif (NOT Python3_EXECUTABLE)

  mark_as_advanced(Python3_EXECUTABLE)
else (WINDOWS)
  if (DEFINED ENV{VIRTUAL_ENV})
    find_program(Python3_EXECUTABLE
      NAMES python3 python
      PATHS
      "$ENV{VIRTUAL_ENV}/bin"
      NO_DEFAULT_PATH
      )
  else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
  endif()
endif (WINDOWS)
