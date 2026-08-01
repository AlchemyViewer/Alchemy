#!/bin/bash

SCRIPTSRC=$(readlink -f "$0" || echo "$0")
RUN_PATH=$(dirname "${SCRIPTSRC}" || echo .)

install_prefix="$(realpath -- "${RUN_PATH}/..")"

# Installs $desktop_entries_dir/$filename from $content, but only if $content
# has actually changed since we last wrote it (tracked via a hidden sha256
# stamp file next to the .desktop entry -- hidden so desktop environments'
# *.desktop directory scans ignore it). desktop-file-install and
# update-desktop-database are real work (they rewrite desktop/MIME caches);
# this script used to run them unconditionally on every single launch of the
# viewer, which was pointless I/O for the overwhelmingly common case where
# nothing changed since last time. Still self-healing: if the .desktop file
# is missing (e.g. the user deleted it) or its content would differ (e.g. an
# upgrade changed the Exec path), it gets reinstalled.
function install_desktop_entry_if_changed()
{
    local filename="$1"
    local desktop_entries_dir="$2"
    local content="$3"

    local target="${desktop_entries_dir}/${filename}"
    local stamp="${desktop_entries_dir}/.${filename}.sha256"
    local new_hash
    new_hash="$(echo -e "$content" | sha256sum | cut -d' ' -f1)"

    if [ -f "$target" ] && [ -f "$stamp" ] && [ "$(cat "$stamp")" == "$new_hash" ]; then
        return
    fi

    echo " - Installing menu entry ${filename} in ${desktop_entries_dir}"
    WORK_DIR=$(mktemp -d)
    echo -e "$content" > "${WORK_DIR}/${filename}" || echo "Failed to install application menu!"
    desktop-file-install --dir="${desktop_entries_dir}" "${WORK_DIR}/${filename}"
    rm -r "$WORK_DIR"

    echo "$new_hash" > "$stamp"
    UPDATED=1
}

function install_desktop_entries()
{
    local installation_prefix="$1"
    local desktop_entries_dir="$2"

    UPDATED=

    # Actions= + a [Desktop Action ...] section is the freedesktop Desktop
    # Actions spec -- it surfaces as a right-click context-menu item on the
    # launcher icon (GNOME Shell, KDE Plasma, etc.), rather than a second,
    # separate app entry. GPU selection (switcherooctl) and GameMode are
    # handled explicitly by the "vayu" wrapper script itself rather than via
    # the PrefersNonDefaultGPU hint here, since that hint is only honored by
    # some desktop environments and does nothing when launching from a
    # terminal.
    local main_entry="\
[Desktop Entry]\n\
Name=Vayu Viewer\n\
GenericName=Vayu Viewer\n\
Comment=Client for the On-line Virtual World, Second Life\n\
Exec=${installation_prefix}/vayu\n\
Path=${installation_prefix}\n\
Icon=${installation_prefix}/vayu_icon.png\n\
Terminal=false\n\
Type=Application\n\
Categories=Game;Simulation;\n\
StartupNotify=true\n\
StartupWMClass="org.vayuviewer.viewer"\n\
Actions=Zink;\n\
X-Desktop-File-Install-Version=3.0\n\
\n\
[Desktop Action Zink]\n\
Name=Launch with Zink (OpenGL-over-Vulkan)\n\
Exec=${installation_prefix}/vayu-zink\n\
Icon=${installation_prefix}/vayu_icon.png"

    install_desktop_entry_if_changed "vayu-viewer.desktop" "$desktop_entries_dir" "$main_entry"

    if [ -n "$UPDATED" ]; then
        update-desktop-database "${desktop_entries_dir}"
    fi
}

if [ "$UID" == "0" ]; then
    # system-wide
    install_desktop_entries "$install_prefix" /usr/local/share/applications
else
    # user-specific
    install_desktop_entries "$install_prefix" "$HOME/.local/share/applications"
fi
