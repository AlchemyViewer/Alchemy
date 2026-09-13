include_guard()

# Allow explicit Python path via environment variable
if(DEFINED ENV{PYTHON})
  set(Python3_ROOT_DIR "$ENV{PYTHON}")
endif()

# On Windows the registry names the native installations; a Cygwin or MSYS
# python on the PATH would otherwise be found first.
if(WINDOWS)
  set(Python3_FIND_REGISTRY FIRST)
endif()

# We always want to find the active virtual env first
set(Python3_FIND_VIRTUALENV FIRST)

# The interpreter is for the tests that spawn a Python peer; without it they
# are registered disabled. Nothing that builds or packages the viewer runs it.
find_package(Python3 COMPONENTS Interpreter)
