# -*- cmake -*-
#
# Clang for the ports of the -clang Linux triplets, which chainload this in
# place of vcpkg's own Linux toolchain. Every port goes through it, whatever
# its build system: vcpkg configures make and meson ports from the compilers
# a CMake toolchain picks. A viewer built with Clang links only ports built
# with Clang, and BootstrapVcpkg.cmake picks these triplets for it.
#
# vcpkg hashes this file and the compiler it finds into every port's ABI, but
# not the files this includes.

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

# What vcpkg's Linux toolchain does for a port, which a chainloaded toolchain
# replaces: the triplet's flags, -fPIC, and the variables a try-compile
# inherits. It sits beside the vcpkg.cmake that includes this file, which a
# try-compile loads without setting CMAKE_TOOLCHAIN_FILE.
get_filename_component(z_al_vcpkg_buildsystems "${CMAKE_PARENT_LIST_FILE}" DIRECTORY)
include("${z_al_vcpkg_buildsystems}/../toolchains/linux.cmake")
