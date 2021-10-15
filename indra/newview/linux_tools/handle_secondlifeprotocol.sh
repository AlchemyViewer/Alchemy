#!/bin/bash

# Send a URL of the form secondlife://... to any running viewer, if not, launch Alchemy viewer.
#

URL="$1"

if [ -z "$URL" ]; then
    echo "Usage: $0 secondlife:// ..."
    exit
fi

RUN_PATH=$(dirname "$0" || echo .)
#ch "${RUN_PATH}"

#Poll DBus to get a list of registered services, then look through the list for the Second Life API Service - if present, this means a viewer is running, if not, then no viewer is running and a new instance should be launched
LIST=$(dbus-send --print-reply --dest=org.freedesktop.DBus  /org/freedesktop/DBus org.freedesktop.DBus.ListNames
)
SERVICE="com.secondlife.ViewerAppAPIService" #Name of Second Life DBus service. This should be the same across all viewers.
if echo "$LIST" | grep -q "$SERVICE"; then
	echo "Second Life running, sending to DBus...";
	exec dbus-send --type=method_call --dest=$SERVICE /com/secondlife/ViewerAppAPI com.secondlife.ViewerAppAPI.GoSLURL string:"$1"
else
	echo "Second Life not running, launching new instance...";
	cd "${RUN_PATH}"/..
	#Go to .sh location (/etc), then up a directory to the viewer location
	#exec ./alchemy -url \'"${URL}"\'
	exec ./alchemy -url ${URL}
	#Remove some of the wrapping around the URL, as this was breaking the handover upon startup
fi
