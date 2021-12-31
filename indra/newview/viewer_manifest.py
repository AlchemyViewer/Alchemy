#!/usr/bin/env python3
"""\
@file viewer_manifest.py
@author Ryan Williams
@brief Description of all installer viewer files, and methods for packaging
       them into installers for all supported platforms.

$LicenseInfo:firstyear=2006&license=viewerlgpl$
Second Life Viewer Source Code
Copyright (C) 2006-2014, Linden Research, Inc.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation;
version 2.1 of the License only.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
$/LicenseInfo$
"""

from io import open
import errno
import json
import os
import os.path
import plistlib
import random
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import time
import zipfile

viewer_dir = os.path.dirname(__file__)
# Add indra/lib/python to our path so we don't have to muck with PYTHONPATH.
# Put it FIRST because some of our build hosts have an ancient install of
# indra.util.llmanifest under their system Python!
sys.path.insert(0, os.path.join(viewer_dir, os.pardir, "lib", "python"))
from indra.util.llmanifest import LLManifest, main, path_ancestors, CHANNEL_VENDOR_BASE, RELEASE_CHANNEL, ManifestError, MissingError
from llbase import llsd

class ViewerManifest(LLManifest):
    def is_packaging_viewer(self):
        # Some commands, files will only be included
        # if we are packaging the viewer on windows.
        # This manifest is also used to copy
        # files during the build (see copy_w_viewer_manifest
        # and copy_l_viewer_manifest targets)
        return 'package' in self.args['actions']
    
    def construct(self):
        super(ViewerManifest, self).construct()
        self.path(src="../../scripts/messages/message_template.msg", dst="app_settings/message_template.msg")
        self.path(src="../../etc/message.xml", dst="app_settings/message.xml")

        if self.is_packaging_viewer():
            with self.prefix(src_dst="app_settings"):
                self.exclude("logcontrol.xml")
                self.exclude("logcontrol-dev.xml")
                self.path("*.ini")
                self.path("*.xml")

                # include static assets
                self.path("static_assets")

                # include the entire shaders directory recursively
                self.path("shaders")
                # include the extracted list of contributors
                contributions_path = "../../doc/contributions.txt"
                contributor_names = self.extract_names(contributions_path)
                self.put_in_file(contributor_names, "contributors.txt", src=contributions_path)

                # ... and the default camera position settings
                self.path("camera")

                # ... and the entire windlight directory
                self.path("windlight")

                # ... and the entire image filters directory
                self.path("filters")

                # ... and the entire color lut texture directory
                self.path("colorlut")
            
                # ... and the included spell checking dictionaries
                pkgdir = os.path.join(self.args['build'], os.pardir, 'packages')
                with self.prefix(src=pkgdir):
                    self.path("dictionaries")

                # include the extracted packages information (see BuildPackagesInfo.cmake)
                self.path(src=os.path.join(self.args['build'],"packages-info.txt"), dst="packages-info.txt")
                # CHOP-955: If we have "sourceid" or "viewer_channel" in the
                # build process environment, generate it into
                # settings_install.xml.
                settings_template = dict(
                    sourceid=dict(Comment='Identify referring agency to Linden web servers',
                                  Persist=1,
                                  Type='String',
                                  Value=''),
                    CmdLineGridChoice=dict(Comment='Default grid',
                                  Persist=0,
                                  Type='String',
                                  Value=''),
                    CmdLineChannel=dict(Comment='Command line specified channel name',
                                        Persist=0,
                                        Type='String',
                                        Value=''))
                settings_install = {}
                sourceid = self.args.get('sourceid')
                if sourceid:
                    settings_install['sourceid'] = settings_template['sourceid'].copy()
                    settings_install['sourceid']['Value'] = sourceid
                    print("Set sourceid in settings_install.xml to '%s'" % sourceid)

                if self.args.get('channel_suffix'):
                    settings_install['CmdLineChannel'] = settings_template['CmdLineChannel'].copy()
                    settings_install['CmdLineChannel']['Value'] = self.channel_with_pkg_suffix()
                    print("Set CmdLineChannel in settings_install.xml to '%s'" % self.channel_with_pkg_suffix())

                if self.args.get('grid'):
                    settings_install['CmdLineGridChoice'] = settings_template['CmdLineGridChoice'].copy()
                    settings_install['CmdLineGridChoice']['Value'] = self.grid()
                    print("Set CmdLineGridChoice in settings_install.xml to '%s'" % self.grid())

                # put_in_file(src=) need not be an actual pathname; it
                # only needs to be non-empty
                self.put_in_file(llsd.format_pretty_xml(settings_install),
                                 "settings_install.xml",
                                 src="environment")


            with self.prefix(src_dst="character"):
                self.path("*.llm")
                self.path("*.xml")
                self.path("*.tga")

            # Include our fonts
            with self.prefix(src=os.path.join(pkgdir, 'fonts'), dst="fonts"):
                self.path("*.ttf")
                self.path("*.ttc")
                self.path("*.txt")

            # skins
            with self.prefix(src_dst="skins"):
                # include the entire textures directory recursively
                with self.prefix(src_dst="*/textures"):
                    self.path("*/*.jpg")
                    self.path("*/*.png")
                    self.path("*.tga")
                    self.path("*.j2c")
                    self.path("*.png")
                    self.path("textures.xml")
                self.path("*/xui/*/*.xml")
                self.path("*/xui/*/widgets/*.xml")
                self.path("*/*.xml")
                self.path("*/*.json")

                # Update: 2017-11-01 CP Now we store app code in the html folder
                #         Initially the HTML/JS code to render equirectangular
                #         images for the 360 capture feature but more to follow.
                with self.prefix(src="*/html", dst="*/html"):
                    self.path("*/*/*/*.js")
                    self.path("*/*/*.html")

            #build_data.json.  Standard with exception handling is fine.  If we can't open a new file for writing, we have worse problems
            #platform is computed above with other arg parsing
            build_data_dict = {"Type":"viewer","Version":'.'.join(self.args['version']),
                            "Channel Base": CHANNEL_VENDOR_BASE,
                            "Channel":self.channel_with_pkg_suffix(),
                            "Platform":self.build_data_json_platform,
                            "Address Size":self.address_size,
                            "Update Service":"https://app.alchemyviewer.org/update",
                            }
            # Only store this if it's both present and non-empty
            build_data_dict = self.finish_build_data_dict(build_data_dict)
            with open(os.path.join(os.pardir,'build_data.json'), 'w') as build_data_handle:
                json.dump(build_data_dict,build_data_handle)

            #we likely no longer need the test, since we will throw an exception above, but belt and suspenders and we get the
            #return code for free.
            if not self.path2basename(os.pardir, "build_data.json"):
                print("No build_data.json file")

    def finish_build_data_dict(self, build_data_dict):
        return build_data_dict

    def grid(self):
        return self.args['grid']

    def channel(self):
        return self.args['channel']

    def channel_with_pkg_suffix(self):
        fullchannel=self.channel()
        channel_suffix = self.args.get('channel_suffix')
        if channel_suffix:
            fullchannel+=' '+channel_suffix
        return fullchannel

    def channel_variant(self):
        global CHANNEL_VENDOR_BASE
        return self.channel().replace(CHANNEL_VENDOR_BASE, "").strip()

    def channel_type(self): # returns 'release', 'beta', 'project', or 'test'
        channel_qualifier=self.channel_variant().lower()
        if channel_qualifier.startswith('release'):
            channel_type='release'
        elif channel_qualifier.startswith('beta'):
            channel_type='beta'
        elif channel_qualifier.startswith('project'):
            channel_type='project'
        else:
            channel_type='test'
        return channel_type

    def channel_variant_app_suffix(self):
        # get any part of the channel name after the CHANNEL_VENDOR_BASE
        suffix=self.channel_variant()
        # by ancient convention, we don't use Release in the app name
        if self.channel_type() == 'release':
            suffix=suffix.replace('Release', '').strip()
        # for the base release viewer, suffix will now be null - for any other, append what remains
        if suffix:
            suffix = "_".join([''] + suffix.split())
        # the additional_packages mechanism adds more to the installer name (but not to the app name itself)
        # ''.split() produces empty list, so suffix only changes if
        # channel_suffix is non-empty
        suffix = "_".join([suffix] + self.args.get('channel_suffix', '').split())
        return suffix

    def installer_base_name(self):
        global CHANNEL_VENDOR_BASE
        # a standard map of strings for replacing in the templates
        substitution_strings = {
            'channel_vendor_base' : '_'.join(CHANNEL_VENDOR_BASE.split()),
            'channel_variant_underscores':self.channel_variant_app_suffix(),
            'version_underscores' : '_'.join(self.args['version']),
            'arch':self.args['arch']
            }
        return "%(channel_vendor_base)s%(channel_variant_underscores)s_%(version_underscores)s_%(arch)s" % substitution_strings

    def app_name(self):
        global CHANNEL_VENDOR_BASE
        channel_type=self.channel_type()
        if channel_type == 'release':
            app_suffix='Viewer'
        else:
            app_suffix=self.channel_variant()
        return CHANNEL_VENDOR_BASE + ' ' + app_suffix

    def app_name_oneword(self):
        return ''.join(self.app_name().split())
    
    def icon_path(self):
        return "icons/" + self.channel_type()

    def extract_names(self,src):
        try:
            contrib_file = open(src, 'r', encoding='utf-8')
        except IOError:
            print("Failed to open '%s'" % src)
            raise
        lines = contrib_file.readlines()
        contrib_file.close()

        # All lines up to and including the first blank line are the file header; skip them
        lines.reverse() # so that pop will pull from first to last line
        while not re.match(r"\s*$", lines.pop()) :
            pass # do nothing

        # A line that starts with a non-whitespace character is a name; all others describe contributions, so collect the names
        names = []
        for line in lines :
            if re.match(r"\S", line) :
                names.append(line.rstrip())
        # It's not fair to always put the same people at the head of the list
        random.shuffle(names)
        return ', '.join(names).encode("utf-8")

    def relsymlinkf(self, src, dst=None, catch=True):
        """
        relsymlinkf() is just like symlinkf(), but instead of requiring the
        caller to pass 'src' as a relative pathname, this method expects 'src'
        to be absolute, and creates a symlink whose target is the relative
        path from 'src' to dirname(dst).
        """
        dstdir, dst = self._symlinkf_prep_dst(src, dst)

        # Determine the relative path starting from the directory containing
        # dst to the intended src.
        src = self.relpath(src, dstdir)

        self._symlinkf(src, dst, catch)
        return dst

    def symlinkf(self, src, dst=None, catch=True):
        """
        Like ln -sf, but uses os.symlink() instead of running ln. This creates
        a symlink at 'dst' that points to 'src' -- see:
        https://docs.python.org/3/library/os.html#os.symlink

        If you omit 'dst', this creates a symlink with basename(src) at
        get_dst_prefix() -- in other words: put a symlink to this pathname
        here at the current dst prefix.

        'src' must specifically be a *relative* symlink. It makes no sense to
        create an absolute symlink pointing to some path on the build machine!

        Also:
        - We prepend 'dst' with the current get_dst_prefix(), so it has similar
          meaning to associated self.path() calls.
        - We ensure that the containing directory os.path.dirname(dst) exists
          before attempting the symlink.

        If you pass catch=False, exceptions will be propagated instead of
        caught.
        """
        dstdir, dst = self._symlinkf_prep_dst(src, dst)
        self._symlinkf(src, dst, catch)
        return dst

    def _symlinkf_prep_dst(self, src, dst):
        # helper for relsymlinkf() and symlinkf()
        if dst is None:
            dst = os.path.basename(src)
        dst = os.path.join(self.get_dst_prefix(), dst)
        # Seems silly to prepend get_dst_prefix() to dst only to call
        # os.path.dirname() on it again, but this works even when the passed
        # 'dst' is itself a pathname.
        dstdir = os.path.dirname(dst)
        self.cmakedirs(dstdir)
        return (dstdir, dst)

    def _symlinkf(self, src, dst, catch):
        # helper for relsymlinkf() and symlinkf()
        # the passed src must be relative
        if os.path.isabs(src):
            raise ManifestError("Do not symlinkf(absolute %r, asis=True)" % src)

        # The outer catch is the one that reports failure even after attempted
        # recovery.
        try:
            # At the inner layer, recovery may be possible.
            try:
                os.symlink(src, dst)
            except OSError as err:
                if err.errno != errno.EEXIST:
                    raise
                # We could just blithely attempt to remove and recreate the target
                # file, but that strategy doesn't work so well if we don't have
                # permissions to remove it. Check to see if it's already the
                # symlink we want, which is the usual reason for EEXIST.
                elif os.path.islink(dst):
                    if os.readlink(dst) == src:
                        # the requested link already exists
                        pass
                    else:
                        # dst is the wrong symlink; attempt to remove and recreate it
                        os.remove(dst)
                        os.symlink(src, dst)
                elif os.path.isdir(dst):
                    print("Requested symlink (%s) exists but is a directory; replacing" % dst)
                    shutil.rmtree(dst)
                    os.symlink(src, dst)
                elif os.path.exists(dst):
                    print("Requested symlink (%s) exists but is a file; replacing" % dst)
                    os.remove(dst)
                    os.symlink(src, dst)
                else:
                    # out of ideas
                    raise
        except Exception as err:
            # report
            print("Can't symlink %r -> %r: %s: %s" % \
                  (dst, src, err.__class__.__name__, err))
            # if caller asked us not to catch, re-raise this exception
            if not catch:
                raise

    def relpath(self, path, base=None, symlink=False):
        """
        Return the relative path from 'base' to the passed 'path'. If base is
        omitted, self.get_dst_prefix() is assumed. In other words: make a
        same-name symlink to this path right here in the current dest prefix.

        Normally we resolve symlinks. To retain symlinks, pass symlink=True.
        """
        if base is None:
            base = self.get_dst_prefix()

        # Since we use os.path.relpath() for this, which is purely textual, we
        # must ensure that both pathnames are absolute.
        if symlink:
            # symlink=True means: we know path is (or indirects through) a
            # symlink, don't resolve, we want to use the symlink.
            abspath = os.path.abspath
        else:
            # symlink=False means to resolve any symlinks we may find
            abspath = os.path.realpath

        return os.path.relpath(abspath(path), abspath(base))


