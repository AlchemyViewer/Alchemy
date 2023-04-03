# -*- cmake -*-

include(Variables)
include(Prebuilt)
include(FindOpenGL)

add_library( ll::opengl INTERFACE IMPORTED )

if(TARGET OpenGL::OpenGL)
	target_link_libraries( ll::opengl INTERFACE OpenGL::OpenGL)
elseif(TARGET OpenGL::GL)
	target_link_libraries( ll::opengl INTERFACE OpenGL::GL)
endif()