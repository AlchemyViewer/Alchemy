# -*- cmake -*-

include(00-Common)

project(cmake)

set(cmake_SOURCE_FILES
        CMakeLists.txt
        00-Common.cmake
        APR.cmake
        Audio.cmake
        Boost.cmake
        BuildPackagesInfo.cmake
        BuildVersion.cmake
        CEFPlugin.cmake
        CMakeCopyIfDifferent.cmake
        CURL.cmake
        Copy3rdPartyLibs.cmake
        DBusGlib.cmake
        DeploySharedLibs.cmake
        DiscordSDK.cmake
        DragDrop.cmake
        EXPAT.cmake
        FindAutobuild.cmake
        FMODSTUDIO.cmake
        FreeType.cmake
        GLEXT.cmake
        Hunspell.cmake
        LibXML2.cmake
        LLAddBuildTest.cmake
        LLAppearance.cmake
        LLAudio.cmake
        LLCommon.cmake
        LLImage.cmake
        LLKDU.cmake
        LLPhysicsExtensions.cmake
        LLPrimitive.cmake
        LLSharedLibs.cmake
        LLTestCommand.cmake
        LLWindow.cmake
        Linking.cmake
        Meshoptimizer.cmake
        NDOF.cmake
        OPENAL.cmake
        OpenGL.cmake
        OpenJPEG.cmake
        OpenSSL.cmake
        PNG.cmake
        PluginAPI.cmake
        Prebuilt.cmake
        PulseAudio.cmake
	Python.cmake
        SDL2.cmake
        Sentry.cmake
        TemplateCheck.cmake
        Tut.cmake
        UI.cmake
        UnixInstall.cmake
        URIPARSER.cmake
        WebP.cmake
        Variables.cmake
        ViewerMiscLibs.cmake
        VisualLeakDetector.cmake
        LibVLCPlugin.cmake
        XmlRpcEpi.cmake
        XXHash.cmake
        ZLIBNG.cmake
        )

source_group("Shared Rules" FILES ${cmake_SOURCE_FILES})

set(master_SOURCE_FILES
        ../CMakeLists.txt
        )

source_group("Master Rules" FILES ${master_SOURCE_FILES})

add_library(cmake
        cmake_dummy.cpp
        ${cmake_SOURCE_FILES}
        ${master_SOURCE_FILES}
        )
