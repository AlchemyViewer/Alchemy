# -*- cmake -*-

list(APPEND CMAKE_MESSAGE_CONTEXT viewer)

include(BuildPackagesInfo)
include(BuildVersion)
include(TemplateCheck)

set(AL_VIEWER_BINARY_NAME "alchemy-bin" CACHE STRING
    "The name of the viewer executable to create.")

# Generate version vars header
configure_file(llversioninfovars.h.in llversioninfovars.h @ONLY)

if (AL_BUILD_VIEWER)

# Javascript files for 3D snapshot functionality
configure_file("${INDRA_SOURCE_DIR}/externals/js/CubemapToEquirectangular.js" "${CMAKE_CURRENT_SOURCE_DIR}/skins/default/html/common/equirectangular/js/CubemapToEquirectangular.js" COPYONLY)
configure_file("${INDRA_SOURCE_DIR}/externals/js/jpeg_encoder_basic.js" "${CMAKE_CURRENT_SOURCE_DIR}/skins/default/html/common/equirectangular/js/jpeg_encoder_basic.js" COPYONLY)
configure_file("${INDRA_SOURCE_DIR}/externals/js/three.min.js" "${CMAKE_CURRENT_SOURCE_DIR}/skins/default/html/common/equirectangular/js/three.min.js" COPYONLY)
configure_file("${INDRA_SOURCE_DIR}/externals/js/OrbitControls.js" "${CMAKE_CURRENT_SOURCE_DIR}/skins/default/html/common/equirectangular/js/OrbitControls.js" COPYONLY)

# LSL Definitions
find_package(unofficial-lsl-definitions CONFIG REQUIRED)
configure_file("${LSL_DEFINITIONS_DIR}/lsl_keywords.xml"
               "${CMAKE_CURRENT_SOURCE_DIR}/app_settings/keywords_lsl_default.xml" COPYONLY)
configure_file("${LSL_DEFINITIONS_DIR}/lua_keywords.xml"
               "${CMAKE_CURRENT_SOURCE_DIR}/app_settings/keywords_lua_default.xml" COPYONLY)

