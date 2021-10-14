# -*- cmake -*-
include(Linking)
include(Prebuilt)

# Note that the use_prebuilt_binary macros below do not in fact include binaries;
# the llphysicsextensions_* packages are source only and are built here.
# The source package and the stub package both build libraries of the same name.
if (NOT USE_LL_STUBS)
   use_prebuilt_binary( ndPhysicsStub )
   if (WINDOWS)
      set(LLPHYSICSEXTENSIONS_LIBRARIES 
         debug ${ARCH_PREBUILT_DIRS_DEBUG}/nd_hacdConvexDecomposition.lib
         optimized ${ARCH_PREBUILT_DIRS_RELEASE}/nd_hacdConvexDecomposition.lib
         debug ${ARCH_PREBUILT_DIRS_DEBUG}/hacd.lib
         optimized ${ARCH_PREBUILT_DIRS_RELEASE}/hacd.lib
         debug ${ARCH_PREBUILT_DIRS_DEBUG}/nd_Pathing.lib
         optimized ${ARCH_PREBUILT_DIRS_RELEASE}/nd_Pathing.lib)
   else ()
      set(LLPHYSICSEXTENSIONS_LIBRARIES nd_hacdConvexDecomposition hacd nd_Pathing )
   endif ()
   set(LLPHYSICSEXTENSIONS_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/ )
else ()
   use_prebuilt_binary(llphysicsextensions_stub)
   set(LLPHYSICSEXTENSIONS_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include/llphysicsextensions) 
   set(LLPHYSICSEXTENSIONS_SRC_DIR ${LIBS_PREBUILT_DIR}/llphysicsextensions/stub)
   set(LLPHYSICSEXTENSIONS_LIBRARIES    llphysicsextensionsstub)
endif ()
