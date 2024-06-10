#! /usr/bin/env sh

## Here are some configuration options for Linux Client Users.

## - Avoids using any FMOD STUDIO audio driver.
#export LL_BAD_FMODSTUDIO_DRIVER=x
## - Avoids using any OpenAL audio driver.
#export LL_BAD_OPENAL_DRIVER=x

## - Avoids using the FMOD Studio PulseAudio audio driver.
#export LL_BAD_FMOD_PULSEAUDIO=x
## - Avoids using the FMOD Studio ALSA audio driver.
#export LL_BAD_FMOD_ALSA=x

# Completely prevent gamemode from enabling even if set to true in the settings
# This can be useful if you run Alchemy on a battery-operated device (i.e. laptop)
# export DISABLE_GAMEMODE=1

## Everything below this line is just for advanced troubleshooters.
##-------------------------------------------------------------------

## - For advanced debugging cases, you can run the viewer under the
##   control of another program, such as strace, gdb, or valgrind.  If
##   you're building your own viewer, bear in mind that the executable
##   in the bin directory will be stripped: you should replace it with
##   an unstripped binary before you run.
if [ -n "${AL_GDB}" ]; then
    export LL_WRAPPER='gdb --args'
fi

if [ -n "${AL_VALGRIND}" ]; then
    export LL_WRAPPER='valgrind --smc-check=all --error-limit=no --log-file=secondlife.vg --leak-check=full --suppressions=/usr/lib/valgrind/glibc-2.5.supp --suppressions=secondlife-i686.supp'
fi

if [ -n "${AL_MANGO}" ]; then
    export LL_WRAPPER='mangohud --dlsym'
fi

## For controlling various sanitizer options
#export ASAN_OPTIONS="halt_on_error=0 detect_leaks=1 symbolize=1"
#export UBSAN_OPTIONS="print_stacktrace=1 print_summary=1 halt_on_error=0"

install_dir=$(dirname "$0")
build_data_file="${install_dir}/build_data.json"
if [ -f "${build_data_file}" ]; then
    channel=$(sed -n 's/.*"Channel"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "${build_data_file}")
else
    echo "Error: File ${build_data_file} not found." >&2
    channel="Alchemy" # Fail safely if we're unable to determine the channel
fi

## Allow Gnome 3 to properly display window title in app bar
export SDL_VIDEO_WAYLAND_WMCLASS=$channel
export SDL_VIDEO_X11_WMCLASS=$channel

## - Enable threaded mesa GL impl
export mesa_glthread=true

## - Enable nvidia threaded GL
export __GL_THREADED_OPTIMIZATIONS=1

## Nothing worth editing below this line.
##-------------------------------------------------------------------

SCRIPTSRC=$(readlink -f "$0" || echo "$0")
RUN_PATH=$(dirname "${SCRIPTSRC}" || echo .)
echo "Running from ${RUN_PATH}"
cd "${RUN_PATH}" || return

# Re-register the secondlife:// protocol handler every launch, for now.
# NOTE: this should no longer be required with the new desktop shortcut, combined with XDG integration.
#./etc/register_secondlifeprotocol.sh

# Re-register the application with the desktop system every launch, for now.
# NOTE: this should no longer be required with XDG integration. App icon should be created at install time, not run time.
#./etc/refresh_desktop_app_entry.sh

## Before we mess with LD_LIBRARY_PATH, save the old one to restore for
##  subprocesses that care.
export SAVED_LD_LIBRARY_PATH="${LD_LIBRARY_PATH}"

# Add our library directory
export LD_LIBRARY_PATH="$PWD/lib:${LD_LIBRARY_PATH}"

# Copy "$@" to ARGS string specifically to delete the --skip-gridargs switch.
# The gridargs.dat file is no more, but we still want to avoid breaking
# scripts that invoke this one with --skip-gridargs.
# Note: In sh, we don't have arrays like in Bash. So, we use a string instead to store the arguments.
ARGS=""
for ARG in "$@"; do
    if [ "--skip-gridargs" != "$ARG" ]; then
        ARGS="$ARGS \"$ARG\""
    fi
done

# Check chrome-sandbox permissions, and try to set them if they are not already
SANDBOX_BIN=bin/llplugin/chrome-sandbox
# if set-user-id = false || is writable || executable = false || read is false || is owned by effective uid || is owned by effective gid
OPTOUT_FILE="bin/llplugin/.user_does_not_want_chrome_sandboxing_and_accepts_the_risks"
if [ ! -u "$SANDBOX_BIN" ] || [ -w "$SANDBOX_BIN" ] || [ ! -x "$SANDBOX_BIN" ] || [ ! -r "$SANDBOX_BIN" ] || [ -O "$SANDBOX_BIN" ] || [ -G "$SANDBOX_BIN" ]; then
    echo "$SANDBOX_BIN permissions are not set properly to run under sandboxing."
    if [ ! -f "$OPTOUT_FILE" ]; then
        SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
        pkexec "$SCRIPT_DIR/etc/chrome_sandboxing_permissions_setup.sh"
    fi
fi

#setup wine voice
if command -v wine >/dev/null 2>&1; then
    export WINEDEBUG=-all # disable all debug output for wine
    export WINEPREFIX="$HOME/.alchemynext/wine"
    if [ ! -d "$WINEPREFIX" ]; then
        DISPLAY="" wine hostname
    fi
else
    export VIEWER_DISABLE_WINE=1
    echo "Please install wine to enable full voice functionality."
fi

# Check if switcheroo is needed
if [[ -d /sys/class/drm/card1 ]] && command -v switcherooctl >/dev/null 2>&1 && [[ "$(switcherooctl)" == "" ]]; then
  notify-send "Automatic GPU selection is not available" "Please enable switcheroo-control.service"
fi

# Run the program.
# Don't quote $LL_WRAPPER because, if empty, it should simply vanish from the
# command line. But DO quote "$ARGS": preserve separate args as
# individually quoted.
# Note: In sh, we don't have arrays like in Bash. So, we use a string instead to store the arguments.
eval "$LL_WRAPPER bin/do-not-directly-run-alchemy-bin $ARGS"
LL_RUN_ERR=$?

# Handle any resulting errors
if [ $LL_RUN_ERR -ne 0 ]; then
    # generic error running the binary
    echo "*** Bad shutdown ($LL_RUN_ERR). ***"
    if [ "$(uname -m)" = "x86_64" ]; then
        echo
        cat << EOFMARKER
You are running Alchemy Viewer on a x86_64 platform.
EOFMARKER
    fi
fi
