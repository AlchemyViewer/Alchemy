#!/bin/bash

# Register a protocol handler (default: handle_secondlifeprotocol.sh) for
# URLs of the form secondlife://...
#

desired_handler="$1"

print() {
	log_prefix="RegisterSLProtocol:"
	echo -e "${log_prefix} $*"
}
run_path=$(dirname "$0" || echo .)
cd "${run_path}/.." || exit

if [ -z "${desired_handler}" ]; then
	desired_handler="$(pwd)/etc/handle_secondlifeprotocol.sh"
fi

# # Register handler for KDE-aware apps
# for kdeconfig in ${LLKDECONFIG} kf5-config kde4-config kde-config; do
#     if command -v "${kdeconfig}" >/dev/null 2>&1; then
#         kde_protocols_folder=$("${kdeconfig}" --path services | cut -d ':' -f 1)
# 				mkdir -p "${kde_protocols_folder}"
#         if [ -d "${kde_protocols_folder}" ]; then
#             kde_proto_file=${kde_protocols_folder}/secondlife.protocol
# 						kde_handler_string="
# [Protocol]
# exec=${desired_handler} %U
# protocol=secondlife
# input=none
# output=none
# helper=true
# listing=
# reading=false
# writing=false
# makedir=false
# deleting=false"
#             if echo "${kde_handler_string}" > "${kde_proto_file}"; then
# 						print "Registered secondlife:// handler with KDE"
# 							success=1
# 						else
# 							print 'Warning: Did not register secondlife:// handler with KDE: Could not write '"${kde_proto_file}"
# 						fi
#         else
#             print 'Warning: Did not register secondlife:// handler with KDE: Directory '"${kde_protocols_folder}"' does not exist.'
#         fi
#     # else
# 			# print "Warning: KDE-Config binary '${kdeconfig}' not found"
# 		fi
# done
# if [[ -z "${success}" ]]; then
# 	print "Warning: secondlife:// protocol not registered with KDE: could not find any configuration utility. You can still register the application manually by editing ${HOME}/.local/share/mimeapps.list"
# fi

#Check if xdg-mime is present, if so, use it to register new protocol.
if command -v xdg-mime query default x-scheme-handler/secondlife >/dev/null 2>&1; then
	urlhandler=$(xdg-mime query default x-scheme-handler/secondlife)
	localappdir="$HOME/.local/share/applications"
	newhandler="secondlifeprotocol_$(basename "$(dirname "${desired_handler}")").desktop"
	handlerpath="$localappdir/$newhandler"
	cat >"${handlerpath}" <<EOFnew || print "Warning: Did not register secondlife:// handler with xdg-mime: Could not write $newhandler"s
[Desktop Entry]
Version=1.5
Name="Second Life URL handler"
Comment="secondlife:// URL handler"
Type=Application
Exec=$desired_handler %u
Terminal=false
StartupNotify=true
NoDisplay=true
MimeType=x-scheme-handler/secondlife
EOFnew

	# TODO: use absolute path for the desktop file
	# TODO: Ensure that multiple channels behave properly due to different desktop file names in /usr/share/applications/
	# TODO: Better detection of what the handler actually is, as other viewer projects may use the same filename
	if [ -z "$urlhandler" ]; then
		print No SLURL handler currently registered, creating new...
	else
		#xdg-mime uninstall $localappdir/$urlhandler
		#Clean up handlers from other viewers
		if [ "$urlhandler" != "$newhandler" ]; then
			print "Current SLURL Handler: ${urlhandler} - Setting ${newhandler} as the new default..."
			mv "$localappdir"/"$urlhandler" "$localappdir"/"$urlhandler".bak
		else
			print "SLURL Handler has not changed, leaving as-is."
		fi
	fi
	xdg-mime default $newhandler x-scheme-handler/secondlife
	if command -v update-desktop-database >/dev/null 2>&1; then
		update-desktop-database "$localappdir"
		print "Registered ${desired_handler} as secondlife:// protocol handler with xdg-mime."
	else
		print "Warning: Cannot update desktop database, command missing - installation may be incomplete."
	fi
else
	print "Warning: Did not register secondlife:// handler with xdg-mime: Package not found."
	# TODO: use dconf or another modern alternative
	gconftool=$(command -v "${LLGCONFTOOL:=gconftool-2}")
	if [[ -n "${gconftool}" ]]; then
		print "=== USING DEPRECATED GCONF API ==="
		if "${gconftool}" -s -t string /desktop/gnome/url-handlers/secondlife/command "${desired_handler} \"%s\"" &&
			${gconftool} -s -t bool /desktop/gnome/url-handlers/secondlife/enabled true; then
			print "Registered ${desired_handler} as secondlife:// handler with (deprecated) gconf."
		else
			print "Failed to register secondlife:// handler with (deprecated) gconf."
		fi
	fi
fi
