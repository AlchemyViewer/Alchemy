#!/bin/bash

## Here are some configuration options for Linux Client Users.

## - Avoids using any OpenAL audio driver.
#export LL_BAD_OPENAL_DRIVER=x

## GL Driver Options
export mesa_glthread=true

## --- AMD PERFORMANCE TUNING (carried over from the Firestorm High
##     Performance launcher) ---
## AMD_DEBUG=lowprecision trades a small amount of rendering precision
## for throughput on radeonsi/radv. Harmless no-op on non-AMD Mesa
## drivers. Comment out if you see visual artifacts.
export AMD_DEBUG=lowprecision

## Swap in mimalloc as the allocator -- measurably lower allocator
## overhead than glibc malloc for this workload. Path assumes an
## RPM-family distro layout (openSUSE/Fedora); adjust for others
## (e.g. /usr/lib/x86_64-linux-gnu/libmimalloc.so.3 on Debian/Ubuntu).
## A missing path here just produces a harmless ld.so warning, it
## doesn't stop the viewer from starting.
export LD_PRELOAD=/usr/lib64/libmimalloc.so.3
# export LD_PRELOAD=/usr/lib64/libjemalloc.so.2

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

# Our statically-linked OpenSSL still reads the system's /etc/ssl/openssl.cnf
# (compiled-in OPENSSLDIR). On distros whose crypto-policy include file uses
# properties our vendored OpenSSL doesn't recognize (e.g. openSUSE/Fedora's
# rh-allow-sha1-signatures), the very first TLS handshake of the process
# fails config parsing and curl misreports it as CURLE_OUT_OF_MEMORY; every
# handshake after that succeeds since OpenSSL doesn't retry the failed load.
# Skip the system config entirely so we always get OpenSSL's built-in defaults.
export OPENSSL_CONF=/dev/null

SCRIPTSRC=$(readlink -f "$0" || echo "$0")
RUN_PATH=$(dirname "${SCRIPTSRC}" || echo .)
echo "Running from ${RUN_PATH}"
cd "${RUN_PATH}" || exit

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

# gamemoderun asks the gamemode daemon for performance-mode tuning (CPU
# governor, GPU power profile, etc.) for the process tree below it.
# switcherooctl launch runs that process tree on the system's preferred
# (usually discrete/more powerful) GPU via switcheroo-control. We do this
# directly here rather than relying on desktop-entry hints like
# PrefersNonDefaultGPU, since those are only honored by some desktop
# environments and don't cover launching from a terminal at all. Both are
# optional performance tools: skip whichever isn't installed rather than
# failing to launch, and skip both under LL_WRAPPER (gdb/valgrind/etc.)
# since advanced troubleshooters want a plain, unwrapped process tree.
CMD=()
if [ -z "$LL_WRAPPER" ]; then
    if command -v gamemoderun >/dev/null 2>&1; then
        CMD+=(gamemoderun)
    else
        echo "gamemoderun not found; launching without GameMode." >&2
    fi
    if command -v switcherooctl >/dev/null 2>&1; then
        CMD+=(switcherooctl launch)
    else
        echo "switcherooctl not found; launching without explicit GPU selection." >&2
    fi
fi

# Run the program.
# Don't quote $LL_WRAPPER because, if empty, it should simply vanish from the
# command line. But DO quote "${ARGS[@]}": preserve separate args as
# individually quoted.
CMD+=($LL_WRAPPER bin/vayu-bin "${ARGS[@]}")
"${CMD[@]}"
LL_RUN_ERR=$?

# Handle any resulting errors
if [ $LL_RUN_ERR -ne 0 ]; then
	# generic error running the binary
	echo "*** Bad shutdown ($LL_RUN_ERR). ***"
fi
