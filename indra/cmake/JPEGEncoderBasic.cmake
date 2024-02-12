# -*- cmake -*-
use_prebuilt_binary(jpegencoderbasic)

# Main JS file
configure_file("${LIBS_PREBUILT_DIR}/js/jpeg_encoder_basic.js" "${CMAKE_SOURCE_DIR}/newview/skins/default/html/common/equirectangular/js/jpeg_encoder_basic.js" COPYONLY)