class WindowsManifest(ViewerManifest):
    # We want the platform, per se, for every Windows build to be 'win'. The
    # VMP will concatenate that with the address_size.
    build_data_json_platform = 'win'

    def final_exe(self):
        return self.app_name_oneword()+".exe"

    def finish_build_data_dict(self, build_data_dict):
        build_data_dict['Executable'] = self.final_exe()
        build_data_dict['AppName']    = self.app_name()
        return build_data_dict

    def construct(self):
        super(WindowsManifest, self).construct()

        pkgdir = os.path.join(self.args['build'], os.pardir, 'packages')
        relpkgdir = os.path.join(pkgdir, "lib", "release")
        debpkgdir = os.path.join(pkgdir, "lib", "debug")

        if self.is_packaging_viewer():
            # Find alchemy-bin.exe in the 'configuration' dir, then rename it to the result of final_exe.
            self.path(src='%s/alchemy-bin.exe' % self.args['configuration'], dst=self.final_exe())

            with self.prefix(src=os.path.join(pkgdir, "VMP")):
                # include the compiled launcher scripts so that it gets included in the file_list
                self.path('SLVersionChecker.exe')

            with self.prefix(dst="vmp_icons"):
                with self.prefix(src=self.icon_path()):
                    self.path(src="alchemy.ico", dst="secondlife.ico")
                #VMP  Tkinter icons
                with self.prefix(src="vmp_icons"):
                    self.path("*.png")
                    self.path("*.gif")

        # Plugin host application
        self.path2basename(os.path.join(os.pardir,
                                        'llplugin', 'slplugin', self.args['configuration']),
                           "slplugin.exe")
        
        # Get shared libs from the shared libs staging directory
        with self.prefix(src=os.path.join(self.args['build'], os.pardir,
                                          'sharedlibs', self.args['configuration'])):
            # APR Libraries
            self.path("libapr-1.dll")
            self.path("libapriconv-1.dll")
            self.path("libaprutil-1.dll")

            # Mesh 3rd party libs needed for auto LOD and collada reading
            self.path("glod.dll")

            # For image support
            self.path("openjp2.dll")

            # For OpenGL extensions
            self.path("epoxy-0.dll")

            # HTTP and Network
            if self.args['configuration'].lower() == 'debug':
                self.path("xmlrpc-epid.dll")
            else:
                self.path("xmlrpc-epi.dll")

            # Hunspell
            self.path("libhunspell.dll")

            # Audio
            self.path("libogg.dll")
            self.path("libvorbis.dll")
            self.path("libvorbisfile.dll")      

            # Misc
            if self.args['configuration'].lower() == 'debug':
                self.path("libexpatd.dll")
            else:
                self.path("libexpat.dll")

            # Get openal dll for audio engine, continue if missing
            if self.args['openal'] == 'ON' or self.args['openal'] == 'TRUE':
                # Get openal dll
                self.path("OpenAL32.dll")
                self.path("alut.dll")

            # Get fmodstudio dll for audio engine, continue if missing
            if self.args['fmodstudio'] == 'ON' or self.args['fmodstudio'] == 'TRUE':
                if self.args['configuration'].lower() == 'debug':
                    self.path("fmodL.dll", "fmodL.dll")
                else:
                    self.path(src="fmod.dll", dst="fmod.dll")

            # KDU
            if self.args['kdu'] == 'ON' or self.args['kdu'] == 'TRUE':
                if self.args['configuration'].lower() == 'debug':
                    self.path("kdud.dll", "kdud.dll")
                else:
                    self.path(src="kdu.dll", dst="kdu.dll")

            # SLVoice executable
            with self.prefix(src=os.path.join(pkgdir, 'bin', 'release')):
                self.path("SLVoice.exe")

            # Vivox libraries
            if (self.address_size == 64):
                self.path("vivoxsdk_x64.dll")
                self.path("ortp_x64.dll")
            else:
                self.path("vivoxsdk.dll")
                self.path("ortp.dll")

            # Sentry
            if self.args.get('sentry'):
                self.path("sentry.dll")
                with self.prefix(src=os.path.join(pkgdir, 'bin', 'release')):
                    self.path("crashpad_handler.exe")

        self.path(src="licenses-win32.txt", dst="licenses.txt")
        self.path("featuretable.txt")

        with self.prefix(src=pkgdir):
            self.path("ca-bundle.crt")

        # Media plugins - CEF
        with self.prefix(dst="llplugin"):
            with self.prefix(src=os.path.join(self.args['build'], os.pardir, 'media_plugins')):
                if self.args['configuration'].lower() != 'debug':
                    with self.prefix(src=os.path.join('cef', self.args['configuration'])):
                        self.path("media_plugin_cef.dll")

                # Media plugins - LibVLC
                with self.prefix(src=os.path.join('libvlc', self.args['configuration'])):
                    self.path("media_plugin_libvlc.dll")

                # Media plugins - Example (useful for debugging - not shipped with release viewer)
                if self.channel_type() != 'release':
                    with self.prefix(src=os.path.join('example', self.args['configuration'])):
                        self.path("media_plugin_example.dll")

            # CEF runtime files - debug
            # CEF runtime files - not debug (release, relwithdebinfo etc.)
            config = 'debug' if self.args['configuration'].lower() == 'debug' else 'release'
            if self.args['configuration'].lower() != 'debug':
                with self.prefix(src=os.path.join(pkgdir, 'bin', config)):
                    self.path("chrome_elf.dll")
                    self.path("d3dcompiler_47.dll")
                    self.path("libcef.dll")
                    self.path("libEGL.dll")
                    self.path("libGLESv2.dll")
                    self.path("vk_swiftshader.dll")
                    self.path("vulkan-1.dll")
                    self.path("dullahan_host.exe")
                    self.path("snapshot_blob.bin")
                    self.path("v8_context_snapshot.bin")
                    self.path("vk_swiftshader_icd.json")

                # CEF software renderer files
                with self.prefix(src=os.path.join(pkgdir, 'bin', config, 'swiftshader'), dst='swiftshader'):
                    self.path("libEGL.dll")
                    self.path("libGLESv2.dll")

                # CEF files common to all configurations
                with self.prefix(src=os.path.join(pkgdir, 'resources')):
                    self.path("chrome_100_percent.pak")
                    self.path("chrome_200_percent.pak")
                    self.path("resources.pak")
                    self.path("icudtl.dat")

                with self.prefix(src=os.path.join(pkgdir, 'resources', 'locales'), dst='locales'):
                    self.path("am.pak")
                    self.path("ar.pak")
                    self.path("bg.pak")
                    self.path("bn.pak")
                    self.path("ca.pak")
                    self.path("cs.pak")
                    self.path("da.pak")
                    self.path("de.pak")
                    self.path("el.pak")
                    self.path("en-GB.pak")
                    self.path("en-US.pak")
                    self.path("es-419.pak")
                    self.path("es.pak")
                    self.path("et.pak")
                    self.path("fa.pak")
                    self.path("fi.pak")
                    self.path("fil.pak")
                    self.path("fr.pak")
                    self.path("gu.pak")
                    self.path("he.pak")
                    self.path("hi.pak")
                    self.path("hr.pak")
                    self.path("hu.pak")
                    self.path("id.pak")
                    self.path("it.pak")
                    self.path("ja.pak")
                    self.path("kn.pak")
                    self.path("ko.pak")
                    self.path("lt.pak")
                    self.path("lv.pak")
                    self.path("ml.pak")
                    self.path("mr.pak")
                    self.path("ms.pak")
                    self.path("nb.pak")
                    self.path("nl.pak")
                    self.path("pl.pak")
                    self.path("pt-BR.pak")
                    self.path("pt-PT.pak")
                    self.path("ro.pak")
                    self.path("ru.pak")
                    self.path("sk.pak")
                    self.path("sl.pak")
                    self.path("sr.pak")
                    self.path("sv.pak")
                    self.path("sw.pak")
                    self.path("ta.pak")
                    self.path("te.pak")
                    self.path("th.pak")
                    self.path("tr.pak")
                    self.path("uk.pak")
                    self.path("vi.pak")
                    self.path("zh-CN.pak")
                    self.path("zh-TW.pak")

                with self.prefix(src=os.path.join(pkgdir, 'bin', 'release')):
                    self.path("libvlc.dll")
                    self.path("libvlccore.dll")
                    self.path("plugins/")
        if not self.is_packaging_viewer():
            self.package_file = "copied_deps"    

    def nsi_file_commands(self, install=True):
        def wpath(path):
            if path.endswith('/') or path.endswith(os.path.sep):
                path = path[:-1]
            path = path.replace('/', '\\')
            return path

        result = ""
        dest_files = [pair[1] for pair in self.file_list if pair[0] and os.path.isfile(pair[1])]
        # sort deepest hierarchy first
        dest_files.sort(key=lambda path: (path.count(os.path.sep), path), reverse=True)
        out_path = None
        for pkg_file in dest_files:
            rel_file = os.path.normpath(pkg_file.replace(self.get_dst_prefix()+os.path.sep,''))
            installed_dir = wpath(os.path.join('$INSTDIR', os.path.dirname(rel_file)))
            pkg_file = wpath(os.path.normpath(pkg_file))
            if installed_dir != out_path:
                if install:
                    out_path = installed_dir
                    result += 'SetOutPath ' + out_path + '\n'
            if install:
                result += 'File ' + pkg_file + '\n'
            else:
                result += 'Delete ' + wpath(os.path.join('$INSTDIR', rel_file)) + '\n'

        # at the end of a delete, just rmdir all the directories
        if not install:
            deleted_file_dirs = [os.path.dirname(pair[1].replace(self.get_dst_prefix()+os.path.sep,'')) for pair in self.file_list]
            # find all ancestors so that we don't skip any dirs that happened to have no non-dir children
            deleted_dirs = []
            for d in deleted_file_dirs:
                deleted_dirs.extend(path_ancestors(d))
            # sort deepest hierarchy first
            deleted_dirs.sort(key=lambda path: (path.count(os.path.sep), path), reverse=True)
            prev = None
            for d in deleted_dirs:
                if d != prev:   # skip duplicates
                    result += 'RMDir ' + wpath(os.path.join('$INSTDIR', os.path.normpath(d))) + '\n'
                prev = d

        return result

    def package_finish(self):
        # a standard map of strings for replacing in the templates
        substitution_strings = {
            'version' : '.'.join(self.args['version']),
            'version_short' : '.'.join(self.args['version'][:-1]),
            'version_dashes' : '-'.join(self.args['version']),
            'version_registry' : '%s(%s)' %
            ('.'.join(self.args['version']), self.address_size),
            'final_exe' : self.final_exe(),
            'flags':'',
            'app_name':self.app_name(),
            'app_name_oneword':self.app_name_oneword()
            }

        installer_file = self.installer_base_name() + '_Setup.exe'
        substitution_strings['installer_file'] = installer_file
        
        version_vars = """
        !define INSTEXE "SLVersionChecker.exe"
        !define VERSION "%(version_short)s"
        !define VERSION_LONG "%(version)s"
        !define VERSION_DASHES "%(version_dashes)s"
        !define VERSION_REGISTRY "%(version_registry)s"
        !define VIEWER_EXE "%(final_exe)s"
        """ % substitution_strings
        
        if self.channel_type() == 'release':
            substitution_strings['caption'] = CHANNEL_VENDOR_BASE
        else:
            substitution_strings['caption'] = self.app_name() + ' ${VERSION}'

        inst_vars_template = """
            OutFile "%(installer_file)s"
            !define INSTNAME   "%(app_name_oneword)s"
            !define SHORTCUT   "%(app_name)s"
            !define URLNAME   "secondlife"
            Caption "%(caption)s"
            """

        if(self.address_size == 64):
            engage_registry="SetRegView 64"
            program_files="!define MULTIUSER_USE_PROGRAMFILES64"
        else:
            engage_registry="SetRegView 32"
            program_files=""

        tempfile = "alchemy_setup_tmp.nsi"
        # the following replaces strings in the nsi template
        # it also does python-style % substitution
        self.replace_in("installers/windows/installer_template.nsi", tempfile, {
                "%%VERSION%%":version_vars,
                "%%SOURCE%%":self.get_src_prefix(),
                "%%INST_VARS%%":inst_vars_template % substitution_strings,
                "%%INSTALL_FILES%%":self.nsi_file_commands(True),
                "%%PROGRAMFILES%%":program_files,
                "%%ENGAGEREGISTRY%%":engage_registry,
                "%%DELETE_FILES%%":self.nsi_file_commands(False)})

        # If we're on a build machine, sign the code using our Authenticode certificate. JC
        # note that the enclosing setup exe is signed later, after the makensis makes it.
        # Unlike the viewer binary, the VMP filenames are invariant with respect to version, os, etc.
        for exe in (
            self.final_exe(),
            "SLVersionChecker.exe",
            "llplugin/dullahan_host.exe",
            ):
            self.sign(exe)
            
        # Check two paths, one for Program Files, and one for Program Files (x86).
        # Yay 64bit windows.
        for ProgramFiles in 'ProgramFiles', 'ProgramFiles(x86)':
            NSIS_path = os.path.expandvars(r'${%s}\NSIS\makensis.exe' % ProgramFiles)
            if os.path.exists(NSIS_path):
                break
        installer_created=False
        nsis_attempts=3
        nsis_retry_wait=15
        for attempt in range(nsis_attempts):
            try:
                self.run_command([NSIS_path, '/V2', self.dst_path_of(tempfile)])
            except ManifestError as err:
                if attempt+1 < nsis_attempts:
                    print("nsis failed, waiting %d seconds before retrying" % nsis_retry_wait, file=sys.stderr)
                    time.sleep(nsis_retry_wait)
                    nsis_retry_wait*=2
            else:
                # NSIS worked! Done!
                break
        else:
            print("Maximum nsis attempts exceeded; giving up", file=sys.stderr)
            raise

        self.sign(installer_file)
        self.created_path(self.dst_path_of(installer_file))
        self.package_file = installer_file

    def sign(self, exe):
        sign_py = os.environ.get('SIGN', r'C:\buildscripts\code-signing\sign.py')
        python  = os.environ.get('PYTHON', sys.executable)
        if os.path.exists(sign_py):
            dst_path = self.dst_path_of(exe)
            print("about to run signing of: ", dst_path)
            self.run_command([python, sign_py, dst_path])
        else:
            print("Skipping code signing of %s %s: %s not found" % (self.dst_path_of(exe), exe, sign_py))

    def escape_slashes(self, path):
        return path.replace('\\', '\\\\\\\\')

