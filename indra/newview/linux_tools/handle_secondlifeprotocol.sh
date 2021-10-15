#!/bin/bash

# Send a URL of the form secondlife://... to any running viewer, if not, launch Alchemy viewer.
#

#enable or disable debugging code
debug=0

URL="$1"

if [ -z "$URL" ]; then
    echo "Usage: $0 [ secondlife://  | hop:// ] ..."
    exit
fi

RUN_PATH=`dirname "$0" || echo .`
#ch "${RUN_PATH}"

# Use pgrep to return the NUMBER of matching processes, not their IDs. Stragely returning IDs via pidof will cause the script to fail at random, even when it shouldn't.
# pgrep -c returns 0 to show if process (any matching viewer) is not running, 
if [ `pgrep -c do-not-directly` != 0 ]; then
	if [ $debug == 1 ]; then
		zenity --info --text="pgrep passed\!" --title="secondlife_protocol"
	fi
	exec dbus-send --type=method_call --dest=com.secondlife.ViewerAppAPIService /com/secondlife/ViewerAppAPI com.secondlife.ViewerAppAPI.GoSLURL string:"$1"
else
	if [ $debug == 1 ]; then
		zenity --info --text="pgrep fail\!" --title="secondlife_protocol"
	fi
	cd "${RUN_PATH}"/..
	#Go to .sh location (/etc), then up a directory to the viewer location
	exec ./alchemy -url \'"${URL}"\'
fi
`
