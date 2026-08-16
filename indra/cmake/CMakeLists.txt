# -*- cmake -*-

set(cmake_SOURCE_FILES
        00-Common.cmake
        APR.cmake
        AVIF.cmake
        Benchmark.cmake
        Boost.cmake
        BootstrapVcpkg.cmake
        bugsplat.cmake
        BuildPackagesInfo.cmake
        BuildVersion.cmake
        CEFPlugin.cmake
        CMakeCopyIfDifferent.cmake
        Collada-Dom.cmake
        Copy3rdPartyLibs.cmake
        CURL.cmake
        DBUS.cmake
        Discord.cmake
        EXPAT.cmake
        FAudio.cmake
        FindPipeWire.cmake
        FMODSTUDIO.cmake
        FontConfig.cmake
        FreeType.cmake
        GLEXT.cmake
        GTest.cmake
        GLIB.cmake
        GLM.cmake
        GStreamer10Plugin.cmake
        Havok.cmake
        Hunspell.cmake
        JPEG.cmake
        LibVLCPlugin.cmake
        Linking.cmake
        LLAddBuildTest.cmake
        LLKDU.cmake
        LLPhysicsExtensions.cmake
        LLTestCommand.cmake
        Mesa.cmake
        Meshoptimizer.cmake
        Mikktspace.cmake
        NDOF.cmake
        NVAPI.cmake
        OpenAL.cmake
        OpenGL.cmake
        OpenJPEG.cmake
        OpenSSL.cmake
        OpenXR.cmake
        PluginAPI.cmake
        PNG.cmake
        Python.cmake
        run_build_test.py
        SDL3.cmake
        SSE2NEON.cmake
        TemplateCheck.cmake
        TinyEXR.cmake
        TinyGLTF.cmake
        Tracy.cmake
        Tut.cmake
        UI.cmake
        UnixInstall.cmake
        Variables.cmake
        Velopack.cmake
        VHACD.cmake
        Vorbis.cmake
        WebRTC.cmake
        websocketpp.cmake
        xxHash.cmake
        ZLIBNG.cmake
        )

source_group("Shared Rules" FILES ${cmake_SOURCE_FILES})

source_group("CMake Rules" FILES CMakeLists.txt)

set(master_SOURCE_FILES
        ${CMAKE_SOURCE_DIR}/CMakeLists.txt
        )

source_group("Master Rules" FILES ${master_SOURCE_FILES})

add_library(cmake
        cmake_dummy.cpp
        ${cmake_SOURCE_FILES}
        ${master_SOURCE_FILES}
        )
