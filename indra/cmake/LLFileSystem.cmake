# -*- cmake -*-
include(Abseil)
include(Boost)

set(LLFILESYSTEM_INCLUDE_DIRS
    ${LIBS_OPEN_DIR}/llfilesystem
    )

set(LLFILESYSTEM_LIBRARIES llfilesystem ${BOOST_FILESYSTEM_LIBRARY} ${BOOST_SYSTEM_LIBRARY} absl::strings)
