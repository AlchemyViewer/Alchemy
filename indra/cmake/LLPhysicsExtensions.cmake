# -*- cmake -*-
include(Variables)
include(Linking)
include(Prebuilt)

# There are three possible solutions to provide the llphysicsextensions:
# - The full source package, selected by -DHAVOK:BOOL=ON
# - The stub source package, selected by -DHAVOK:BOOL=OFF 
# - The prebuilt package available to those with sublicenses, selected by -DHAVOK_TPV:BOOL=ON

if (INSTALL_PROPRIETARY AND NOT LINUX)
   set(HAVOK_TPV ON CACHE BOOL "Use Havok physics library" FORCE)
endif ()

include_guard()
add_library( llphysicsextensions_impl INTERFACE IMPORTED )


# Note that the use_prebuilt_binary macros below do not in fact include binaries;
# the llphysicsextensions_* packages are source only and are built here.
# The source package and the stub package both build libraries of the same name.

if (HAVOK)
   include(Havok)
   use_prebuilt_binary(llphysicsextensions_source)
   set(LLPHYSICSEXTENSIONS_SRC_DIR ${LIBS_PREBUILT_DIR}/llphysicsextensions/src)
   target_link_libraries( llphysicsextensions_impl INTERFACE llphysicsextensions)
   target_include_directories( llphysicsextensions_impl INTERFACE   ${LIBS_PREBUILT_DIR}/include/llphysicsextensions)
   target_compile_definitions( llphysicsextensions_impl INTERFACE LL_HAVOK=1)
elseif (HAVOK_TPV)
   use_prebuilt_binary(llphysicsextensions_tpv)
   if(WINDOWS)
      target_link_libraries( llphysicsextensions_impl INTERFACE    ${ARCH_PREBUILT_DIRS}/llphysicsextensions_tpv.lib)
   else()
      target_link_libraries( llphysicsextensions_impl INTERFACE    ${ARCH_PREBUILT_DIRS}/libllphysicsextensions_tpv.a)
   endif()
   target_include_directories( llphysicsextensions_impl INTERFACE   ${LIBS_PREBUILT_DIR}/include/llphysicsextensions)
   target_compile_definitions( llphysicsextensions_impl INTERFACE LL_HAVOK=1)
else (HAVOK)
  if (NOT USE_LL_STUBS)
     use_prebuilt_binary( ndPhysicsStub )
     if (WINDOWS)
        target_link_libraries( llphysicsextensions_impl INTERFACE
           debug ${ARCH_PREBUILT_DIRS_DEBUG}/nd_hacdConvexDecomposition.lib
           optimized ${ARCH_PREBUILT_DIRS_RELEASE}/nd_hacdConvexDecomposition.lib
           debug ${ARCH_PREBUILT_DIRS_DEBUG}/hacd.lib
           optimized ${ARCH_PREBUILT_DIRS_RELEASE}/hacd.lib
           debug ${ARCH_PREBUILT_DIRS_DEBUG}/nd_Pathing.lib
           optimized ${ARCH_PREBUILT_DIRS_RELEASE}/nd_Pathing.lib)
     else ()
        target_link_libraries( llphysicsextensions_impl INTERFACE nd_hacdConvexDecomposition hacd nd_Pathing )
     endif ()
     target_include_directories( llphysicsextensions_impl INTERFACE   ${LIBS_PREBUILT_DIR}/include/)
  else()
   use_prebuilt_binary(llphysicsextensions_stub)
   set(LLPHYSICSEXTENSIONS_SRC_DIR ${LIBS_PREBUILT_DIR}/llphysicsextensions/stub)
   target_link_libraries( llphysicsextensions_impl INTERFACE llphysicsextensionsstub)
   target_include_directories( llphysicsextensions_impl INTERFACE   ${LIBS_PREBUILT_DIR}/include/llphysicsextensions)
  endif()
endif ()