class Windows_i686_Manifest(WindowsManifest):
    # Although we aren't literally passed ADDRESS_SIZE, we can infer it from
    # the passed 'arch', which is used to select the specific subclass.
    address_size = 32

class Windows_x86_64_Manifest(WindowsManifest):
    address_size = 64


class DarwinManifest(ViewerManifest):
    build_data_json_platform = 'mac'

    def finish_build_data_dict(self, build_data_dict):
        build_data_dict.update({'Bundle Id':self.args['bundleid']})
        return build_data_dict

    def is_packaging_viewer(self):
        # darwin requires full app bundle packaging even for debugging.
        return True

    def is_rearranging(self):
        # That said, some stuff should still only be performed once.
        # Are either of these actions in 'actions'? Is the set intersection
        # non-empty?
        return bool(set(["package", "unpacked"]).intersection(self.args['actions']))

    def construct(self):
        # copy over the build result (this is a no-op if run within the xcode script)
        self.path(os.path.join(self.args['configuration'], self.channel()+".app"), dst="")

        pkgdir = os.path.join(self.args['build'], os.pardir, 'packages')
        relpkgdir = os.path.join(pkgdir, "lib", "release")
        debpkgdir = os.path.join(pkgdir, "lib", "debug")
        libdir = debpkgdir if self.args['configuration'].lower() == 'debug' else relpkgdir

        with self.prefix(src="", dst="Contents"):  # everything goes in Contents
            # CEF framework goes inside Contents/Frameworks.
            # Remember where we parked this car.
            with self.prefix(src=libdir, dst="Frameworks"):
                for libfile in (
                                'libapr-1.*.dylib',
                                'libaprutil-1.*.dylib',
                                'libepoxy.*.dylib',
                                'libGLOD.dylib',
                                'libhunspell-*.dylib',
                                'libndofdev.dylib',
                                'libxmlrpc-epi.*.dylib',
                                ):
                    self.path(libfile)

                if self.args.get('sentry'):
                    self.path("Sentry.framework")

                if self.args['openal'] == 'ON' or self.args['openal'] == 'TRUE':
                    for libfile in (
                                    'libopenal.*.dylib',
                                    'libalut.*.dylib',
                                    ):
                        self.path(libfile)

                if self.args['fmodstudio'] == 'ON' or self.args['fmodstudio'] == 'TRUE':
                    if self.args['configuration'].lower() == 'debug':
                        self.path("libfmodL.dylib")
                    else:
                        self.path("libfmod.dylib")

            with self.prefix(dst="MacOS"):
                executable = self.dst_path_of(self.channel())

                # NOTE: the -S argument to strip causes it to keep
                # enough info for annotated backtraces (i.e. function
                # names in the crash log). 'strip' with no arguments
                # yields a slightly smaller binary but makes crash
                # logs mostly useless. This may be desirable for the
                # final release. Or not.
                if ("package" in self.args['actions'] or 
                    "unpacked" in self.args['actions']):
                    self.run_command(
                        ['strip', '-S', executable])

            with self.prefix(dst="Resources"):
                # defer cross-platform file copies until we're in the
                # nested Resources directory
                super(DarwinManifest, self).construct()

                # need .icns file referenced by Info.plist
                with self.prefix(src=self.icon_path(), dst="") :
                    self.path("alchemy.icns")

                # Copy in the updater script and helper modules
                self.path(src=os.path.join(pkgdir, 'VMP'), dst="updater")

                with self.prefix(src="", dst=os.path.join("updater", "icons")):
                    self.path2basename(self.icon_path(), "alchemy.ico")
                    with self.prefix(src="vmp_icons", dst=""):
                        self.path("*.png")
                        self.path("*.gif")

                with self.prefix(src_dst="cursors_mac"):
                    self.path("*.tif")

                self.path("licenses-mac.txt", dst="licenses.txt")
                self.path("featuretable_mac.txt")

                with self.prefix(src=pkgdir,dst=""):
                    self.path("ca-bundle.crt")

                # Translations
                self.path("English.lproj/language.txt")
                self.replace_in(src="English.lproj/InfoPlist.strings",
                                dst="English.lproj/InfoPlist.strings",
                                searchdict={'%%VERSION%%':'.'.join(self.args['version'])}
                                )
                self.path("German.lproj")
                self.path("Japanese.lproj")
                self.path("Korean.lproj")
                self.path("da.lproj")
                self.path("es.lproj")
                self.path("fr.lproj")
                self.path("hu.lproj")
                self.path("it.lproj")
                self.path("nl.lproj")
                self.path("pl.lproj")
                self.path("pt.lproj")
                self.path("ru.lproj")
                self.path("tr.lproj")
                self.path("uk.lproj")
                self.path("zh-Hans.lproj")

                # dylibs is a list of all the .dylib files we expect to need
                # in our bundled sub-apps. For each of these we'll create a
                # symlink from sub-app/Contents/Resources to the real .dylib.
                # Need to get the llcommon dll from any of the build directories as well.
                libfile_parent = self.get_dst_prefix()
                dylibs=[]

                # SLVoice executable
                with self.prefix(src=os.path.join(pkgdir, 'bin', 'release')):
                    self.path("SLVoice")

                # Vivox libraries
                for libfile in (
                                'libortp.dylib',
                                'libvivoxsdk.dylib',
                                ):
                    self.path2basename(relpkgdir, libfile)

                # our apps
                executable_path = {}
                embedded_apps = [ (os.path.join("llplugin", "slplugin"), "SLPlugin.app") ]
                for app_bld_dir, app in embedded_apps:
                    self.path2basename(os.path.join(os.pardir,
                                                    app_bld_dir, self.args['configuration']),
                                       app)
                    executable_path[app] = \
                        self.dst_path_of(os.path.join(app, "Contents", "MacOS"))

                    # our apps dependencies on shared libs
                    # for each app, for each dylib we collected in dylibs,
                    # create a symlink to the real copy of the dylib.
                    with self.prefix(dst=os.path.join(app, "Contents", "Resources")):
                        for libfile in dylibs:
                            self.relsymlinkf(os.path.join(libfile_parent, libfile))

                # Dullahan helper apps go inside SLPlugin.app
                with self.prefix(dst=os.path.join("SLPlugin.app", "Contents", "Frameworks")):
                    # copy CEF plugin
                    self.path2basename("../media_plugins/cef/" + self.args['configuration'],
                                       "media_plugin_cef.dylib")

                    # copy LibVLC plugin
                    self.path2basename("../media_plugins/libvlc/" + self.args['configuration'],
                                       "media_plugin_libvlc.dylib")

                    with self.prefix(src=os.path.join(pkgdir, 'bin', 'release')):
                        self.path("Chromium Embedded Framework.framework")
                        self.path("DullahanHost.app")
                        self.path("DullahanHost (GPU).app")
                        self.path("DullahanHost (Renderer).app")
                        self.path("DullahanHost (Plugin).app")
                    with self.prefix(src=os.path.join(pkgdir, 'lib', 'release')):
                        self.path( "libvlc*.dylib*" )
                        # copy LibVLC plugins folder
                        with self.prefix(src='plugins', dst="plugins"):
                            self.path( "*.dylib" )
                            self.path( "plugins.dat" )


    def package_finish(self):
        import dmgbuild

        volname=self.app_name() + " Installer"
        finalname = self.installer_base_name() + ".dmg"

        application = self.get_dst_prefix()
        appname = os.path.basename(application)

        vol_icon = self.src_path_of(os.path.join(self.icon_path(), 'alchemy.icns'))
        print("DEBUG: icon_path '%s'" % vol_icon)

        dmgoptions = {
            'format': 'ULFO',
            'compression_level': 9,
            'files': [application],
            'symlinks': { 'Applications': '/Applications' },
            'icon': vol_icon,
            'background': 'builtin-arrow',
            'show_status_bar': False,
            'show_tab_view': False,
            'show_toolbar': False,
            'show_pathbar': False,
            'show_sidebar': False,
            'sidebar_width': 180,
            'arrange_by': None,
            'grid_offset': (0, 0),
            'grid_spacing': 100.0,
            'scroll_position': (0.0, 0.0),
            'show_icon_preview': False,
            'show_item_info': False,
            'label_pos': 'bottom',
            'text_size': 16.0,
            'icon_size': 128.0,
            'include_icon_view_settings': 'auto',
            'include_list_view_settings': 'auto',
            'list_icon_size': 16.0,
            'list_text_size': 12.0,
            'list_scroll_position': (0, 0),
            'list_sort_by': 'name',
            'list_use_relative_dates': True,
            'list_calculate_all_sizes': False,
            'list_columns': ('name', 'date-modified', 'size', 'kind', 'date-added'),
            'list_column_widths': {
                'name': 300,
                'date-modified': 181,
                'date-created': 181,
                'date-added': 181,
                'date-last-opened': 181,
                'size': 97,
                'kind': 115,
                'label': 100,
                'version': 75,
                'comments': 300,
                },
            'list_column_sort_directions': {
                'name': 'ascending',
                'date-modified': 'descending',
                'date-created': 'descending',
                'date-added': 'descending',
                'date-last-opened': 'descending',
                'size': 'descending',
                'kind': 'ascending',
                'label': 'ascending',
                'version': 'ascending',
                'comments': 'ascending',
                },
            'window_rect': ((100, 100), (640, 280)),
            'default_view': 'icon-view',
            'icon_locations': {
                appname:        (140, 120),
                'Applications': (500, 120)
            },
            'license': None,
            }

        dmgbuild.build_dmg(filename=finalname, volume_name=volname, settings=dmgoptions)

        self.package_file = finalname


