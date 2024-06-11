#!/usr/bin/env sh

SCRIPTSRC="$(readlink -f "$0" || echo "$0")"
RUN_PATH="$(dirname "${SCRIPTSRC}" || echo .)"

install_prefix="${RUN_PATH}"/..

build_data_file="${install_prefix}/build_data.json"
if [ -f "${build_data_file}" ]; then
    version=$(sed -n 's/.*"Version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "${build_data_file}")
    channel_base=$(sed -n 's/.*"Channel Base"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "${build_data_file}")
    channel=$(sed -n 's/.*"Channel"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "${build_data_file}")
    desktopfilename=$(echo "$channel" | tr '[:upper:]' '[:lower:]' | tr ' ' '-' )-viewer.desktop
else
    echo "Error: File ${build_data_file} not found." >&2
    exit 1
fi

# Check for the Release channel. This channel should not have the channel name in its launcher.
if [ "$channel" = "Alchemy Release" ]; then
    launcher_name="Alchemy"
else
    launcher_name=$channel
fi

install_desktop_entry()
{
    installation_prefix="${1}"
    desktop_entries_dir="${2}"

    desktop_entry="\
[Desktop Entry]\n\
Name=${launcher_name}\n\
Comment=Client for the On-line Virtual World, Second Life\n\
Exec=${installation_prefix}/alchemy\n\
Icon=${installation_prefix}/alchemy_icon.png\n\
Terminal=false\n\
Type=Application\n\
Categories=Game;Simulation;\n\
StartupNotify=true\n\
StartupWMClass=${channel}\n\
X-Desktop-File-Install-Version=3.0"

    printf " - Installing menu entries in %s\n" "${desktop_entries_dir}"
    mkdir -vp "${desktop_entries_dir}"
    printf "%b" "${desktop_entry}" > "${desktop_entries_dir}/${desktopfilename}" || echo "Failed to install application menu!"
}

if [ "$(id -u)" = "0" ]; then
    # system-wide
    install_desktop_entry "${install_prefix}" /usr/local/share/applications
else
    # user-specific
    install_desktop_entry "${install_prefix}" "${HOME}/.local/share/applications"
fi