find_package(unofficial-alchemy-fonts CONFIG REQUIRED)
message(VERBOSE "Copying fonts from ${ALCHEMY_FONTS_DIR}")
file(GLOB FONT_FILE_GLOB_LIST
  "${ALCHEMY_FONTS_DIR}/*"
)
file(COPY ${FONT_FILE_GLOB_LIST} DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/fonts")

# Copy over the Emoji/shortcodes mapping XML files (and create dependency
# if they are changed, CMake will run again and copy over new versions)
find_package(unofficial-emoji-shortcodes CONFIG REQUIRED)
message(VERBOSE "Copying emoji shortcode mappings from ${EMOJI_SHORTCODES_XUI_DIR}")
set(emoji_mapping_src_folder ${EMOJI_SHORTCODES_XUI_DIR})
set(emoji_mapping_dst_folder ${CMAKE_CURRENT_SOURCE_DIR}/skins/default/xui)

# Turkish (tr) is in the generator's ALLOWED_FOLDERS but not yet shipped
# in the emojibase data we consume; add it here once the upstream port
# starts producing tr/emoji_characters.xml.
set(country_codes "da;de;en;es;fr;it;ja;pl;pt;ru;zh")
foreach(elem ${country_codes})
   set(emoji_mapping_src_file
      "${emoji_mapping_src_folder}/${elem}/emoji_characters.xml")
   set(emoji_mapping_dst_file
      "${emoji_mapping_dst_folder}/${elem}/emoji_characters.xml")
   configure_file(${emoji_mapping_src_file} ${emoji_mapping_dst_file} COPYONLY)
endforeach()

# Copy Icons
# Replace the icons with the appropriate ones for the channel
# ('test' is the default)
set(ICON_PATH "test")
set(VIEWER_MACOSX_PHASE "d")
string(TOLOWER ${AL_CHANNEL} channel_lower)
if(channel_lower MATCHES "^alchemy release")
    set(ICON_PATH "release")
    set(VIEWER_MACOSX_PHASE "f")
elseif(channel_lower MATCHES "^alchemy beta")
    set(ICON_PATH "beta")
    set(VIEWER_MACOSX_PHASE "b")
elseif(channel_lower MATCHES "^alchemy project")
    set(ICON_PATH "project")
    set(VIEWER_MACOSX_PHASE "a")
endif()
message(VERBOSE "Copying icons for ${ICON_PATH}")

find_package(unofficial-alchemy-branding CONFIG REQUIRED)
set(BRANDING_SOURCE_DIR "${ALCHEMY_BRANDING_DIR}")
if (WINDOWS)
  file(COPY_FILE "${BRANDING_SOURCE_DIR}/viewer/icons/${ICON_PATH}/alchemy.ico" "${CMAKE_CURRENT_SOURCE_DIR}/res/ll_icon.ico" ONLY_IF_DIFFERENT)
  file(COPY_FILE "${BRANDING_SOURCE_DIR}/viewer/icons/${ICON_PATH}/alchemy_256.BMP" "${CMAKE_CURRENT_SOURCE_DIR}/res/ll_icon.BMP" ONLY_IF_DIFFERENT)
  file(COPY_FILE "${BRANDING_SOURCE_DIR}/viewer/icons/${ICON_PATH}/alchemy_256.BMP" "${CMAKE_CURRENT_SOURCE_DIR}/res-sdl/ll_icon.BMP" ONLY_IF_DIFFERENT)
  file(COPY_FILE "${BRANDING_SOURCE_DIR}/installer/icons/install_icon.ico" "${CMAKE_CURRENT_SOURCE_DIR}/installers/windows/install_icon.ico" ONLY_IF_DIFFERENT)
  file(COPY_FILE "${BRANDING_SOURCE_DIR}/installer/splash/${ICON_PATH}/install_splash.gif" "${CMAKE_CURRENT_SOURCE_DIR}/installers/windows/install_splash.gif" ONLY_IF_DIFFERENT)
elseif(DARWIN)
  file(COPY_FILE "${BRANDING_SOURCE_DIR}/viewer/icons/${ICON_PATH}/alchemy.icns" "${CMAKE_CURRENT_SOURCE_DIR}/alchemy.icns" ONLY_IF_DIFFERENT)
elseif(LINUX)
  file(COPY_FILE "${BRANDING_SOURCE_DIR}/viewer/icons/${ICON_PATH}/alchemy_256.BMP" "${CMAKE_CURRENT_SOURCE_DIR}/res-sdl/ll_icon.BMP" ONLY_IF_DIFFERENT)
endif ()

# Branded splash icon for the SDL splash screen (LLSplashScreenSDL loads this
# PNG at runtime from app_settings). Staged on all platforms so the SDL build
# finds it regardless of which backend the package ends up using.
file(COPY_FILE "${BRANDING_SOURCE_DIR}/viewer/icons/${ICON_PATH}/alchemy_256.png" "${CMAKE_CURRENT_SOURCE_DIR}/app_settings/alchemy_logo.png" ONLY_IF_DIFFERENT)

set(viewer_SOURCES
    ao.cpp
    aoengine.cpp
    aoset.cpp
    alassetblocklist.cpp
    alavataractions.cpp
    alavatargroups.cpp
    alchatautocomplete.cpp
    alchatbar.cpp
    alchatcommand.cpp
    alcheatcodes.cpp
    alclassifieditem.cpp
    alcolorwheelctrl.cpp
    alcolorwheelmodel.cpp
    alcurveeditorctrl.cpp
    alcurvemodel.cpp
    aldaycyclelandmarks.cpp
    alderenderlist.cpp
    aldroptarget.cpp
    alfloaterblocked.cpp
    alfloaterdirectory.cpp
    alfloaterevent.cpp
    alfloaterexploresounds.cpp
    alfloatergenerictext.cpp
    alfloatergroupprofile.cpp
    alfloaterlightbox.cpp
    alfloaterparticleeditor.cpp
    alfloaterprofilelegacy.cpp
    alfloaterprogressview.cpp
    alfloaterregiontracker.cpp
    alfloatersceneexplorer.cpp
    alfloatersceneexplorerfilters.cpp
    alfloaterscopes.cpp
    alfloatertransactionlog.cpp
    alsceneexplorerpredicate.cpp
    alscopedata.cpp
    alfloaterwebprofile.cpp
    alfloaterxuilibrary.cpp
    alfloaterxuistudio.cpp
    algradehistory.cpp
    allegacynotificationwellwindow.cpp
    alobjectproperties.cpp
    alagentlocationformat.cpp
    alpanelaomini.cpp
    alparceliconstrip.cpp
    alpanelaopulldown.cpp
    alpanelavatarlegacy.cpp
    alpanelclassified.cpp
    alpanelclassifiededit.cpp
    alpaneleventinfo.cpp
    alpanelmusicticker.cpp
    alpanelpick.cpp
    alpanelprofilelegacy.cpp
    alpanelquicksettings.cpp
    alpanelquicksettingspulldown.cpp
    alpanelradaralert.cpp
    alpanelsearchbase.cpp
    alpanelsearchclassifieds.cpp
    alpanelsearchevents.cpp
    alpanelsearchgroups.cpp
    alpanelsearchlandsales.cpp
    alpanelsearchpeople.cpp
    alpanelsearchplaces.cpp
    alpanelsearchweb.cpp
    alpanelstreaminfo.cpp
    alpickitem.cpp
    alsceneexplorermodel.cpp
    alstreaminfo.cpp
    altoolalign.cpp
    altoolscenepicker.cpp
    alunzip.cpp
    alviewermenu.cpp
    alwhitebalancesolver.cpp
    fsfloaterposer.cpp
    fsjointpose.cpp
    fsmaniprotatejoint.cpp
    fsmaniptranslatejoint.cpp
    fsposeranimator.cpp
    fsposestate.cpp
    fsposingmotion.cpp
    gltf/asset.cpp
    gltf/accessor.cpp
    gltf/primitive.cpp
    gltf/animation.cpp
    gltf/llgltfloader.cpp
    groupchatlistener.cpp
    llaccountingcostmanager.cpp
    llaisapi.cpp
    llagent.cpp
    llagentaccess.cpp
    llagentbenefits.cpp
    llagentcamera.cpp
    llagentdata.cpp
    llagenthandler.cpp
    llagentlanguage.cpp
    llagentlistener.cpp
    llagentpicksinfo.cpp
    llagentpilot.cpp
    llagentui.cpp
    llagentwearables.cpp
    llanimstatelabels.cpp
    llappcorehttp.cpp
    llappearancelistener.cpp
    llappearancemgr.cpp
    llappviewer.cpp
    llappviewerlistener.cpp
    llattachmentsmgr.cpp
    llaudiosourcevo.cpp
    llautoreplace.cpp
    llavataractions.cpp
    llavatariconctrl.cpp
    llavatarlist.cpp
    llavatarlistitem.cpp
    llavatarrenderinfoaccountant.cpp
    llavatarrendernotifier.cpp
    llavatarpropertiesprocessor.cpp
    llblockedlistitem.cpp
    llblocklist.cpp
    llbox.cpp
    llbrowsernotification.cpp
    llbuycurrencyhtml.cpp
    llcallingcard.cpp
    llcefaccelinterop.cpp
    llcefsurfacereceiver.cpp
    llchannelmanager.cpp
    #llchatbar.cpp
    llchathistory.cpp
    llchatitemscontainerctrl.cpp
    llchatmsgbox.cpp
    llchiclet.cpp
    llchicletbar.cpp
    llclassifiedinfo.cpp
    llcofwearables.cpp
    llcolorswatch.cpp
    llcommanddispatcherlistener.cpp
    llcommandhandler.cpp
    llcommandlineparser.cpp
    llcommunicationchannel.cpp
    llcompilequeue.cpp
    llconfirmationmanager.cpp
    llcontrolavatar.cpp
    llconversationlog.cpp
    llconversationloglist.cpp
    llconversationloglistitem.cpp
    llconversationmodel.cpp
    llconversationview.cpp
    llcurrencyuimanager.cpp
    llcylinder.cpp
    lldateutil.cpp
    lldebugmessagebox.cpp
    lldebugview.cpp
    lldeferredsounds.cpp
    lldelayedgestureerror.cpp
    lldirpicker.cpp
    lldonotdisturbnotificationstorage.cpp
    lldndbutton.cpp
    lldrawable.cpp
    lldrawpool.cpp
    lldrawpoolalpha.cpp
    lldrawpoolavatar.cpp
    lldrawpoolbump.cpp
    lldrawpoolmaterials.cpp
    lldrawpoolpbropaque.cpp
    lldrawpoolsimple.cpp
    lldrawpoolsky.cpp
    lldrawpoolterrain.cpp
    lldrawpooltree.cpp
    lldrawpoolwater.cpp
    lldrawpoolwlsky.cpp
    lldrawpoolwaterexclusion.cpp
    lldynamictexture.cpp
    lleasymessagelogentry.cpp
    lleasymessagereader.cpp
    lleasymessagesender.cpp
    llemote.cpp
    llenvironment.cpp
    llestateinfomodel.cpp
    lleventnotifier.cpp
    lleventpoll.cpp
    llexpandabletextbox.cpp
    llexperiencelog.cpp
    llexternaleditor.cpp
    llface.cpp
    llfavoritesbar.cpp
    llfeaturemanager.cpp
    llfetchedgltfmaterial.cpp
    llfilepicker.cpp
    llfilteredwearablelist.cpp
    llfirstuse.cpp
    llflexibleobject.cpp
    llfloater360capture.cpp
    llfloaterabout.cpp
    llfloaterassetrecovery.cpp
    llfloateravatarwelcomepack.cpp
    llfloaterbvhpreview.cpp
    llfloateraddpaymentmethod.cpp
    llfloaterauction.cpp
    llfloaterautoreplacesettings.cpp
    llfloateravatarpicker.cpp
#   llfloateravatarrendersettings.cpp
    llfloateravatartextures.cpp
    llfloaterbanduration.cpp
    llfloaterbeacons.cpp
    llfloaterbuildoptions.cpp
    llfloaterbulkpermission.cpp
    llfloaterbulkupload.cpp
    llfloaterbump.cpp
    llfloaterbuy.cpp
    llfloaterbuycontents.cpp
    llfloaterbuycurrency.cpp
    llfloaterbuycurrencyhtml.cpp
    llfloaterbuyland.cpp
    llfloatercamera.cpp
    llfloatercamerapresets.cpp
    llfloaterchangeitemthumbnail.cpp
    llfloaterchatalerts.cpp
    llfloaterchatmentionpicker.cpp
    llfloaterchatvoicevolume.cpp
    llfloaterclassified.cpp
    llfloatercolorpicker.cpp
    llfloaterconversationlog.cpp
    llfloaterconversationpreview.cpp
    llfloatercreatelandmark.cpp
    llfloaterdeleteprefpreset.cpp
    llfloaterdestinations.cpp
    llfloaterdirectory.cpp
    llfloaterdisplayname.cpp
    llfloatereditenvironmentbase.cpp
    llfloatereditextdaycycle.cpp
    llfloateremojipicker.cpp
    llfloaterenvironmentadjust.cpp
    llfloaterevent.cpp
    llfloaterexperiencepicker.cpp
    llfloaterexperienceprofile.cpp
    llfloaterexperiences.cpp
    llfloaterfixedenvironment.cpp
    llfloaterfonttest.cpp
    llfloaterforgetuser.cpp
    llfloatergesture.cpp
    llfloatergestureautocompletepicker.cpp
    llfloatergodtools.cpp
    llfloatergotoline.cpp
    llfloatergridstatus.cpp
    llfloatergroupbulkban.cpp
    llfloatergroupinvite.cpp
    llfloatergroups.cpp
    llfloaterhandler.cpp
    llfloaterhelpbrowser.cpp
    llfloaterhexeditor.cpp
    llfloaterhoverheight.cpp
    llfloaterhud.cpp
    llfloaterimagepreview.cpp
    llfloaterimsessiontab.cpp
    llfloaterimsession.cpp
    llfloaterimcontainer.cpp
    llfloaterinspect.cpp
    llfloaterinventorysettings.cpp
    llfloaterinventorythumbnailshelper.cpp
    llfloaterjoystick.cpp
    llfloaterlagmeter.cpp
    llfloaterland.cpp
    llfloaterlandholdings.cpp
    llfloaterlinkreplace.cpp
    llfloaterloadprefpreset.cpp
    llfloaterlocalassets.cpp
    llfloatermarketplacelistings.cpp
    llfloatermap.cpp
    llfloatermediasettings.cpp
    llfloatermemleak.cpp
    llfloatermessagebuilder.cpp
    llfloatermessagelog.cpp
    llfloatermessagerewriter.cpp
    llfloatermodelpreview.cpp
    llfloatermodeluploadbase.cpp
    llfloatermyscripts.cpp
    llfloatermyenvironment.cpp
    llfloaternamedesc.cpp
    llfloaternewfeaturenotification.cpp
    llfloaternotificationstabbed.cpp
    llfloaterobjectweights.cpp
    llfloateropenobject.cpp
    llfloatersimplesnapshot.cpp
    llfloaterpathfindingcharacters.cpp
    llfloaterpathfindingconsole.cpp
    llfloaterpathfindinglinksets.cpp
    llfloaterpathfindingobjects.cpp
    llfloaterpay.cpp
    llfloaterperformance.cpp
    llfloaterperms.cpp
    llfloaterprofile.cpp
    llfloaterpreference.cpp
    llfloaterpreferencesgraphicsadvanced.cpp
    llfloaterpreferenceviewadvanced.cpp
    llfloaterpreviewtrash.cpp
    llfloaterprofiletexture.cpp
    llfloaterpublishclassified.cpp
    llfloaterregiondebugconsole.cpp
    llfloaterregioninfo.cpp
    llfloaterreporter.cpp
    llfloaterregionrestarting.cpp
    llfloaterregionrestartschedule.cpp
    llfloatersavecamerapreset.cpp
    llfloatersaveprefpreset.cpp
    llfloatersceneloadstats.cpp
    llfloaterscriptdebug.cpp
    llfloaterscriptedprefs.cpp
    llfloaterscripting.cpp
    llfloaterscriptlimits.cpp
    llfloatersearchreplace.cpp
    llfloatersellland.cpp
    llfloatersettingscolor.cpp
    llfloatersettingsdebug.cpp
    llfloatersidepanelcontainer.cpp
    llfloaterslapptest.cpp
    llfloatersnapshot.cpp
    llfloaterspellchecksettings.cpp
    llfloatertelehub.cpp
    llfloatertestinspectors.cpp
    llfloatertestlistview.cpp
    llfloatertools.cpp
    llfloatertopobjects.cpp
    llfloatertos.cpp
    llfloatertoybox.cpp
    llfloatertranslationsettings.cpp
    llfloaterurlentry.cpp
    llfloatervoicevolume.cpp
    llfloaterwebcontent.cpp
    llfloaterwhitelistentry.cpp
    llfloaterwindowsize.cpp
    llfloaterworldmap.cpp
    llfolderviewmodelinventory.cpp
    llfollowcam.cpp
    llfriendcard.cpp
    llflyoutcombobtn.cpp
    llgesturelistener.cpp
    llgesturemgr.cpp
    llgiveinventory.cpp
    llglsandbox.cpp
    llgltffolderitem.cpp
    llgltffoldermodel.cpp
    llgltfmateriallist.cpp
    llgltfmaterialpreviewmgr.cpp
    llgroupactions.cpp
    llgroupiconctrl.cpp
    llgrouplist.cpp
    llgroupmgr.cpp
    llhasheduniqueid.cpp
    llhexeditor.cpp
    llhints.cpp
    llhttpretrypolicy.cpp
    llhudeffect.cpp
    llhudeffectlookat.cpp
    llhudeffectpointat.cpp
    llhudeffecttrail.cpp
    llhudeffectblob.cpp
    llhudeffectresetskeleton.cpp
    llhudicon.cpp
    llhudmanager.cpp
    llhudnametag.cpp
    llhudobject.cpp
    llhudrender.cpp
    llhudtext.cpp
    llhudview.cpp
    llimagefiltersmanager.cpp
    llimhandler.cpp
    llimprocessing.cpp
    llimview.cpp
    llinspect.cpp
    llinspectavatar.cpp
    llinspectgroup.cpp
    llinspectobject.cpp
    llinspectremoteobject.cpp
    llinspecttexture.cpp
    llinspecttoast.cpp
    llinventorybridge.cpp
    llinventoryfilter.cpp
    llinventoryfunctions.cpp
    llinventorygallery.cpp
    llinventorygallerymenu.cpp
    llinventoryicon.cpp
    llinventoryitemslist.cpp
    llinventorylistener.cpp
    llinventorylistitem.cpp
    llinventorymodel.cpp
    llinventorymodelbackgroundfetch.cpp
    llinventoryobserver.cpp
    llinventorypanel.cpp
    lljoystickbutton.cpp
    llkeyconflict.cpp
    lllandmarkactions.cpp
    lllandmarklist.cpp
    lllegacyatmospherics.cpp
    lllistcontextmenu.cpp
    lllistview.cpp
    lllocalbitmaps.cpp
    lllocalgltfmaterials.cpp
    lllocalmesh.cpp
    lllocalanim.cpp
    lllocalassetpaths.cpp
    lllocationhistory.cpp
    lllocationinputctrl.cpp
    lllogchat.cpp
    llloginhandler.cpp
    lllogininstance.cpp
    llmachineid.cpp
    llmanip.cpp
    llmaniprotate.cpp
    llmanipscale.cpp
    llmaniptranslate.cpp
    llfloatermarketplace.cpp
    llmarketplacefunctions.cpp
    llmarketplacenotifications.cpp
    llmaterialeditor.cpp
    llmaterialmgr.cpp
    llmediactrl.cpp
    llmediadataclient.cpp
    llmenuoptionpathfindingrebakenavmesh.cpp
    llmeshrepository.cpp
    llmimetypes.cpp
    llmodelpreview.cpp
    llmorphview.cpp
    llmoveview.cpp
    llmutelist.cpp
    llnamebox.cpp
    llnameeditor.cpp
    llnamelistctrl.cpp
    llnavigationbar.cpp
    llfloaterimnearbychat.cpp
    llfloaterimnearbychathandler.cpp
    llfloaterimnearbychatlistener.cpp
    llnearbyvoicemoderation.cpp
    llnetmap.cpp
    llnotificationalerthandler.cpp
    llnotificationgrouphandler.cpp
    llnotificationhandlerutil.cpp
    llnotificationhinthandler.cpp
    llnotificationlistitem.cpp
    llnotificationlistview.cpp
    llnotificationmanager.cpp
    llnotificationofferhandler.cpp
    llnotificationscripthandler.cpp
    llnotificationstorage.cpp
    llnotificationtiphandler.cpp
    lloutfitgallery.cpp
    lloutfitslist.cpp
    lloutfitobserver.cpp
    lloutputmonitorctrl.cpp
    llpanelappearancetab.cpp
    llpanelavatar.cpp
    llpanelavatartag.cpp
    llpanelblockedlist.cpp
    llpanelclassified.cpp
    llpanelcontents.cpp
    llpaneldirbrowser.cpp
    llpaneldirclassified.cpp
    llpaneldirevents.cpp
    llpaneldirgroups.cpp
    llpaneldirland.cpp
    llpaneldirpeople.cpp
    llpaneldirplaces.cpp
    llpaneldirweb.cpp
    llpaneleditsky.cpp
    llpaneleditwater.cpp
    llpaneleditwearable.cpp
    llpanelemojicomplete.cpp
    llpanelenvironment.cpp
    llpaneleventinfo.cpp
    llpanelexperiencelisteditor.cpp
    llpanelexperiencelog.cpp
    llpanelexperiencepicker.cpp
    llpanelexperiences.cpp
    llpanelface.cpp
    llpanelgenerictip.cpp
    llpanelgroup.cpp
    llpanelgroupcreate.cpp
    llpanelgroupbulk.cpp
    llpanelgroupbulkban.cpp
    llpanelgroupexperiences.cpp
    llpanelgroupgeneral.cpp
    llpanelgroupinvite.cpp
    llpanelgrouplandmoney.cpp
    llpanelgroupnotices.cpp
    llpanelgrouproles.cpp
    llpanelhome.cpp
    llpanelland.cpp
    llpanellandaudio.cpp
    llpanellandmarkinfo.cpp
    llpanellandmarks.cpp
    llpanellandmedia.cpp
    llpanellogin.cpp
    llpanelloginlistener.cpp
    llpanelmaininventory.cpp
    llpanelmarketplaceinbox.cpp
    llpanelmarketplaceinboxinventory.cpp
    llpanelmediasettingsgeneral.cpp
    llpanelmediasettingspermissions.cpp
    llpanelmediasettingssecurity.cpp
    llpanelnearbymedia.cpp
    llpanelobject.cpp
    llpanelobjectinventory.cpp
    llpanelonlinestatus.cpp
    llpaneloutfitedit.cpp
    llpaneloutfitsinventory.cpp
    llpanelpeople.cpp
    llpanelpeoplemenus.cpp
    llpanelpermissions.cpp
    llpanelplaceinfo.cpp
    llpanelplaceprofile.cpp
    llpanelplaces.cpp
    llpanelplacestab.cpp
    llpanelpresetscamerapulldown.cpp
    llpanelpresetspulldown.cpp
    llpanelprimmediacontrols.cpp
    llpanelprofile.cpp
    llpanelprofileclassifieds.cpp
    llpanelprofilepicks.cpp
    llpanelsnapshot.cpp
    llpanelsnapshotinventory.cpp
    llpanelsnapshotlocal.cpp
    llpanelsnapshotoptions.cpp
    llpanelsnapshotpostcard.cpp
    llpanelsnapshotprofile.cpp
    llpanelteleporthistory.cpp
    llpaneltiptoast.cpp
    llpaneltopinfobar.cpp
    llpanelpulldown.cpp
    llpanelvoicedevicesettings.cpp
    llpanelvolume.cpp
    llpanelvolumepulldown.cpp
    llpanelwearing.cpp
    llparcelselection.cpp
    llparticipantlist.cpp
    llpatchvertexarray.cpp
    llpathfindingcharacter.cpp
    llpathfindingcharacterlist.cpp
    llpathfindinglinkset.cpp
    llpathfindinglinksetlist.cpp
    llpathfindingmanager.cpp
    llpathfindingnavmesh.cpp
    llpathfindingnavmeshstatus.cpp
    llpathfindingnavmeshzone.cpp
    llpathfindingobject.cpp
    llpathfindingobjectlist.cpp
    llpathfindingpathtool.cpp
    llpbrterrainfeatures.cpp
    llperfstats.cpp
    llpersistentnotificationstorage.cpp
    llphysicsmotion.cpp
    llphysicsshapebuilderutil.cpp
    llpipelinelistener.cpp
    llplacesinventorybridge.cpp
    llplacesinventorypanel.cpp
    llplacesfolderview.cpp
    llpopupview.cpp
    llpostcard.cpp
    llprofileimagectrl.cpp
    llprofileimagepicker.cpp
    llpresetsmanager.cpp
    llpreview.cpp
    llpreviewanim.cpp
    llpreviewgesture.cpp
    llpreviewnotecard.cpp
    llpreviewscript.cpp
    llpreviewsound.cpp
    llpreviewtexture.cpp
    llproductinforequest.cpp
    llprogressview.cpp
    llpublishedobjectmgr.cpp
    llrecentpeople.cpp
    llreflectionmap.cpp
    llreflectionmapmanager.cpp
    llheroprobemanager.cpp
    llregioninfomodel.cpp
    llregionposition.cpp
    llremoteparcelrequest.cpp
    llsaveoutfitcombobtn.cpp
    llscenemonitor.cpp
    llsceneview.cpp
    llscreenchannel.cpp
    llscripteditor.cpp
    llscripteditorws.cpp
    llscriptfloater.cpp
    llscrollingpanelparam.cpp
    llscrollingpanelparambase.cpp
    llsculptidsize.cpp
    llsearchableui.cpp
    llsearchcombobox.cpp
    llsearchhistory.cpp
    llsecapi.cpp
    llsechandler_basic.cpp
    llselectmgr.cpp
    llsetkeybinddialog.cpp
    llsettingspicker.cpp
    llsettingsvo.cpp
    llshareavatarhandler.cpp
    llsidepanelappearance.cpp
    llsidepanelinventory.cpp
    llsidepanelinventorysubpanel.cpp
    llsidepaneliteminfo.cpp
    llsidepaneltaskinfo.cpp
    llsidetraypanelcontainer.cpp
    llskinningutil.cpp
    llsky.cpp
    llslurl.cpp
    llsnapshotlivepreview.cpp
    llspatialpartition.cpp
    llstatslistener.cpp
    llspeakers.cpp
    llspeakingindicatormanager.cpp
    llsplitbutton.cpp
    llsprite.cpp
    llstartup.cpp
    llstartuplistener.cpp
    llstatusbar.cpp
    llstylemap.cpp
    llsurface.cpp
    llsurfacepatch.cpp
    llsyntaxid.cpp
    llsyswellitem.cpp
    llsyswellwindow.cpp
    llteleporthistory.cpp
    llteleporthistorystorage.cpp
    llterrainpaintmap.cpp
    lltexturecache.cpp
    lltexturectrl.cpp
    lltexturefetch.cpp
    lltextureinfo.cpp
    lltextureinfodetails.cpp
    lltexturestats.cpp
    lltextureview.cpp
    llthumbnailctrl.cpp
    lltinygltfhelper.cpp
    lltoast.cpp
    lltoastalertpanel.cpp
    lltoastgroupnotifypanel.cpp
    lltoastimpanel.cpp
    lltoastnotifypanel.cpp
    lltoastpanel.cpp
    lltoastscripttextbox.cpp
    lltoastscriptquestion.cpp
    lltool.cpp
    lltoolbarview.cpp
    lltoolbrush.cpp
    lltoolcomp.cpp
    lltooldraganddrop.cpp
    lltoolface.cpp
    lltoolfocus.cpp
    lltoolgrab.cpp
    lltoolgun.cpp
    lltoolindividual.cpp
    lltoolmgr.cpp
    lltoolmorph.cpp
    lltoolobjpicker.cpp
    lltoolpie.cpp
    lltoolpipette.cpp
    lltoolplacer.cpp
    lltoolselect.cpp
    lltoolselectland.cpp
    lltoolselectrect.cpp
    lltracker.cpp
    lltrackpicker.cpp
    lltransientdockablefloater.cpp
    lltransientfloatermgr.cpp
    lltranslate.cpp
    lluiavatar.cpp
    lluilistener.cpp
    lluploaddialog.cpp
    llurl.cpp
    llurldispatcher.cpp
    llurldispatcherlistener.cpp
    llurlfloaterdispatchhandler.cpp
    llurlhistory.cpp
    llurllineeditorctrl.cpp
    llurlwhitelist.cpp
    llversioninfo.cpp
#    llvvmquery.cpp
    llviewchildren.cpp
    llviewerassetstats.cpp
    llviewerassetstorage.cpp
    llviewerassettype.cpp
    llviewerassetupload.cpp
    llviewerattachmenu.cpp
    llvieweraudio.cpp
    llviewercamera.cpp
    llviewerchat.cpp
    llviewercontrol.cpp
    llviewercontrollistener.cpp
    llviewerdisplay.cpp
    llviewerdisplayname.cpp
    llviewerfloaterreg.cpp
    llviewerfoldertype.cpp
    llviewergenericmessage.cpp
    llviewergesture.cpp
    llviewerhelp.cpp
    llviewerhelputil.cpp
    llviewerhome.cpp
    llviewerinventory.cpp
    llviewerjoint.cpp
    llviewerjointattachment.cpp
    llviewerjointmesh.cpp
    llviewerjoystick.cpp
    llviewerinput.cpp
    llviewerlayer.cpp
    llviewermedia.cpp
    llviewermedia_streamingaudio.cpp
    llviewermediafocus.cpp
    llviewermenu.cpp
    llviewermenufile.cpp
    llviewermessage.cpp
    llviewernetwork.cpp
    llviewerobject.cpp
    llviewerobjectlist.cpp
    llvieweroctree.cpp
    llviewerparcelaskplay.cpp
    llviewerparcelmedia.cpp
    llviewerparcelmediaautoplay.cpp
    llviewerparcelmgr.cpp
    llviewerparceloverlay.cpp
    llviewerpartsim.cpp
    llviewerpartsource.cpp
    llviewerregion.cpp
    llviewershadermgr.cpp
    llviewerstats.cpp
    llviewerstatsrecorder.cpp
    llviewertexlayer.cpp
    llviewertexteditor.cpp
    llviewertexture.cpp
    llviewertextureanim.cpp
    llviewertexturelist.cpp
    llviewerthrottle.cpp
    llviewerwearable.cpp
    llviewerwindow.cpp
    llviewerwindowlistener.cpp
	llvisualeffect.cpp
    llvlcomposition.cpp
    llvlmanager.cpp
    llvoavatar.cpp
    llvoavatarself.cpp
    llvocache.cpp
    llvograss.cpp
    llvoicecallhandler.cpp
    llvoicechannel.cpp
    llvoiceclient.cpp
    llvoicevisualizer.cpp
    llvoicewebrtc.cpp
    llvoinventorylistener.cpp
    llvopartgroup.cpp
    llvosky.cpp
    llvosurfacepatch.cpp
    llvotree.cpp
    llvovolume.cpp
    llvowater.cpp
    llvowlsky.cpp
    llwearableitemslist.cpp
    llwearablelist.cpp
    llweb.cpp
    llwebprofile.cpp
    llwind.cpp
    llwindowlistener.cpp
    llwlhandlers.cpp
    llworld.cpp
    llworldmap.cpp
    llworldmapmessage.cpp
    llworldmipmap.cpp
    llworldmapview.cpp
    llxmlrpclistener.cpp
    llxmlrpctransaction.cpp
    lutcube.cpp
    noise.cpp
    pipeline.cpp
    rlvactions.cpp
    rlvenvironment.cpp
    rlvcommon.cpp
    rlveffects.cpp
    rlvextensions.cpp
    rlvfloaters.cpp
    rlvhandler.cpp
    rlvhelper.cpp
    rlvinventory.cpp
    rlvlocks.cpp
    rlvui.cpp
    )

set(viewer_HEADERS
    ao.h
    aoengine.h
    aoset.h
    alassetblocklist.h
    alavataractions.h
    alavatargroups.h
    alchatautocomplete.h
    alchatbar.h
    alchatcommand.h
    alclassifieditem.h
    alcolorwheelctrl.h
    alcolorwheelmodel.h
    alcurveeditorctrl.h
    alcurvemodel.h
    aldaycyclelandmarks.h
    alderenderlist.h
    aldroptarget.h
    alfloaterblocked.h
    alfloaterdirectory.h
    alfloaterevent.h
    alfloaterexploresounds.h
    alfloatergenerictext.h
    alfloatergroupprofile.h
    alfloaterlightbox.h
    alfloaterparticleeditor.h
    alfloaterprofilelegacy.h
    alfloaterprogressview.h
    alfloaterregiontracker.h
    alfloatersceneexplorer.h
    alfloatersceneexplorerfilters.h
    alfloaterscopes.h
    alfloatertransactionlog.h
    alsceneexplorerpredicate.h
    alscopedata.h
    altexturetable.h
    alfloaterwebprofile.h
    alfloaterxuilibrary.h
    alfloaterxuistudio.h
    algradehistory.h
    allegacynotificationwellwindow.h
    alobjectproperties.h
    alagentlocationformat.h
    alpanelaomini.h
    alparceliconstrip.h
    alpanelaopulldown.h
    alpanelavatarlegacy.h
    alpanelclassified.h
    alpanelclassifiededit.h
    alpaneleventinfo.h
    alpanelmusicticker.h
    alpanelpick.h
    alpanelprofilelegacy.h
    alpanelquicksettings.h
    alpanelquicksettingspulldown.h
    alpanelradaralert.h
    alpanelsearchbase.h
    alpanelsearchclassifieds.h
    alpanelsearchevents.h
    alpanelsearchgroups.h
    alpanelsearchlandsales.h
    alpanelsearchpeople.h
    alpanelsearchplaces.h
    alpanelsearchweb.h
    alpanelstreaminfo.h
    alpickitem.h
    alsceneexplorermodel.h
    alstreaminfo.h
    altoolalign.h
    altoolscenepicker.h
    alunzip.h
    alviewermenu.h
    alwhitebalancesolver.h
    fsfloaterposer.h
    fsjointpose.h
    fsmaniprotatejoint.h
    fsmaniptranslatejoint.h
    fsposeranimator.h
    fsposestate.h
    fsposingmotion.h
    groupchatlistener.h
    gltf/asset.h
    gltf/accessor.h
    gltf/buffer_util.h
    gltf/primitive.h
    gltf/animation.h
    gltf/llgltfloader.h
    llaccountingcost.h
    llaccountingcostmanager.h
    llaisapi.h
    llagent.h
    llagentaccess.h
    llagentbenefits.h
    llagentcamera.h
    llagentdata.h
    llagentlanguage.h
    llagentlistener.h
    llagentpicksinfo.h
    llagentpilot.h
    llagentui.h
    llagentwearables.h
    llanimstatelabels.h
    llappcorehttp.h
    llappearance.h
    llappearancelistener.h
    llappearancemgr.h
    llappviewer.h
    llappviewerlistener.h
    llattachmentsmgr.h
    llaudiosourcevo.h
    llautoreplace.h
    llavataractions.h
    llavatariconctrl.h
    llavatarlist.h
    llavatarlistitem.h
    llavatarpropertiesprocessor.h
    llavatarrenderinfoaccountant.h
    llavatarrendernotifier.h
    llblockedlistitem.h
    llblocklist.h
    llbox.h
    llbuycurrencyhtml.h
    llcallingcard.h
    llcapabilityprovider.h
    llcefaccelinterop.h
    llcefsurfacereceiver.h
    llchannelmanager.h
    #llchatbar.h
    llchathistory.h
    llchatitemscontainerctrl.h
    llchatmsgbox.h
    llchiclet.h
    llchicletbar.h
    llclassifiedinfo.h
    llcofwearables.h
    llcolorswatch.h
    llcommanddispatcherlistener.h
    llcommandhandler.h
    llcommandlineparser.h
    llcommunicationchannel.h
    llcompilequeue.h
    llconfirmationmanager.h
    llcontrolavatar.h
    llconversationlog.h
    llconversationloglist.h
    llconversationloglistitem.h
    llconversationmodel.h
    llconversationview.h
    llcurrencyuimanager.h
    llcylinder.h
    lldateutil.h
    lldebugmessagebox.h
    lldebugview.h
    lldeferredsounds.h
    lldelayedgestureerror.h
    lldirpicker.h
    lldonotdisturbnotificationstorage.h
    lldndbutton.h
    lldrawable.h
    lldrawpool.h
    lldrawpoolalpha.h
    lldrawpoolavatar.h
    lldrawpoolbump.h
    lldrawpoolmaterials.h
    lldrawpoolpbropaque.h
    lldrawpoolsimple.h
    lldrawpoolsky.h
    lldrawpoolterrain.h
    lldrawpooltree.h
    lldrawpoolwater.h
    lldrawpoolwlsky.h
    lldrawpoolwaterexclusion.h
    lldynamictexture.h
    lleasymessagelogentry.h
    lleasymessagereader.h
    lleasymessagesender.h
    llemote.h
    llenvironment.h
    llestateinfomodel.h
    lleventnotifier.h
    lleventpoll.h
    llexpandabletextbox.h
    llexperiencelog.h
    llexternaleditor.h
    llface.h
    llfavoritesbar.h
    llfeaturemanager.h
    llfetchedgltfmaterial.h
    llfilepicker.h
    llsdlfiledialog.h
    llfilteredwearablelist.h
    llfirstuse.h
    llflexibleobject.h
    llfloater360capture.h
    llfloaterabout.h
    llfloaterassetrecovery.h
    llfloateravatarwelcomepack.h
    llfloaterbvhpreview.h
    llfloateraddpaymentmethod.h
    llfloaterauction.h
    llfloaterautoreplacesettings.h
    llfloateravatarpicker.h
#   llfloateravatarrendersettings.h
    llfloateravatartextures.h
    llfloaterbanduration.h
    llfloaterbeacons.h
    llfloaterbuildoptions.h
    llfloaterbulkpermission.h
    llfloaterbulkupload.h
    llfloaterbump.h
    llfloaterbuy.h
    llfloaterbuycontents.h
    llfloaterbuycurrency.h
    llfloaterbuycurrencyhtml.h
    llfloaterbuyland.h
    llfloatercamerapresets.h
    llfloaterchangeitemthumbnail.h
    llfloaterchatalerts.h
    llfloaterchatmentionpicker.h
    llfloatercamera.h
    llfloaterchatvoicevolume.h
    llfloaterclassified.h
    llfloatercolorpicker.h
    llfloaterconversationlog.h
    llfloaterconversationpreview.h
    llfloatercreatelandmark.h
    llfloaterdeleteprefpreset.h
    llfloaterdestinations.h
    llfloaterdirectory.h
    llfloaterdisplayname.h
    llfloatereditenvironmentbase.h
    llfloatereditextdaycycle.h
    llfloateremojipicker.h
    llfloaterenvironmentadjust.h
    llfloaterevent.h
    llfloaterexperiencepicker.h
    llfloaterexperienceprofile.h
    llfloaterexperiences.h
    llfloaterfixedenvironment.h
    llfloaterfonttest.h
    llfloaterforgetuser.h
    llfloatergesture.h
    llfloatergestureautocompletepicker.h
    llfloatergodtools.h
    llfloatergotoline.h
    llfloatergridstatus.h
    llfloatergroupbulkban.h
    llfloatergroupinvite.h
    llfloatergroups.h
    llfloaterhandler.h
    llfloaterhelpbrowser.h
    llfloaterhexeditor.h
    llfloaterhoverheight.h
    llfloaterhud.h
    llfloaterimagepreview.h
    llfloaterimnearbychat.h
    llfloaterimnearbychathandler.h
    llfloaterimnearbychatlistener.h
    llfloaterimsessiontab.h
    llfloaterimsession.h
    llfloaterimcontainer.h
    llfloaterinspect.h
    llfloaterinventorysettings.h
    llfloaterinventorythumbnailshelper.h
    llfloaterjoystick.h
    llfloaterlagmeter.h
    llfloaterland.h
    llfloaterlandholdings.h
    llfloaterlinkreplace.h
    llfloaterloadprefpreset.h
    llfloaterlocalassets.h
    llfloatermap.h
    llfloatermarketplace.h
    llfloatermarketplacelistings.h
    llfloatermediasettings.h
    llfloatermemleak.h
    llfloatermessagebuilder.h
    llfloatermessagelog.h
    llfloatermessagerewriter.h
    llfloatermodelpreview.h
    llfloatermodeluploadbase.h
    llfloatermyscripts.h
    llfloatermyenvironment.h
    llfloaternamedesc.h
    llfloaternewfeaturenotification.h
    llfloaternotificationstabbed.h
    llfloaterobjectweights.h
    llfloateropenobject.h
    llfloatersimplesnapshot.h
    llfloaterpathfindingcharacters.h
    llfloaterpathfindingconsole.h
    llfloaterpathfindinglinksets.h
    llfloaterpathfindingobjects.h
    llfloaterpay.h
    llfloaterperformance.h
    llfloaterperms.h
    llfloaterprofile.h
    llfloaterpreference.h
    llfloaterpreferencesgraphicsadvanced.h
    llfloaterpreferenceviewadvanced.h
    llfloaterpreviewtrash.h
    llfloaterprofiletexture.h
    llfloaterpublishclassified.h
    llfloaterregiondebugconsole.h
    llfloaterregioninfo.h
    llfloaterreporter.h
    llfloaterregionrestarting.h
    llfloaterregionrestartschedule.h
    llfloatersavecamerapreset.h
    llfloatersaveprefpreset.h
    llfloatersceneloadstats.h
    llfloaterscriptdebug.h
    llfloaterscriptedprefs.h
    llfloaterscripting.h
    llfloaterscriptlimits.h
    llfloatersearchreplace.h
    llfloatersellland.h
    llfloatersettingscolor.h
    llfloatersettingsdebug.h
    llfloatersidepanelcontainer.h
    llfloaterslapptest.h
    llfloatersnapshot.h
    llfloaterspellchecksettings.h
    llfloatertelehub.h
    llfloatertestinspectors.h
    llfloatertestlistview.h
    llfloatertools.h
    llfloatertopobjects.h
    llfloatertos.h
    llfloatertoybox.h
    llfloatertranslationsettings.h
    llfloaterurlentry.h
    llfloatervoicevolume.h
    llfloaterwebcontent.h
    llfloaterwhitelistentry.h
    llfloaterwindowsize.h
    llfloaterworldmap.h
    llfolderviewmodelinventory.h
    llfollowcam.h
    llfriendcard.h
    llflyoutcombobtn.h
    llgesturelistener.h
    llgesturemgr.h
    llgiveinventory.h
    llgltffolderitem.h
    llgltffoldermodel.h
    llgltfmateriallist.h
    llgltfmaterialpreviewmgr.h
    llgroupactions.h
    llgroupiconctrl.h
    llgrouplist.h
    llgroupmgr.h
    llhasheduniqueid.h
    llhexeditor.h
    llhints.h
    llhttpretrypolicy.h
    llhudeffect.h
    llhudeffectlookat.h
    llhudeffectpointat.h
    llhudeffecttrail.h
    llhudeffectblob.h
    llhudeffectresetskeleton.h
    llhudicon.h
    llhudmanager.h
    llhudnametag.h
    llhudobject.h
    llhudrender.h
    llhudtext.h
    llhudview.h
    llimagefiltersmanager.h
    llimprocessing.h
    llimview.h
    llinspect.h
    llinspectavatar.h
    llinspectgroup.h
    llinspectobject.h
    llinspectremoteobject.h
    llinspecttexture.h
    llinspecttoast.h
    llinventorybridge.h
    llinventoryfilter.h
    llinventoryfunctions.h
    llinventorygallery.h
    llinventorygallerymenu.h
    llinventoryicon.h
    llinventoryitemslist.h
    llinventorylistener.h
    llinventorylistitem.h
    llinventorymodel.h
    llinventorymodelbackgroundfetch.h
    llinventoryobserver.h
    llinventorypanel.h
    lljoystickbutton.h
    llkeyconflict.h
    lllandmarkactions.h
    lllandmarklist.h
    lllightconstants.h
    lllistcontextmenu.h
    lllistview.h
    lllocalbitmaps.h
    lllocalgltfmaterials.h
    lllocalmesh.h
    lllocalanim.h
    lllocalassetpaths.h
    lllocationhistory.h
    lllocationinputctrl.h
    lllogchat.h
    llloginhandler.h
    lllogininstance.h
    llmachineid.h
    llmanip.h
    llmaniprotate.h
    llmanipscale.h
    llmaniptranslate.h
    llmarketplacefunctions.h
    llmarketplacenotifications.h
    llmaterialeditor.h
    llmaterialmgr.h
    llmediactrl.h
    llmediadataclient.h
    llmenuoptionpathfindingrebakenavmesh.h
    llmeshrepository.h
    llmimetypes.h
    llmodelpreview.h
    llmorphview.h
    llmoveview.h
    llmutelist.h
    llnamebox.h
    llnameeditor.h
    llnamelistctrl.h
    llnavigationbar.h
    llnearbyvoicemoderation.h
    llnetmap.h
    llnotificationhandler.h
    llnotificationlistitem.h
    llnotificationlistview.h
    llnotificationmanager.h
    llnotificationstorage.h
    lloutfitgallery.h
    lloutfitslist.h
    lloutfitobserver.h
    lloutputmonitorctrl.h
    llpanelappearancetab.h
    llpanelavatar.h
    llpanelavatartag.h
    llpanelblockedlist.h
    llpanelclassified.h
    llpanelcontents.h
    llpaneldirbrowser.h
    llpaneldirclassified.h
    llpaneldirevents.h
    llpaneldirgroups.h
    llpaneldirland.h
    llpaneldirpeople.h
    llpaneldirplaces.h
    llpaneldirweb.h
    llpaneleditsky.h
    llpaneleditwater.h
    llpaneleditwearable.h
    llpanelemojicomplete.h
    llpanelenvironment.h
    llpaneleventinfo.h
    llpanelexperiencelisteditor.h
    llpanelexperiencelog.h
    llpanelexperiencepicker.h
    llpanelexperiences.h
    llpanelface.h
    llpanelgenerictip.h
    llpanelgroup.h
    llpanelgroupcreate.h
    llpanelgroupbulk.h
    llpanelgroupbulkimpl.h
    llpanelgroupbulkban.h
    llpanelgroupexperiences.h
    llpanelgroupgeneral.h
    llpanelgroupinvite.h
    llpanelgrouplandmoney.h
    llpanelgroupnotices.h
    llpanelgrouproles.h
    llpanelhome.h
    llpanelland.h
    llpanellandaudio.h
    llpanellandmarkinfo.h
    llpanellandmarks.h
    llpanellandmedia.h
    llpanellogin.h
    llpanelloginlistener.h
    llpanelmaininventory.h
    llpanelmarketplaceinbox.h
    llpanelmarketplaceinboxinventory.h
    llpanelmediasettingsgeneral.h
    llpanelmediasettingspermissions.h
    llpanelmediasettingssecurity.h
    llpanelnearbymedia.h
    llpanelobject.h
    llpanelobjectinventory.h
    llpanelonlinestatus.h
    llpaneloutfitedit.h
    llpaneloutfitsinventory.h
    llpanelpeople.h
    llpanelpeoplemenus.h
    llpanelpermissions.h
    llpanelplaceinfo.h
    llpanelplaceprofile.h
    llpanelplaces.h
    llpanelplacestab.h
    llpanelpresetscamerapulldown.h
    llpanelpresetspulldown.h
    llpanelprimmediacontrols.h
    llpanelprofile.h
    llpanelprofileclassifieds.h
    llpanelprofilepicks.h
    llpanelsnapshot.h
    llpanelteleporthistory.h
    llpaneltiptoast.h
    llpanelpulldown.h
    llpanelvoicedevicesettings.h
    llpaneltopinfobar.h
    llpanelvolume.h
    llpanelvolumepulldown.h
    llpanelwearing.h
    llparcelselection.h
    llparticipantlist.h
    llpatchvertexarray.h
    llpathfindingcharacter.h
    llpathfindingcharacterlist.h
    llpathfindinglinkset.h
    llpathfindinglinksetlist.h
    llpathfindingmanager.h
    llpathfindingnavmesh.h
    llpathfindingnavmeshstatus.h
    llpathfindingnavmeshzone.h
    llpathfindingobject.h
    llpathfindingobjectlist.h
    llpathfindingpathtool.h
    llperfstats.h
    llpbrterrainfeatures.h
    llpersistentnotificationstorage.h
    llphysicsmotion.h
    llphysicsshapebuilderutil.h
    llpipelinelistener.h
    llplacesinventorybridge.h
    llplacesinventorypanel.h
    llplacesfolderview.h
    llpopupview.h
    llpostcard.h
    llprofileimagectrl.h
    llprofileimagepicker.h
    llpresetsmanager.h
    llpreview.h
    llpreviewanim.h
    llpreviewgesture.h
    llpreviewnotecard.h
    llpreviewscript.h
    llpreviewsound.h
    llpreviewtexture.h
    llproductinforequest.h
    llprogressview.h
    llpublishedobjectmgr.h
    llrecentpeople.h
    llreflectionmap.h
    llreflectionmapmanager.h
    llheroprobemanager.h
    llregioninfomodel.h
    llregionposition.h
    llremoteparcelrequest.h
    llresourcedata.h
    llrootview.h
    llsaveoutfitcombobtn.h
    llscenemonitor.h
    llsceneview.h
    llscreenchannel.h
    llscripteditor.h
    llscripteditorws.h
    llscriptfloater.h
    llscriptruntimeperms.h
    llscrollingpanelparam.h
    llscrollingpanelparambase.h
    llsculptidsize.h
    llsearchableui.h
    llsearchcombobox.h
    llsearchhistory.h
    llsecapi.h
    llsechandler_basic.h
    llselectmgr.h
    llsetkeybinddialog.h
    llsettingspicker.h
    llsettingsvo.h
    llsidepanelappearance.h
    llsidepanelinventory.h
    llsidepanelinventorysubpanel.h
    llsidepaneliteminfo.h
    llsidepaneltaskinfo.h
    llsidetraypanelcontainer.h
    llskinningutil.h
    llsky.h
    llslurl.h
    llsnapshotlivepreview.h
    llsnapshotmodel.h
    llspatialpartition.h
    llstatslistener.h
    llspeakers.h
    llspeakingindicatormanager.h
    llsplitbutton.h
    llsprite.h
    llstartup.h
    llstartuplistener.h
    llstatusbar.h
    llstylemap.h
    llsurface.h
    llsurfacepatch.h
    llsyntaxid.h
    llsyswellitem.h
    llsyswellwindow.h
    lltable.h
    llteleporthistory.h
    llteleporthistorystorage.h
    llterrainpaintmap.h
    lltexturecache.h
    lltexturectrl.h
    lltexturefetch.h
    lltextureinfo.h
    lltextureinfodetails.h
    lltexturestats.h
    lltextureview.h
    llthumbnailctrl.h
    lltinygltfhelper.h
    lltoast.h
    lltoastalertpanel.h
    lltoastgroupnotifypanel.h
    lltoastimpanel.h
    lltoastnotifypanel.h
    lltoastpanel.h
    lltoastscripttextbox.h
    lltoastscriptquestion.h
    lltool.h
    lltoolbarview.h
    lltoolbrush.h
    lltoolcomp.h
    lltooldraganddrop.h
    lltoolface.h
    lltoolfocus.h
    lltoolgrab.h
    lltoolgun.h
    lltoolindividual.h
    lltoolmgr.h
    lltoolmorph.h
    lltoolobjpicker.h
    lltoolpie.h
    lltoolpipette.h
    lltoolplacer.h
    lltoolselect.h
    lltoolselectland.h
    lltoolselectrect.h
    lltracker.h
    lltrackpicker.h
    lltransientdockablefloater.h
    lltransientfloatermgr.h
    lltranslate.h
    lluiconstants.h
    lluiavatar.h
    lluilistener.h
    lluploaddialog.h
    lluploadfloaterobservers.h
    llurl.h
    llurldispatcher.h
    llurldispatcherlistener.h
    llurlfloaterdispatchhandler.h
    llurlhistory.h
    llurllineeditorctrl.h
    llurlwhitelist.h
    llversioninfo.h
#    llvvmquery.h
    llviewchildren.h
    llviewerassetstats.h
    llviewerassetstorage.h
    llviewerassettype.h
    llviewerassetupload.h
    llviewerattachmenu.h
    llvieweraudio.h
    llviewercamera.h
    llviewerchat.h
    llviewercontrol.h
    llviewercontrollistener.h
    llviewerdisplay.h
    llviewerdisplayname.h
    llviewerfloaterreg.h
    llviewerfoldertype.h
    llviewergenericmessage.h
    llviewergesture.h
    llviewerhelp.h
    llviewerhome.h
    llviewerinventory.h
    llviewerjoint.h
    llviewerjointattachment.h
    llviewerjointmesh.h
    llviewerjoystick.h
    llviewerinput.h
    llviewerlayer.h
    llviewermedia.h
    llviewermediafocus.h
    llviewermediaobserver.h
    llviewermenu.h
    llviewermenufile.h
    llviewermessage.h
    llviewernetwork.h
    llviewerobject.h
    llviewerobjectlist.h
    llvieweroctree.h
    llviewerparcelaskplay.h
    llviewerparcelmedia.h
    llviewerparcelmediaautoplay.h
    llviewerparcelmgr.h
    llviewerparceloverlay.h
    llviewerpartsim.h
    llviewerpartsource.h
    llviewerprecompiledheaders.h
    llviewerregion.h
    llviewershadermgr.h
    llviewerstats.h
    llviewerstatsrecorder.h
    llviewertexlayer.h
    llviewertexteditor.h
    llviewertexture.h
    llviewertextureanim.h
    llviewertexturelist.h
    llviewerthrottle.h
    llviewerwearable.h
    llviewerwindow.h
    llviewerwindowlistener.h
	llvisualeffect.h
    llvlcomposition.h
    llvlmanager.h
    llvoavatar.h
    llvoavatarself.h
    llvocache.h
    llvograss.h
    llvoicechannel.h
    llvoiceclient.h
    llvoicevisualizer.h
    llvoicewebrtc.h
    llvoinventorylistener.h
    llvopartgroup.h
    llvosky.h
    llvosurfacepatch.h
    llvotree.h
    llvovolume.h
    llvowater.h
    llvowlsky.h
    llwearableitemslist.h
    llwearablelist.h
    llweb.h
    llwebprofile.h
    llwind.h
    llwindowlistener.h
    llwlhandlers.h
    llworld.h
    llworldmap.h
    llworldmapmessage.h
    llworldmipmap.h
    llworldmapview.h
    llxmlrpclistener.h
    llxmlrpctransaction.h
    lutcube.h
    noise.h
    pipeline.h
    rlvactions.h
    rlvcommon.h
    rlvdefines.h
    rlveffects.h
    rlvenvironment.h
    rlvextensions.h
    rlvfloaters.h
    rlvhandler.h
    rlvhelper.h
    rlvinventory.h
    rlvlocks.h
    rlvmodifiers.h
    rlvui.h
    roles_constants.h
    )

# The version as one line, for the hosted build's metadata.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/viewer_version.txt"
           "${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_REVISION}\n")

