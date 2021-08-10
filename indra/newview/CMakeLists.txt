# -*- cmake -*-

project(viewer)

include(00-Common)
# DON'T move Linking.cmake to its place in the alphabetized list below: it
# sets variables on which the 3p .cmake files depend.
include(Linking)

include(Abseil)
include(Boost)
include(BuildPackagesInfo)
include(BuildVersion)
include(CMakeCopyIfDifferent)
include(DBusGlib)
include(DragDrop)
include(Epoxy)
include(EXPAT)
include(FMODSTUDIO)
include(GLOD)
include(Hunspell)
include(LLAppearance)
include(LLAudio)
include(LLCA)
include(LLCharacter)
include(LLCommon)
include(LLCoreHttp)
include(LLImage)
include(LLImageJ2COJ)
include(LLInventory)
include(LLKDU)
include(LLLogin)
include(LLMath)
include(LLMessage)
include(LLPhysicsExtensions)
include(LLPlugin)
include(LLPrimitive)
include(LLRender)
include(LLUI)
include(LLFileSystem)
include(LLWindow)
include(LLXML)
include(NDOF)
include(NVAPI)
include(OPENAL)
include(OpenGL)
include(OpenSSL)
include(PNG)
include(Sentry)
include(TemplateCheck)
include(UI)
include(UnixInstall)
include(ViewerMiscLibs)
include(ViewerManager)
include(VisualLeakDetector)
include(ZLIB)
include(URIPARSER)

if( LLPHYSICSEXTENSIONS_SRC_DIR )
  if (NOT HAVOK_TPV)
    # When using HAVOK_TPV, the library is precompiled, so no need for this
    add_subdirectory(${LLPHYSICSEXTENSIONS_SRC_DIR} llphysicsextensions)
    target_link_libraries(${LLPHYSICSEXTENSIONS_LIBRARIES} llcommon)
  endif (NOT HAVOK_TPV)
endif( LLPHYSICSEXTENSIONS_SRC_DIR )

message(STATUS "CMAKE_CURRENT_BINARY_DIR = ${CMAKE_CURRENT_BINARY_DIR}")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${DBUSGLIB_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${GLOD_INCLUDE_DIR}
    ${LLAUDIO_INCLUDE_DIRS}
    ${LLCHARACTER_INCLUDE_DIRS}
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLCOREHTTP_INCLUDE_DIRS}
    ${LLPHYSICS_INCLUDE_DIRS}
    ${LLIMAGE_INCLUDE_DIRS}
    ${LLKDU_INCLUDE_DIRS}
    ${LLINVENTORY_INCLUDE_DIRS}
    ${LLMATH_INCLUDE_DIRS}
    ${LLMESSAGE_INCLUDE_DIRS}
    ${LLPLUGIN_INCLUDE_DIRS}
    ${LLPRIMITIVE_INCLUDE_DIRS}
    ${LLRENDER_INCLUDE_DIRS}
    ${LLUI_INCLUDE_DIRS}
    ${LLFILESYSTEM_INCLUDE_DIRS}
    ${LLWINDOW_INCLUDE_DIRS}
    ${LLXML_INCLUDE_DIRS}
    ${LLLOGIN_INCLUDE_DIRS}
    "${LIBS_PREBUILT_DIR}/include/collada"
    "${LIBS_PREBUILD_DIR}/include/hunspell"
    ${OPENAL_INCLUDE_DIRS}
    ${FREEALUT_INCLUDE_DIRS}
    "${LIBS_PREBUILT_DIR}/include/collada/1.4"
    ${LLAPPEARANCE_INCLUDE_DIRS}
    )

if(USE_FMODSTUDIO)
  include_directories(${FMODSTUDIO_INCLUDE_DIR})
endif(USE_FMODSTUDIO)

include_directories(SYSTEM
    ${LLCOMMON_SYSTEM_INCLUDE_DIRS}
    ${LLXML_SYSTEM_INCLUDE_DIRS}
    ${LLPHYSICSEXTENSIONS_INCLUDE_DIRS}
    )

if (USE_SENTRY)
  include_directories(AFTER
    ${SENTRY_INCLUDE_DIRS}
    )
endif (USE_SENTRY)

