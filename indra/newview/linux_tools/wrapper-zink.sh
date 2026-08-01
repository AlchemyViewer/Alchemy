#!/bin/bash

## Force Mesa's Zink (OpenGL-over-Vulkan) driver instead of the GPU's
## native GL driver. Everything else -- including GameMode and GPU
## selection -- lives in the regular "vayu" wrapper, which this script
## hands off to below.
export MESA_LOADER_DRIVER_OVERRIDE=zink

SCRIPTSRC=$(readlink -f "$0" || echo "$0")
RUN_PATH=$(dirname "${SCRIPTSRC}" || echo .)

exec "${RUN_PATH}/vayu" "$@"
