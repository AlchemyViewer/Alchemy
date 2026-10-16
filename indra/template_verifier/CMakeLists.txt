# -*- cmake -*-

# The message template check, a tool the viewer's build runs; see
# cmake/TemplateCheck.cmake.
add_executable(template_verifier)
target_sources(template_verifier PRIVATE template_verifier.cpp)
target_link_libraries(template_verifier PRIVATE al::flags)
set_target_properties(template_verifier PROPERTIES FOLDER "Tools")
