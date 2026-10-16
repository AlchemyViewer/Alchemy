#!/usr/bin/env bash

## Here are some configuration options for Linux Client Users.

## - Avoids using any OpenAL audio driver.
#export LL_BAD_OPENAL_DRIVER=x

## - Leaves the application menu alone: the viewer is not added to it, nor
##   made the handler of secondlife:// links, when it is run.
#export AL_NO_DESKTOP_INTEGRATION=1

## GL Driver Options
## - Mesa's threaded GL dispatch, on unless mesa_glthread is set already:
##   mesa_glthread=false in the environment turns it off.
export mesa_glthread="${mesa_glthread:-true}"

## Everything below this line is just for advanced troubleshooters.
##-------------------------------------------------------------------

## - For advanced debugging cases, you can run the viewer under the
##   control of another program, such as strace, gdb, or valgrind.  If
##   you're building your own viewer, bear in mind that the executable
##   in the bin directory will be stripped: you should replace it with
##   an unstripped binary before you run.
#export LL_WRAPPER='gdb --args'
#export LL_WRAPPER='valgrind --smc-check=all --error-limit=no --log-file=secondlife.vg --leak-check=full --suppressions=/usr/lib/valgrind/glibc-2.5.supp'
#export ASAN_OPTIONS="halt_on_error=0 detect_leaks=1 symbolize=1"
#export UBSAN_OPTIONS="print_stacktrace=1 print_summary=1 halt_on_error=0"

## Nothing worth editing below this line.
##-------------------------------------------------------------------

# Run from a symlink, such as one on the PATH, too: the tree is where the
# script itself is.
script=$(readlink -f -- "${BASH_SOURCE[0]}")
cd -- "$(dirname -- "$script")" || exit 1

# Adds this tree to the application menu and makes it the handler of
# secondlife:// links when nothing has, or points them here from another
# tree of the same channel. A failure is not the viewer's.
if [[ -x etc/desktop_integration.sh ]]; then
    etc/desktop_integration.sh refresh || echo "Desktop integration failed; see etc/desktop_integration.sh" >&2
fi

# --skip-gridargs is gone with gridargs.dat; scripts still pass it.
args=()
for arg in "$@"; do
    if [[ $arg != "--skip-gridargs" ]]; then
        args+=("$arg")
    fi
done

# A secondlife:// or x-grid-location-info:// link, which the desktop entry
# passes, is the viewer's positional argument. $LL_WRAPPER is unquoted so it
# vanishes when empty.
# shellcheck disable=SC2086
exec $LL_WRAPPER bin/alchemy-bin "${args[@]}"
