#!/usr/bin/env bash

# Adds the viewer this tree holds to the desktop: its entry in the
# application menu, its icons, and the handling of secondlife:// and
# x-grid-location-info:// links. Or takes them away again.
#
#   desktop_integration.sh install [--user|--system]
#   desktop_integration.sh uninstall [--user|--system]
#   desktop_integration.sh refresh
#
# --system writes under /usr/local/share and is the default for root;
# --user writes under $XDG_DATA_HOME and is the default otherwise. refresh is
# what the launcher runs: for a user, it points the entry at this tree when
# there is none, or when the one there is another tree's, and does nothing
# when the system already has an entry or AL_NO_DESKTOP_INTEGRATION is set.
#
# The tree carries the entry and icons under share/, as a package installs
# them under /usr/share; the copy made here runs this tree's launcher.

set -euo pipefail

script=$(readlink -f -- "${BASH_SOURCE[0]}")
prefix=$(dirname -- "$(dirname -- "$script")")
launcher="$prefix/alchemy"

shopt -s nullglob
entries=("$prefix"/share/applications/*.desktop)
shopt -u nullglob
if [[ ${#entries[@]} -ne 1 ]]; then
    echo "No desktop entry under $prefix/share/applications" >&2
    exit 1
fi
template=${entries[0]}
app_id=$(basename -- "$template" .desktop)

user_data=${XDG_DATA_HOME:-$HOME/.local/share}
schemes=(x-scheme-handler/secondlife x-scheme-handler/x-grid-location-info)

# A path as the Exec key quotes an argument, then as a string value escapes
# a backslash. A field code's % is doubled.
exec_quote()
{
    local s=$1
    s=${s//\\/\\\\}
    s=${s//\"/\\\"}
    s=${s//\`/\\\`}
    s=${s//\$/\\\$}
    s=${s//%/%%}
    s=${s//\\/\\\\}
    printf '"%s"' "$s"
}

# The tree an entry made here points at, from the key it was written with.
entry_install()
{
    local value
    value=$(sed -n 's/^X-Alchemy-Install=//p' "$1" 2>/dev/null | head -n 1)
    printf '%s' "${value//\\\\/\\}"
}

# What the old scripts wrote: an entry named for no channel, and a link
# handler that ran a launcher no tree has. The entry goes if it was this
# tree's or its tree is gone; the handler goes either way.
remove_legacy()
{
    local apps=$1
    local legacy="$apps/alchemy-viewer.desktop"
    if [[ -f $legacy ]]; then
        local exec
        exec=$(sed -n 's/^Exec=//p' "$legacy" | head -n 1)
        if [[ $exec == "$launcher" || ! -x $exec ]]; then
            rm -f -- "$legacy"
        fi
    fi
    rm -f -- "$apps/secondlife-protocol.desktop"
}

update_caches()
{
    local data=$1
    if command -v update-desktop-database >/dev/null; then
        update-desktop-database -q "$data/applications" || true
    fi
    # A theme cache that exists has to be kept up to date; one that does not
    # is better not started, as it hides icons added later without one.
    local hicolor="$data/icons/hicolor"
    if [[ -f $hicolor/icon-theme.cache ]] && command -v gtk-update-icon-cache >/dev/null; then
        gtk-update-icon-cache -q -t -f "$hicolor" || true
    elif [[ -d $hicolor ]]; then
        touch -- "$hicolor"
    fi
}

do_install()
{
    local data=$1 scope=$2
    if [[ $prefix == *$'\n'* ]]; then
        echo "Cannot add a tree whose path holds a newline: $prefix" >&2
        exit 1
    fi

    local apps="$data/applications"
    mkdir -p -- "$apps"
    remove_legacy "$apps"

    local entry="$apps/$app_id.desktop"
    local tmp
    tmp=$(mktemp -- "$apps/.$app_id.XXXXXX")
    {
        local line
        while IFS= read -r line || [[ -n $line ]]; do
            if [[ $line == Exec=* ]]; then
                printf 'Exec=%s %%u\n' "$(exec_quote "$launcher")"
            else
                printf '%s\n' "$line"
            fi
        done <"$template"
        printf 'X-Alchemy-Install=%s\n' "${prefix//\\/\\\\}"
    } >"$tmp"
    chmod 644 -- "$tmp"
    mv -f -- "$tmp" "$entry"

    local icon size
    for icon in "$prefix"/share/icons/hicolor/*/apps/"$app_id".png; do
        size=$(basename -- "$(dirname -- "$(dirname -- "$icon")")")
        install -D -m 644 -- "$icon" "$data/icons/hicolor/$size/apps/$app_id.png"
    done

    update_caches "$data"

    # A user's choice of handler is the user's own: the system scope offers
    # the entry as one, and leaves the choice to each user.
    if [[ $scope == user ]] && command -v xdg-mime >/dev/null; then
        mkdir -p -- "${XDG_CONFIG_HOME:-$HOME/.config}"
        xdg-mime default "$app_id.desktop" "${schemes[@]}" || true
    fi

    echo "Added $app_id to the desktop in $data"
}

do_uninstall()
{
    local data=$1
    rm -f -- "$data/applications/$app_id.desktop"
    rm -f -- "$data"/icons/hicolor/*/apps/"$app_id".png
    update_caches "$data"
    echo "Removed $app_id from the desktop in $data"
}

do_refresh()
{
    if [[ $EUID -eq 0 || -n ${AL_NO_DESKTOP_INTEGRATION:-} ]]; then
        return 0
    fi

    local entry="$user_data/applications/$app_id.desktop"
    if [[ -f $entry ]]; then
        # One the user wrote, or one pointing here already, stays as it is.
        local installed
        installed=$(entry_install "$entry")
        if [[ -z $installed || $installed == "$prefix" ]]; then
            return 0
        fi
    else
        local dir dirs
        IFS=: read -r -a dirs <<<"${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
        for dir in "${dirs[@]}"; do
            if [[ -f $dir/applications/$app_id.desktop ]]; then
                return 0
            fi
        done
    fi

    do_install "$user_data" user >/dev/null
}

usage()
{
    echo "Usage: $(basename -- "$0") install|uninstall [--user|--system]" >&2
    echo "       $(basename -- "$0") refresh" >&2
    exit 2
}

[[ $# -ge 1 ]] || usage
command=$1
shift

if [[ $EUID -eq 0 ]]; then
    scope=system
else
    scope=user
fi
for arg in "$@"; do
    case $arg in
        --user) scope=user ;;
        --system) scope=system ;;
        *) usage ;;
    esac
done
if [[ $scope == system ]]; then
    data=/usr/local/share
else
    data=$user_data
fi

case $command in
    install) do_install "$data" "$scope" ;;
    uninstall) do_uninstall "$data" ;;
    refresh) do_refresh ;;
    *) usage ;;
esac
