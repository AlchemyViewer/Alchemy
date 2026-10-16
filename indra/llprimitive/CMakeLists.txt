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
  # llgltfmaterial_test.cpp is the one file that defines tinygltf's and stb's
  # implementations, which the library leaves to the viewer.
  al_add_test_binary(
    llprimitive_tests
    PROJECT llprimitive
    SOURCES
      tests/llgltfmaterial_test.cpp
      tests/llmaterial_test.cpp
      tests/llmaterialid_test.cpp
      tests/llmediaentry_test.cpp
    LIBRARIES llprimitive
  )
  # This one stubs LLMaterialID and LLPrimTextureList, which the library
  # defines, so it compiles the one file it tests instead of linking it.
  al_add_test_binary(
    llprimitive_test
    PROJECT llprimitive
    SOURCES tests/llprimitive_test.cpp
    UNIT llprimitive.cpp
    LIBRARIES llmessage llmath llcommon
  )
endif()
