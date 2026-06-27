# -*- cmake -*-

include_guard()

if (USE_WINSPELLCHECK)
  # Link target for the Windows Spell Checking engine (llspellcheckengine_win32.cpp). ole32 provides
  # CoInitializeEx/CoCreateInstance/CoTaskMemFree/CoUninitialize; <spellcheck.h> ships with the
  # Windows SDK and the API uses LPCWSTR (not BSTR), so no oleaut32/uuid.lib is required. No compile
  # definitions are needed: the engine is selected by which source CMake compiles, and the shared
  # llspellcheck.h is platform-clean (it only forward-declares the abstract LLSpellCheckEngine).
  add_library(ll::winspellcheck INTERFACE IMPORTED)
  target_link_libraries(ll::winspellcheck INTERFACE ole32)
endif ()