class Darwin_i386_Manifest(DarwinManifest):
    address_size = 32


class Darwin_i686_Manifest(DarwinManifest):
    """alias in case arch is passed as i686 instead of i386"""
    pass


class Darwin_x86_64_Manifest(DarwinManifest):
    address_size = 64


class LinuxManifest(ViewerManifest):
    build_data_json_platform = 'lnx'

    def construct(self):
        super(LinuxManifest, self).construct()

        pkgdir = os.path.join(self.args['build'], os.pardir, 'packages')
        relpkgdir = os.path.join(pkgdir, "lib", "release")
        debpkgdir = os.path.join(pkgdir, "lib", "debug")

        self.path("licenses-linux.txt","licenses.txt")
        with self.prefix("linux_tools"):
            self.path("client-readme.txt","README-linux.txt")
            self.path("client-readme-voice.txt","README-linux-voice.txt")
            self.path("client-readme-joystick.txt","README-linux-joystick.txt")
            self.path("wrapper.sh","alchemy")
            with self.prefix(dst="etc"):
                self.path("handle_secondlifeprotocol.sh")
                self.path("register_secondlifeprotocol.sh")
                self.path("refresh_desktop_app_entry.sh")
                self.path("chrome_sandboxing_permissions_setup.sh")
            self.path("install.sh")

        with self.prefix(dst="bin"):
            self.path("alchemy-bin","do-not-directly-run-alchemy-bin")
            self.path2basename("../llplugin/slplugin", "SLPlugin")
            #this copies over the python wrapper script, associated utilities and required libraries, see SL-321, SL-322 and SL-323
            #with self.prefix(src="../viewer_components/manager", dst=""):
            #    self.path("*.py")

        # recurses, packaged again
        self.path("res-sdl")

        # Get the icons based on the channel type
        icon_path = self.icon_path()
        print("DEBUG: icon_path '%s'" % icon_path)
        with self.prefix(src=icon_path) :
            self.path("alchemy_256.png","alchemy_icon.png")
            with self.prefix(dst="res-sdl") :
                self.path("alchemy_256.BMP","ll_icon.BMP")

        # plugins
        with self.prefix(src=os.path.join(self.args['build'], os.pardir, "media_plugins"), dst="bin/llplugin"):
        #    self.path("gstreamer010/libmedia_plugin_gstreamer010.so",
        #              "libmedia_plugin_gstreamer.so")
            self.path2basename("example", "libmedia_plugin_example.so")
            self.path2basename("libvlc", "libmedia_plugin_libvlc.so")
            self.path2basename("cef", "libmedia_plugin_cef.so")

        # CEF files 
        with self.prefix(src=os.path.join(pkgdir, 'bin', 'release'), dst=os.path.join('bin', 'llplugin')):
            self.path("chrome-sandbox")
            self.path("dullahan_host")
            self.path("snapshot_blob.bin")
            self.path("v8_context_snapshot.bin")
            self.path("vk_swiftshader_icd.json")

        with self.prefix(src=os.path.join(pkgdir, 'lib', 'release'), dst=os.path.join('bin', 'llplugin')):
            self.path("libcef.so")
            self.path("libEGL.so")
            self.path("libGLESv2.so")
            self.path("libvk_swiftshader.so")
            self.path("libvulkan.so.1")

        with self.prefix(src=os.path.join(pkgdir, 'resources'), dst=os.path.join('bin', 'llplugin')):
            self.path("chrome_100_percent.pak")
            self.path("chrome_200_percent.pak")
            self.path("resources.pak")
            self.path("icudtl.dat")

        with self.prefix(src=os.path.join(pkgdir, 'lib', 'release', 'swiftshader'), dst=os.path.join('bin', 'llplugin', 'swiftshader') ):
            self.path("libEGL.so")
            self.path("libGLESv2.so")

        with self.prefix(src=os.path.join(pkgdir, 'resources', 'locales'), dst=os.path.join('bin', 'llplugin', 'locales')):
            self.path("*.pak")

        self.path("featuretable_linux.txt")

        with self.prefix(src=pkgdir, dst="app_settings"):
            self.path("ca-bundle.crt")

    def package_finish(self):
        installer_name = self.installer_base_name()

        self.strip_binaries()

        # Fix access permissions
        self.run_command(['find', self.get_dst_prefix(),
                          '-type', 'd', '-exec', 'chmod', '755', '{}', ';'])
        for old, new in ('0700', '0755'), ('0500', '0555'), ('0600', '0644'), ('0400', '0444'):
            self.run_command(['find', self.get_dst_prefix(),
                              '-type', 'f', '-perm', old,
                              '-exec', 'chmod', new, '{}', ';'])
        self.package_file = installer_name + '.tar.bz2'

        # temporarily move directory tree so that it has the right
        # name in the tarfile
        realname = self.get_dst_prefix()
        tempname = self.build_path_of(installer_name)
        self.run_command(["mv", realname, tempname])
        try:
            # only create tarball if it's a release build.
            if self.args['buildtype'].lower() == 'release':
                # --numeric-owner hides the username of the builder for
                # security etc.
                self.run_command(['tar', '-C', self.get_build_prefix(),
                                  '--numeric-owner', '-cjf',
                                 tempname + '.tar.bz2', installer_name])
            else:
                print("Skipping %s.tar.bz2 for non-Release build (%s)" % \
                      (installer_name, self.args['buildtype']))
        finally:
            self.run_command(["mv", tempname, realname])

    def strip_binaries(self):
        if self.args['buildtype'].lower() == 'release' and self.is_packaging_viewer():
            print("* Going strip-crazy on the packaged binaries, since this is a RELEASE build")
            # makes some small assumptions about our packaged dir structure
            self.run_command(
                ["find"] +
                [os.path.join(self.get_dst_prefix(), dir) for dir in ('bin', 'lib')] +
                ['-type', 'f', '!', '-name', '*.py', '!', '-name', '*.pak', '!', '-name', '*.bin', '!', '-name', '*.dat',
                 '!', '-name', 'update_install', '-exec', 'strip', '-S', '{}', ';'])

