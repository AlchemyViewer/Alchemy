# -*- cmake -*-

add_library(lltut_runner_lib STATIC)

target_sources(lltut_runner_lib PRIVATE lltut.cpp test.cpp)

target_sources(
  lltut_runner_lib
  PUBLIC
    FILE_SET HEADERS
      FILES
        catch_and_store_what_in.h
        chained_callback.h
        debug.h
        llsdtraits.h
        lltestapp.h
        lltut.h
        namedtempfile.h
        print.h
        setenv.h
        sync.h
)

target_link_libraries(lltut_runner_lib PRIVATE al::flags PUBLIC llcommon ll::tut)

set_target_properties(lltut_runner_lib PROPERTIES FOLDER "Tests")

# The Catch2 harness: the main, what an LL_ERRS does, LOGFAIL's replay, the
# state guard, the teardown and the matchers. An OBJECT library, which each
# test binary links directly: in a static archive the listeners'
# registrations, which nothing references, would be dropped by the linker.
add_library(al_test_support OBJECT)

target_sources(
  al_test_support
  PRIVATE
    altestfatal.cpp
    altestlogreplay.cpp
    altestmain.cpp
    altestmatchers.cpp
    alteststateguard.cpp
    altestteardown.cpp
)

target_sources(
  al_test_support
  PUBLIC
    FILE_SET HEADERS
      FILES altestfatal.h altestlogreplay.h altestmatchers.h alteststateguard.h altestteardown.h
)

# The matchers compare as llmath's is_approx_equal_fraction() does, a header
# function, so the tests need llmath's headers but not its library.
target_include_directories(al_test_support PUBLIC ${INDRA_SOURCE_DIR}/llmath)
target_link_libraries(al_test_support PRIVATE al::flags PUBLIC llcommon ll::catch2)

set_target_properties(al_test_support PROPERTIES FOLDER "Tests")
