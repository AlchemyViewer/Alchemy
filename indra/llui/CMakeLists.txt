# -*- cmake -*-

add_library(llui STATIC)

# Linden Lab's widgets are at the top, as upstream has them. Alchemy's own
# are in folders, each a part of the library and each on the include path,
# so that a header is named alone wherever it is used:
#   base/     what every part leans on: a view's type, words in the skin's
#             language, a surface's colours, matching, placing, dragging;
#   text/     the text view and what it is made of: the document, its undo,
#             editing, layout, search, the ruler and the find bar;
#   code/     the code editor over it: cards, completion, fixes, folds,
#             snippets, the grammar and the highlighter, the keymap;
#   diff/     two texts compared: the diff, the view and its bar;
#   vim/      vim over a text view;
#   xui/      XUI Studio's model of a skin's files;
#   fields/   the property grid and the fields it edits values with;
#   studio/   a studio's window: its panes, folds, docks and quick open;
#   widgets/  small widgets of ours the rest are built from.

target_sources(
  llui
  PRIVATE
    fields/alangledial.cpp
    base/aldraggesture.cpp
    fields/alcornerfield.cpp
    fields/aloffsetpad.cpp
    fields/alpicturefield.cpp
    studio/alpanefolds.cpp
    fsvirtualtrackpad.cpp
    llaccordionctrl.cpp
    llaccordionctrltab.cpp
    llbadge.cpp
    llbadgeholder.cpp
    llbadgeowner.cpp
    llbutton.cpp
    llchatautocompletemodel.cpp
    llchatentry.cpp
    llchatmentionhelper.cpp
    llcheckboxctrl.cpp
    llclipboard.cpp
    llcombobox.cpp
    llcommandmanager.cpp
    llconsole.cpp
    llcontainerview.cpp
    llctrlselectioninterface.cpp
    lldockablefloater.cpp
    lldockcontrol.cpp
    lldraghandle.cpp
    lleditmenuhandler.cpp
    llemojidictionary.cpp
    llemojihelper.cpp
    llf32uictrl.cpp
    llfiltereditor.cpp
    llflashtimer.cpp
    llflatlistview.cpp
    llfloater.cpp
    llfloaterreg.cpp
    llfloaterreglistener.cpp
    llflyoutbutton.cpp
    llfocusmgr.cpp
    llgestureautocompletehelper.cpp
    llfolderview.cpp
    llfolderviewitem.cpp
    llfolderviewmodel.cpp
    lliconctrl.cpp
    lllayoutstack.cpp
    lllineeditor.cpp
    llloadingindicator.cpp
    lllocalcliprect.cpp
    llmenubutton.cpp
    llmenugl.cpp
    llmodaldialog.cpp
    llmultifloater.cpp
    llmultislider.cpp
    llmultisliderctrl.cpp
    llnotifications.cpp
    llnotificationslistener.cpp
    llnotificationsutil.cpp
    llpanel.cpp
    llprogressbar.cpp
    llradiogroup.cpp
    llresizebar.cpp
    llresizehandle.cpp
    llresmgr.cpp
    llscrollbar.cpp
    llscrollcontainer.cpp
    llscrollingpanellist.cpp
    llscrolllistcell.cpp
    llscrolllistcolumn.cpp
    llscrolllistctrl.cpp
    llscrolllistitem.cpp
    llsearcheditor.cpp
    llslider.cpp
    llsliderctrl.cpp
    llspinctrl.cpp
    llstatbar.cpp
    llstatgraph.cpp
    llstatview.cpp
    llstyle.cpp
    lltabcontainer.cpp
    lltextbase.cpp
    lltextbox.cpp
    lltexteditor.cpp
    lltextparser.cpp
    lltextutil.cpp
    lltextvalidate.cpp
    lltimectrl.cpp
    lltrans.cpp
    lltransutil.cpp
    lltoggleablemenu.cpp
    lltoolbar.cpp
    lltooltip.cpp
    llui.cpp
    lluicolor.cpp
    lluicolortable.cpp
    lluictrl.cpp
    lluictrlfactory.cpp
    lluistring.cpp
    llundo.cpp
    llurlaction.cpp
    llurlentry.cpp
    llurlmatch.cpp
    llurlregistry.cpp
    llviewborder.cpp
    llviewinject.cpp
    llviewmodel.cpp
    llview.cpp
    llviewquery.cpp
    llvirtualtrackball.cpp
    llwindowshade.cpp
    llxuiparser.cpp
    xui/alcanvasview.cpp
    fields/alcolorfield.cpp
    fields/alcolortablepanel.cpp
    fields/alcolorsheet.cpp
    base/aldeferredrebuild.cpp
    diff/aldiffbar.cpp
    diff/aldiffview.cpp
    studio/aldockpanel.cpp
    studio/alemptystate.cpp
    fields/alflagsfield.cpp
    studio/alhistorylist.cpp
    fields/alimagefield.cpp
    widgets/aljumpbar.cpp
    studio/alpanelist.cpp
    base/alplace.cpp
    widgets/alpopover.cpp
    studio/alquickask.cpp
    studio/alquickopen.cpp
    widgets/alscopebar.cpp
    fields/alsettingrow.cpp
    widgets/alspecimenlist.cpp
    studio/alstudiofloater.cpp
    base/alsurface.cpp
    code/alcodecards.cpp
    code/alcodeeditor.cpp
    code/albracketindex.cpp
    code/alcompletionmodel.cpp
    base/alfuzzymatch.cpp
    code/alfixlistmodel.cpp
    code/alfoldmodel.cpp
    text/alsmartselect.cpp
    code/alsnippetsession.cpp
    text/alfindbar.cpp
    widgets/alflatbutton.cpp
    studio/alkeycapture.cpp
    code/alkeychord.cpp
    code/alkeymap.cpp
    studio/almenuslot.cpp
    vim/alvimcommandline.cpp
    vim/alvimexcommands.cpp
    vim/alvimkeymap.cpp
    vim/alvimmappings.cpp
    vim/alvimpattern.cpp
    vim/alvimregisters.cpp
    vim/alvimsearch.cpp
    xui/alxuiservice.cpp
    widgets/alchoicelist.cpp
    widgets/alchoicepopup.cpp
    studio/aloutputview.cpp
    code/alsyntaxgrammar.cpp
    code/alsyntaxhighlighter.cpp
    widgets/altabstrip.cpp
    text/altextcarets.cpp
    diff/altextdiff.cpp
    text/altextdocument.cpp
    text/altextediting.cpp
    text/altextfind.cpp
    text/altextgotoline.cpp
    text/altextindent.cpp
    text/altextlayout.cpp
    text/altextruler.cpp
    text/altextsearch.cpp
    text/altextspelling.cpp
    text/altextview.cpp
    text/altextundo.cpp
    text/alnotecarditems.cpp
    fields/alfieldeditors.cpp
    studio/alfolderfilter.cpp
    fields/alfollowscontrol.cpp
    fields/alfontfield.cpp
    fields/alcolorpicker.cpp
    fields/alpropertygrid.cpp
    xui/alxuicatalog.cpp
    xui/alxuidiagnostics.cpp
    xui/alxuiedit.cpp
    xui/alxuifields.cpp
    xui/alxuidocuments.cpp
    xui/alxuilint.cpp
    xui/alxuinotes.cpp
    xui/alxuioverlay.cpp
    xui/alxuischema.cpp
    xui/alxuiselection.cpp
    xui/alxuishellbuild.cpp
    xui/alxuisourcemap.cpp
    xui/alxuitranslate.cpp
    xui/alxuitreemodel.cpp
    llxyvector.cpp
  PUBLIC
    FILE_SET HEADERS
      FILES
        fields/alangledial.h
        base/aldraggesture.h
        fields/alcornerfield.h
        fields/aloffsetpad.h
        fields/alpicturefield.h
        studio/alpanefolds.h
        text/altextcaret.h
        base/alviewtype.h
        fsvirtualtrackpad.h
        llaccordionctrl.h
        llaccordionctrltab.h
        llbadge.h
        llbadgeholder.h
        llbadgeowner.h
        llbutton.h
        llcallbackmap.h
        llchatautocompletemodel.h
        llchatentry.h
        llchat.h
        llchatmentionhelper.h
        llcheckboxctrl.h
        llclipboard.h
        llcombobox.h
        llcommandmanager.h
        llconsole.h
        llcontainerview.h
        llctrlselectioninterface.h
        lldraghandle.h
        lldockablefloater.h
        lldockcontrol.h
        lleditmenuhandler.h
        llemojidictionary.h
        llemojihelper.h
        llf32uictrl.h
        llfiltereditor.h
        llflashtimer.h
        llflatlistview.h
        llfloater.h
        llfloaterreg.h
        llfloaterreglistener.h
        llflyoutbutton.h
        llfocusmgr.h
        llgestureautocompletehelper.h
        llfolderview.h
        llfolderviewitem.h
        llfolderviewmodel.h
        llfunctorregistry.h
        llhelp.h
        lliconctrl.h
        lllayoutstack.h
        lllazyvalue.h
        lllineeditor.h
        llloadingindicator.h
        lllocalcliprect.h
        llmenubutton.h
        llmenugl.h
        llmodaldialog.h
        llmultifloater.h
        llmultisliderctrl.h
        llmultislider.h
        llnotificationptr.h
        llnotifications.h
        llnotificationslistener.h
        llnotificationsutil.h
        llnotificationtemplate.h
        llnotificationvisibilityrule.h
        llpanel.h
        llprogressbar.h
        llradiogroup.h
        llresizebar.h
        llresizehandle.h
        llresmgr.h
        llsearchablecontrol.h
        llsearcheditor.h
        llscrollbar.h
        llscrollcontainer.h
        llscrollingpanellist.h
        llscrolllistcell.h
        llscrolllistcolumn.h
        llscrolllistctrl.h
        llscrolllistitem.h
        llsliderctrl.h
        llslider.h
        llspellcheck.h
        llspellcheckengine.h
        llspellcheckmenuhandler.h
        llspinctrl.h
        llstatbar.h
        llstatgraph.h
        llstatview.h
        llstyle.h
        lltabcontainer.h
        lltextbase.h
        lltextbox.h
        lltexteditor.h
        lltextparser.h
        lltextutil.h
        lltextvalidate.h
        lltimectrl.h
        lltoggleablemenu.h
        lltoolbar.h
        lltooltip.h
        lltrans.h
        lltransutil.h
        lluicolortable.h
        lluiconstants.h
        lluictrlfactory.h
        lluictrl.h
        lluifwd.h
        llui.h
        lluicolor.h
        lluistring.h
        llundo.h
        llurlaction.h
        llurlentry.h
        llurlmatch.h
        llurlregistry.h
        llviewborder.h
        llviewinject.h
        llviewmodel.h
        llview.h
        llviewquery.h
        llvirtualtrackball.h
        llwindowshade.h
        llxuiparser.h
        xui/alcanvasview.h
        fields/alcolorfield.h
        fields/alcolortablepanel.h
        fields/alcolorsheet.h
        base/aldeferredrebuild.h
        diff/aldiffbar.h
        diff/aldiffview.h
        studio/aldockpanel.h
        studio/alemptystate.h
        fields/alflagsfield.h
        studio/alhistorylist.h
        studio/alkeyshome.h
        fields/alimagefield.h
        widgets/aljumpbar.h
        studio/alpanelist.h
        base/alplace.h
        widgets/alpopover.h
        studio/alquickask.h
        studio/alquickopen.h
        base/alsaid.h
        text/altextchars.h
        widgets/alscopebar.h
        fields/alsettingrow.h
        widgets/alspecimenlist.h
        studio/alstudiofloater.h
        base/alsurface.h
        base/alundostack.h
        base/alstringmatch.h
        code/alcodecards.h
        code/alcodeeditor.h
        code/albracketindex.h
        code/alcompletionmodel.h
        base/alfuzzymatch.h
        code/alfixlistmodel.h
        code/alfoldmodel.h
        text/alsmartselect.h
        code/alsnippetsession.h
        studio/alfindings.h
        text/alanchoredranges.h
        text/alfenwicktree.h
        text/alfindbar.h
        widgets/alflatbutton.h
        studio/alkeycapture.h
        code/alkeychord.h
        code/alkeymap.h
        text/allinetable.h
        studio/almenuslot.h
        vim/alvimcommandline.h
        vim/alvimexcommands.h
        vim/alvimhost.h
        vim/alvimkeymap.h
        vim/alvimmappings.h
        vim/alvimpattern.h
        vim/alvimregisters.h
        vim/alvimsearch.h
        vim/alvimtext.h
        xui/alxuiservice.h
        widgets/alchoicelist.h
        widgets/alchoicepopup.h
        studio/aloutputview.h
        code/alsyntaxgrammar.h
        code/alsyntaxhighlighter.h
        widgets/altabstrip.h
        text/altextcarets.h
        diff/altextdiff.h
        text/altextdocument.h
        text/altextediting.h
        text/altextfeatures.h
        text/altextfind.h
        text/altextgotoline.h
        text/altextindent.h
        text/altextlayout.h
        text/altextruler.h
        text/altextsearch.h
        text/altextspelling.h
        text/altextview.h
        text/altextundo.h
        text/alnotecarditems.h
        fields/alfieldeditors.h
        studio/alfolderfilter.h
        fields/alfollowscontrol.h
        fields/alfontfield.h
        fields/alcolorpicker.h
        fields/alpropertygrid.h
        xui/alxuicatalog.h
        xui/alxuidiagnostics.h
        xui/alxuiedit.h
        xui/alxuifields.h
        xui/alxuidocuments.h
        xui/alxuifindings.h
        xui/alxuilint.h
        xui/alxuinotes.h
        xui/alxuioverlay.h
        xui/alxuischema.h
        xui/alxuiselection.h
        xui/alxuishellbuild.h
        xui/alxuisourcemap.h
        xui/alxuitranslate.h
        xui/alxuitreemodel.h
        llxyvector.h
)