class Linux_i686_Manifest(LinuxManifest):
    address_size = 32

    def construct(self):
        super(Linux_i686_Manifest, self).construct()

        pkgdir = os.path.join(self.args['build'], os.pardir, 'packages')
        relpkgdir = os.path.join(pkgdir, "lib", "release")
        debpkgdir = os.path.join(pkgdir, "lib", "debug")

        with self.prefix(src=relpkgdir, dst="lib"):
            self.path("libapr-1.so*")
            self.path("libaprutil-1.so*")
            self.path("libdb*.so")
            self.path("libexpat.so.*")
            self.path("libGLOD.so")
            self.path("libSDL2*.so*")
            self.path("libopenjp2.*so*")
            self.path("libepoxy.so")
            self.path("libepoxy.so.0")
            self.path("libepoxy.so.0.0.0")
            self.path("libjpeg.so*")

            if self.args['openal'] == 'ON' or self.args['openal'] == 'TRUE':
                self.path("libalut.so*")
                self.path("libopenal.so*")

            if self.args['fmodstudio'] == 'ON' or self.args['fmodstudio'] == 'TRUE':
                self.path("libfmod.so*")

        # Vivox runtimes
        with self.prefix(src=os.path.join(pkgdir, 'bin', 'release'), dst="bin"):
            self.path("SLVoice")
        with self.prefix(src=relpkgdir, dst="lib"):
            self.path("libortp.so")
            self.path("libsndfile.so.1")
            self.path("libvivoxoal.so.1")
            self.path("libvivoxplatform.so")
            self.path("libvivoxsdk.so")

        self.strip_binaries()