if (DARWIN)
  list(APPEND viewer_SOURCES llvelopack.cpp)
  list(APPEND viewer_HEADERS llvelopack.h)

  # Crash-metadata helpers shared by both the legacy Cocoa and SDL backends.
  list(APPEND viewer_SOURCES llbugsplat_mac.cpp)
  list(APPEND viewer_HEADERS llbugsplat_mac.h)

  # Add resource files to the project.
  set(viewer_RESOURCE_FILES
    alchemy.icns
    # CMake doesn't seem to support Xcode language variants well just yet
    English.lproj/InfoPlist.strings
    English.lproj/language.txt
    German.lproj/language.txt
    Japanese.lproj/language.txt
    Korean.lproj/language.txt
    )

  list(APPEND viewer_SOURCES llappviewersdl.cpp)
  list(APPEND viewer_HEADERS llappviewersdl.h)
  list(APPEND viewer_RESOURCE_FILES
    Info-AlchemySDL.plist
    )

  set_source_files_properties(llappviewersdl.cpp PROPERTIES LANGUAGE OBJCXX)

  # Preserved for possible build option to fall back to native macos filepicker
  # list(APPEND viewer_SOURCES llfilepicker_mac.mm)
  # list(APPEND viewer_HEADERS llfilepicker_mac.h)

  # set_source_files_properties(
  #   llfilepicker_mac.mm
  #   PROPERTIES
  #   SKIP_PRECOMPILE_HEADERS TRUE
  # )

  source_group("Resources" FILES ${viewer_RESOURCE_FILES})