# Spell checking: the backend-agnostic LLSpellChecker core (llspellcheck.cpp) is always compiled;
# exactly one platform engine is selected by build option — native macOS NSSpellChecker
# (Objective-C++), native Windows Spell Checking API, or Hunspell (default).
target_sources(llui PRIVATE llspellcheck.cpp)
if(AL_USE_NSSPELLCHECKER)
  target_sources(llui PRIVATE llspellcheckengine_mac.mm)
  set_source_files_properties(llspellcheckengine_mac.mm PROPERTIES SKIP_PRECOMPILE_HEADERS TRUE)
elseif(AL_USE_WINSPELLCHECK)
  target_sources(llui PRIVATE llspellcheckengine_win32.cpp)
  set_source_files_properties(llspellcheckengine_win32.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS TRUE)
else()
  target_sources(llui PRIVATE llspellcheckengine_hunspell.cpp)
endif()

target_include_directories(
  llui
  PUBLIC base text code diff vim xui fields studio widgets
)

target_link_libraries(
  llui
  PRIVATE al::flags
  PUBLIC
    llrender
    llwindow
    llimage
    llinventory
    llmessage
    llcorehttp
    llfilesystem
    llxml
    llmath
    llcommon
  PRIVATE ll::fastfloat
)

if(AL_USE_NSSPELLCHECKER)
  target_link_libraries(llui PUBLIC ll::nsspellchecker)
