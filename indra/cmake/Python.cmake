# -*- cmake -*-

if (WINDOWS)
  # On Windows, explicitly avoid Cygwin Python.

  if (DEFINED ENV{VIRTUAL_ENV})
    find_program(Python2_EXECUTABLE
      NAMES python.exe
      PATHS
      "$ENV{VIRTUAL_ENV}\\scripts"
      NO_DEFAULT_PATH
      )
  else()
    find_program(Python2_EXECUTABLE
      NAMES python25.exe python23.exe python.exe
      NO_DEFAULT_PATH # added so that cmake does not find cygwin python
      PATHS
      [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\2.7\\InstallPath]
      [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\2.6\\InstallPath]
      [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\2.5\\InstallPath]
      [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\2.4\\InstallPath]
      [HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore\\2.3\\InstallPath]
      [HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore\\2.7\\InstallPath]
      [HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore\\2.6\\InstallPath]
      [HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore\\2.5\\InstallPath]
      [HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore\\2.4\\InstallPath]
      [HKEY_CURRENT_USER\\SOFTWARE\\Python\\PythonCore\\2.3\\InstallPath]
      )
  endif()

  if (NOT Python2_EXECUTABLE)
    message(FATAL_ERROR "No Python interpreter found")
  endif (NOT Python2_EXECUTABLE)

  mark_as_advanced(Python2_EXECUTABLE)
else (WINDOWS)
  find_package(Python2 REQUIRED COMPONENTS Interpreter)
endif (WINDOWS)
