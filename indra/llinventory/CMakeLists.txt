# -*- cmake -*-

add_library(llinventory STATIC)

target_sources(
  llinventory
  PRIVATE
    llcategory.cpp
    llfoldertype.cpp
    llinventory.cpp
    llinventorydefines.cpp
    llinventorysettings.cpp
    llinventorytype.cpp
    lllandmark.cpp
    llnotecard.cpp
    llparcel.cpp
    llpermissions.cpp
    llsaleinfo.cpp
    llsettingsbase.cpp
    llsettingsdaycycle.cpp
    llsettingssky.cpp
    llsettingswater.cpp
    lltransactionflags.cpp
    lluserrelations.cpp
)

target_sources(
  llinventory
  PUBLIC
    FILE_SET HEADERS
      FILES
        llcategory.h
        llfoldertype.h
        llinventory.h
        llinventorydefines.h
        llinventorysettings.h
        llinventorytype.h
        llinvtranslationbrdg.h
        lllandmark.h
        llnotecard.h
        llparcel.h
        llparcelflags.h
        llpermissions.h
        llpermissionsflags.h
        llsaleinfo.h
        llsettingsbase.h
        llsettingsdaycycle.h
        llsettingssky.h
        llsettingswater.h
        lltransactionflags.h
        lltransactiontypes.h
        lluserrelations.h
)

target_link_libraries(llinventory PRIVATE al::flags PUBLIC llcommon llmath llmessage llxml)
target_precompile_headers(llinventory REUSE_FROM llprecompiled)

# add unit tests
if(AL_BUILD_TESTS)
  foreach(
    test
    inventorymisc
    llparcel
    llpermissions
    llsaleinfo
    lluserrelations
  )
    al_add_test(${test} PROJECT llinventory LIBRARIES llinventory llmath llcorehttp llfilesystem)
  endforeach()
endif()