elseif(AL_USE_WINSPELLCHECK)
  target_link_libraries(llui PUBLIC ll::winspellcheck)
else()
  target_link_libraries(llui PUBLIC ll::hunspell)
endif()

target_precompile_headers(llui REUSE_FROM llprecompiled)

# The console utilities over the UI: a XUI file built and fingerprinted
# without a viewer, the overlay census that gates the merge, and the
# translation repair. They live beside the library they are about rather
# than under tests, since two of them write to the source tree and none of
# them is a test.
add_subdirectory(utils)

# Add tests
if(AL_BUILD_TESTS)
  if(AL_USE_NSSPELLCHECKER)
    set(spellcheck_lib ll::nsspellchecker)
  elseif(AL_USE_WINSPELLCHECK)
    set(spellcheck_lib ll::winspellcheck)
  else()
    set(spellcheck_lib ll::hunspell)
  endif()

  set(
    test_libs
    llmessage
    llcorehttp
    llxml
    llrender
    llcommon
    ${spellcheck_lib}
  )
  al_add_test(llchatautocompletemodel UNIT PROJECT llui LIBRARIES ${test_libs})
  al_add_test(llurlmatch UNIT PROJECT llui LIBRARIES ${test_libs})
  al_add_test(alkeymap UNIT PROJECT llui DIR code LIBRARIES ${test_libs})
  al_add_test(altextdiff UNIT PROJECT llui DIR diff LIBRARIES ${test_libs})
  al_add_test(alanchoredranges PROJECT llui SOURCES text/altextdocument.cpp LIBRARIES ${test_libs})
  al_add_test(alfenwicktree PROJECT llui LIBRARIES ${test_libs})
  al_add_test(allinetable PROJECT llui LIBRARIES ${test_libs})
  al_add_test(altextdocument UNIT PROJECT llui DIR text LIBRARIES ${test_libs})
  al_add_test(
    altextcarets
    UNIT
    PROJECT llui
    DIR text
    SOURCES text/altextdocument.cpp
    LIBRARIES ${test_libs}
  )
  al_add_test(
    altextundo
    UNIT
    PROJECT llui
    DIR text
    SOURCES text/altextdocument.cpp
    LIBRARIES ${test_libs}
  )
  al_add_test(alnotecarditems UNIT PROJECT llui DIR text LIBRARIES ${test_libs})
  al_add_test(
    alsyntaxhighlighter
    UNIT
    PROJECT llui
    DIR code
    SOURCES code/alsyntaxgrammar.cpp text/altextdocument.cpp
    LIBRARIES ${test_libs}
  )

  set(test_libs llui ${test_libs})
  foreach(
    test
    llstatbar
    lltextparser
    llview
    lluistring
    llxuiparser
    llfocusmgr
    alxuioverlay
    alxuidocuments
    alxuifindings
    alxuitranslate
  )
    al_add_test(${test} PROJECT llui LIBRARIES ${test_libs})
  endforeach()
  al_add_test(llurlentry PROJECT llui SOURCES llurlentry.cpp LIBRARIES ${test_libs})
  al_add_test(llemojidictionary PROJECT llui SOURCES llemojidictionary.cpp LIBRARIES ${test_libs})

  # Tests that build widgets above LLView stand up a UI over the source
  # tree's skins and fonts; this is where it is.
  foreach(
    test
    lllayoutstack
    llresizebar
    llaccordionctrltab
    llspinctrl
    llcombobox
    llradiogroup
    lltabcontainer
    llfloater
    alpropertygrid
    alcolorfield
    alcolortablepanel
    alimagefield
    alcolorpicker
    alfontfield
    alflagsfield
    alxuinotes
    alfollowscontrol
    alcanvasview
    aldockpanel
    alpanefolds
    alangledial
    alcornerfield
    aldraggesture
    aloffsetpad
    alemptystate
    alxuifill
    alviewtype
    alxuidiagnostics
    alxuishellbuild
    alxuicatalog
    alxuilint
    alxuiedit
    alfindbar
    alfolderfilter
    alhistorylist
    alplace
    alpopover
    alkeycapture
    alscopebar
    aljumpbar
    almenuslot
    alpanelist
    alspecimenlist
    altabstrip
    alquickopen
    alstudiofloater
    altextgotoline
    alxuifields
    alxuischema
    alxuiselection
    alxuisourcemap
    alxuitreemodel
    alsettingrow
    alstringmatch
    alsurface
    alcolorsheet
    lluicolortable
    alundostack
    alsyntaxgrammars
    altextediting
    altextfind
    altextindent
    altextlayout
    altextsearch
    altextspelling
    altextview
    alcodecards
    alcodeeditor
    aldiffview
    albracketindex
    alcompletionmodel
    alfuzzymatch
    alfixlistmodel
    alfoldmodel
    alsnippetsession
    alvimkeymap
    alvimmappings
    alvimpattern
    alvimregisters
    alxuiservice
    aloutputview
    alchoicelist
    llurlregistry
  )
    al_add_test(
      ${test}
      PROJECT llui
      LIBRARIES ${test_libs}
      DEFINES "LLUI_TEST_APP_DIR=\"${INDRA_SOURCE_DIR}/newview\""
    )
  endforeach()

  # What the text view draws, read back from llrender's hidden window.
  al_add_test(
    altextview_gl
    GL
    PROJECT llui
    LIBRARIES ${test_libs}
    DEFINES "LLUI_TEST_APP_DIR=\"${INDRA_SOURCE_DIR}/newview\""
  )

  # A surface's ground and frame, and the tab strip's tabs, read back the
  # same way.
  al_add_test(
    alsurface_gl
    GL
    PROJECT llui
    LIBRARIES ${test_libs}
    DEFINES "LLUI_TEST_APP_DIR=\"${INDRA_SOURCE_DIR}/newview\""
  )

  # What a comparison draws over its sides -- the current change's outline,
  # a folded row's words -- read back the same way.
  al_add_test(
    aldiffview_gl
    GL
    PROJECT llui
    LIBRARIES ${test_libs}
    DEFINES "LLUI_TEST_APP_DIR=\"${INDRA_SOURCE_DIR}/newview\""
  )

  # The code editor over 50,000-line scripts: what each change to the edit
  # pipeline, the highlighter, the layout, the folds or find is measured by.
  al_add_bench(
    altextengine
    PROJECT llui
    LIBRARIES ${test_libs}
    INCLUDES ${INDRA_SOURCE_DIR}/test
    DEFINES "LLUI_TEST_APP_DIR=\"${INDRA_SOURCE_DIR}/newview\""
  )
  # The same editor drawn, a frame at a time, into llrender's hidden window:
  # what the drawing steps are measured by.
  al_add_bench(
    altextdraw
    PROJECT llui
    LIBRARIES ${test_libs}
    INCLUDES ${INDRA_SOURCE_DIR}/test
    DEFINES "LLUI_TEST_APP_DIR=\"${INDRA_SOURCE_DIR}/newview\""
  )
endif()
