# -*- cmake -*-

add_library(llmessage STATIC)

target_sources(
  llmessage
  PRIVATE
    alnetimpairment.cpp
    llassetstorage.cpp
    llavatarname.cpp
    llavatarnamecache.cpp
    llbuffer.cpp
    llbufferstream.cpp
    llcachename.cpp
    llchainio.cpp
    llcircuit.cpp
    llclassifiedflags.cpp
    llcoproceduremanager.cpp
    llcorehttputil.cpp
    lldatapacker.cpp
    lldispatcher.cpp
    llexperiencecache.cpp
    llgenericstreamingmessage.cpp
    llhost.cpp
    llhttpnode.cpp
    llhttpsdhandler.cpp
    llinstantmessage.cpp
    lliobuffer.cpp
    lliopipe.cpp
    lliosocket.cpp
    llioutil.cpp
    llmessagebuilder.cpp
    llmessageconfig.cpp
    llmessagelog.cpp
    llmessagereader.cpp
    llmessagetemplate.cpp
    llmessagetemplateparser.cpp
    llmessagethrottle.cpp
    llnamevalue.cpp
    llpacketack.cpp
    llpacketbuffer.cpp
    llpacketring.cpp
    llpartdata.cpp
    llproxy.cpp
    llpumpio.cpp
    llsdappservices.cpp
    llsdhttpserver.cpp
    llsdmessagebuilder.cpp
    llsdmessagereader.cpp
    llstoredmessage.cpp
    lltemplatemessagebuilder.cpp
    lltemplatemessagedispatcher.cpp
    lltemplatemessagereader.cpp
    llthrottle.cpp
    lltransfermanager.cpp
    lltransfersourceasset.cpp
    lltransfersourcefile.cpp
    lltransfertargetfile.cpp
    lltransfertargetvfile.cpp
    lltrustedmessageservice.cpp
    llxfer.cpp
    llxfer_file.cpp
    llxfermanager.cpp
    llxfer_mem.cpp
    llxfer_vfile.cpp
    llxorcipher.cpp
    machine.cpp
    message.cpp
    message_prehash.cpp
    message_string_table.cpp
    net.cpp
    patch_code.cpp
    patch_dct.cpp
    patch_idct.cpp
    sound_ids.cpp
)

target_sources(
  llmessage
  PUBLIC
    FILE_SET HEADERS
      FILES
        alnetimpairment.h
        llassetstorage.h
        llavatarname.h
        llavatarnamecache.h
        llbuffer.h
        llbufferstream.h
        llcachename.h
        llchainio.h
        llcipher.h
        llcircuit.h
        llclassifiedflags.h
        llcoproceduremanager.h
        llcorehttputil.h
        lldatapacker.h
        lldbstrings.h
        lldispatcher.h
        lleventflags.h
        llexperiencecache.h
        llextendedstatus.h
        llfollowcamparams.h
        llgenericstreamingmessage.h
        llhost.h
        llhttpnode.h
        llhttpnodeadapter.h
        llhttpsdhandler.h
        llinstantmessage.h
        llinvite.h
        lliobuffer.h
        lliopipe.h
        lliosocket.h
        llioutil.h
        llloginflags.h
        llmessagebuilder.h
        llmessageconfig.h
        llmessagelog.h
        llmessagereader.h
        llmessagetemplate.h
        llmessagetemplateparser.h
        llmessagethrottle.h
        llmsgvariabletype.h
        llnamevalue.h
        llpacketack.h
        llpacketbuffer.h
        llpacketring.h
        llpartdata.h
        llpumpio.h
        llproxy.h
        llqueryflags.h
        llregionflags.h
        llregionhandle.h
        llsdappservices.h
        llsdhttpserver.h
        llsdmessagebuilder.h
        llsdmessagereader.h
        llstoredmessage.h
        lltaskname.h
        llteleportflags.h
        lltemplatemessagebuilder.h
        lltemplatemessagedispatcher.h
        lltemplatemessagereader.h
        llthrottle.h
        lltransfermanager.h
        lltransfersourceasset.h
        lltransfersourcefile.h
        lltransfertargetfile.h
        lltransfertargetvfile.h
        lltrustedmessageservice.h
        llvehicleparams.h
        llxfer.h
        llxfermanager.h
        llxfer_file.h
        llxfer_mem.h
        llxfer_vfile.h
        llxorcipher.h
        llzerocode.h
        machine.h
        mean_collision_data.h
        message.h
        message_prehash.h
        net.h
        patch_code.h
        patch_dct.h
        sound_ids.h
)

target_link_libraries(llmessage PRIVATE al::flags PUBLIC llcommon llfilesystem llmath llcorehttp)
target_precompile_headers(llmessage REUSE_FROM llprecompiled)

# tests
if(AL_BUILD_TESTS)
  # These three stand in for parts of llmessage and llcorehttp with stubs of
  # their own, so they build their one file and do not link llmessage.
  foreach(test llcoproceduremanager lltrustedmessageservice lltemplatemessagedispatcher)
    al_add_test_binary(
      ${test}_test
      PROJECT llmessage
      SOURCES tests/${test}_test.cpp
      UNIT ${test}.cpp
      LIBRARIES llmath llcorehttp llcommon
    )
  endforeach()

  # The state guard's checks on what llmessage keeps for the process, for
  # every test binary that links it.
  add_library(llmessage_test_checks OBJECT tests/llmessage_test_checks.cpp)
  target_link_libraries(llmessage_test_checks PRIVATE al::flags al_test_support llmessage)
  set_target_properties(llmessage_test_checks PROPERTIES FOLDER "Tests/llmessage")

  # io_test's pipes, which are not tests of their own.
  add_library(llmessage_test_pipes OBJECT tests/llpipeutil.cpp)
  target_link_libraries(llmessage_test_pipes PRIVATE al::flags llmessage)
  set_target_properties(llmessage_test_pipes PROPERTIES FOLDER "Tests/llmessage")

  al_add_test_binary(
    llmessage_tests
    PROJECT llmessage
    SOURCES
      tests/alnetimpairment_test.cpp
      tests/io_test.cpp
      tests/llavatarnamecache_test.cpp
      tests/llbuffer_test.cpp
      tests/lldatapacker_test.cpp
      tests/llhost_test.cpp
      tests/llhttpnode_test.cpp
      tests/llmessageconfig_test.cpp
      tests/llmessagetemplateparser_test.cpp
      tests/llnamevalue_test.cpp
      tests/llpartdata_test.cpp
      tests/llsdmessagebuilder_test.cpp
      tests/llsdmessagereader_test.cpp
      tests/lltemplatemessagebuilder_test.cpp
      tests/llxfer_file_test.cpp
      tests/llxorcipher_test.cpp
      tests/llzerocode_test.cpp
      tests/message_test.cpp
    LIBRARIES llfilesystem llmath llcorehttp llmessage llcommon
    SUPPORT llmessage_test_checks llmessage_test_pipes
  )
endif()
