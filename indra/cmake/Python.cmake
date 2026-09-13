include_guard()

# Allow explicit Python path via environment variable
if(DEFINED ENV{PYTHON})
  set(Python3_ROOT_DIR "$ENV{PYTHON}")
endif()

# On Windows, prefer registry entries to avoid Cygwin/MSYS Python
# The registry is searched first by default, which finds native Windows Python
# installations rather than Cygwin/MSYS Python
if(WINDOWS)
  set(Python3_FIND_REGISTRY FIRST CACHE STRING "Python search order")
endif()

# We always want to find the active virtual env first
set(Python3_FIND_VIRTUALENV FIRST)

# The interpreter is for the tests that spawn a Python peer; without it they
# are registered disabled. Nothing that builds or packages the viewer runs it.
find_package(Python3 COMPONENTS Interpreter)
