# -*- cmake -*-
#
# The dependencies that are one line each: an ll:: target over what a port
# exports, a header a port installs, or a directory under externals/.
# Anything with a decision of its own -- a search it makes itself, a
# platform branch with substance, a per-port fix -- keeps its own module and
# is listed in the root beside this one.
include_guard()

# al_import(<ll::name>
#           [PACKAGE <package> [CONFIG] [COMPONENTS <c>...] TARGETS <t>...]
#           [HEADER <file>] [INCLUDE <dir>]
#           [LINK <item>...] [DEFINES <define>...] [WHEN <variable>])
#
# Declares <ll::name> as an INTERFACE IMPORTED target. Unless WHEN names a
# variable that is false, it then finds PACKAGE and links its TARGETS, or
# finds the directory holding HEADER, or takes INCLUDE as the directory, and
# adds LINK and DEFINES. The target exists either way, so a consumer links it
# without asking whether the dependency is on.
function(al_import name)
  cmake_parse_arguments(
    PARSE_ARGV 1
    arg
    "CONFIG"
    "PACKAGE;HEADER;INCLUDE;WHEN"
    "COMPONENTS;TARGETS;LINK;DEFINES"
  )
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "al_import(${name}): unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()

  add_library(${name} INTERFACE IMPORTED)
  if(DEFINED arg_WHEN AND NOT ${arg_WHEN})
    return()
  endif()

  # What the find prints is attributed to the dependency under --log-context.
  string(REGEX REPLACE "^ll::" "" short "${name}")
  string(MAKE_C_IDENTIFIER "${short}" context)
  list(APPEND CMAKE_MESSAGE_CONTEXT ${context})

  if(arg_PACKAGE)
    set(config "")
    if(arg_CONFIG)
      set(config CONFIG)
    endif()
    set(components "")
    if(arg_COMPONENTS)
      set(components COMPONENTS ${arg_COMPONENTS})
    endif()
    find_package(${arg_PACKAGE} ${config} REQUIRED ${components})
    target_link_libraries(${name} INTERFACE ${arg_TARGETS})
  endif()
  if(arg_HEADER)
    string(TOUPPER "${short}_INCLUDE_DIR" include_var)
    string(MAKE_C_IDENTIFIER "${include_var}" include_var)
    find_path(${include_var} "${arg_HEADER}" REQUIRED)
    mark_as_advanced(${include_var})
    target_include_directories(${name} SYSTEM INTERFACE ${${include_var}})
  endif()
  if(arg_INCLUDE)
    target_include_directories(${name} SYSTEM INTERFACE ${arg_INCLUDE})
  endif()
  if(arg_LINK)
    target_link_libraries(${name} INTERFACE ${arg_LINK})
  endif()
  if(arg_DEFINES)
    target_compile_definitions(${name} INTERFACE ${arg_DEFINES})
  endif()
endfunction()

# Ports, in link order where one is built on another.
# gersemi: off
al_import(ll::zlib-ng      PACKAGE ZLIB                   TARGETS ZLIB::ZLIB)
al_import(ll::minizip      PACKAGE minizip         CONFIG TARGETS MINIZIP::minizip LINK ll::zlib-ng)
al_import(ll::colladadom   PACKAGE unofficial-collada-dom CONFIG TARGETS unofficial::collada-dom::collada14dom LINK ll::minizip)
al_import(ll::fastfloat    PACKAGE FastFloat       CONFIG TARGETS FastFloat::fast_float)
al_import(ll::fmt          PACKAGE fmt             CONFIG TARGETS fmt::fmt)
al_import(ll::freetype     PACKAGE Freetype               TARGETS Freetype::Freetype)
al_import(ll::glm          PACKAGE glm             CONFIG TARGETS glm::glm-header-only)
al_import(ll::libavif      PACKAGE libavif         CONFIG TARGETS avif)
al_import(ll::libjpeg      PACKAGE JPEG                   TARGETS JPEG::JPEG)
al_import(ll::libpng       PACKAGE PNG                    TARGETS PNG::PNG)
al_import(ll::libwebp      PACKAGE WebP            CONFIG TARGETS WebP::webp WebP::webpdecoder WebP::webpdemux)
al_import(ll::meshoptimizer PACKAGE meshoptimizer  CONFIG TARGETS meshoptimizer::meshoptimizer)
al_import(ll::opengl       PACKAGE OpenGL                 TARGETS OpenGL::GL)
al_import(ll::openjpeg     PACKAGE OpenJPEG        CONFIG TARGETS openjp2)
al_import(ll::openssl      PACKAGE OpenSSL                TARGETS OpenSSL::SSL OpenSSL::Crypto)
al_import(ll::plutosvg     PACKAGE plutosvg        CONFIG TARGETS plutosvg::plutosvg)
al_import(ll::pugixml      PACKAGE pugixml         CONFIG TARGETS pugixml::pugixml)
al_import(ll::simdjson     PACKAGE simdjson        CONFIG TARGETS simdjson::simdjson)
al_import(ll::simdutf      PACKAGE simdutf                TARGETS simdutf::simdutf)
al_import(ll::tinyexr      PACKAGE tinyexr         CONFIG TARGETS unofficial::tinyexr::tinyexr)
al_import(ll::vorbis       PACKAGE Vorbis          CONFIG TARGETS Vorbis::vorbisfile Vorbis::vorbisenc Vorbis::vorbis)
al_import(ll::websocketpp  PACKAGE websocketpp     CONFIG TARGETS websocketpp::websocketpp)
al_import(ll::xxhash       PACKAGE xxHash          CONFIG TARGETS xxHash::xxhash)

# Header-only ports, and the two libraries kept in the tree.
al_import(ll::glext        HEADER GL/glcorearb.h)
al_import(ll::tinygltf     HEADER tiny_gltf.h)
al_import(ll::vhacd        HEADER VHACD.h)
al_import(ll::mikktspace   INCLUDE ${INDRA_SOURCE_DIR}/externals/mikktspace/)
al_import(ll::tut          INCLUDE ${INDRA_SOURCE_DIR}/externals/tut/)

# Per platform, and per option. The target is empty where the condition is
# false, so the option gates sources at the consumer and nothing else.
al_import(ll::nvapi        PACKAGE unofficial-nvapi    CONFIG TARGETS unofficial::nvapi::nvapi       WHEN WINDOWS)
al_import(ll::fontconfig   PACKAGE Fontconfig                 TARGETS Fontconfig::Fontconfig        WHEN LINUX)
al_import(ll::faudio       PACKAGE FAudio              CONFIG TARGETS FAudio::FAudio DEFINES LL_FAUDIO=1     WHEN AL_USE_FAUDIO)
al_import(ll::openal       PACKAGE OpenAL              CONFIG TARGETS OpenAL::OpenAL DEFINES LL_OPENAL=1     WHEN AL_USE_OPENAL)
al_import(ll::openxr       PACKAGE OpenXR              CONFIG TARGETS OpenXR::headers OpenXR::openxr_loader WHEN AL_USE_OPENXR)
al_import(ll::velopack     PACKAGE unofficial-velopack CONFIG TARGETS unofficial::velopack::velopack DEFINES LL_VELOPACK=1 WHEN AL_USE_VELOPACK)
al_import(ll::nsspellchecker LINK "-framework AppKit" "-framework Foundation"                       WHEN AL_USE_NSSPELLCHECKER)
al_import(ll::winspellcheck  LINK ole32                                                            WHEN AL_USE_WINSPELLCHECK)
# gersemi: on
