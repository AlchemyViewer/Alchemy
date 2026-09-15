# -*- cmake -*-

add_library(llprimitive STATIC)

target_sources(
  llprimitive
  PRIVATE
    lldaeloader.cpp
    llgltfmaterial.cpp
    llmaterialid.cpp
    llmaterial.cpp
    llmaterialtable.cpp
    llmediaentry.cpp
    llmodel.cpp
    llmodelloader.cpp
    llprimitive.cpp
    llprimtexturelist.cpp
    lltextureanim.cpp
    lltextureentry.cpp
    lltreeparams.cpp
    llvolumemessage.cpp
    material_codes.cpp
  PUBLIC
    FILE_SET HEADERS
      FILES
        lldaeloader.h
        llgltfmaterial.h
        llgltfmaterial_templates.h
        legacy_object_types.h
        llmaterial.h
        llmaterialid.h
        llmaterialtable.h
        llmediaentry.h
        llmodel.h
        llmodelloader.h
        llprimitive.h
        llprimtexturelist.h
        lllslconstants.h
        lltextureanim.h
        lltextureentry.h
        lltreeparams.h
        lltree_common.h
        llvolumemessage.h
        material_codes.h
        object_flags.h
)

target_link_libraries(
  llprimitive
  PRIVATE al::flags
  PUBLIC llcharacter llrender llmessage llcorehttp llxml llcommon llmath ll::colladadom ll::tinygltf
)

target_link_libraries(llprimitive PUBLIC llphysicsextensionsos)

target_precompile_headers(llprimitive REUSE_FROM llprecompiled)

# add unit tests
if(AL_BUILD_TESTS)
  foreach(test llmediaentry llgltfmaterial llmaterialid llmaterial)
    al_add_test(${test} UNIT PROJECT llprimitive)
  endforeach()
  al_add_test(llprimitive UNIT PROJECT llprimitive LIBRARIES llmessage)
endif()
