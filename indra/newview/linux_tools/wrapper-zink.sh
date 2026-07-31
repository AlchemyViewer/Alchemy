#!/bin/bash

## Force Mesa's Zink (OpenGL-over-Vulkan) driver instead of the GPU's
## native GL driver. All other environment tuning lives in the regular
## "vayu" wrapper, which this script hands off to below.
export MESA_LOADER_DRIVER_OVERRIDE=zink

SCRIPTSRC=$(readlink -f "$0" || echo "$0")
RUN_PATH=$(dirname "${SCRIPTSRC}" || echo .)
cd "${RUN_PATH}" || exit

# gamemoderun asks the gamemode daemon for performance-mode tuning (CPU
# governor, GPU power profile, etc.) for the process tree below it.
# switcherooctl launch runs that process tree on the system's preferred
# (usually discrete/more powerful) GPU via switcheroo-control -- the same
# mechanism the PrefersNonDefaultGPU desktop-entry hint relies on, but
# explicit here so it also works from launchers that don't honor that hint.
# Both are optional performance tools: skip whichever isn't installed
# rather than failing to launch at all.
CMD=()
if command -v gamemoderun >/dev/null 2>&1; then
    CMD+=(gamemoderun)
else
    echo "gamemoderun not found; launching without GameMode." >&2
fi
if command -v switcherooctl >/dev/null 2>&1; then
    CMD+=(switcherooctl launch --)
else
    echo "switcherooctl not found; launching without explicit GPU selection." >&2
fi
CMD+=("${RUN_PATH}/vayu" "$@")

exec "${CMD[@]}"
