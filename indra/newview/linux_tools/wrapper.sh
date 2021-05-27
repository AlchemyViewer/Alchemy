#! /usr/bin/env bash

## Here are some configuration options for Linux Client Testers.

## - Avoids using any FMOD STUDIO audio driver.
#export LL_BAD_FMODSTUDIO_DRIVER=x
## - Avoids using any OpenAL audio driver.
#export LL_BAD_OPENAL_DRIVER=x

## - Avoids using the FMOD Studio or FMOD Ex PulseAudio audio driver.
#export LL_BAD_FMOD_PULSEAUDIO=x
## - Avoids using the FMOD Studio or FMOD Ex ALSA audio driver.
#export LL_BAD_FMOD_ALSA=x

## - Avoids the optional OpenGL extensions which have proven most problematic
##   on some hardware.  Disabling this option may cause BETTER PERFORMANCE but
##   may also cause CRASHES and hangs on some unstable combinations of drivers
##   and hardware.
## NOTE: This is now disabled by default.
#export LL_GL_BASICEXT=x

## - Avoids *all* optional OpenGL extensions.  This is the safest and least-
##   exciting option.  Enable this if you experience stability issues, and
##   report whether it helps in the Linux Client Testers forum.
#export LL_GL_NOEXT=x

## - For advanced troubleshooters, this lets you disable specific GL
##   extensions, each of which is represented by a letter a-o.  If you can
##   narrow down a stability problem on your system to just one or two
##   extensions then please post details of your hardware (and drivers) to
##   the Linux Client Testers forum along with the minimal
##   LL_GL_BLACKLIST which solves your problems.
#export LL_GL_BLACKLIST=abcdefghijklmno

if [ "`uname -m`" = "x86_64" ]; then
    echo '64-bit Linux detected.'
fi


## Everything below this line is just for advanced troubleshooters.
##-------------------------------------------------------------------

## - For advanced debugging cases, you can run the viewer under the
##   control of another program, such as strace, gdb, or valgrind.  If
##   you're building your own viewer, bear in mind that the executable
##   in the bin directory will be stripped: you should replace it with
##   an unstripped binary before you run.
if [[ -v AL_GDB ]]; then
    export LL_WRAPPER='gdb --args'
fi

if [[ -v AL_VALGRIND ]]; then
    export LL_WRAPPER='valgrind --smc-check=all --error-limit=no --log-file=secondlife.vg --leak-check=full --suppressions=/usr/lib/valgrind/glibc-2.5.supp --suppressions=secondlife-i686.supp'
fi

if [[ -v AL_MANGO ]]; then
    export MANGOHUD_DLSYM=1
	export LL_WRAPPER='mangohud'
fi

## For controlling various sanitizer options
#export ASAN_OPTIONS="halt_on_error=0 detect_leaks=1 symbolize=1"
#export UBSAN_OPTIONS="print_stacktrace=1 print_summary=1 halt_on_error=0"

## Allow Gnome 3 to properly display window title in app bar
export SDL_VIDEO_X11_WMCLASS=Alchemy

## - Enable threaded mesa GL impl
export mesa_glthread=true


## Nothing worth editing below this line.
##-------------------------------------------------------------------

SCRIPTSRC=`readlink -f "$0" || echo "$0"`
RUN_PATH=`dirname "${SCRIPTSRC}" || echo .`
echo "Running from ${RUN_PATH}"
cd "${RUN_PATH}"

# Re-register the secondlife:// protocol handler every launch, for now.
./etc/register_secondlifeprotocol.sh

# Re-register the application with the desktop system every launch, for now.
./etc/refresh_desktop_app_entry.sh

## Before we mess with LD_LIBRARY_PATH, save the old one to restore for
##  subprocesses that care.
export SAVED_LD_LIBRARY_PATH="${LD_LIBRARY_PATH}"

# Add our library directory
export LD_LIBRARY_PATH="$PWD/lib:${LD_LIBRARY_PATH}"

# Copy "$@" to ARGS array specifically to delete the --skip-gridargs switch.
# The gridargs.dat file is no more, but we still want to avoid breaking
# scripts that invoke this one with --skip-gridargs.
ARGS=()
for ARG in "$@"; do
    if [ "--skip-gridargs" != "$ARG" ]; then
        ARGS[${#ARGS[*]}]="$ARG"
    fi
done

# Check chrome-sandbox permissions, and try to set them if they are not already
SANDBOX_BIN=bin/chrome-sandbox
# if set-user-id = false || is writable || executable = false || read is false || is owned by effective uid || is owned by effective gid
if [[ !(-u $SANDBOX_BIN) || (-w $SANDBOX_BIN) || !(-x $SANDBOX_BIN) || !(-r $SANDBOX_BIN) || ( -O $SANDBOX_BIN) || (-G $SANDBOX_BIN) ]]; then
    echo "$SANDBOX_BIN permissions are incorrect and will be reset"
    SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
    pkexec "$SCRIPT_DIR/etc/chrome_sandboxing_permissions_setup.sh"
fi

# Run the program.
# Don't quote $LL_WRAPPER because, if empty, it should simply vanish from the
# command line. But DO quote "${ARGS[@]}": preserve separate args as
# individually quoted.
$LL_WRAPPER bin/do-not-directly-run-alchemy-bin "${ARGS[@]}"
LL_RUN_ERR=$?

# Handle any resulting errors
if [ $LL_RUN_ERR -ne 0 ]; then
	# generic error running the binaryecho '*** Bad shutdown ($LL_RUN_ERR). ***'
	if [ "$(uname -m)" = "x86_64" ]; then
		echo
		cat << EOFMARKER
You are running Alchemy Viewer on a x86_64 platform.
EOFMARKER
	fi
fi

echo
echo '*******************************************************'
echo 'This is a BETA release of the Alchemy Viewer Linux client.'
echo 'Thank you for testing!'
echo 'Please see README-linux.txt before reporting problems.'
echo
