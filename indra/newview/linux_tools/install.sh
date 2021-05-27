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
        echo "Permissions on $SANDBOX_BIN need to be set to enable security sandboxing for the integrated browser. You may be asked to authorize this step with administrative credentials."
        prompt "This step is optional, though recommended for safety and security. Proceed with the installation? [Y/N]: "
        if [[ $? == 0 ]]; then
            exit 0 
        fi
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
