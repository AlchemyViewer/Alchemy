#!/bin/bash

# Register a protocol handler (default: handle_secondlifeprotocol.sh) for
# URLs of the form secondlife://...
#

HANDLER="$1"

RUN_PATH=$(dirname "$0" || echo .)
cd "${RUN_PATH}/.."

if [ -z "$HANDLER" ]; then
    #HANDLER="$RUN_PATH/etc/handle_secondlifeprotocol.sh"
    HANDLER="$(pwd)/etc/handle_secondlifeprotocol.sh"
fi

# Register handler for GNOME-aware apps
LLGCONFTOOL=gconftool-2
if which ${LLGCONFTOOL} >/dev/null; then
    (${LLGCONFTOOL} -s -t string /desktop/gnome/url-handlers/secondlife/command "${HANDLER} \"%s\"" && ${LLGCONFTOOL} -s -t bool /desktop/gnome/url-handlers/secondlife/enabled true) || echo Warning: Did not register secondlife:// handler with GNOME: ${LLGCONFTOOL} failed.
else
    echo Warning: Did not register secondlife:// handler with GNOME: ${LLGCONFTOOL} not found.
fi

# Register handler for KDE-aware apps
for LLKDECONFIG in kde-config kde4-config; do
    if [ $(which $LLKDECONFIG) ]; then
        LLKDEPROTODIR=$($LLKDECONFIG --path services | cut -d ':' -f 1)
        if [ -d "$LLKDEPROTODIR" ]; then
            LLKDEPROTOFILE=${LLKDEPROTODIR}/secondlife.protocol
            cat > ${LLKDEPROTOFILE} <<EOF || echo Warning: Did not register secondlife:// handler with KDE: Could not write ${LLKDEPROTOFILE} 
[Protocol]
exec=${HANDLER} '%u'
protocol=secondlife
input=none
output=none
helper=true
listing=
reading=false
writing=false
makedir=false
deleting=false
EOF
        else
            echo Warning: Did not register secondlife:// handler with KDE: Directory $LLKDEPROTODIR does not exist.
        fi
    fi
done

#Check if xdg-mime is present, if so, use it to register new protocol.
if command -v xdg-mime query default x-scheme-handler/secondlife > /dev/null 2>&1; then
	urlhandler=$(xdg-mime query default x-scheme-handler/secondlife)
	localappdir="$HOME/.local/share/applications"
	newhandler="handle_secondlifeprotocol.desktop"
	cat >"$localappdir/$newhandler" <<EOFnew || echo Warning: Did not register secondlife:// handler with xdg-mime: Could not write $newhandler
[Desktop Entry]
Version=1.5
Name="Second Life URL handler"
Comment="secondlife:// URL handler"
Type=Application
Exec=$HANDLER %u
Terminal=false
StartupNotify=false
NoDisplay=true
MimeType=x-scheme-handler/secondlife
EOFnew

	if [ -z $urlhandler ]; then
		echo No SLURL handler currently registered, creating new...
	else
		echo Current SLURL Handler: $urlhandler - Setting new default...
		#xdg-mime uninstall $localappdir/$urlhandler
		#Clean up handlers from other viewers
		if [ "$urlhandler" != "$newhandler" ]; then
			mv $localappdir/$urlhandler $localappdir/$urlhandler.bak
		fi
	fi
	xdg-mime default $newhandler x-scheme-handler/secondlife
	if command -v update-desktop-database > /dev/null 2>&1; then
		update-desktop-database $localappdir
		echo -e "Registered secondlife:// protocol with xdg-mime\nNew default: $(xdg-mime query default x-scheme-handler/secondlife)"
	else
		echo Warning: Cannot update desktop database, command missing - installation may be incomplete.
	fi
else
	echo Warning: Did not register secondlife:// handler with xdg-mime: Package not found.
fi
