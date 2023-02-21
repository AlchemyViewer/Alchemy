#!/bin/bash

# Install Alchemy Viewer. This script can install the viewer both
# system-wide and for an individual user.

VT102_STYLE_NORMAL='\E[0m'
VT102_COLOR_RED='\E[31m'

SCRIPTSRC=`readlink -f "$0" || echo "$0"`
RUN_PATH=`dirname "${SCRIPTSRC}" || echo .`
tarball_path=${RUN_PATH}

function prompt()
{
    local prompt=$1
    local input

    echo -n "$prompt"

    while read input; do
        case $input in
            [Yy]* )
                return 1
                ;;
            [Nn]* )
                return 0
                ;;
            * )
                echo "Please enter yes or no."
                echo -n "$prompt"
        esac
    done
}

function die()
{
    warn $1
    exit 1
}

function warn()
{
    echo -n -e $VT102_COLOR_RED
    echo $1
    echo -n -e $VT102_STYLE_NORMAL
}

function homedir_install()
{
    warn "You are not running as a privileged user, so you will only be able"
    warn "to install Alchemy Viewer in your home directory. If you"
    warn "would like to install Alchemy Viewer system-wide, please run"
    warn "this script as the root user, or with the 'sudo' command."
    echo

    prompt "Proceed with the installation? [Y/N]: "
    if [[ $? == 0 ]]; then
	exit 0
    fi

    install_to_prefix "$HOME/.local/share/alchemy-install"
    $HOME/.local/share/alchemy-install/etc/refresh_desktop_app_entry.sh
}

function root_install()
{
    local default_prefix="/opt/alchemy-install"

    echo -n "Enter the desired installation directory [${default_prefix}]: ";
    read
    if [[ "$REPLY" = "" ]] ; then
	local install_prefix=$default_prefix
    else
	local install_prefix=$REPLY
    fi

    install_to_prefix "$install_prefix"

    mkdir -p /usr/local/share/applications
    ${install_prefix}/etc/refresh_desktop_app_entry.sh
}

function install_to_prefix()
{
    test -e "$1" && backup_previous_installation "$1"
    mkdir -p "$1" || die "Failed to create installation directory!"

    echo " - Installing to $1"

    cp -a "${tarball_path}"/* "$1/" || die "Failed to complete the installation!"
    
    SANDBOX_BIN="$1/bin/llplugin/chrome-sandbox"
    if [ "$UID" == "0" ]; then
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
        if [[ $? == 0 ]]; then
            # Save this choice so that we don't ask for creds on every viewer launch
            touch "$1/bin/llplugin/.user_does_not_want_chrome_sandboxing_and_accepts_the_risks"
            exit 0 
        fi
        # Remove any previous opt-out file since we're opting in now
        rm "$1/bin/llplugin/.user_does_not_want_chrome_sandboxing_and_accepts_the_risks" 2> /dev/null
        pkexec "$1/etc/chrome_sandboxing_permissions_setup.sh" || die "Failed to set permissions on chrome-sandbox"
    fi
}

function backup_previous_installation()
{
    local backup_dir="$1".backup-$(date -I)
    echo " - Backing up previous installation to $backup_dir"

    mv "$1" "$backup_dir" || die "Failed to create backup of existing installation!"
}


if [ "$UID" == "0" ]; then
    root_install
else
    homedir_install
fi
