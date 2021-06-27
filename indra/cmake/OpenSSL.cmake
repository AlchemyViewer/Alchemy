# -*- cmake -*-
include(Prebuilt)
include(Linking)

set(OpenSSL_FIND_QUIETLY ON)
set(OpenSSL_FIND_REQUIRED ON)

if (USESYSTEMLIBS)
  include(FindOpenSSL)
else (USESYSTEMLIBS)
  use_prebuilt_binary(openssl)
  if (WINDOWS)
    set(SSLEAY_LIBRARY
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/ssleay32.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/ssleay32.lib)
    set(LIBEAY_LIBRARY
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libeay32.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libeay32.lib)
    set(OPENSSL_LIBRARIES ${SSLEAY_LIBRARY} ${LIBEAY_LIBRARY})
  else (WINDOWS)
    set(OPENSSL_LIBRARIES ssl crypto)
  endif (WINDOWS)
  set(OPENSSL_INCLUDE_DIRS ${LIBS_PREBUILT_DIR}/include)
endif (USESYSTEMLIBS)

if (LINUX)
  set(CRYPTO_LIBRARIES crypto dl)
elseif (DARWIN)
  set(CRYPTO_LIBRARIES crypto)
endif (LINUX)