endif (DARWIN)

if (LINUX)
    list(APPEND viewer_SOURCES llappviewersdl.cpp)
    list(APPEND viewer_HEADERS llappviewersdl.h)
endif (LINUX)

if (WINDOWS)
    list(APPEND viewer_SOURCES
         llappviewerwin32.cpp
         llvelopack.cpp
         llwindebug.cpp
         )

    list(APPEND viewer_HEADERS
         llappviewerwin32.h
         llvelopack.h
         llwindebug.h
         )

    if (AL_USE_SDL_WINDOW)
        # Unified SDL main-callbacks entry point shared with Linux/macOS.
        # llappviewerwin32's native wWinMain is compiled out (LL_SDL_WINDOW);
        # SDL_AppInit instantiates LLAppViewerWin32 to keep the Win32
        # platform overrides.
        list(APPEND viewer_SOURCES llappviewersdl.cpp)
        list(APPEND viewer_HEADERS llappviewersdl.h)
    endif ()

    # What viewerRes.rc reads. Listed for the IDE; the .rc itself is the
    # build input.
    set(viewer_RESOURCE_FILES
        res/arrow.cur
        res/arrowcop.cur
        res/arrowcopmulti.cur
        res/arrowdrag.cur
        res/circleandline.cur
        res/icon1.ico
        res/llarrow.cur
        res/llarrowdrag.cur
        res/llarrowdragmulti.cur
        res/llarrowlocked.cur
        res/llgrablocked.cur
        res/llno.cur
        res/llnolocked.cur
        res/lltoolcamera.cur
        res/lltoolcreate.cur
        res/lltoolfocus.cur
        res/lltoolgrab.cur
        res/lltoolland.cur
        res/lltoolpan.cur
        res/lltoolpathfinding.cur
        res/lltoolpathfindingpathend.cur
        res/lltoolpathfindingpathendadd.cur
        res/lltoolpathfindingpathstart.cur
        res/lltoolpathfindingpathstartadd.cur
        res/lltoolpipette.cur
        res/lltoolrotate.cur
        res/lltoolscale.cur
        res/lltooltranslate.cur
        res/lltoolzoomin.cur
        res/lltoolzoomout.cur
        res-sdl/ll_icon.BMP
        res/ll_icon.BMP
        res/ll_icon.ico
        res/resource.h
        res/toolpickobject.cur
        res/toolpickobject2.cur
        res/toolpickobject3.cur
        res/toolpipette.cur
        res/toolbuy.cur
        res/toolopen.cur
        res/toolsit.cur
        )

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/res/viewerRes.rc
                   ${CMAKE_CURRENT_BINARY_DIR}/viewerRes.rc
                   )
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/viewerRes.rc
      PROPERTIES COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/res"
      )
    list(APPEND viewer_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/viewerRes.rc)

    source_group("Resource Files" FILES ${CMAKE_CURRENT_BINARY_DIR}/viewerRes.rc ${viewer_RESOURCE_FILES})

    if (ADDRESS_SIZE EQUAL 64)
        # We deliberately omit this from the 32bit build because it declares that
        # the viewer is compatible with Windows 10; we need that to properly detect
        # the Windows version, but doing so causes systems with certain HD video
        # cards to fail because Windows 10 does not support them.  Leaving this out
        # causes those systems to run in a Windows 8 compatibility mode, which works.
        list(APPEND viewer_SOURCES windows.manifest)
    endif (ADDRESS_SIZE EQUAL 64)

