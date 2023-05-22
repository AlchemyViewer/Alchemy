# -*- cmake -*-
include(Prebuilt)
include(FreeType)

add_library( ll::uilibraries INTERFACE IMPORTED )

if (LINUX)
  option(USE_NFD "Enable NFD universal filepicker library" ON)
  option(USE_X11 "Enable undefined behavior sanitizer" OFF)

  if(USE_X11)
    target_compile_definitions(ll::uilibraries INTERFACE LL_X11=1 )
  endif()

  include(FindPkgConfig)

  if(USE_X11)
    set(PKGCONFIG_PACKAGES
          x11
          )
  endif()

  foreach(pkg ${PKGCONFIG_PACKAGES})
    pkg_check_modules(${pkg} REQUIRED IMPORTED_TARGET ${pkg})
    target_link_libraries( ll::uilibraries INTERFACE PkgConfig::${pkg})
  endforeach(pkg)
endif (LINUX)
if( WINDOWS )
  target_link_libraries( ll::uilibraries INTERFACE
          opengl32
          comdlg32
          dxguid
          kernel32
          odbc32
          odbccp32
          oleaut32
          shell32
          Vfw32
          wer
          winspool
          imm32
          )
endif()

if(USE_NFD)
  target_compile_definitions(ll::uilibraries INTERFACE LL_NFD=1 )
endif()

target_include_directories( ll::uilibraries SYSTEM INTERFACE
        ${LIBS_PREBUILT_DIR}/include
        )