set(viewer_SOURCE_FILES
    alcinematicmode.cpp
    alaoengine.cpp
    alaoset.cpp
    alavataractions.cpp
    alchatcommand.cpp
    alcontrolcache.cpp
    alfloaterao.cpp
    alfloaterparticleeditor.cpp
    alfloaterregiontracker.cpp
    alpanelaomini.cpp
    alpanelaopulldown.cpp
    alpanelmusicticker.cpp
    alpanelquicksettings.cpp
    alpanelquicksettingspulldown.cpp
    altoolalign.cpp
    alunzip.cpp
    alviewermenu.cpp
    groupchatlistener.cpp
    llaccountingcostmanager.cpp
    llaisapi.cpp
    llagent.cpp
    llagentaccess.cpp
    llagentbenefits.cpp
    llagentcamera.cpp
    llagentdata.cpp
    llagentlanguage.cpp
    llagentlistener.cpp
    llagentpicksinfo.cpp
    llagentpilot.cpp
    llagentui.cpp
    llagentwearables.cpp
    llanimstatelabels.cpp
    llappcorehttp.cpp
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
    llbreadcrumbview.cpp
    llbrowsernotification.cpp
    llbuycurrencyhtml.cpp
    llcallingcard.cpp
    llchannelmanager.cpp
    llchatbar.cpp
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
    lldrawpoolground.cpp
    lldrawpoolmaterials.cpp
    lldrawpoolsimple.cpp
    lldrawpoolsky.cpp
    lldrawpoolterrain.cpp
    lldrawpooltree.cpp
    lldrawpoolwater.cpp
    lldrawpoolwlsky.cpp
    lldynamictexture.cpp
    llemote.cpp
    llenvironment.cpp
    llestateinfomodel.cpp
    lleventnotifier.cpp
    lleventpoll.cpp
    llexpandabletextbox.cpp
    llexperiencelog.cpp
    llexternaleditor.cpp
    llface.cpp
    llfasttimerview.cpp
    llfavoritesbar.cpp
    llfeaturemanager.cpp
    llfilepicker.cpp
    llfilteredwearablelist.cpp
    llfirstuse.cpp
    llflexibleobject.cpp
    llfloaterabout.cpp
    llfloaterassetrecovery.cpp
    llfloaterbvhpreview.cpp
    llfloateraddpaymentmethod.cpp
    llfloaterauction.cpp
    llfloaterautoreplacesettings.cpp
    llfloateravatar.cpp
    llfloateravatarpicker.cpp
    llfloateravatarrendersettings.cpp
    llfloateravatartextures.cpp
    llfloaterbanduration.cpp
    llfloaterbeacons.cpp
    llfloaterbigpreview.cpp
    llfloaterbuildoptions.cpp
    llfloaterbulkpermission.cpp
    llfloaterbump.cpp
    llfloaterbuy.cpp
    llfloaterbuycontents.cpp
    llfloaterbuycurrency.cpp
    llfloaterbuycurrencyhtml.cpp
    llfloaterbuyland.cpp
    llfloatercamera.cpp
    llfloatercamerapresets.cpp
    llfloaterchatalerts.cpp
    llfloaterchatvoicevolume.cpp
    llfloatercolorpicker.cpp
    llfloaterconversationlog.cpp
    llfloaterconversationpreview.cpp
    llfloatercreatelandmark.cpp
    llfloaterdeleteprefpreset.cpp
    llfloaterdestinations.cpp
    llfloaterdisplayname.cpp
    llfloatereditenvironmentbase.cpp
    llfloatereditextdaycycle.cpp
    llfloaterenvironmentadjust.cpp
    llfloaterevent.cpp
    llfloaterexperiencepicker.cpp
    llfloaterexperienceprofile.cpp
    llfloaterexperiences.cpp
    llfloaterfixedenvironment.cpp
    llfloaterfonttest.cpp
    llfloaterforgetuser.cpp
    llfloatergesture.cpp
    llfloatergodtools.cpp
    llfloatergotoline.cpp
    llfloatergridstatus.cpp
    llfloatergroupbulkban.cpp
    llfloatergroupinvite.cpp
    llfloatergroups.cpp
    llfloaterhandler.cpp
    llfloaterhelpbrowser.cpp
    llfloaterhoverheight.cpp
    llfloaterhowto.cpp
    llfloaterhud.cpp
    llfloaterimagepreview.cpp
    llfloaterimsessiontab.cpp
    llfloaterimsession.cpp
    llfloaterimcontainer.cpp
    llfloaterinspect.cpp
    llfloaterjoystick.cpp
    llfloaterlagmeter.cpp
    llfloaterland.cpp
    llfloaterlandholdings.cpp
    llfloaterlinkreplace.cpp
    llfloaterloadprefpreset.cpp
    llfloatermarketplacelistings.cpp
    llfloatermap.cpp
    llfloatermediasettings.cpp
    llfloatermemleak.cpp
    llfloatermodelpreview.cpp
    llfloatermodeluploadbase.cpp
    llfloatermyscripts.cpp
    llfloatermyenvironment.cpp
    llfloaternamedesc.cpp
    llfloaternotificationsconsole.cpp
    llfloaternotificationstabbed.cpp
    llfloateroutfitphotopreview.cpp 
    llfloateroutfitsnapshot.cpp
    llfloaterobjectweights.cpp
    llfloateropenobject.cpp
    llfloaterpathfindingcharacters.cpp
    llfloaterpathfindingconsole.cpp
    llfloaterpathfindinglinksets.cpp
    llfloaterpathfindingobjects.cpp
    llfloaterpay.cpp
    llfloaterperms.cpp
    llfloaterprofile.cpp
    llfloaterpreference.cpp
    llfloaterpreferenceviewadvanced.cpp
    llfloaterpreviewtrash.cpp
    llfloaterproperties.cpp
    llfloaterregiondebugconsole.cpp
    llfloaterregioninfo.cpp
    llfloaterreporter.cpp
    llfloaterregionrestarting.cpp
    llfloatersavecamerapreset.cpp
    llfloatersaveprefpreset.cpp
    llfloatersceneloadstats.cpp
    llfloaterscriptdebug.cpp
    llfloaterscriptedprefs.cpp
    llfloaterscriptlimits.cpp
    llfloatersearch.cpp
    llfloatersearchreplace.cpp
    llfloatersellland.cpp
    llfloatersettingsdebug.cpp
    llfloatersidepanelcontainer.cpp
    llfloatersnapshot.cpp
    llfloatersounddevices.cpp
    llfloaterspellchecksettings.cpp
    llfloatertelehub.cpp
    llfloatertestinspectors.cpp
    llfloatertestlistview.cpp
    llfloatertexturefetchdebugger.cpp
    llfloatertools.cpp
    llfloatertopobjects.cpp
    llfloatertos.cpp
    llfloatertoybox.cpp
    llfloatertranslationsettings.cpp
    llfloateruipreview.cpp
    llfloaterurlentry.cpp
    llfloatervoiceeffect.cpp
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
    llgroupactions.cpp
    llgroupiconctrl.cpp
    llgrouplist.cpp
    llgroupoptions.cpp
    llgroupmgr.cpp
    llhasheduniqueid.cpp
    llhints.cpp
    llhttpretrypolicy.cpp
    llhudeffect.cpp
    llhudeffectbeam.cpp
    llhudeffectlookat.cpp
    llhudeffectpointat.cpp
    llhudeffecttrail.cpp
    llhudeffectblob.cpp
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
    llinspecttoast.cpp
    llinventorybridge.cpp
    llinventoryfilter.cpp
    llinventoryfunctions.cpp
    llinventoryicon.cpp
    llinventoryitemslist.cpp
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
    lllistbrowser.cpp
    lllistcontextmenu.cpp
    lllistview.cpp
    lllocalbitmaps.cpp
    lllocationhistory.cpp
    lllocationinputctrl.cpp
    lllogchat.cpp
    llloginhandler.cpp
    lllogininstance.cpp
    llmachineid.cpp
    llmainlooprepeater.cpp
    llmanip.cpp
    llmaniprotate.cpp
    llmanipscale.cpp
    llmaniptranslate.cpp
    llmarketplacefunctions.cpp
    llmarketplacenotifications.cpp
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
    llpaneleditsky.cpp
    llpaneleditwater.cpp
    llpaneleditwearable.cpp
    llpanelenvironment.cpp
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
    llpanelpick.cpp
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
    llpanelvoiceeffect.cpp
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
    llpersistentnotificationstorage.cpp
    llphysicsmotion.cpp
    llphysicsshapebuilderutil.cpp
    llpipelinelistener.cpp
    llplacesinventorybridge.cpp
    llplacesinventorypanel.cpp
    llplacesfolderview.cpp
    llpopupview.cpp
    llpostcard.cpp
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
    llrecentpeople.cpp
    llregioninfomodel.cpp
    llregionposition.cpp
    llremoteparcelrequest.cpp
    llsavedsettingsglue.cpp
    llsaveoutfitcombobtn.cpp
    llscenemonitor.cpp
    llsceneview.cpp
    llscreenchannel.cpp
    llscripteditor.cpp
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
    lltelemetry.cpp
    llteleporthistory.cpp
    llteleporthistorystorage.cpp
    lltextureatlas.cpp
    lltextureatlasmanager.cpp
    lltexturecache.cpp
    lltexturectrl.cpp
    lltexturefetch.cpp
    lltextureinfo.cpp
    lltextureinfodetails.cpp
    lltexturestats.cpp
    lltextureview.cpp
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
    llvectorperfoptions.cpp
    llversioninfo.cpp
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
    llvoground.cpp
    llvoicecallhandler.cpp
    llvoicechannel.cpp
    llvoiceclient.cpp
    llvoicevisualizer.cpp
    llvoicevivox.cpp
    llvoinventorylistener.cpp
    llvopartgroup.cpp
    llvosky.cpp
    llvosurfacepatch.cpp
    llvotree.cpp
    llvovolume.cpp
    llvowater.cpp
    llvowlsky.cpp
    llwatchdog.cpp
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

set(VIEWER_BINARY_NAME "alchemy-bin" CACHE STRING
    "The name of the viewer executable to create.")

set(viewer_HEADER_FILES
    CMakeLists.txt
    ViewerInstall.cmake
    alcinematicmode.h
    alaoengine.h
    alaoset.h
    alavataractions.h
    alchatcommand.h
    alcontrolcache.h
    alfloaterao.h
    alfloaterparticleeditor.h
    alfloaterregiontracker.h
    alpanelaomini.h
    alpanelaopulldown.h
    alpanelmusicticker.h
    alpanelquicksettings.h
    alpanelquicksettingspulldown.h
    altoolalign.h
    alunzip.h
    alviewermenu.h
    groupchatlistener.h
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
    llbreadcrumbview.h
    llbuycurrencyhtml.h
    llcallingcard.h
    llcapabilityprovider.h
    llchannelmanager.h
    llchatbar.h
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
    lldrawpoolground.h
    lldrawpoolsimple.h
    lldrawpoolsky.h
    lldrawpoolterrain.h
    lldrawpooltree.h
    lldrawpoolwater.h
    lldrawpoolwlsky.h
    lldynamictexture.h
    llemote.h
    llenvironment.h
    llestateinfomodel.h
    lleventnotifier.h
    lleventpoll.h
    llexpandabletextbox.h
    llexperiencelog.h
    llexternaleditor.h
    llface.h
    llfasttimerview.h
    llfavoritesbar.h
    llfeaturemanager.h
    llfilepicker.h
    llfilteredwearablelist.h
    llfirstuse.h
    llflexibleobject.h
    llfloaterabout.h
    llfloaterassetrecovery.h
    llfloaterbvhpreview.h
    llfloateraddpaymentmethod.h
    llfloaterauction.h
    llfloaterautoreplacesettings.h
    llfloateravatar.h
    llfloateravatarpicker.h
    llfloateravatarrendersettings.h
    llfloateravatartextures.h
    llfloaterbanduration.h
    llfloaterbeacons.h
    llfloaterbigpreview.h
    llfloaterbuildoptions.h
    llfloaterbulkpermission.h
    llfloaterbump.h
    llfloaterbuy.h
    llfloaterbuycontents.h
    llfloaterbuycurrency.h
    llfloaterbuycurrencyhtml.h
    llfloaterbuyland.h
    llfloatercamerapresets.h
    llfloatercamera.h
    llfloaterchatalerts.h
    llfloaterchatvoicevolume.h
    llfloatercolorpicker.h
    llfloaterconversationlog.h
    llfloaterconversationpreview.h
    llfloatercreatelandmark.h
    llfloaterdeleteprefpreset.h
    llfloaterdestinations.h
    llfloaterdisplayname.h
    llfloatereditenvironmentbase.h
    llfloatereditextdaycycle.h
    llfloaterenvironmentadjust.h
    llfloaterevent.h
    llfloaterexperiencepicker.h
    llfloaterexperienceprofile.h
    llfloaterexperiences.h
    llfloaterfixedenvironment.h
    llfloaterfonttest.h
    llfloaterforgetuser.h
    llfloatergesture.h
    llfloatergodtools.h
    llfloatergotoline.h
    llfloatergridstatus.h
    llfloatergroupbulkban.h
    llfloatergroupinvite.h
    llfloatergroups.h
    llfloaterhandler.h
    llfloaterhelpbrowser.h
    llfloaterhoverheight.h
    llfloaterhowto.h
    llfloaterhud.h
    llfloaterimagepreview.h
    llfloaterimnearbychat.h
    llfloaterimnearbychathandler.h
    llfloaterimnearbychatlistener.h
    llfloaterimsessiontab.h
    llfloaterimsession.h
    llfloaterimcontainer.h
    llfloaterinspect.h
    llfloaterjoystick.h
    llfloaterlagmeter.h
    llfloaterland.h
    llfloaterlandholdings.h
    llfloaterlinkreplace.h
    llfloaterloadprefpreset.h
    llfloatermap.h
    llfloatermarketplacelistings.h
    llfloatermediasettings.h
    llfloatermemleak.h
    llfloatermodelpreview.h
    llfloatermodeluploadbase.h
    llfloatermyscripts.h
    llfloatermyenvironment.h
    llfloaternamedesc.h
    llfloaternotificationsconsole.h
    llfloaternotificationstabbed.h
    llfloateroutfitphotopreview.h
    llfloateroutfitsnapshot.h
    llfloaterobjectweights.h
    llfloateropenobject.h
    llfloaterpathfindingcharacters.h
    llfloaterpathfindingconsole.h
    llfloaterpathfindinglinksets.h
    llfloaterpathfindingobjects.h
    llfloaterpay.h
    llfloaterperms.h
    llfloaterprofile.h
    llfloaterpreference.h
    llfloaterpreferenceviewadvanced.h
    llfloaterpreviewtrash.h
    llfloaterproperties.h
    llfloaterregiondebugconsole.h
    llfloaterregioninfo.h
    llfloaterreporter.h
    llfloaterregionrestarting.h
    llfloatersavecamerapreset.h
    llfloatersaveprefpreset.h
    llfloatersceneloadstats.h
    llfloaterscriptdebug.h
    llfloaterscriptedprefs.h
    llfloaterscriptlimits.h
    llfloatersearch.h
    llfloatersearchreplace.h
    llfloatersellland.h
    llfloatersettingsdebug.h
    llfloatersidepanelcontainer.h
    llfloatersnapshot.h
    llfloatersounddevices.h
    llfloaterspellchecksettings.h
    llfloatertelehub.h
    llfloatertestinspectors.h
    llfloatertestlistview.h
    llfloatertexturefetchdebugger.h
    llfloatertools.h
    llfloatertopobjects.h
    llfloatertos.h
    llfloatertoybox.h
    llfloatertranslationsettings.h
    llfloateruipreview.h
    llfloaterurlentry.h
    llfloatervoiceeffect.h
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
    llgroupactions.h
    llgroupiconctrl.h
    llgrouplist.h
    llgroupoptions.h
    llgroupmgr.h
    llhasheduniqueid.h
    llhints.h
    llhttpretrypolicy.h
    llhudeffect.h
    llhudeffectbeam.h
    llhudeffectlookat.h
    llhudeffectpointat.h
    llhudeffecttrail.h
    llhudeffectblob.h
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
    llinspecttoast.h
    llinventorybridge.h
    llinventoryfilter.h
    llinventoryfunctions.h
    llinventoryicon.h
    llinventoryitemslist.h
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
    lllistbrowser.h
    lllistcontextmenu.h
    lllistview.h
    lllocalbitmaps.h
    lllocationhistory.h
    lllocationinputctrl.h
    lllogchat.h
    llloginhandler.h
    lllogininstance.h
    llmachineid.h
    llmainlooprepeater.h
    llmanip.h
    llmaniprotate.h
    llmanipscale.h
    llmaniptranslate.h
    llmarketplacefunctions.h
    llmarketplacenotifications.h
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
    llpaneleditsky.h
    llpaneleditwater.h
    llpaneleditwearable.h
    llpanelenvironment.h
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
    llpanelpick.h
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
    llpanelvoiceeffect.h
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
    llpersistentnotificationstorage.h
    llphysicsmotion.h
    llphysicsshapebuilderutil.h
    llpipelinelistener.h
    llplacesinventorybridge.h
    llplacesinventorypanel.h
    llplacesfolderview.h
    llpopupview.h
    llpostcard.h
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
    llrecentpeople.h
    llregioninfomodel.h
    llregionposition.h
    llremoteparcelrequest.h
    llresourcedata.h
    llrootview.h
    llsavedsettingsglue.h
    llsaveoutfitcombobtn.h
    llscenemonitor.h
    llsceneview.h
    llscreenchannel.h
    llscripteditor.h
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
    lltextureatlas.h
    lltextureatlasmanager.h
    lltexturecache.h
    lltexturectrl.h
    lltexturefetch.h
    lltextureinfo.h
    lltextureinfodetails.h
    lltexturestats.h
    lltextureview.h
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
    llvectorperfoptions.h
    llversioninfo.h
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
    llviewermedia_streamingaudio.h
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
    llvoground.h
    llvoicechannel.h
    llvoiceclient.h
    llvoicevisualizer.h
    llvoicevivox.h
    llvoinventorylistener.h
    llvopartgroup.h
    llvosky.h
    llvosurfacepatch.h
    llvotree.h
    llvovolume.h
    llvowater.h
    llvowlsky.h
    llwatchdog.h
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
    macmain.h
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
    VertexCache.h
    VorbisFramework.h
    )

source_group("CMake Rules" FILES ViewerInstall.cmake)

#build_data.json creation moved to viewer_manifest.py MAINT-6413
# the viewer_version.txt file created here is for passing to viewer_manifest and autobuild
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/viewer_version.txt"
           "${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_REVISION}\n")

