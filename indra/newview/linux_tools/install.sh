#!/usr/bin/env sh

# Install Alchemy Viewer. This script can install the viewer both
# system-wide and for an individual user.

build_data_file="build_data.json"
if [ -f "${build_data_file}" ]; then
    version=$(sed -n 's/.*"Version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "${build_data_file}")
    channel=$(sed -n 's/.*"Channel"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "${build_data_file}")
    installdir_name=$(echo "$channel" | tr '[:upper:]' '[:lower:]' | tr ' ' '-' )-install
else
    echo "Error: File ${build_data_file} not found." >&2
    exit 1
fi

echo "Installing ${channel} version ${version}"

VT102_STYLE_NORMAL='\E[0m'
VT102_COLOR_RED='\E[31m'

SCRIPTSRC=$(readlink -f "$0" || echo "$0")
RUN_PATH=$(dirname "${SCRIPTSRC}" || echo .)
tarball_path=${RUN_PATH}

prompt()
{
    prompt=$1

    printf "%s" "$prompt"

    while read -r input; do
        case $input in
            [Yy]* )
                return 1
                ;;
            [Nn]* )
                return 0
                ;;
            * )
                echo "Please enter yes or no."
                printf "%s" "$prompt"
        esac
    done
}

die()
{
    warn "$1"
    exit 1
}

warn()
{
    printf "%b%b%b\n" "$VT102_COLOR_RED" "$1" "$VT102_STYLE_NORMAL"
}


homedir_install()
{
    warn "You are not running as a privileged user, so you will only be able"
    warn "to install Alchemy Viewer in your home directory. If you"
    warn "would like to install Alchemy Viewer system-wide, please run"
    warn "this script as the root user, or with the 'sudo' command."
    echo

    prompt "Proceed with the installation? [Y/N]: "
    if [ $? -eq 0 ]; then
    exit 0
    fi

    if [ -d "$XDG_DATA_HOME" ] ; then
        install_to_prefix "$XDG_DATA_HOME/$installdir_name" #$XDG_DATA_HOME is a synonym for $HOME/.local/share/ unless the user has specified otherwise (unlikely).
    else
        install_to_prefix "$HOME/.local/share/$installdir_name" #XDG_DATA_HOME not set, so use default path as defined by XDG spec.
    fi

}

root_install()
{
    
    default_prefix="/opt/${installdir_name}"

    printf "Enter the desired installation directory [%s]: " "${default_prefix}"
    read -r REPLY
    if [ "$REPLY" = "" ] ; then
        install_prefix=$default_prefix
    else
        install_prefix=$REPLY
    fi

    install_to_prefix "$install_prefix"

    mkdir -p /usr/local/share/applications
}

install_to_prefix()
{
    test -e "$1" && backup_previous_installation "$1"
    mkdir -p "$1" || die "Failed to create installation directory!"

    echo " - Installing to $1"

    cp -a "${tarball_path}"/* "$1/" || die "Failed to complete the installation!"

    if [ "$(id -u)" = "0" ]; then
        "$1/etc/chrome_sandboxing_permissions_setup.sh"
    else
        echo "                 ╭──────────────────────────────────────────╮"
        echo "╭────────────────┘    Web Media Process Sandboxing Setup    └──────────────────╮"
        echo "│                                                                              │"
        echo "│    Embedded Chromium sandboxing is a highly recommended security feature!    │"
        echo "│                                                                              │"
        echo "│Sandboxing helps prevents malicious code from running in the browser process, │"
        echo "│which could otherwise be used to compromise the viewer or your system.        │"
        echo "│                                                                              │"
        echo "│For more information please see the following resources:                      │"
        echo "│https://chromium.googlesource.com/chromium/src/+/HEAD/docs/design/sandbox.md  │"
        echo "│https://chromium.googlesource.com/chromium/src/+/HEAD/docs/linux/sandboxing.md│"
        echo "│                                                                              │"
        echo "│Permissions on the following viewer file must be set to enable sandboxing.    │"
        echo "│   bin/llplugin/chrome-sandbox                                                │"
        echo "│                                                                              │"
        echo "│You may be asked provide credentials to authorize this setup.                 │"
        echo "╰──────────────────────────────────────────────────────────────────────────────╯"
        echo "Saying no will not enable sandboxing, which endangers your system security."
        echo "Saying yes will run a chown and chmod command to enable sandboxing."
        echo ""
        warn "By refusing this step, you accept this risk."
        prompt "Proceed with enabling web media process sandboxing? [Y/N]: "
        if [ $? = 0 ]; then
            # Save this choice so that we don't ask for creds on every viewer launch
            touch "$1/bin/llplugin/.user_does_not_want_chrome_sandboxing_and_accepts_the_risks"
            exit 0 
        fi
        # Remove any previous opt-out file since we're opting in now
        rm "$1/bin/llplugin/.user_does_not_want_chrome_sandboxing_and_accepts_the_risks" 2> /dev/null
        pkexec "$1/etc/chrome_sandboxing_permissions_setup.sh" || die "Failed to set permissions on chrome-sandbox"
    fi
    "$1"/etc/refresh_desktop_app_entry.sh || echo "Failed to integrate into DE via XDG."
    set_slurl_handler "$1"
}

backup_previous_installation()
{
    backup_dir="$1".backup-$(date -I)
    echo " - Backing up previous installation to $backup_dir"

    mv "$1" "$backup_dir" || die "Failed to create backup of existing installation!"
}

set_slurl_handler()
{
    install_dir=$1
    echo
    prompt "Would you like to set Alchemy as your default SLurl handler? [Y/N]: "
    if [ $? -eq 0 ]; then
	exit 0
    fi
    "$install_dir"/etc/register_secondlifeprotocol.sh #Successful association comes with a notification to the user.
}

if [ "$(id -u)" = "0" ]; then
    root_install
else
    homedir_install
fi