class Linux_x86_64_Manifest(LinuxManifest):
    address_size = 64

    def construct(self):
        super(Linux_x86_64_Manifest, self).construct()

        pkgdir = os.path.join(self.args['build'], os.pardir, 'packages')
        relpkgdir = os.path.join(pkgdir, "lib", "release")
        debpkgdir = os.path.join(pkgdir, "lib", "debug")

        with self.prefix(src=relpkgdir, dst="lib"):
            self.path("libapr-1.so*")
            self.path("libaprutil-1.so*")
            self.path("libexpat.so.*")
            self.path("libGLOD.so")
            self.path("libSDL2*.so*")
            self.path("libopenjp2.*so*")
            self.path("libepoxy.so")
            self.path("libepoxy.so.0")
            self.path("libepoxy.so.0.0.0")
            self.path("libjpeg.so*")

            if self.args['openal'] == 'ON' or self.args['openal'] == 'TRUE':
                self.path("libalut.so*")
                self.path("libopenal.so*")

            if self.args['fmodstudio'] == 'ON' or self.args['fmodstudio'] == 'TRUE':
                self.path("libfmod.so*")

        # Vivox runtimes
        with self.prefix(src=os.path.join(pkgdir, 'bin', 'release'), dst="bin"):
            self.path("SLVoice")
        with self.prefix(src=relpkgdir, dst="lib"):
            self.path("libortp.so")
            self.path("libsndfile.so.1")
            self.path("libvivoxoal.so.1")
            self.path("libvivoxplatform.so")
            self.path("libvivoxsdk.so")

        self.strip_binaries()

################################################################

if __name__ == "__main__":
    # Report our own command line so that, in case of trouble, a developer can
    # manually rerun the same command.
    print('%s \\\n%s' %
          (sys.executable,
           ' '.join((("'%s'" % arg) if ' ' in arg else arg) for arg in sys.argv)))
    # fmodstudio and openal can be used simultaneously and controled by environment
    extra_arguments = [
        dict(name='sentry', description="""Enable Sentry crash report system""", default=''),
        dict(name='fmodstudio', description="""Indication if fmod studio libraries are needed""", default='OFF'),
        dict(name='openal', description="""Indication if openal libraries are needed""", default='OFF'),
        dict(name='kdu', description="""Indication if kdu libraries are needed""", default='OFF'),
        ]
    try:
        main(extra=extra_arguments)
    except (ManifestError, MissingError) as err:
        sys.exit("\nviewer_manifest.py failed: "+err.msg)
    except:
        raise