configure_file(llviewerbuildconfig.h.in llviewerbuildconfig.h @ONLY)
LIST(APPEND viewer_HEADER_FILES ${CMAKE_CURRENT_BINARY_DIR}/llviewerbuildconfig.h)

if (DARWIN)
  LIST(APPEND viewer_SOURCE_FILES llappviewermacosx.cpp)
  LIST(APPEND viewer_SOURCE_FILES llappviewermacosx-objc.mm)
  LIST(APPEND viewer_HEADER_FILES llappviewermacosx-objc.h)
  LIST(APPEND viewer_SOURCE_FILES llfilepicker_mac.mm)
  LIST(APPEND viewer_HEADER_FILES llfilepicker_mac.h)

  # This should be compiled with the viewer.
  LIST(APPEND viewer_SOURCE_FILES llappdelegate-objc.mm)
  set_source_files_properties(
    llappdelegate-objc.mm
    PROPERTIES
    SKIP_PRECOMPILE_HEADERS TRUE
    COMPILE_FLAGS "-fmodules -fcxx-modules -Wno-nullability-completeness"
    )

    set_source_files_properties(
      llappviewermacosx-objc.mm
      PROPERTIES 
      SKIP_PRECOMPILE_HEADERS TRUE
      )

    set_source_files_properties(
      llfilepicker_mac.mm
      PROPERTIES 
      SKIP_PRECOMPILE_HEADERS TRUE
      )

  find_library(APPKIT_LIBRARY AppKit)
  find_library(COCOA_LIBRARY Cocoa)
  find_library(IOKIT_LIBRARY IOKit)
  find_library(COREFOUNDATION_LIBRARY CoreFoundation)
  find_library(COREAUDIO_LIBRARY CoreAudio)

  set(viewer_LIBRARIES
    ${COCOA_LIBRARY}
    ${APPKIT_LIBRARY}
    ${IOKIT_LIBRARY}
    ${COREFOUNDATION_LIBRARY}
    ${COREAUDIO_LIBRARY}
    )

  # Add resource files to the project.
  set(viewer_RESOURCE_FILES
    macview.r
    Info-Alchemy.plist
    Alchemy.xib/
    # CMake doesn't seem to support Xcode language variants well just yet
    English.lproj/InfoPlist.strings
    English.lproj/language.txt
    German.lproj/language.txt
    Japanese.lproj/language.txt
    Korean.lproj/language.txt
    )
  set_source_files_properties(
    ${viewer_RESOURCE_FILES}
    PROPERTIES
    HEADER_FILE_ONLY TRUE
    #MACOSX_PACKAGE_LOCATION Resources #don't do this! this tells cmake to copy the files.
    )
  SOURCE_GROUP("Resources" FILES ${viewer_RESOURCE_FILES})
  list(APPEND viewer_SOURCE_FILES ${viewer_RESOURCE_FILES})
endif (DARWIN)

if (LINUX)
    LIST(APPEND viewer_SOURCE_FILES llappviewerlinux.cpp)
    LIST(APPEND viewer_SOURCE_FILES llappviewerlinux_api_dbus.cpp)
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--as-needed")

    set(viewer_LIBRARIES
        Xinerama
        )
endif (LINUX)

if (WINDOWS)
    list(APPEND viewer_SOURCE_FILES
         llappviewerwin32.cpp
         llwindebug.cpp
         )

    list(APPEND viewer_HEADER_FILES
         llappviewerwin32.h
         llwindebug.h
         )

    # Replace the icons with the appropriate ones for the channel
    # ('test' is the default)
    set(ICON_PATH "test")
    set(VIEWER_MACOSX_PHASE "d")
    string(TOLOWER ${VIEWER_CHANNEL} channel_lower)
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
    message(STATUS "Copying icons for ${ICON_PATH}")
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/icons/${ICON_PATH}/alchemy.ico"
        "${CMAKE_CURRENT_SOURCE_DIR}/res/ll_icon.ico"
      )
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/icons/${ICON_PATH}/alchemy_256.BMP"
        "${CMAKE_CURRENT_SOURCE_DIR}/res/ll_icon.BMP"
      )
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/icons/${ICON_PATH}/alchemy_256.BMP"
        "${CMAKE_CURRENT_SOURCE_DIR}/res-sdl/ll_icon.BMP"
      )

    # Add resource files to the project.
    # viewerRes.rc is the only buildable file, but
    # the rest are all dependencies of it.
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

    set_source_files_properties(${viewer_RESOURCE_FILES}
                                PROPERTIES HEADER_FILE_ONLY TRUE)

    configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/res/viewerRes.rc
                    ${CMAKE_CURRENT_BINARY_DIR}/viewerRes.rc
                    )

    list(APPEND viewer_RESOURCE_FILES
    	windows.manifest
        ${CMAKE_CURRENT_BINARY_DIR}/viewerRes.rc
        )

    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/viewerRes.rc
      PROPERTIES COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/res"
      )

    source_group("Resource Files" FILES ${viewer_RESOURCE_FILES})

    if (NOT USESYSTEMLIBS)
        list(APPEND viewer_SOURCE_FILES ${viewer_RESOURCE_FILES})
    endif (NOT USESYSTEMLIBS)

    set_source_files_properties(llappviewer.cpp PROPERTIES COMPILE_FLAGS /bigobj)
    set_source_files_properties(llviewermenu.cpp PROPERTIES COMPILE_FLAGS /bigobj)

