#!/usr/bin/env bash

# Adds the viewer this tree holds to the desktop: its entry in the
# application menu, its icons, and the handling of secondlife:// and
# x-grid-location-info:// links where no other viewer has them. Or takes
# them away again.
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
# them under /usr/share; the copy made here runs this tree's launcher. Run
# from an AppImage, whose tree is mounted somewhere new each time, it runs
# the AppImage instead, which Velopack updates in place.

set -euo pipefail

script=$(readlink -f -- "${BASH_SOURCE[0]}")
prefix=$(dirname -- "$(dirname -- "$script")")
launcher="$prefix/alchemy"
# What the entry runs and is recorded as pointing at.
origin="$prefix"
if [[ -n ${APPIMAGE:-} && -f $APPIMAGE && -n ${APPDIR:-} && $prefix == "$APPDIR"/* ]]; then
    launcher=$APPIMAGE
    origin=$APPIMAGE
fi

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

# Whether a desktop file ID, as xdg-mime names a default, is an entry in
# any applications directory. An entry in a subdirectory has its path there
# as its ID, with each / made a -.
entry_exists()
{
    local id=$1 dir apps path
    local -a dirs
    IFS=: read -r -a dirs <<<"$user_data:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
    for dir in "${dirs[@]}"; do
        apps="$dir/applications"
        if [[ -f $apps/$id ]]; then
            return 0
        fi
        if [[ $id == *-* && -d $apps ]]; then
            while IFS= read -r -d '' path; do
                path=${path#"$apps"/}
                if [[ ${path//\//-} == "$id" ]]; then
                    return 0
                fi
            done < <(find "$apps" -mindepth 2 -name '*.desktop' -print0 2>/dev/null)
        fi
    done
    return 1
}

# What the old scripts wrote: an entry named for no channel, and a link
# handler that ran a launcher no tree has. The entry goes if it was this
# tree's or its tree is gone. Linden Lab's script, which other viewers
# carry too, writes a handler of the same name, so it goes only when it is
# recognisably Alchemy's: named for Alchemy, or written for a tree that
# holds Alchemy's viewer.
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
    local handler="$apps/secondlife-protocol.desktop"
    if [[ -f $handler ]]; then
        local name tree
        name=$(sed -n 's/^Name=//p' "$handler" | head -n 1)
        tree=$(sed -n 's/^Path=//p' "$handler" | head -n 1)
        if [[ $name == *Alchemy* || (-n $tree && -x $tree/bin/alchemy-bin) ]]; then
            rm -f -- "$handler"
        fi
    fi
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
    if [[ $origin == *$'\n'* ]]; then
        echo "Cannot add a viewer whose path holds a newline: $origin" >&2
        exit 1
    fi

    local apps="$data/applications"
    mkdir -p -- "$apps"
    remove_legacy "$apps"

    # A user's choice of handler is the user's own: the system scope offers
    # the entry as one, and leaves the choice to each user. For a user, a
    # scheme is made this entry's only when nothing handles it, this channel
    # does, or the entry that did is gone, so another viewer -- or another
    # channel of this one -- keeps the links it has. xdg-mime is asked before
    # the entry is written, after which it could name the entry itself.
    local -a take=()
    if [[ $scope == user ]] && command -v xdg-mime >/dev/null; then
        local scheme current
        for scheme in "${schemes[@]}"; do
            current=$(xdg-mime query default "$scheme" 2>/dev/null | head -n 1) || true
            current=${current%%;*}
            if [[ -z $current || $current == "$app_id.desktop" ]] || ! entry_exists "$current"; then
                take+=("$scheme")
            fi
        done
    fi

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
        printf 'X-Alchemy-Install=%s\n' "${origin//\\/\\\\}"
    } >"$tmp"
    chmod 644 -- "$tmp"
    mv -f -- "$tmp" "$entry"

    local icon size
    for icon in "$prefix"/share/icons/hicolor/*/apps/"$app_id".png; do
        size=$(basename -- "$(dirname -- "$(dirname -- "$icon")")")
        install -D -m 644 -- "$icon" "$data/icons/hicolor/$size/apps/$app_id.png"
    done

    update_caches "$data"

    if [[ ${#take[@]} -gt 0 ]]; then
        mkdir -p -- "${XDG_CONFIG_HOME:-$HOME/.config}"
        xdg-mime default "$app_id.desktop" "${take[@]}" || true
    fi

    echo "Added $app_id to the desktop in $data"
}

do_uninstall()
{
    local data=$1
    local entry="$data/applications/$app_id.desktop"
    # An entry the user wrote, or another tree's of the same channel, stays
    # with its icons. One whose tree is gone goes, as it runs nothing.
    if [[ -f $entry ]]; then
        local installed
        installed=$(entry_install "$entry")
        if [[ -z $installed || ($installed != "$origin" && -e $installed) ]]; then
            echo "Left $app_id on the desktop in $data: its entry is not this viewer's"
            return 0
        fi
    fi
    rm -f -- "$entry"
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
        if [[ -z $installed || $installed == "$origin" ]]; then
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