endif (WINDOWS)

# Add the xui files. This is handy for searching for xui elements
# from within the IDE.
file(GLOB_RECURSE viewer_XUI_FILES LIST_DIRECTORIES FALSE
    ${CMAKE_CURRENT_SOURCE_DIR}/skins/*.xml)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/skins PREFIX "XUI Files" FILES ${viewer_XUI_FILES})

# Add the shader sources
file(GLOB_RECURSE viewer_SHADER_FILES LIST_DIRECTORIES FALSE
    ${CMAKE_CURRENT_SOURCE_DIR}/app_settings/shaders/*.glsl)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/app_settings/shaders PREFIX "Shaders" FILES ${viewer_SHADER_FILES})

# note that the scripts which publish these files are sensitive to the file
# extensions. Be cautious about adding new file types here.
set(viewer_APPSETTINGS_FILES
    app_settings/anim.ini
    app_settings/autoreplace.xml
    app_settings/cmd_line.xml
    app_settings/commands.xml
    app_settings/emoji_groups.xml
    app_settings/foldertypes.xml
    app_settings/grass.xml
    app_settings/ignorable_dialogs.xml
    app_settings/key_bindings.xml
    app_settings/logcontrol.xml
    app_settings/settings.xml
    app_settings/settings_alchemy.xml
    app_settings/settings_crash_behavior.xml
    app_settings/settings_files.xml
    app_settings/settings_per_account.xml
    app_settings/settings_per_account_alchemy.xml
    app_settings/std_bump.ini
    app_settings/toolbars.xml
    app_settings/trees.xml
    app_settings/viewerart.xml
    app_settings/message.xml
    ${SCRIPTS_DIR}/messages/message_template.msg
    featuretable.txt
    featuretable_mac.txt
    featuretable_linux.txt
    )

source_group("App Settings" FILES ${viewer_APPSETTINGS_FILES})

set(viewer_CHARACTER_FILES
    character/attentions.xml
    character/attentionsN.xml
    character/avatar_lad.xml
    character/avatar_skeleton.xml
    character/genepool.xml
    )

source_group("Character File" FILES ${viewer_CHARACTER_FILES})

# What the viewer loads at run time, kept in the IDE beside the code. None
# of it is compiled, so it hangs off its own target and the viewer's project
# holds only what the compiler and linker consume.
add_custom_target(${AL_VIEWER_BINARY_NAME}-resources
    SOURCES
    ${viewer_XUI_FILES}
    ${viewer_SHADER_FILES}
    ${viewer_APPSETTINGS_FILES}
    ${viewer_CHARACTER_FILES}
    ${viewer_RESOURCE_FILES}
    )

add_executable(${AL_VIEWER_BINARY_NAME} WIN32 MACOSX_BUNDLE)

target_sources(${AL_VIEWER_BINARY_NAME}
    PRIVATE
    ${viewer_SOURCES}
    ${viewer_HEADERS}
    )

check_message_template(${AL_VIEWER_BINARY_NAME})

target_include_directories(${AL_VIEWER_BINARY_NAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

target_precompile_headers(${AL_VIEWER_BINARY_NAME} PRIVATE llviewerprecompiledheaders.h)

# *NOTE: - this list is very sensitive to ordering, test carefully on all
# platforms if you change the relative order of the entries here.
# In particular, cmake 2.6.4 (when building with linux/makefile generators)
# appears to sometimes de-duplicate redundantly listed dependencies improperly.
# To work around this, higher level modules should be listed before the modules
# that they depend upon. -brad
#
# *NOTE:  On mixing system shared libraries and updated static archives.
# We use a number of libraries that have an existence as system libraries,
# internal-use libraries and applications libraries.  The most-referenced
# one of these being libz where you can find four or more versions in play
# at once.  On Linux, libz can be found at link and run time via a number
# of paths:
#
#      => -lfreetype
#        => libz.so.1 (on install machine, not build)
#      => -lSDL
#        => libz.so.1 (on install machine, not build)
#      => -lgdk-x11-2.0
#        => libz.so.1
#      => -lz
#
# We generally want the newest version of the library to provide all symbol
# resolution.  To that end, when using static archives, the *_PRELOAD_ARCHIVES
# variables, PNG_PRELOAD_ARCHIVES and ZLIBNG_PRELOAD_ARCHIVES, get the archives
# dumped into the target binary and runtime lookup will find the most
# modern version.

target_link_libraries(${AL_VIEWER_BINARY_NAME}
        PRIVATE
        al::flags
        llaudio
        llcharacter
        llimage
        llinventory
        llmessage
        llplugin
        llprimitive
        llrender
        llui
        llfilesystem
        llwindow
        llxml
        llmath
        llcorehttp
        llcommon
        lllogin
        llprimitive
        llappearance
        ll::bugsplat
        ll::ndof
        ll::tracy
        ll::openxr
        ll::discord_sdk
        ll::nvapi
        ll::tinygltf
        ll::tinyexr
        ll::minizip
        ll::pugixml
        )

target_link_libraries(${AL_VIEWER_BINARY_NAME} PRIVATE ll::velopack ll::webrtc)

if (DARWIN)
    find_library(IOSURFACE_FRAMEWORK IOSurface)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
    target_link_libraries(${AL_VIEWER_BINARY_NAME}
      PRIVATE
      ${IOSURFACE_FRAMEWORK}
      ${COREGRAPHICS_FRAMEWORK}
    )
endif()

# The media plugins are built before the viewer so the staging step after
# the viewer's link can install them.
add_custom_target(copy_input_dependencies
    DEPENDS
    $<TARGET_NAME_IF_EXISTS:media_plugin_cef>
    $<TARGET_NAME_IF_EXISTS:media_plugin_gstreamer10>
    $<$<CONFIG:OptDebug,RelWithDebInfo,Release>:$<TARGET_NAME_IF_EXISTS:media_plugin_libvlc>>
    $<TARGET_NAME_IF_EXISTS:media_plugin_example>
    )
add_dependencies(${AL_VIEWER_BINARY_NAME} copy_input_dependencies)

if (WINDOWS)
  target_link_libraries(${AL_VIEWER_BINARY_NAME} PRIVATE d3d11 dxgi)

    string(TOLOWER ${AL_CHANNEL} channel_lower)
    if(channel_lower MATCHES "^alchemy release")
        set(OUTPUT_BINARY_NAME "AlchemyViewer")
    else()
        set(OUTPUT_BINARY_NAME "${AL_CHANNEL_ONEWORD}")
    endif()

  set_target_properties(${AL_VIEWER_BINARY_NAME}
      PROPERTIES
      OUTPUT_NAME "${OUTPUT_BINARY_NAME}"
      RUNTIME_OUTPUT_DIRECTORY ${VIEWER_STAGING_DIR}
      ENABLE_EXPORTS OFF
      VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" # Sets VS debugger working directory
      )
  set(product ${OUTPUT_BINARY_NAME}-${ARCH}-${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_REVISION})
elseif(DARWIN)
  # These all get set with PROPERTIES. It's not that the property names are
  # magically known to CMake -- it's that these names are referenced in the
  # Info-Alchemy.plist file in the configure_file() directive below.
  set(product "${AL_CHANNEL}")
  set(MACOSX_EXECUTABLE_NAME "${AL_CHANNEL}")
  set(MACOSX_BUNDLE_INFO_STRING "${AL_CHANNEL}")
  set(MACOSX_BUNDLE_ICON_FILE "alchemy.icns")
  set(MACOSX_BUNDLE_GUI_IDENTIFIER "org.alchemyviewer.viewer")
  set(MACOSX_BUNDLE_LONG_VERSION_STRING "${AL_CHANNEL} ${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_REVISION}")
  set(MACOSX_BUNDLE_BUNDLE_NAME "Alchemy")
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_REVISION}")
  set(MACOSX_BUNDLE_BUNDLE_VERSION "${VIEWER_SHORT_VERSION}${VIEWER_MACOSX_PHASE}${VIEWER_REVISION}")
  set(MACOSX_BUNDLE_COPYRIGHT "Copyright © Alchemy Development Team 2012-2026")
  set(MACOSX_BUNDLE_NSPRINCIPAL_CLASS "LLApplication")

  set_target_properties(
    ${AL_VIEWER_BINARY_NAME}
    PROPERTIES
    OUTPUT_NAME "${product}"
    RUNTIME_OUTPUT_DIRECTORY ${VIEWER_STAGING_DIR}
    # From Contents/MacOS/Alchemy, look in Contents/Frameworks
    BUILD_WITH_INSTALL_RPATH 1
    INSTALL_RPATH "@executable_path/../Frameworks"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info-AlchemySDL.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${MACOSX_BUNDLE_GUI_IDENTIFIER}"
    )

  # Support generating dsym for non-xcode generators
  if(NOT XCODE)
    if(AL_USE_BUGSPLAT)
      set(VIEWER_DEBUG_BUNDLE "${SYMBOLS_STAGING_DIR}/dSYMs/${product}.dSYM")
      find_program(DSYMUTIL_PROGRAM dsymutil REQUIRED)
      add_custom_command(TARGET ${AL_VIEWER_BINARY_NAME} POST_BUILD
            COMMAND ${DSYMUTIL_PROGRAM} -o ${VIEWER_DEBUG_BUNDLE} $<TARGET_FILE:${AL_VIEWER_BINARY_NAME}>
            COMMENT "Generating Alchemy.dSYM")
    endif()
  endif()

  target_link_libraries(${AL_VIEWER_BINARY_NAME} PRIVATE llphysicsextensionsos)

  # The bundle's localised strings carry the version.
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/English.lproj/InfoPlist.strings" infoplist_strings)
  string(REPLACE "%%VERSION%%" "${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_REVISION}" infoplist_strings "${infoplist_strings}")
  file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/InfoPlist.strings" CONTENT "${infoplist_strings}" @ONLY)
elseif(LINUX)
  set_target_properties(
    ${AL_VIEWER_BINARY_NAME}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${VIEWER_STAGING_DIR}/bin/
    )

  if(AL_USE_BUGSPLAT)
    find_program(OBJCOPY_PROGRAM objcopy REQUIRED)
    set(DEBUG_BINARY_TARGET $<TARGET_FILE:${AL_VIEWER_BINARY_NAME}>)
    set(DEBUG_FILE_NAME "${SYMBOLS_STAGING_DIR}/$<TARGET_FILE_NAME:${AL_VIEWER_BINARY_NAME}>.debug")
    add_custom_command(TARGET ${AL_VIEWER_BINARY_NAME} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E make_directory ${SYMBOLS_STAGING_DIR}
          COMMAND ${OBJCOPY_PROGRAM} --only-keep-debug --compress-debug-sections=zlib ${DEBUG_BINARY_TARGET} ${DEBUG_FILE_NAME}
          COMMAND ${OBJCOPY_PROGRAM} --strip-debug --strip-unneeded ${DEBUG_BINARY_TARGET}
          COMMAND ${OBJCOPY_PROGRAM} --add-gnu-debuglink=${DEBUG_FILE_NAME} ${DEBUG_BINARY_TARGET}
          COMMENT "Generating ${DEBUG_FILE_NAME}")
  endif()

  string(TOLOWER ${AL_CHANNEL} channel_lower)
  if(channel_lower MATCHES "^alchemy release")
    set(OUTPUT_BINARY_NAME "AlchemyViewer")
  else()
    set(OUTPUT_BINARY_NAME "${AL_CHANNEL_ONEWORD}")
  endif()

  set(product ${OUTPUT_BINARY_NAME}-${ARCH}-${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_REVISION})
endif()

# What ships, and where; the names the package goes by.
include(ViewerInstall)

# The files generated for the package: the contributor list the About
# floater shows, the settings an installation is built with, and the build
# description the crash reporter reads.
include(ContributorNames)
al_contributor_names("${INDRA_SOURCE_DIR}/../doc/contributions.txt" "${CMAKE_CURRENT_BINARY_DIR}/contributors.txt")

set(AL_SOURCEID "$ENV{sourceid}" CACHE STRING "Referring agency recorded in settings_install.xml")
set(settings_install "")
if(AL_SOURCEID)
  string(APPEND settings_install "  <key>sourceid</key>
  <map>
    <key>Comment</key>
    <string>Identify referring agency to Linden web servers</string>
    <key>Persist</key>
    <integer>1</integer>
    <key>Type</key>
    <string>String</string>
    <key>Value</key>
    <string>${AL_SOURCEID}</string>
  </map>
")
endif()
if(NOT AL_GRID MATCHES "^(|default|agni)$")
  string(APPEND settings_install "  <key>CmdLineGridChoice</key>
  <map>
    <key>Comment</key>
    <string>Default grid</string>
    <key>Persist</key>
    <integer>0</integer>
    <key>Type</key>
    <string>String</string>
    <key>Value</key>
    <string>${AL_GRID}</string>
  </map>
")
endif()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/settings_install.xml" CONTENT
"<?xml version=\"1.0\" ?>
<llsd>
<map>
${settings_install}</map>
</llsd>
" @ONLY)

set(build_data_extra "")
if (WINDOWS)
  set(build_data_platform win)
  string(APPEND build_data_extra ", \"Executable\": \"${OUTPUT_BINARY_NAME}.exe\", \"AppName\": \"${AL_APP_NAME}\"")
elseif (DARWIN)
  set(build_data_platform mac)
  string(APPEND build_data_extra ", \"Bundle Id\": \"${MACOSX_BUNDLE_GUI_IDENTIFIER}\"")
else ()
  set(build_data_platform lnx)
endif ()
if (AL_BUGSPLAT_DATABASE)
  string(APPEND build_data_extra ", \"BugSplat DB\": \"${AL_BUGSPLAT_DATABASE}\"")
endif ()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/build_data.json" CONTENT
"{\"Type\": \"viewer\", \"Version\": \"${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_REVISION}\", \"Channel Base\": \"Alchemy\", \"Channel\": \"${AL_CHANNEL}\", \"Platform\": \"${build_data_platform}\", \"Address Size\": ${ADDRESS_SIZE}, \"Update Service\": \"https://update.secondlife.com/update\"${build_data_extra}}
" @ONLY)

# The archive of the installed tree, and the Velopack step.
include(ViewerPackage)

# After every link, stage the tree the viewer runs from: the install rules
# applied to the build directory the viewer already sits in.
add_custom_command(
  TARGET ${AL_VIEWER_BINARY_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --config $<CONFIG> --prefix ${VIEWER_STAGING_DIR}
  COMMENT "Staging the viewer into ${VIEWER_STAGING_DIR}"
  VERBATIM
  )

if (AL_BUILD_PACKAGE AND AL_ENABLE_CRASH_REPORTING)
  if (AL_USE_BUGSPLAT)
    # BugSplat symbol-file generation
    if (WINDOWS)
      # Redirect symbols to staging directory for upload to BugSplat.
      set_target_properties(${AL_VIEWER_BINARY_NAME}
          PROPERTIES
          PDB_OUTPUT_DIRECTORY "${SYMBOLS_STAGING_DIR}"
          )

      set(VIEWER_APP_SYMBOLS_ARCHIVE "${SYMBOLS_STAGING_DIR}.sym.tar.xz")

      # Just pack up a tarball containing only the .pdb files for the
      # executables.
      add_custom_command(OUTPUT "${VIEWER_APP_SYMBOLS_ARCHIVE}"
        COMMAND "tar"
        ARGS
          "cJf"
          "${AL_CHANNEL}.sym.tar.xz"
          "${AL_CHANNEL}"
        DEPENDS "${AL_VIEWER_BINARY_NAME}"
        WORKING_DIRECTORY "${SYMBOLS_STAGING_DIR}/.."
        COMMENT "Packing viewer PDBs into ${VIEWER_APP_SYMBOLS_ARCHIVE}"
        )
      add_custom_target(generate_symbols DEPENDS "${VIEWER_APP_SYMBOLS_ARCHIVE}")
      add_dependencies(generate_symbols ${AL_VIEWER_BINARY_NAME})

    endif (WINDOWS)
    if (DARWIN)
      set(VIEWER_APP_XCARCHIVE "${SYMBOLS_STAGING_DIR}.xcarchive.zip")

      # we only need an xcarchive with dSYMs (including the application)
      set_target_properties(${AL_VIEWER_BINARY_NAME}
            PROPERTIES
            XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
            XCODE_ATTRIBUTE_DWARF_DSYM_FOLDER_PATH "${SYMBOLS_STAGING_DIR}/dSYMs")

      add_custom_command(OUTPUT "${VIEWER_APP_XCARCHIVE}"
        COMMAND "zip"
        ARGS
          "-r"
          "${VIEWER_APP_XCARCHIVE}"
            "${AL_CHANNEL}"
          WORKING_DIRECTORY "${SYMBOLS_STAGING_DIR}/.."
          DEPENDS "${AL_VIEWER_BINARY_NAME}"
          COMMENT "Generating ${VIEWER_APP_XCARCHIVE} for upload to BugSplat"
        )
      add_custom_target(generate_symbols DEPENDS
        "${VIEWER_APP_XCARCHIVE}"
        )
    endif (DARWIN)
    if (LINUX)
      # TBD
    endif (LINUX)

    add_dependencies(package generate_symbols)
  endif (AL_USE_BUGSPLAT)
endif ()

elseif(AL_BUILD_TESTS)

# Create a dummy executable for test projects to generate against when viewer build is disabled
add_executable(${AL_VIEWER_BINARY_NAME} cmake_dummy.cpp)
target_link_libraries(${AL_VIEWER_BINARY_NAME} PRIVATE al::flags)
target_include_directories(${AL_VIEWER_BINARY_NAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

endif(AL_BUILD_VIEWER)

if (AL_BUILD_TESTS)
  set(test_libs
    llfilesystem
    llmath
    llcommon
    llmessage
    llcorehttp
    llplugin
    llappearance
    llprimitive
    lllogin
  )
  set(test_project ${AL_VIEWER_BINARY_NAME})

  # A unit test compiles one viewer source file on its own. Each line is one
  # such file and its test under tests/.
  foreach(test
      alagentlocationformat alcolorwheelmodel alcurvemodel aldaycyclelandmarks
      algradehistory alsceneexplorerpredicate alwhitebalancesolver
      llagentaccess lldateutil llviewerhelputil llversioninfo llvocache
      lutcube)
    al_add_test(${test} UNIT PROJECT ${test_project} LIBRARIES ${test_libs})
  endforeach()
  # The vectorscope bins on the colour wheels' own chroma plane, so the scope
  # data test links the model that defines it rather than keeping a second copy
  # of the basis that could drift from it.
  al_add_test(alscopedata UNIT PROJECT ${test_project} LIBRARIES ${test_libs}
    SOURCES alcolorwheelmodel.cpp)
  al_add_test(lllogininstance UNIT PROJECT ${test_project} LIBRARIES ${test_libs}
    SOURCES llversioninfo.cpp)
  al_add_test(llworldmap UNIT PROJECT ${test_project} LIBRARIES ${test_libs}
    SOURCES tests/llviewertexture_stub.cpp)
  al_add_test(llworldmipmap UNIT PROJECT ${test_project} LIBRARIES ${test_libs}
    SOURCES tests/llviewertexture_stub.cpp)

  al_add_test(cppfeatures PROJECT ${test_project} LIBRARIES ${test_libs})
  al_add_test(altexturetable PROJECT ${test_project} LIBRARIES ${test_libs})
  al_add_test(llsechandler_basic PROJECT ${test_project} LIBRARIES ${test_libs}
    SOURCES llsechandler_basic.cpp)
  al_add_test(llsecapi PROJECT ${test_project} LIBRARIES ${test_libs}
    SOURCES llsecapi.cpp)
  al_add_test(llslurl PROJECT ${test_project} LIBRARIES ${test_libs}
    SOURCES llslurl.cpp llviewernetwork.cpp)
  al_add_test(llviewercontrollistener PROJECT ${test_project} LIBRARIES ${test_libs}
    SOURCES
    llviewercontrollistener.cpp
    ../llxml/llcontrol.cpp
    ../llxml/llxmltree.cpp
    ../llxml/alxmldocument.cpp
    ../llcommon/commoncontrol.cpp)
  al_add_test(llviewernetwork PROJECT ${test_project} LIBRARIES ${test_libs}
    SOURCES llviewernetwork.cpp)
  al_add_test(llviewerassetstats PROJECT ${test_project} LIBRARIES ${test_libs}
    SOURCES llviewerassetstats.cpp)
  al_add_test(llhttpretrypolicy PROJECT ${test_project} LIBRARIES ${test_libs}
    SOURCES llhttpretrypolicy.cpp)
  al_add_test(lltextureinfo PROJECT ${test_project} LIBRARIES ${test_libs}
    SOURCES
    lltextureinfo.cpp
    ../llxml/llcontrol.cpp
    ../llxml/llxmltree.cpp
    ../llxml/alxmldocument.cpp
    ../llcommon/commoncontrol.cpp)
endif ()