# see EXP-1765 - theory is opengl32.lib needs to be included before gdi32.lib (windows libs)
    set(viewer_LIBRARIES
        opengl32
        ${WINDOWS_LIBRARIES}
        comdlg32
        dxguid
        kernel32
        odbc32
        odbccp32
        oleaut32
        shell32
        Vfw32
        wer
        winspool
        )
endif (WINDOWS)

if (USE_OPENAL)
    LIST(APPEND viewer_LIBRARIES ${OPENAL_LIBRARIES} ${FREEALUT_LIBRARIES})
endif ()

# Add the xui files. This is handy for searching for xui elements
# from within the IDE.
set(viewer_XUI_FILES
    skins/default/colors.xml
    skins/default/default_languages.xml
    skins/default/textures/textures.xml
    )
file(GLOB DEFAULT_XUI_FILE_GLOB_LIST
     ${CMAKE_CURRENT_SOURCE_DIR}/skins/*/xui/en/*.xml)
list(APPEND viewer_XUI_FILES ${DEFAULT_XUI_FILE_GLOB_LIST})

file(GLOB DEFAULT_WIDGET_FILE_GLOB_LIST
     ${CMAKE_CURRENT_SOURCE_DIR}/skins/*/xui/en/widgets/*.xml)
list(APPEND viewer_XUI_FILES ${DEFAULT_WIDGET_FILE_GLOB_LIST})

list(SORT viewer_XUI_FILES)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/skins" PREFIX "XUI Files" FILES ${viewer_XUI_FILES})

set_source_files_properties(${viewer_XUI_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND viewer_SOURCE_FILES ${viewer_XUI_FILES})

set(viewer_APPSETTINGS_FILES
    app_settings/anim.ini
    app_settings/cmd_line.xml
    app_settings/commands.xml
    app_settings/grass.xml
    app_settings/high_graphics.xml
    app_settings/ignorable_dialogs.xml
    app_settings/key_bindings.xml
    app_settings/keywords_lsl_default.xml
    app_settings/logcontrol.xml
    app_settings/low_graphics.xml
    app_settings/mid_graphics.xml
    app_settings/settings.xml
    app_settings/settings_alchemy.xml
    app_settings/settings_crash_behavior.xml
    app_settings/settings_files.xml
    app_settings/settings_per_account.xml
    app_settings/settings_per_account_alchemy.xml
    app_settings/std_bump.ini
    app_settings/toolbars.xml
    app_settings/trees.xml
    app_settings/ultra_graphics.xml
    app_settings/viewerart.xml
    ${CMAKE_SOURCE_DIR}/../etc/message.xml
    ${CMAKE_SOURCE_DIR}/../scripts/messages/message_template.msg
    packages-info.txt
    )

source_group("App Settings" FILES ${viewer_APPSETTINGS_FILES})

set_source_files_properties(${viewer_APPSETTINGS_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND viewer_SOURCE_FILES ${viewer_APPSETTINGS_FILES})

file(GLOB_RECURSE viewer_SHADER_FILES ${CMAKE_CURRENT_SOURCE_DIR}/app_settings/shaders/*)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/app_settings/shaders" PREFIX "Shaders" FILES ${viewer_SHADER_FILES})

set_source_files_properties(${viewer_SHADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND viewer_SOURCE_FILES ${viewer_SHADER_FILES})

set(viewer_CHARACTER_FILES
    character/attentions.xml
    character/attentionsN.xml
    character/avatar_lad.xml
    character/avatar_skeleton.xml
    character/genepool.xml
    )

source_group("Character File" FILES ${viewer_CHARACTER_FILES})

set_source_files_properties(${viewer_CHARACTER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)
if (NOT USESYSTEMLIBS)
    list(APPEND viewer_SOURCE_FILES ${viewer_CHARACTER_FILES})
endif (NOT USESYSTEMLIBS)

set(viewer_MISC_FILES
    viewer_manifest.py
    )

set_source_files_properties(${viewer_MISC_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND viewer_SOURCE_FILES ${viewer_MISC_FILES})

if (WINDOWS)
  file(GLOB viewer_INSTALLER_FILES installers/windows/*.nsi)

  source_group("Installer Files" FILES ${viewer_INSTALLER_FILES})

  set_source_files_properties(${viewer_INSTALLER_FILES}
                              PROPERTIES HEADER_FILE_ONLY TRUE)

  list(APPEND viewer_SOURCE_FILES ${viewer_INSTALLER_FILES})
endif (WINDOWS)

if (USE_FMODSTUDIO)
  set(FMODWRAPPER_LIBRARY ${FMODSTUDIO_LIBRARY})
endif (USE_FMODSTUDIO)

list(APPEND viewer_SOURCE_FILES ${viewer_HEADER_FILES})

set_source_files_properties(${viewer_HEADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

add_executable(${VIEWER_BINARY_NAME}
    WIN32
    MACOSX_BUNDLE
    ${viewer_SOURCE_FILES}
    )

if (SDL_FOUND)
  target_compile_definitions(${VIEWER_BINARY_NAME} PRIVATE LL_SDL=1)
endif (SDL_FOUND)

if (USE_SENTRY)
  target_compile_definitions(${VIEWER_BINARY_NAME} PRIVATE ${SENTRY_DEFINE})

  list(APPEND viewer_LIBRARIES
       ${SENTRY_LIBRARIES}
       )
endif ()

# add package files
file(GLOB EVENT_HOST_SCRIPT_GLOB_LIST
     ${CMAKE_CURRENT_SOURCE_DIR}/../viewer_components/*.py)
list(APPEND EVENT_HOST_SCRIPTS ${EVENT_HOST_SCRIPT_GLOB_LIST})

set(PACKAGE ON CACHE BOOL
    "Add a package target that builds an installer package.")

if(USE_PRECOMPILED_HEADERS AND ${CMAKE_VERSION} VERSION_GREATER "3.15.0")
  # precompiled header configuration
  # llviewerprecompiledheaders.cpp generates
  # the .pch file.
  # All sources added to viewer_SOURCE_FILES
  # at this point use it.
  target_precompile_headers(${VIEWER_BINARY_NAME}
    PRIVATE
    [["llviewerprecompiledheaders.h"]]
    )
endif()
    
if (WINDOWS)
    set_target_properties(${VIEWER_BINARY_NAME}
        PROPERTIES
        # *TODO -reenable this once we get server usage sorted out
        LINK_FLAGS "/debug /NODEFAULTLIB:LIBCMT /SUBSYSTEM:WINDOWS /LARGEADDRESSAWARE"
        LINK_FLAGS_DEBUG "/NODEFAULTLIB:\"LIBCMT;LIBCMTD;MSVCRT\" /INCREMENTAL:NO /LARGEADDRESSAWARE"
        LINK_FLAGS_RELEASE "/OPT:REF /LARGEADDRESSAWARE"
        )

    if(GEN_IS_MULTI_CONFIG)
      set(VIEWER_BUILD_DEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}")
    else()
      set(VIEWER_BUILD_DEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/package")
      set_target_properties(${VIEWER_BINARY_NAME} PROPERTIES
       			    RUNTIME_OUTPUT_DIRECTORY "${VIEWER_BUILD_DEST_DIR}")
    endif()

    # If adding a file to viewer_manifest.py in the WindowsManifest.construct() method, be sure to add the dependency
    # here.
    # *NOTE:Mani - This is a crappy hack to have important dependencies for the viewer_manifest copy action
    # be met. I'm looking forward to a source-code split-up project next year that will address this kind of thing.
    # In the meantime, if you have any ideas on how to easily maintain one list, either here or in viewer_manifest.py
    # and have the build deps get tracked *please* tell me about it.
    # nat: https://cmake.org/cmake/help/v3.14/command/file.html
    # "For example, the code
    # file(STRINGS myfile.txt myfile)
    # stores a list in the variable myfile in which each item is a line from the input file."
    # And of course it's straightforward to read a text file in Python.

    set(COPY_INPUT_DEPENDENCIES
      # The following commented dependencies are determined at variably at build time. Can't do this here.
      ${CMAKE_SOURCE_DIR}/../etc/message.xml
      ${CMAKE_SOURCE_DIR}/../scripts/messages/message_template.msg
      ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/libapr-1.dll
      ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/libaprutil-1.dll
      ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/libapriconv-1.dll
      ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/glod.dll
      ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/openjp2.dll
      ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/libhunspell.dll
      ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/epoxy-0.dll
      ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/SLVoice.exe
      ${CMAKE_CURRENT_SOURCE_DIR}/licenses-win32.txt
      ${CMAKE_CURRENT_SOURCE_DIR}/featuretable.txt
      ${viewer_APPSETTINGS_FILES}
      SLPlugin
      media_plugin_example
      )

    if (GEN_IS_MULTI_CONFIG)
      list(APPEND COPY_INPUT_DEPENDENCIES
          ${SHARED_LIB_STAGING_DIR}/Release/libexpat.dll
          ${SHARED_LIB_STAGING_DIR}/RelWithDebInfo/libexpat.dll
          ${SHARED_LIB_STAGING_DIR}/Debug/libexpatd.dll
          )
    elseif (UPPERCASE_CMAKE_BUILD_TYPE MATCHES DEBUG)
      list(APPEND COPY_INPUT_DEPENDENCIES
          ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/libexpatd.dll
          )
    else()
      list(APPEND COPY_INPUT_DEPENDENCIES
          ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/libexpat.dll
          )
    endif()

    if (GEN_IS_MULTI_CONFIG)
      list(APPEND COPY_INPUT_DEPENDENCIES
          ${SHARED_LIB_STAGING_DIR}/Release/xmlrpc-epi.dll
          ${SHARED_LIB_STAGING_DIR}/RelWithDebInfo/xmlrpc-epi.dll
          ${SHARED_LIB_STAGING_DIR}/Debug/xmlrpc-epid.dll
          )
    elseif (UPPERCASE_CMAKE_BUILD_TYPE MATCHES DEBUG)
      list(APPEND COPY_INPUT_DEPENDENCIES
          ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/xmlrpc-epid.dll
          )
    else()
      list(APPEND COPY_INPUT_DEPENDENCIES
          ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/xmlrpc-epi.dll
          )
    endif()
    
    if (ADDRESS_SIZE EQUAL 64)
        list(APPEND COPY_INPUT_DEPENDENCIES
            ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/vivoxsdk_x64.dll
            ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/ortp_x64.dll
            )
    else (ADDRESS_SIZE EQUAL 64)
        list(APPEND COPY_INPUT_DEPENDENCIES
            ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/vivoxsdk.dll
            ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/ortp.dll
            )
    endif (ADDRESS_SIZE EQUAL 64)

    if (USE_CEF)
      list(APPEND COPY_INPUT_DEPENDENCIES
           media_plugin_cef
           )
    endif ()

    if (USE_VLC)
      list(APPEND COPY_INPUT_DEPENDENCIES
           media_plugin_libvlc
           )
    endif ()

    if (USE_FMODSTUDIO)
      if (GEN_IS_MULTI_CONFIG)
        list(APPEND COPY_INPUT_DEPENDENCIES
            ${SHARED_LIB_STAGING_DIR}/Release/fmod.dll
            ${SHARED_LIB_STAGING_DIR}/RelWithDebInfo/fmod.dll
            ${SHARED_LIB_STAGING_DIR}/Debug/fmodL.dll
            )
      elseif (UPPERCASE_CMAKE_BUILD_TYPE MATCHES DEBUG)
        list(APPEND COPY_INPUT_DEPENDENCIES
            ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/fmodL.dll
            )
      else()
        list(APPEND COPY_INPUT_DEPENDENCIES
            ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/fmod.dll
            )
      endif()
    endif (USE_FMODSTUDIO)

    if (USE_OPENAL)
      list(APPEND COPY_INPUT_DEPENDENCIES
           ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/OpenAL32.dll
           ${SHARED_LIB_STAGING_DIR}/${CMAKE_CFG_INTDIR}/alut.dll
          )
    endif ()

    add_custom_command(
      OUTPUT  ${VIEWER_BUILD_DEST_DIR}/copy_touched.bat
      COMMAND ${Python3_EXECUTABLE}
      ARGS
        ${CMAKE_CURRENT_SOURCE_DIR}/viewer_manifest.py
        --actions=copy
        --arch=${ARCH}
        --artwork=${ARTWORK_DIR}
        "--sentry=${SENTRY_DSN}"
        "--fmodstudio=${USE_FMODSTUDIO}"
        "--openal=${USE_OPENAL}"
        "--kdu=${USE_KDU}"
        --build=${CMAKE_CURRENT_BINARY_DIR}
        --buildtype=${CMAKE_BUILD_TYPE}
        "--channel=${VIEWER_CHANNEL}"
        --configuration=${CMAKE_CFG_INTDIR}
        --dest=${VIEWER_BUILD_DEST_DIR}
        --grid=${GRID}
        --source=${CMAKE_CURRENT_SOURCE_DIR}
        --touch=${VIEWER_BUILD_DEST_DIR}/copy_touched.bat
        --versionfile=${CMAKE_CURRENT_BINARY_DIR}/viewer_version.txt
      DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/viewer_manifest.py
        stage_third_party_libs
        ${COPY_INPUT_DEPENDENCIES}
      COMMENT "Performing viewer_manifest copy"
      )

    add_custom_target(copy_w_viewer_manifest ALL DEPENDS ${VIEWER_BUILD_DEST_DIR}/copy_touched.bat)

    add_dependencies(${VIEWER_BINARY_NAME} stage_third_party_libs llcommon copy_w_viewer_manifest)

    if (EXISTS ${CMAKE_SOURCE_DIR}/copy_win_scripts)
      add_dependencies(${VIEWER_BINARY_NAME} copy_win_scripts)
    endif (EXISTS ${CMAKE_SOURCE_DIR}/copy_win_scripts)

    add_dependencies(${VIEWER_BINARY_NAME} SLPlugin)

    # sets the 'working directory' for debugging from visual studio.
    # Condition for version can be moved to requirements once build agents will be updated (see TOOL-3865)
    if (NOT UNATTENDED)
        set_property(
          TARGET ${VIEWER_BINARY_NAME}
          PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
          )
    endif (NOT UNATTENDED)

    if (PACKAGE)
      add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/event_host.tar.bz2
        COMMAND ${Python3_EXECUTABLE}
        ARGS
          ${CMAKE_CURRENT_SOURCE_DIR}/event_host_manifest.py
          ${CMAKE_CURRENT_SOURCE_DIR}/..
          ${CMAKE_CURRENT_BINARY_DIR}
          ${VIEWER_BUILD_DEST_DIR}
        DEPENDS
          lleventhost
          ${EVENT_HOST_SCRIPTS}
          ${CMAKE_CURRENT_SOURCE_DIR}/event_host_manifest.py
        )

      add_custom_command(
        OUTPUT ${VIEWER_BUILD_DEST_DIR}/touched.bat
        COMMAND ${Python3_EXECUTABLE}
        ARGS
          ${CMAKE_CURRENT_SOURCE_DIR}/viewer_manifest.py
          --arch=${ARCH}
          --artwork=${ARTWORK_DIR}
          "--sentry=${SENTRY_DSN}"
          "--fmodstudio=${USE_FMODSTUDIO}"
          "--openal=${USE_OPENAL}"
          "--kdu=${USE_KDU}"
          --build=${CMAKE_CURRENT_BINARY_DIR}
          --buildtype=${CMAKE_BUILD_TYPE}
          "--channel=${VIEWER_CHANNEL}"
          --configuration=${CMAKE_CFG_INTDIR}
          --dest=${VIEWER_BUILD_DEST_DIR}
          --grid=${GRID}
          --source=${CMAKE_CURRENT_SOURCE_DIR}
          --touch=${VIEWER_BUILD_DEST_DIR}/touched.bat
          --versionfile=${CMAKE_CURRENT_BINARY_DIR}/viewer_version.txt
        DEPENDS
            ${VIEWER_BINARY_NAME}
            ${CMAKE_CURRENT_SOURCE_DIR}/viewer_manifest.py
            ${CMAKE_CURRENT_BINARY_DIR}/viewer_version.txt
            ${COPY_INPUT_DEPENDENCIES}
        )

      add_custom_target(llpackage ALL DEPENDS
        ${VIEWER_BUILD_DEST_DIR}/touched.bat
        )
        # temporarily disable packaging of event_host until hg subrepos get
        # sorted out on the parabuild cluster...
        #${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/event_host.tar.bz2)
    endif (PACKAGE)
elseif (DARWIN)
    set_target_properties(${VIEWER_BINARY_NAME}
        PROPERTIES
        LINK_FLAGS_RELEASE "${LINK_FLAGS_RELEASE} -Xlinker -dead_strip -Xlinker -map -Xlinker ${CMAKE_CURRENT_BINARY_DIR}/${VIEWER_BINARY_NAME}.MAP"
        )
else (WINDOWS)
        # Linux
    set_target_properties(${VIEWER_BINARY_NAME}
        PROPERTIES
        LINK_FLAGS_RELEASE "${LINK_FLAGS_RELEASE} -Wl,--Map=${VIEWER_BINARY_NAME}.MAP"
        )
endif (WINDOWS)

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
# variables, PNG_PRELOAD_ARCHIVES and ZLIB_PRELOAD_ARCHIVES, get the archives
# dumped into the target binary and runtime lookup will find the most
# modern version.

target_link_libraries(${VIEWER_BINARY_NAME}
    ${LEGACY_STDIO_LIBS}
    ${PNG_PRELOAD_ARCHIVES}
    ${ZLIB_PRELOAD_ARCHIVES}
    ${URIPARSER_PRELOAD_ARCHIVES}
    ${GOOGLE_PERFTOOLS_LIBRARIES}
    ${LLAUDIO_LIBRARIES}
    ${LLCHARACTER_LIBRARIES}
    ${LLIMAGE_LIBRARIES}
    ${LLINVENTORY_LIBRARIES}
    ${LLMESSAGE_LIBRARIES}
    ${LLPLUGIN_LIBRARIES}
    ${LLPRIMITIVE_LIBRARIES}
    ${LLRENDER_LIBRARIES}
    ${FREETYPE_LIBRARIES}
    ${LLUI_LIBRARIES}
    ${LLFILESYSTEM_LIBRARIES}
    ${LLWINDOW_LIBRARIES}
    ${LLXML_LIBRARIES}
    ${LLMATH_LIBRARIES}
    ${LLCOREHTTP_LIBRARIES}
    ${LLCOMMON_LIBRARIES}
    ${NDOF_LIBRARY}
    ${NVAPI_LIBRARY}
    ${HUNSPELL_LIBRARY}
    ${viewer_LIBRARIES}
    ${BOOST_PROGRAM_OPTIONS_LIBRARY}
    ${BOOST_REGEX_LIBRARY}
    ${BOOST_FIBER_LIBRARY}
    ${BOOST_CONTEXT_LIBRARY}
    ${DBUSGLIB_LIBRARIES}
    ${EPOXY_LIBRARIES}
    ${OPENGL_LIBRARIES}
    ${FMODWRAPPER_LIBRARY} # must come after LLAudio
    ${GLOD_LIBRARIES}
    ${OPENGL_LIBRARIES}
    ${SDL_LIBRARIES}
    ${SMARTHEAP_LIBRARY}
    ${UI_LIBRARIES}
    ${WINDOWS_LIBRARIES}
    ${EXPAT_LIBRARIES}
    ${XMLRPCEPI_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${CRYPTO_LIBRARIES}
    ${LLLOGIN_LIBRARIES}
    ${LLPHYSICS_LIBRARIES}
    ${LLPHYSICSEXTENSIONS_LIBRARIES}
    ${LLAPPEARANCE_LIBRARIES}
    ${MINIZIP_LIBRARIES}
    absl::flat_hash_map
    absl::node_hash_map
    absl::strings
    nlohmann_json::nlohmann_json
    fmt::fmt
    )

set(ARTWORK_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH
    "Path to artwork files.")

if (LINUX)
  set(product Alchemy-${ARCH}-${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_REVISION})

  # These are the generated targets that are copied to package/
  set(COPY_INPUT_DEPENDENCIES
    ${VIEWER_BINARY_NAME}
    SLPlugin
    media_plugin_example
    media_plugin_libvlc
    )

  find_program (OBJCOPY NAMES "objcopy" REQUIRED)

  add_custom_command (TARGET ${VIEWER_BINARY_NAME} POST_BUILD
                      COMMAND ${OBJCOPY}
                      ARGS
                        --only-keep-debug --compress-debug-sections=zlib
                        ${VIEWER_BINARY_NAME} "${VIEWER_BINARY_NAME}.debug"
                      BYPRODUCTS "${VIEWER_BINARY_NAME}.debug"
                      COMMENT "Extracting and compressing debug information")

  add_custom_command (TARGET ${VIEWER_BINARY_NAME} POST_BUILD
                      COMMAND ${OBJCOPY}
                      ARGS
                        --strip-all ${VIEWER_BINARY_NAME}
                      COMMENT "Extracting and compressing debug information")

  add_custom_command (TARGET ${VIEWER_BINARY_NAME} POST_BUILD
                      COMMAND ${OBJCOPY}
                      ARGS
                        --add-gnu-debuglink="${VIEWER_BINARY_NAME}.debug" ${VIEWER_BINARY_NAME}
                      COMMENT "Extracting and compressing debug information")

  if (PACKAGE)
  add_custom_command(
      OUTPUT ${product}.tar.xz
      COMMAND ${Python3_EXECUTABLE}
      ARGS
        ${CMAKE_CURRENT_SOURCE_DIR}/viewer_manifest.py
        --arch=${ARCH}
        --artwork=${ARTWORK_DIR}
        "--sentry=${SENTRY_DSN}"
        "--fmodstudio=${USE_FMODSTUDIO}"
        "--openal=${USE_OPENAL}"
        "--kdu=${USE_KDU}"
        --build=${CMAKE_CURRENT_BINARY_DIR}
        --buildtype=${CMAKE_BUILD_TYPE}
        "--channel=${VIEWER_CHANNEL}"
        --configuration=${CMAKE_CFG_INTDIR}
        --dest=${CMAKE_CURRENT_BINARY_DIR}/packaged
        --grid=${GRID}
        --source=${CMAKE_CURRENT_SOURCE_DIR}
        --touch=${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/.${product}.touched
        --versionfile=${CMAKE_CURRENT_BINARY_DIR}/viewer_version.txt
      DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/viewer_manifest.py
        ${COPY_INPUT_DEPENDENCIES}
      )
  endif (PACKAGE)

  add_custom_command(
    OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/.${product}.copy_touched
    COMMAND ${Python3_EXECUTABLE}
    ARGS
      ${CMAKE_CURRENT_SOURCE_DIR}/viewer_manifest.py
      --actions=copy
      --arch=${ARCH}
      --artwork=${ARTWORK_DIR}
      "--sentry=${SENTRY_DSN}"
      "--fmodstudio=${USE_FMODSTUDIO}"
      "--openal=${USE_OPENAL}"
      "--kdu=${USE_KDU}"
      --build=${CMAKE_CURRENT_BINARY_DIR}
      --buildtype=${CMAKE_BUILD_TYPE}
      "--channel=${VIEWER_CHANNEL}"
      --configuration=${CMAKE_CFG_INTDIR}
      --dest=${CMAKE_CURRENT_BINARY_DIR}/packaged
      --grid=${GRID}
      --source=${CMAKE_CURRENT_SOURCE_DIR}
      --versionfile=${CMAKE_CURRENT_BINARY_DIR}/viewer_version.txt
    DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/viewer_manifest.py
      ${COPY_INPUT_DEPENDENCIES}
    COMMENT "Performing viewer_manifest copy"
    )

  add_custom_target(copy_l_viewer_manifest ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/.${product}.copy_touched)

  if (PACKAGE)
    add_custom_target(llpackage ALL DEPENDS ${product}.tar.xz)
    # Make sure we don't run two instances of viewer_manifest.py at the same time.
    add_dependencies(llpackage copy_l_viewer_manifest)
  endif (PACKAGE)
endif (LINUX)

if (DARWIN)
  # These all get set with PROPERTIES. It's not that the property names are
  # magically known to CMake -- it's that these names are referenced in the
  # Info-SecondLife.plist file in the configure_file() directive below.
  set(product "${VIEWER_CHANNEL}")
  set(MACOSX_EXECUTABLE_NAME "${VIEWER_CHANNEL}")
  set(MACOSX_BUNDLE_INFO_STRING "${VIEWER_CHANNEL}")
  set(MACOSX_BUNDLE_ICON_FILE "alchemy.icns")
  set(MACOSX_BUNDLE_GUI_IDENTIFIER "org.alchemyviewer.viewer")
  set(MACOSX_BUNDLE_LONG_VERSION_STRING "${VIEWER_CHANNEL} ${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_REVISION}")
  set(MACOSX_BUNDLE_BUNDLE_NAME "Alchemy")
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_REVISION}")
  set(MACOSX_BUNDLE_BUNDLE_VERSION "${VIEWER_SHORT_VERSION}${VIEWER_VERSION_REVISION}")
  set(MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2013-2021 Alchemy Development Group")
  set(MACOSX_BUNDLE_NSMAIN_NIB_FILE "Alchemy.nib")
  set(MACOSX_BUNDLE_NSPRINCIPAL_CLASS "LLApplication")

  # https://blog.kitware.com/upcoming-in-cmake-2-8-12-osx-rpath-support/
  set(CMAKE_MACOSX_RPATH 1)
  
  set(VIEWER_APP_BUNDLE "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/${product}.app")
  set(VIEWER_APP_EXE "${VIEWER_APP_BUNDLE}/Contents/MacOS/${product}")
  set(VIEWER_APP_DSYM "${VIEWER_APP_EXE}.dSYM")
  set(VIEWER_APP_XCARCHIVE "${VIEWER_APP_BUNDLE}/../${product}.xcarchive.zip")

  configure_file(
     "${CMAKE_CURRENT_SOURCE_DIR}/Info-Alchemy.plist"
     "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/Info-Alchemy.plist"
    )

  set_target_properties(
    ${VIEWER_BINARY_NAME}
    PROPERTIES
    OUTPUT_NAME "${product}"
    # From Contents/MacOS/Alchemy, look in Contents/Frameworks
    BUILD_WITH_INSTALL_RPATH 1
    INSTALL_RPATH "@loader_path/../Frameworks"
    MACOSX_BUNDLE_INFO_PLIST
    "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/Info-Alchemy.plist"
    )

  # Make sure we can find the 'ibtool' program. If we can NOT find it we
  # skip generation of this project
  find_program(IBTOOL ibtool HINTS "/usr/bin" "${OSX_DEVELOPER_ROOT}/usr/bin")
  if (${IBTOOL} STREQUAL "IBTOOL-NOTFOUND")
    message(SEND_ERROR "ibtool can not be found and is needed to compile the .xib files. It should have been installed with
                      the Apple developer tools. The default system paths were searched in addition to ${OSX_DEVELOPER_ROOT}/usr/bin")
  endif()

  # Compile the .xib files using the 'ibtool' program with the destination being the app package
  add_custom_command (TARGET ${VIEWER_BINARY_NAME} POST_BUILD
                      COMMAND ${IBTOOL} --errors --warnings --notices --target-device mac 
                              --minimum-deployment-target ${CMAKE_OSX_DEPLOYMENT_TARGET}
                              --compile ${VIEWER_APP_BUNDLE}/Contents/Resources/Alchemy.nib
                              ${CMAKE_CURRENT_SOURCE_DIR}/Alchemy.xib
                      COMMENT "Compiling ${CMAKE_CURRENT_SOURCE_DIR}/Alchemy.xib")


  add_custom_command(
    TARGET ${VIEWER_BINARY_NAME} POST_BUILD
    COMMAND ${Python3_EXECUTABLE}
    ARGS
      ${CMAKE_CURRENT_SOURCE_DIR}/viewer_manifest.py
      --actions=copy
      --arch=${ARCH}
      --artwork=${ARTWORK_DIR}
      "--sentry=${SENTRY_DSN}"
      "--fmodstudio=${USE_FMODSTUDIO}"
      "--openal=${USE_OPENAL}"
      "--kdu=${USE_KDU}"
      --build=${CMAKE_CURRENT_BINARY_DIR}
      --buildtype=${CMAKE_BUILD_TYPE}
      --bundleid=${MACOSX_BUNDLE_GUI_IDENTIFIER}
      "--channel=${VIEWER_CHANNEL}"
      --configuration=${CMAKE_CFG_INTDIR}
      --dest=${VIEWER_APP_BUNDLE}
      --grid=${GRID}
      --source=${CMAKE_CURRENT_SOURCE_DIR}
      --versionfile=${CMAKE_CURRENT_BINARY_DIR}/viewer_version.txt
    DEPENDS
      ${VIEWER_BINARY_NAME}
      ${CMAKE_CURRENT_SOURCE_DIR}/viewer_manifest.py
    )

  add_dependencies(${VIEWER_BINARY_NAME} SLPlugin)

  if (USE_CEF)
      add_dependencies(${VIEWER_BINARY_NAME} media_plugin_cef)
  endif ()

  if (USE_VLC)
      add_dependencies(${VIEWER_BINARY_NAME} media_plugin_libvlc)
  endif ()

  if (ENABLE_SIGNING)
      set(SIGNING_SETTING "--signature=${SIGNING_IDENTITY}")
  else (ENABLE_SIGNING)
      set(SIGNING_SETTING "")
  endif (ENABLE_SIGNING)

  if (PACKAGE)
      add_custom_target(llpackage ALL DEPENDS ${VIEWER_BINARY_NAME})

      add_custom_command(
        TARGET llpackage POST_BUILD
        COMMAND ${Python3_EXECUTABLE}
        ARGS
          ${CMAKE_CURRENT_SOURCE_DIR}/viewer_manifest.py
          --arch=${ARCH}
          --artwork=${ARTWORK_DIR}
          "--sentry=${SENTRY_DSN}"
          "--fmodstudio=${USE_FMODSTUDIO}"
          "--openal=${USE_OPENAL}"
          "--kdu=${USE_KDU}"
          --build=${CMAKE_CURRENT_BINARY_DIR}
          --buildtype=${CMAKE_BUILD_TYPE}
          "--channel=${VIEWER_CHANNEL}"
          --configuration=${CMAKE_CFG_INTDIR}
          --dest=${VIEWER_APP_BUNDLE}
          --grid=${GRID}
          --source=${CMAKE_CURRENT_SOURCE_DIR}
          --touch=${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/.${product}.touched
          --versionfile=${CMAKE_CURRENT_BINARY_DIR}/viewer_version.txt
          ${SIGNING_SETTING}
        DEPENDS
          ${CMAKE_CURRENT_SOURCE_DIR}/viewer_manifest.py
      )
  endif (PACKAGE)
endif (DARWIN)

if (INSTALL)
  include(${CMAKE_CURRENT_SOURCE_DIR}/ViewerInstall.cmake)
endif (INSTALL)

# Note that the conventional VIEWER_SYMBOL_FILE is set by ../../build.sh
if (PACKAGE AND (RELEASE_CRASH_REPORTING OR NON_RELEASE_CRASH_REPORTING) AND VIEWER_SYMBOL_FILE)
  if (NOT USE_BUGSPLAT)
    # Breakpad symbol-file generation
    set(SYMBOL_SEARCH_DIRS "")
    if (WINDOWS)
      list(APPEND SYMBOL_SEARCH_DIRS "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}")
      # slplugin.exe failing symbols dump - need to debug, might have to do with updated version of google breakpad
      # set(VIEWER_EXE_GLOBS "${VIEWER_BINARY_NAME}${CMAKE_EXECUTABLE_SUFFIX} slplugin.exe")
      set(VIEWER_EXE_GLOBS "${VIEWER_BINARY_NAME}${CMAKE_EXECUTABLE_SUFFIX}")
      set(VIEWER_LIB_GLOB "*${CMAKE_SHARED_MODULE_SUFFIX}")
      set(VIEWER_COPY_MANIFEST copy_w_viewer_manifest)
    endif (WINDOWS)
    if (DARWIN)
      list(APPEND SYMBOL_SEARCH_DIRS "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}")
      # *TODO: Generate these search dirs in the cmake files related to each binary.
      list(APPEND SYMBOL_SEARCH_DIRS "${CMAKE_BINARY_DIR}/llplugin/slplugin/${CMAKE_CFG_INTDIR}")
      list(APPEND SYMBOL_SEARCH_DIRS "${CMAKE_BINARY_DIR}/media_plugins/gstreamer010/${CMAKE_CFG_INTDIR}")
      set(VIEWER_EXE_GLOBS "'${product}' SLPlugin")
      set(VIEWER_LIB_GLOB "*.dylib")
    endif (DARWIN)
    if (LINUX)
      list(APPEND SYMBOL_SEARCH_DIRS "${CMAKE_CURRENT_BINARY_DIR}/packaged")
      set(VIEWER_EXE_GLOBS "do-not-directly-run-alchemy-bin SLPlugin")
      set(VIEWER_EXE_GLOBS "do-not-directly-run-alchemy-bin")
      set(VIEWER_LIB_GLOB "*${CMAKE_SHARED_MODULE_SUFFIX}*")
      set(VIEWER_COPY_MANIFEST copy_l_viewer_manifest)
    endif (LINUX)

    if(CMAKE_CFG_INTDIR STREQUAL ".")
        set(LLBUILD_CONFIG ${CMAKE_BUILD_TYPE})
    else(CMAKE_CFG_INTDIR STREQUAL ".")
        # set LLBUILD_CONFIG to be a shell variable evaluated at build time
        # reflecting the configuration we are currently building.
        set(LLBUILD_CONFIG ${CMAKE_CFG_INTDIR})
    endif(CMAKE_CFG_INTDIR STREQUAL ".")
    add_custom_command(OUTPUT "${VIEWER_SYMBOL_FILE}"
      COMMAND ${Python3_EXECUTABLE}
      ARGS
        "${CMAKE_CURRENT_SOURCE_DIR}/generate_breakpad_symbols.py"
        "${LLBUILD_CONFIG}"
        "${SYMBOL_SEARCH_DIRS}"
        "${VIEWER_EXE_GLOBS}"
        "${VIEWER_LIB_GLOB}"
        "${AUTOBUILD_INSTALL_DIR}/bin/dump_syms"
        "${VIEWER_SYMBOL_FILE}"
      DEPENDS generate_breakpad_symbols.py
          VERBATIM)

    add_custom_target(generate_symbols DEPENDS "${VIEWER_SYMBOL_FILE}" ${VIEWER_BINARY_NAME} "${VIEWER_COPY_MANIFEST}")
    add_dependencies(generate_symbols ${VIEWER_BINARY_NAME})
    if (WINDOWS OR LINUX)
      add_dependencies(generate_symbols "${VIEWER_COPY_MANIFEST}")
    endif (WINDOWS OR LINUX)

  else (NOT USE_BUGSPLAT)
    # BugSplat symbol-file generation
    if (WINDOWS)
      # Just pack up a tarball containing only the .pdb file for the
      # executable. Because we intend to use cygwin tar, we must render
      # VIEWER_SYMBOL_FILE in cygwin path syntax.
      execute_process(COMMAND "cygpath" "-u" "${VIEWER_SYMBOL_FILE}"
        OUTPUT_VARIABLE VIEWER_SYMBOL_FILE_CYGWIN
        OUTPUT_STRIP_TRAILING_WHITESPACE)
      execute_process(COMMAND "cygpath" "-u" "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}"
        OUTPUT_VARIABLE PARENT_DIRECTORY_CYGWIN
        OUTPUT_STRIP_TRAILING_WHITESPACE)
      add_custom_command(OUTPUT "${VIEWER_SYMBOL_FILE}"
        # Use of 'tar ...j' here assumes VIEWER_SYMBOL_FILE endswith .tar.bz2;
        # testing a string suffix is painful enough in CMake language that
        # we'll continue assuming it until forced to generalize.
        COMMAND "tar"
        ARGS
          "cjf"
          "${VIEWER_SYMBOL_FILE_CYGWIN}"
          "-C"
          "${PARENT_DIRECTORY_CYGWIN}"
          "alchemy-bin.pdb"
        DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/alchemy-bin.pdb"
        COMMENT "Packing viewer PDB into ${VIEWER_SYMBOL_FILE_CYGWIN}"
        )
      add_custom_target(generate_symbols DEPENDS "${VIEWER_SYMBOL_FILE}" ${VIEWER_BINARY_NAME})
      add_dependencies(generate_symbols ${VIEWER_BINARY_NAME})
    endif (WINDOWS)
    if (DARWIN)
      add_custom_command(OUTPUT "${VIEWER_SYMBOL_FILE}"
        # See above comments about "tar ...j"
        COMMAND "tar"
        ARGS
          "cjf"
          "${VIEWER_SYMBOL_FILE}"
          "-C"
          "${VIEWER_APP_DSYM}/.."
          "${product}.dSYM"
        DEPENDS "${VIEWER_APP_DSYM}"
        COMMENT "Packing dSYM into ${VIEWER_SYMBOL_FILE}"
        )
      add_custom_target(dsym_tarball DEPENDS "${VIEWER_SYMBOL_FILE}")
      add_dependencies(dsym_tarball ${VIEWER_BINARY_NAME})
      add_custom_command(OUTPUT "${VIEWER_APP_XCARCHIVE}"
        COMMAND "zip"
        ARGS
          "-r"
          "${VIEWER_APP_XCARCHIVE}"
          "."
        WORKING_DIRECTORY "${VIEWER_APP_DSYM}/.."
        DEPENDS "${VIEWER_APP_DSYM}"
        COMMENT "Generating xcarchive.zip for upload to BugSplat"
        )
      add_custom_target(dsym_xcarchive DEPENDS "${VIEWER_APP_XCARCHIVE}")
      add_dependencies(dsym_xcarchive dsym_generate)
      # Have to create a stamp file, and depend on it, to force CMake to run
      # the cleanup step.
      add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/dsym.stamp"
        COMMAND rm -rf "${VIEWER_APP_DSYM}"
        COMMAND touch "${CMAKE_CURRENT_BINARY_DIR}/dsym.stamp"
        DEPENDS "${VIEWER_SYMBOL_FILE}" "${VIEWER_APP_XCARCHIVE}"
        COMMENT "Cleaning up dSYM"
        )
      add_custom_target(generate_symbols DEPENDS
        "${VIEWER_APP_DSYM}"
        "${VIEWER_SYMBOL_FILE}"
        "${VIEWER_APP_XCARCHIVE}"
        "${CMAKE_CURRENT_BINARY_DIR}/dsym.stamp"
        )
      add_dependencies(generate_symbols dsym_tarball dsym_xcarchive)
    endif (DARWIN)
    if (LINUX)
      # TBD
    endif (LINUX)
  endif (NOT USE_BUGSPLAT)

  # for both Bugsplat and Breakpad
  add_dependencies(llpackage generate_symbols)
endif ()

if (LL_TESTS)
  # To add a viewer unit test, just add the test .cpp file below
  # This creates a separate test project per file listed.
  include(LLAddBuildTest)
  SET(viewer_TEST_SOURCE_FILES
    llagentaccess.cpp
    lldateutil.cpp
#    llmediadataclient.cpp
    lllogininstance.cpp
#    llremoteparcelrequest.cpp
    llviewerhelputil.cpp
    llversioninfo.cpp
    llworldmap.cpp
    llworldmipmap.cpp
  )

  set_source_files_properties(
    llworldmap.cpp
    llworldmipmap.cpp
    PROPERTIES
    LL_TEST_ADDITIONAL_SOURCE_FILES 
    tests/llviewertexture_stub.cpp
    #llviewertexturelist.cpp
  )

  set(test_libs
    ${LLCOMMON_LIBRARIES}
    ${CURL_LIBRARIES}
    ${NGHTTP2_LIBRARIES}
    )

  set_source_files_properties(
    llmediadataclient.cpp
    PROPERTIES
    LL_TEST_ADDITIONAL_LIBRARIES "${test_libs}"
  )

  set_source_files_properties(
    llviewerhelputil.cpp
    PROPERTIES
    LL_TEST_ADDITIONAL_LIBRARIES "${BOOST_SYSTEM_LIBRARY}"
  )

  set_source_files_properties(
    llremoteparcelrequest.cpp
    PROPERTIES
    LL_TEST_ADDITIONAL_LIBRARIES "${BOOST_SYSTEM_LIBRARY}"
  )

  set_source_files_properties(
    llworldmap.cpp
    llworldmipmap.cpp
    PROPERTIES
    LL_TEST_ADDITIONAL_SOURCE_FILES 
    tests/llviewertexture_stub.cpp
    #llviewertexturelist.cpp
    LL_TEST_ADDITIONAL_LIBRARIES "${BOOST_SYSTEM_LIBRARY}"
  )

  set_source_files_properties(
    llmediadataclient.cpp
    PROPERTIES
    LL_TEST_ADDITIONAL_LIBRARIES "${LLPRIMITIVE_LIBRARIES}"
  )

  set_source_files_properties(
    llagentaccess.cpp
    PROPERTIES
    LL_TEST_ADDITIONAL_LIBRARIES "${BOOST_SYSTEM_LIBRARY}"
  )

  set_source_files_properties(
    lllogininstance.cpp
    PROPERTIES
    LL_TEST_ADDITIONAL_SOURCE_FILES llversioninfo.cpp
    LL_TEST_ADDITIONAL_LIBRARIES "${BOOST_SYSTEM_LIBRARY}"
  )

  ##################################################
  # DISABLING PRECOMPILED HEADERS USAGE FOR TESTS
  ##################################################
  # if(USE_PRECOMPILED_HEADERS)
  #     set_source_files_properties(
  #       ${viewer_TEST_SOURCE_FILES}
  #       PROPERTIES
  #         LL_TEST_ADDITIONAL_SOURCE_FILES llviewerprecompiledheaders.cpp
  #       )
  # endif(USE_PRECOMPILED_HEADERS)
  LL_ADD_PROJECT_UNIT_TESTS(${VIEWER_BINARY_NAME} "${viewer_TEST_SOURCE_FILES}")

  #set(TEST_DEBUG on)
  ##################################################
  # DISABLING PRECOMPILED HEADERS USAGE FOR TESTS
  ##################################################
  # if(USE_PRECOMPILED_HEADERS)
  #     set(test_sources "${test_sources}" llviewerprecompiledheaders.cpp)
  # endif(USE_PRECOMPILED_HEADERS)
  set(test_libs
    ${LLMESSAGE_LIBRARIES}
    ${WINDOWS_LIBRARIES}
    ${LLFILESYSTEM_LIBRARIES}
    ${LLMATH_LIBRARIES}
    ${LLCOMMON_LIBRARIES}
    )

  if (LINUX)
    # llcommon uses `clock_gettime' which is provided by librt on linux.
    set(LIBRT_LIBRARY
      rt
      )
  endif (LINUX)

  set(test_libs
    ${WINDOWS_LIBRARIES}
    ${LLFILESYSTEM_LIBRARIES}
    ${LLMATH_LIBRARIES}
    ${LLCOMMON_LIBRARIES}
    ${LLMESSAGE_LIBRARIES}
    ${LLCOREHTTP_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${CRYPTO_LIBRARIES}
    ${LIBRT_LIBRARY}
    ${BOOST_FIBER_LIBRARY}
    ${BOOST_CONTEXT_LIBRARY}
  )

  LL_ADD_INTEGRATION_TEST(llagentaccess 
    llagentaccess.cpp 
    "${test_libs}"
    )

  LL_ADD_INTEGRATION_TEST(cppfeatures
    ""
    "${test_libs}"
    )
  LL_ADD_INTEGRATION_TEST(llsechandler_basic
    llsechandler_basic.cpp
    "${test_libs}"
    )

  LL_ADD_INTEGRATION_TEST(llsecapi
     llsecapi.cpp
    "${test_libs}"
    )

  set(llslurl_test_sources
      llslurl.cpp
      llviewernetwork.cpp
      )

  LL_ADD_INTEGRATION_TEST(llslurl
    "${llslurl_test_sources}"
    "${test_libs}"
    )

  LL_ADD_INTEGRATION_TEST(llviewernetwork
     llviewernetwork.cpp
    "${test_libs}"
    )

  LL_ADD_INTEGRATION_TEST(llviewerassetstats
    llviewerassetstats.cpp
    "${test_libs}"
    )

  LL_ADD_INTEGRATION_TEST(llhttpretrypolicy 
    llhttpretrypolicy.cpp 
    "${test_libs}"
    )

  #ADD_VIEWER_BUILD_TEST(llmemoryview viewer)
  #ADD_VIEWER_BUILD_TEST(lltextureinfo viewer)
  #ADD_VIEWER_BUILD_TEST(lltextureinfodetails viewer)

include(LLAddBuildTest)
SET(viewer_TEST_SOURCE_FILES
  llagentaccess.cpp
  )
set_source_files_properties(
  ${viewer_TEST_SOURCE_FILES}
  PROPERTIES
    LL_TEST_ADDITIONAL_SOURCE_FILES llviewerprecompiledheaders.cpp
  )

endif (LL_TESTS)

check_message_template(${VIEWER_BINARY_NAME})
