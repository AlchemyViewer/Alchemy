# dmgbuild settings for the disk image the hosted build makes from the
# notarized bundle:
#
#   dmgbuild -s dmg_settings.py -D app=<dir>/<Name>.app "<Name>" <out>.dmg
#
# The window shows the bundle and a link to /Applications either side of
# dmgbuild's own arrow, and the volume's icon is badged with the viewer's.
# Badging needs `dmgbuild[badge_icons]`.

import os.path
import plistlib

application = defines["app"]  # noqa: F821 -- dmgbuild provides defines
appname = os.path.basename(application)


def app_icon(app):
    with open(os.path.join(app, "Contents", "Info.plist"), "rb") as f:
        icon = plistlib.load(f)["CFBundleIconFile"]
    if not os.path.splitext(icon)[1]:
        icon += ".icns"
    return os.path.join(app, "Contents", "Resources", icon)


# APFS, not HFS+: HFS+ decomposes file names, and the bundle's seal holds the
# names of the font stand-ins with Japanese names as they were composed.
filesystem = "APFS"
# LZMA, the smallest of hdiutil's formats. LZFSE (ULFO) builds in half the
# time but is a third larger, and a higher lzma level gains nothing.
format = "ULMO"
files = [application]
symlinks = {"Applications": "/Applications"}
badge_icon = app_icon(application)

background = "builtin-arrow"
window_rect = ((100, 100), (640, 280))
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
icon_size = 128
text_size = 14
icon_locations = {appname: (140, 120), "Applications": (500, 120)}
