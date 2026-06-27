# -*- cmake -*-

include_guard()

if (USE_NSSPELLCHECKER)
  # Link target for the macOS NSSpellChecker engine (llspellcheckengine_mac.mm). No compile
  # definitions are needed: the engine is selected by which source CMake compiles, and the shared
  # llspellcheck.h is platform-clean (it only forward-declares the abstract LLSpellCheckEngine).
  add_library(ll::nsspellchecker INTERFACE IMPORTED)
  target_link_libraries(ll::nsspellchecker INTERFACE "-framework AppKit" "-framework Foundation")
endif ()
