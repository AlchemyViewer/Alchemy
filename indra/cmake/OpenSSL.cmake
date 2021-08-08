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
    set(SSL_LIBRARY
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libssl.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libssl.lib)
    set(CRYPTO_LIBRARY
        debug ${ARCH_PREBUILT_DIRS_DEBUG}/libcrypto.lib
        optimized ${ARCH_PREBUILT_DIRS_RELEASE}/libcrypto.lib)
    set(OPENSSL_LIBRARIES ${SSL_LIBRARY} ${CRYPTO_LIBRARY})
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
