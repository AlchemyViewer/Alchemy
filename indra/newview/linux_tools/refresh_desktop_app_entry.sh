#!/bin/bash

SCRIPTSRC=$(readlink -f "$0" || echo "$0")
RUN_PATH=$(dirname "${SCRIPTSRC}" || echo .)

install_prefix="$(realpath -- "${RUN_PATH}/..")"

function install_desktop_entry()
{
    local installation_prefix="$1"
    local desktop_entries_dir="$2"

    local desktop_entry="\
[Desktop Entry]\n\
Name=Vayu Viewer\n\
GenericName=Vayu Viewer\n\
Comment=Client for the On-line Virtual World, Second Life\n\
Path=${installation_prefix}\n\
Exec=${installation_prefix}/vayu\n\
Icon=${installation_prefix}/vayu_icon.png\n\
Terminal=false\n\
Type=Application\n\
Categories=Game;Simulation;\n\
StartupNotify=true\n\
StartupWMClass="org.vayuviewer.viewer"\n\
X-Desktop-File-Install-Version=3.0"

    echo " - Installing menu entries in ${desktop_entries_dir}"
    WORK_DIR=$(mktemp -d)
    echo -e "$desktop_entry" > "${WORK_DIR}/vayu-viewer.desktop" || "Failed to install application menu!"
    desktop-file-install --dir="${desktop_entries_dir}" "${WORK_DIR}/vayu-viewer.desktop"
    rm -r "$WORK_DIR"

    update-desktop-database "${desktop_entries_dir}"
}

if [ "$UID" == "0" ]; then
    # system-wide
    install_desktop_entry "$install_prefix" /usr/local/share/applications
else
    # user-specific
    install_desktop_entry "$install_prefix" "$HOME/.local/share/applications"
fi
