/* SPDX-License-Identifier: MIT OR Apache-2.0 */

/*
 * pnj2k: a JPEG 2000 Part 1 decoder and encoder, with Second Life's
 * upload profile as an encoder preset.
 *
 * ABI
 *   Check pnj2k_abi_version() == PNJ2K_ABI_VERSION once, before any other
 *   call. The structs' layouts are those of 64-bit targets.
 *
 * Structs
 *   Every struct opens with struct_size, which the caller sets to sizeof
 *   the struct before passing it, whether the library reads it or fills
 *   it: zero the struct, then set struct_size, and every field has its
 *   default. A struct_size from another header is taken where it is a
 *   multiple of 8, as every sizeof is, from the struct's size in the
 *   first ABI that had it up to 4096. Fields named reserved must be
 *   zero, and the library writes them zero. Fields that hold an
 *   enumeration's value are uint32_t, and so is pnj2k_status, so that a
 *   program never holds a value outside an enumeration's range.
 *
 * Statuses and messages
 *   Every function that can fail returns a pnj2k_status, PNJ2K_OK being 0.
 *   A call on a decoder keeps the message of its failure, which
 *   pnj2k_decoder_last_error returns, and the warnings of its success; they
 *   last until the next parse, extend, reach, image info or decode on that
 *   decoder, or its free. A call on an encoder keeps the message of its
 *   failure, which pnj2k_encoder_last_error returns, until the next encode
 *   or resize on that encoder, or its free. Calls that take neither keep no
 *   message: pnj2k_status_message describes their status.
 *
 * Ownership
 *   The library keeps no pointer it is handed past the call, but the
 *   allocator's functions and user pointer, which a decoder or encoder and
 *   what it hands out keep. Each object it makes is freed with its own
 *   _free function, which ignores NULL: decoders, encoders, codestreams,
 *   planes and cancellation tokens. Memory it hands out comes from the
 *   allocator of the decoder or encoder that made it, outlives that decoder
 *   or encoder, and is freed with its own function: an image's pixels with
 *   pnj2k_image_free, planes with pnj2k_planes_free, an encode's bytes with
 *   pnj2k_buffer_free. A struct holding such memory must be freed before a
 *   call fills it again, which the call refuses otherwise. A comment's bytes
 *   last until its codestream is extended or freed; a plane's samples until
 *   the planes are freed.
 *
 * Threads
 *   A decoder or an encoder is used by one thread at a time: a program
 *   makes one for each thread. Any number of threads may decode from, ask
 *   the reach of, or query one codestream at once; extending it needs it
 *   alone. Planes are read by any number of threads at once and freed
 *   once. A cancellation token is raised or reset from any thread. Every
 *   object may be freed on any thread; an allocator's allocate runs on the
 *   thread of the call that allocates, and its deallocate on whichever
 *   thread frees. Neither may call a pnj2k function on the decoder, encoder
 *   or planes whose call it runs in. Memory a call is handed to read, such
 *   as codestream data or source pixels, must not be written until the call
 *   returns.
 *
 * Failures
 *   Every pointer is checked for NULL, every struct_size and reserved
 *   field, and every size and stride against what it describes; a pointer
 *   that is not NULL must be valid. No panic crosses the boundary: a bug
 *   inside the library returns PNJ2K_INTERNAL and resets the decoder or
 *   encoder it happened in. The library aborts only where the system
 *   allocator fails to provide its working memory.
 */

#ifndef PNJ2K_H
#define PNJ2K_H

/* Generated from crates/pnj2k-capi by `cargo xtask header`; do not edit. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * The version of the ABI this library implements: every struct's layout,
 * and every function's, constant's and status's meaning.
 *
 * It increases with every change that would break a program built against
 * an earlier header. Adding a function, a constant, an enumeration value,
 * a status, or a field in a struct's `reserved` bytes or at its end, which
 * `struct_size` tells apart, does not.
 */
#define PNJ2K_ABI_VERSION 4

/**
 * `pnj2k_decode_params.flags`: writes the last row first, as images
 * bottom-up in memory want. Not `PNJ2K_ENCODE_FLIP_ROWS`, the encode
 * flag, which a decode refuses.
 */
#define PNJ2K_FLIP_ROWS 1

/**
 * `pnj2k_decode_params.flags`: runs the irreversible (9/7) wavelet in
 * 32-bit floating point for components of up to 8 bits.
 *
 * A decode runs it in 16-bit fixed point for them by default, which is
 * faster and within about a code value of floating point; the tightest
 * limits of ITU-T T.803 ask for floating point.
 */
#define PNJ2K_FLOATING_POINT 2

/**
 * `pnj2k_decode_params.flags`: gives planes floating-point samples,
 * unrounded and unclamped, rather than integers.
 */
#define PNJ2K_FLOAT_PLANES 4

/**
 * `pnj2k_decode_params.flags`: converts a JP2 file's colours to sRGB.
 *
 * Colours go to sRGB, and grey to sRGB's greyscale, as the file's first
 * colour specification says (sYCC, or a restricted ICC profile),
 * upsampling as `PNJ2K_UPSAMPLE` does; sRGB, greyscale and raw
 * codestreams are left as they are. `pnj2k_decode` alone takes it: with
 * components named, or for `pnj2k_decode_planes`, it is
 * `PNJ2K_INVALID_REQUEST`.
 */
#define PNJ2K_SRGB 8

/**
 * `pnj2k_decode_params.flags`: upsamples subsampled components.
 *
 * Components subsampled on the reference grid are brought to the image's
 * size, each pixel taking the sample at or before it, or, with
 * `PNJ2K_INTERPOLATE`, a blend of the samples about it, so that they
 * decode to one image; the region is then in the image's pixels, and
 * `pnj2k_decode_planes` gives every plane at the image's size.
 */
#define PNJ2K_UPSAMPLE 16

/**
 * `pnj2k_decode_params.flags`: interpolates between a subsampled
 * component's samples, where `PNJ2K_UPSAMPLE` or `PNJ2K_SRGB` upsamples
 * it, rather than repeating them.
 *
 * A sample lies on the reference grid at its index times the
 * subsampling, moved by the component's registration offset (CRG)
 * against the first component's. A pixel on a sample takes it, and one
 * between samples is interpolated linearly from the four about it:
 * integers exactly, rounded once to the nearest and a half upwards. Past a
 * component's first or last sample, that sample stands. The picture is
 * the smoother, at no more time than repeating takes. The reference image
 * ITU-T T.803 Annex G compares a subsampled file with repeats samples,
 * so a decode held to its limits leaves this out. Without `PNJ2K_UPSAMPLE`
 * or `PNJ2K_SRGB` it does nothing.
 */
#define PNJ2K_INTERPOLATE 32

/**
 * The alignment of the memory a decoder or encoder allocates for the
 * caller (an image's pixels, a plane's samples, an encoder's codestream
 * bytes), and that it asks a caller's allocator for.
 */
#define PNJ2K_ALIGNMENT 16

/**
 * The entries of `pnj2k_reach.layers`: discarding none to 32 levels.
 */
#define PNJ2K_REACH_LEVELS 33

/**
 * `pnj2k_encode_params.flags`: lossless, with the 5/3 wavelet and the
 * reversible colour transform, and an unbounded last layer.
 *
 * The encode flags start at bit 16 and the decode flags at bit 0, so that
 * they share no bit; each call refuses the other's flags with
 * `PNJ2K_INVALID_ARGUMENT`.
 */
#define PNJ2K_ENCODE_REVERSIBLE (1 << 16)

/**
 * `pnj2k_encode_params.flags`: the image's rows run bottom to top, as
 * OpenGL's do. Not `PNJ2K_FLIP_ROWS`, the decode flag, which an encode
 * refuses.
 */
#define PNJ2K_ENCODE_FLIP_ROWS (1 << 17)

/**
 * `pnj2k_encode_params.flags`: every packet's length in the tile-part
 * header (PLT).
 */
#define PNJ2K_ENCODE_PACKET_LENGTHS (1 << 18)

/**
 * `pnj2k_encode_params.flags`: the colour of fully transparent pixels
 * filled smoothly from their neighbours before coding.
 */
#define PNJ2K_ENCODE_FILL_TRANSPARENT (1 << 19)

/**
 * `pnj2k_encode_params.flags`: no quality cap; every layer filled to its
 * size.
 */
#define PNJ2K_ENCODE_NO_QUALITY_CAP (1 << 20)

/**
 * `pnj2k_codestream_params.fields`: `tile_width`, `tile_height`,
 * `tile_x0` and `tile_y0` set.
 */
#define PNJ2K_CS_TILES (1 << 0)

/**
 * `pnj2k_codestream_params.fields`: `tile_parts` set.
 */
#define PNJ2K_CS_TILE_PARTS (1 << 1)

/**
 * `pnj2k_codestream_params.fields`: `tile_allocation` set.
 */
#define PNJ2K_CS_TILE_ALLOCATION (1 << 2)

/**
 * `pnj2k_codestream_params.fields`: `levels` set.
 */
#define PNJ2K_CS_LEVELS (1 << 3)

/**
 * `pnj2k_codestream_params.fields`: `colour_transform` set.
 */
#define PNJ2K_CS_COLOUR_TRANSFORM (1 << 4)

/**
 * `pnj2k_codestream_params.fields`: `block_width` and `block_height` set.
 */
#define PNJ2K_CS_CODE_BLOCKS (1 << 5)

/**
 * `pnj2k_codestream_params.fields`: `block_style` set.
 */
#define PNJ2K_CS_BLOCK_STYLE (1 << 6)

/**
 * `pnj2k_codestream_params.fields`: `precincts` and `precinct_count` set.
 */
#define PNJ2K_CS_PRECINCTS (1 << 7)

/**
 * `pnj2k_codestream_params.fields`: `progression` set.
 */
#define PNJ2K_CS_PROGRESSION (1 << 8)

/**
 * `pnj2k_codestream_params.fields`: `progression_changes` and
 * `progression_change_count` set.
 */
#define PNJ2K_CS_PROGRESSION_CHANGES (1 << 9)

/**
 * `pnj2k_codestream_params.fields`: `layers` and `layer_count` set.
 */
#define PNJ2K_CS_LAYERS (1 << 10)

/**
 * `pnj2k_codestream_params.fields`: `quantisation_style` set.
 */
#define PNJ2K_CS_QUANTISATION_STYLE (1 << 11)

/**
 * `pnj2k_codestream_params.fields`: `step` set.
 */
#define PNJ2K_CS_STEP (1 << 12)

/**
 * `pnj2k_codestream_params.fields`: `guard_bits` set.
 */
#define PNJ2K_CS_GUARD_BITS (1 << 13)

/**
 * `pnj2k_codestream_params.fields`: `markers` set.
 */
#define PNJ2K_CS_MARKERS (1 << 14)

/**
 * `pnj2k_codestream_params.fields`: `profile` set.
 */
#define PNJ2K_CS_PROFILE (1 << 15)

/**
 * `pnj2k_codestream_params.fields`: `components` and `component_count`
 * set.
 */
#define PNJ2K_CS_COMPONENTS (1 << 16)

/**
 * `pnj2k_codestream_params.fields`: `roi` set.
 */
#define PNJ2K_CS_ROI (1 << 17)

/**
 * `pnj2k_codestream_params.fields`: `comments` and `comment_count` set.
 */
#define PNJ2K_CS_COMMENTS (1 << 18)

/**
 * `pnj2k_codestream_params.fields`: `registration` and
 * `registration_count` set.
 */
#define PNJ2K_CS_REGISTRATION (1 << 19)

/**
 * `pnj2k_codestream_params.block_style`: the arithmetic coder bypassed
 * for the lower bit-planes' significance and refinement passes.
 */
#define PNJ2K_BLOCK_BYPASS 1

/**
 * `pnj2k_codestream_params.block_style`: the coder's contexts reset after
 * each pass.
 */
#define PNJ2K_BLOCK_RESET 2

/**
 * `pnj2k_codestream_params.block_style`: the coder terminated after each
 * pass.
 */
#define PNJ2K_BLOCK_TERMINATE_ALL 4

/**
 * `pnj2k_codestream_params.block_style`: contexts formed from the current
 * stripe alone.
 */
#define PNJ2K_BLOCK_VERTICALLY_CAUSAL 8

/**
 * `pnj2k_codestream_params.block_style`: predictable termination.
 */
#define PNJ2K_BLOCK_PREDICTABLE 16

/**
 * `pnj2k_codestream_params.block_style`: a segmentation symbol after
 * each cleanup pass.
 */
#define PNJ2K_BLOCK_SEGMENTATION 32

/**
 * `pnj2k_codestream_params.markers`: a start of packet marker segment
 * before each packet.
 */
#define PNJ2K_MARKER_SOP 1

/**
 * `pnj2k_codestream_params.markers`: an end of packet header marker after
 * each packet header.
 */
#define PNJ2K_MARKER_EPH 2

/**
 * `pnj2k_codestream_params.markers`: tile-part lengths in the main header.
 */
#define PNJ2K_MARKER_TLM 4

/**
 * `pnj2k_codestream_params.markers`: packet lengths in each tile-part
 * header.
 */
#define PNJ2K_MARKER_PLT 8

/**
 * `pnj2k_codestream_params.markers`: packet lengths in the main header.
 */
#define PNJ2K_MARKER_PLM 16

/**
 * `pnj2k_codestream_params.markers`: packet headers in the main header.
 */
#define PNJ2K_MARKER_PPM 32

/**
 * `pnj2k_codestream_params.markers`: packet headers in each tile-part
 * header.
 */
#define PNJ2K_MARKER_PPT 64

/**
 * `pnj2k_component_params.fields`: `levels` set.
 */
#define PNJ2K_COMP_LEVELS (1 << 0)

/**
 * `pnj2k_component_params.fields`: `reversible` set.
 */
#define PNJ2K_COMP_REVERSIBLE (1 << 1)

/**
 * `pnj2k_component_params.fields`: `block_width` and `block_height` set.
 */
#define PNJ2K_COMP_CODE_BLOCKS (1 << 2)

/**
 * `pnj2k_component_params.fields`: `block_style` set.
 */
#define PNJ2K_COMP_BLOCK_STYLE (1 << 3)

/**
 * `pnj2k_component_params.fields`: `precincts` and `precinct_count` set.
 */
#define PNJ2K_COMP_PRECINCTS (1 << 4)

/**
 * `pnj2k_component_params.fields`: `quantisation_style`, `step` and
 * `guard_bits` set, together.
 */
#define PNJ2K_COMP_QUANTISATION (1 << 5)

/**
 * `pnj2k_jp2_params.flags`: the image header box says the colour space
 * is not known to be right (UnkC).
 */
#define PNJ2K_JP2_COLOURSPACE_UNKNOWN 1

/**
 * An interleaved pixel format, for `pnj2k_decode_params.format` and
 * `pnj2k_image.format`.
 */
typedef enum pnj2k_pixel_format {
  /**
   * In a request: the format the components suggest. One to four give
   * grey, grey and alpha, RGB or RGBA, 16 bits per channel where any is
   * deeper than 8.
   */
  PNJ2K_FORMAT_AUTO = 0,
  /**
   * One 8-bit channel.
   */
  PNJ2K_FORMAT_GRAY8 = 1,
  /**
   * Two 8-bit channels.
   */
  PNJ2K_FORMAT_GRAY_ALPHA8 = 2,
  /**
   * Three 8-bit channels.
   */
  PNJ2K_FORMAT_RGB8 = 3,
  /**
   * Four 8-bit channels.
   */
  PNJ2K_FORMAT_RGBA8 = 4,
  /**
   * One 16-bit channel, in native byte order.
   */
  PNJ2K_FORMAT_GRAY16 = 5,
  /**
   * Two 16-bit channels.
   */
  PNJ2K_FORMAT_GRAY_ALPHA16 = 6,
  /**
   * Three 16-bit channels.
   */
  PNJ2K_FORMAT_RGB16 = 7,
  /**
   * Four 16-bit channels.
   */
  PNJ2K_FORMAT_RGBA16 = 8,
} pnj2k_pixel_format;

/**
 * How a parse and its decodes treat input that departs from the standard,
 * for `pnj2k_parse_params.mode`.
 */
typedef enum pnj2k_mode {
  /**
   * Decode every conforming codestream, and also work around damage and
   * truncation where the data allows, reporting each as a warning.
   */
  PNJ2K_MODE_LENIENT = 0,
  /**
   * Fail with `PNJ2K_STRICT` where lenient mode would report a warning.
   */
  PNJ2K_MODE_STRICT = 1,
} pnj2k_mode;

/**
 * The order packets follow through a tile, for `pnj2k_info.progression`.
 */
typedef enum pnj2k_progression {
  /**
   * Layer, resolution, component, position.
   */
  PNJ2K_PROGRESSION_LRCP = 0,
  /**
   * Resolution, layer, component, position.
   */
  PNJ2K_PROGRESSION_RLCP = 1,
  /**
   * Resolution, position, component, layer.
   */
  PNJ2K_PROGRESSION_RPCL = 2,
  /**
   * Position, component, resolution, layer.
   */
  PNJ2K_PROGRESSION_PCRL = 3,
  /**
   * Component, position, resolution, layer.
   */
  PNJ2K_PROGRESSION_CPRL = 4,
} pnj2k_progression;

/**
 * The type of a plane's samples, for `pnj2k_plane.sample_type`.
 */
typedef enum pnj2k_sample_type {
  /**
   * `int32_t`: integer samples of components up to 31 bits.
   */
  PNJ2K_SAMPLES_I32 = 0,
  /**
   * `int64_t`: integer samples of deeper components.
   */
  PNJ2K_SAMPLES_I64 = 1,
  /**
   * `float`: unrounded, unclamped samples of components up to 16 bits,
   * where the request asked for `PNJ2K_FLOAT_PLANES`.
   */
  PNJ2K_SAMPLES_F32 = 2,
  /**
   * `double`: unrounded, unclamped samples of deeper components.
   */
  PNJ2K_SAMPLES_F64 = 3,
} pnj2k_sample_type;

/**
 * What a parse or decode worked around, for `pnj2k_warning.kind`.
 *
 * Offsets count bytes from the start of the data, a JP2 file's boxes
 * included.
 */
typedef enum pnj2k_warning_kind {
  /**
   * A kind this header does not know; the message describes it.
   */
  PNJ2K_WARNING_OTHER = 0,
  /**
   * The data ends before the codestream does. A prefix of a file decodes
   * to what its whole packets give.
   */
  PNJ2K_WARNING_TRUNCATED = 1,
  /**
   * Bytes follow the EOC marker, at `offset`. They are ignored.
   */
  PNJ2K_WARNING_TRAILING_DATA = 2,
  /**
   * The tile-part at `offset` claims more bytes than the data holds.
   */
  PNJ2K_WARNING_LENGTH_PAST_END = 3,
  /**
   * The tile-part at `offset` has a length that does not end on a
   * marker; it runs to the next one.
   */
  PNJ2K_WARNING_LENGTH_MISMATCH = 4,
  /**
   * A marker segment the decoder does not know, `code` at `offset`, was
   * skipped.
   */
  PNJ2K_WARNING_UNKNOWN_MARKER = 5,
  /**
   * The marker segment `code` at `offset` runs past the end of its
   * header or of the data.
   */
  PNJ2K_WARNING_SEGMENT_OVERRUN = 6,
  /**
   * The marker segment `code` at `offset` has a wrong length: one that
   * does not end on a marker, that counts bytes past the segment's last
   * field, or that is below the least the segment can have.
   */
  PNJ2K_WARNING_BAD_SEGMENT_LENGTH = 7,
  /**
   * The marker `code` at `offset` is not allowed there, and is ignored.
   */
  PNJ2K_WARNING_MISPLACED_MARKER = 8,
  /**
   * The tile-part at `offset` names a tile the image does not have.
   */
  PNJ2K_WARNING_BAD_TILE_INDEX = 9,
  /**
   * A damaged packet header in `tile`; the rest of its tile-part is not
   * read.
   */
  PNJ2K_WARNING_BAD_PACKET_HEADER = 10,
  /**
   * An EPH marker the coding style promises is missing in `tile`.
   */
  PNJ2K_WARNING_MISSING_EPH = 11,
  /**
   * A code-block's data in `tile` holds a marker; its data ends there.
   */
  PNJ2K_WARNING_MARKER_IN_CODE_BLOCK = 12,
  /**
   * A code-block in `tile` has more coding passes than its bit-planes
   * allow.
   */
  PNJ2K_WARNING_TOO_MANY_PASSES = 13,
  /**
   * A segmentation symbol in `tile` decoded wrong, so a code-block's data
   * is damaged.
   */
  PNJ2K_WARNING_BAD_SEGMENTATION_SYMBOL = 14,
  /**
   * The colour transform could not be applied to `tile`.
   */
  PNJ2K_WARNING_COLOUR_TRANSFORM_SKIPPED = 15,
  /**
   * `tile`'s POC marker segments give more progression volumes than
   * `pnj2k_parse_params.max_progression_volumes`; packets only later
   * volumes give are not read.
   */
  PNJ2K_WARNING_TOO_MANY_VOLUMES = 16,
  /**
   * The codestream declares Profile 0 or Profile 1 but breaks one of the
   * profile's restrictions (T.800 Table A.45), which the message names.
   */
  PNJ2K_WARNING_PROFILE_RESTRICTION = 17,
  /**
   * A box a JP2 file must hold is missing; `code` is its type, its four
   * bytes as a big-endian number (0x66747970 for 'ftyp'). The file is
   * read without it.
   */
  PNJ2K_WARNING_MISSING_BOX = 18,
  /**
   * The JP2 box of type `code` at `offset` is not allowed there. It is
   * read where it lies, or ignored where it repeats one.
   */
  PNJ2K_WARNING_MISPLACED_BOX = 19,
  /**
   * The JP2 box of type `code` at `offset` disagrees with the
   * codestream: an image header box whose height, width or component
   * count is not SIZ's, a component mapping box with a reserved mapping
   * type, read as direct use, or that names a component or palette
   * column the file does not have, which a decode into pixels refuses
   * with `PNJ2K_INVALID_DATA`, or a channel definition box that names a
   * channel the image does not have, leaves one out, gives a colour no
   * channel, or gives two channels one type and association. The
   * codestream's size is used, and a definition of a channel the image
   * lacks is ignored.
   */
  PNJ2K_WARNING_INVALID_BOX = 20,
  /**
   * The colour transform joins components of different bit depths in
   * `tile`, which T.800 does not allow; it is applied all the same.
   */
  PNJ2K_WARNING_COLOUR_TRANSFORM_DEPTH = 21,
} pnj2k_warning_kind;

/**
 * What a texture holds, for `pnj2k_encode_params.content`.
 */
typedef enum pnj2k_content {
  /**
   * Colour: base colour, emissive, any ordinary image. The first three
   * components are coded through the colour transform.
   */
  PNJ2K_CONTENT_COLOUR = 0,
  /**
   * Data packed into channels: metallic and roughness, masks. No colour
   * transform, and every component weighs the same.
   */
  PNJ2K_CONTENT_DATA = 1,
  /**
   * A normal map: resized with its vectors renormalised, coded as data,
   * and losslessly with whichever colour transform gives less.
   */
  PNJ2K_CONTENT_NORMAL = 2,
} pnj2k_content;

/**
 * How errors are weighted when deciding what each layer holds, for
 * `pnj2k_encode_params.weighting`.
 */
typedef enum pnj2k_weighting {
  /**
   * The library's default.
   */
  PNJ2K_WEIGHTING_DEFAULT = 0,
  /**
   * Plain squared error in the image.
   */
  PNJ2K_WEIGHTING_OFF = 1,
  /**
   * Colour textures' chroma weighted down.
   */
  PNJ2K_WEIGHTING_CHROMA = 2,
  /**
   * Luminance and chroma weighted by T.800 Annex J's contrast sensitivity
   * weights for texels seen at `pnj2k_encode_params.pixels_per_degree`
   * per degree.
   */
  PNJ2K_WEIGHTING_DISTANCE = 3,
} pnj2k_weighting;

/**
 * The parameters an encode starts from, for `pnj2k_encode_params.preset`.
 */
typedef enum pnj2k_preset {
  /**
   * Part 1's defaults, so that a zeroed struct takes them.
   */
  PNJ2K_PRESET_PART1 = 0,
  /**
   * Second Life's upload profile, which Second Life and OpenSim viewers
   * stream.
   */
  PNJ2K_PRESET_SECOND_LIFE = 1,
} pnj2k_preset;

/**
 * How a tile's packets are split into tile-parts, for
 * `pnj2k_codestream_params.tile_parts`.
 */
typedef enum pnj2k_tile_parts {
  /**
   * One tile-part a tile.
   */
  PNJ2K_TILE_PARTS_ONE = 0,
  /**
   * A tile-part where the resolution changes.
   */
  PNJ2K_TILE_PARTS_RESOLUTIONS = 1,
  /**
   * A tile-part where the layer changes.
   */
  PNJ2K_TILE_PARTS_LAYERS = 2,
  /**
   * A tile-part where the component changes.
   */
  PNJ2K_TILE_PARTS_COMPONENTS = 3,
} pnj2k_tile_parts;

/**
 * How layer targets are met over the tiles, for
 * `pnj2k_codestream_params.tile_allocation`.
 */
typedef enum pnj2k_tile_allocation {
  /**
   * One allocation over every tile, its tile-parts in Profile 0's order.
   */
  PNJ2K_TILE_ALLOCATION_GLOBAL = 0,
  /**
   * Each tile's layers allocated alone, at its share of each target,
   * and its tile-parts written together.
   */
  PNJ2K_TILE_ALLOCATION_PER_TILE = 1,
} pnj2k_tile_allocation;

/**
 * The restrictions a codestream declares and keeps to, for
 * `pnj2k_codestream_params.profile`.
 */
typedef enum pnj2k_profile {
  /**
   * Part 1's limits alone (Rsiz 0).
   */
  PNJ2K_PROFILE_UNRESTRICTED = 0,
  /**
   * Profile 0 (T.800 Table A.45).
   */
  PNJ2K_PROFILE_0 = 1,
  /**
   * Profile 1.
   */
  PNJ2K_PROFILE_1 = 2,
} pnj2k_profile;

/**
 * How the 9/7 wavelet's steps are signalled, for
 * `pnj2k_codestream_params.quantisation_style`.
 */
typedef enum pnj2k_quantisation_style {
  /**
   * Each subband's step written.
   */
  PNJ2K_QUANTISATION_EXPOUNDED = 0,
  /**
   * The lowest subband's step written, the others derived from it.
   */
  PNJ2K_QUANTISATION_DERIVED = 1,
} pnj2k_quantisation_style;

/**
 * What a quality layer holds, for `pnj2k_layer.kind`.
 */
typedef enum pnj2k_layer_kind {
  /**
   * Every pass: lossless where the wavelet is reversible. `value` is
   * ignored.
   */
  PNJ2K_LAYER_ALL = 0,
  /**
   * The output's bytes from its start to the layer's end, `value` a
   * whole number.
   */
  PNJ2K_LAYER_BYTES = 1,
  /**
   * The passes that remove at least `value` weighted squared error per
   * byte, in 8-bit code values.
   */
  PNJ2K_LAYER_SLOPE = 2,
  /**
   * The passes that bring the image's PSNR to `value` decibels.
   */
  PNJ2K_LAYER_PSNR = 3,
} pnj2k_layer_kind;

/**
 * The type of a planar component's samples, for
 * `pnj2k_component_source.sample_type`.
 */
typedef enum pnj2k_source_samples {
  /**
   * `uint8_t`, for unsigned components of up to 8 bits.
   */
  PNJ2K_SOURCE_U8 = 0,
  /**
   * `uint16_t`, for unsigned components of up to 16 bits.
   */
  PNJ2K_SOURCE_U16 = 1,
  /**
   * `int32_t`, for components of up to 32 bits, signed or not.
   */
  PNJ2K_SOURCE_I32 = 2,
  /**
   * `int64_t`, for any component.
   */
  PNJ2K_SOURCE_I64 = 3,
} pnj2k_source_samples;

/**
 * A token that cancels the decodes given it, from any thread.
 */
typedef struct pnj2k_cancel pnj2k_cancel;

/**
 * A parsed codestream: its headers, and where its packet data lies.
 *
 * It holds no reference to the data: each call is handed the data again,
 * the same bytes or a longer prefix of them, so that a buffer that grows
 * as bytes arrive can move between calls. Any number of threads may
 * decode from, ask the reach of, or query one codestream at once;
 * extending or freeing it needs it alone. It outlives the decoder that
 * parsed it, and may be freed on any thread.
 */
typedef struct pnj2k_codestream pnj2k_codestream;

/**
 * A decoder: the working memory decodes reuse, the allocator for what it
 * hands out, and what its last call reported.
 *
 * A program makes one for each thread that decodes. It is used by one
 * thread at a time, and may be freed on any thread.
 */
typedef struct pnj2k_decoder pnj2k_decoder;

/**
 * An encoder: the working memory encodes reuse, the allocator for the
 * bytes it hands out, and the last call's failure.
 *
 * A program makes one for each thread that encodes. It is used by one
 * thread at a time, and may be freed on any thread.
 */
typedef struct pnj2k_encoder pnj2k_encoder;

/**
 * The planes of a decode: one per component, at its own size, precision
 * and signedness.
 *
 * An opaque handle: any number of threads may read it at once, and it is
 * freed once, on any thread, with `pnj2k_planes_free`, which gives the
 * samples back to the decoder's allocator. It outlives its decoder.
 */
typedef struct pnj2k_planes pnj2k_planes;

/**
 * What a call did: `PNJ2K_OK`, or why it failed.
 *
 * A `uint32_t` rather than an enumeration, so that a program never holds
 * a value outside an enumeration's range: the `PNJ2K_*` status constants
 * name the values, and a later version may add more, which
 * `pnj2k_status_message` describes as an unknown status.
 */
typedef uint32_t pnj2k_status;

/**
 * What a codestream's main header says.
 */
typedef struct pnj2k_info {
  /**
   * `sizeof(pnj2k_info)`.
   */
  size_t struct_size;
  /**
   * Image width: the reference grid's width less the image offset.
   */
  uint32_t width;
  /**
   * Image height.
   */
  uint32_t height;
  /**
   * Horizontal offset of the image on the reference grid.
   */
  uint32_t x_offset;
  /**
   * Vertical offset of the image on the reference grid.
   */
  uint32_t y_offset;
  /**
   * Number of components.
   */
  uint32_t components;
  /**
   * The largest component precision, in bits.
   */
  uint32_t precision;
  /**
   * Nominal tile width.
   */
  uint32_t tile_width;
  /**
   * Nominal tile height.
   */
  uint32_t tile_height;
  /**
   * Number of tiles.
   */
  uint32_t tiles;
  /**
   * Decomposition levels in the main header's coding style. Tiles and
   * components may override it.
   */
  uint32_t levels;
  /**
   * The most levels a decode can discard: from a parsed codestream, the
   * fewest any tile-component has; from a probe, which reads only the
   * main header's coding style, `levels`.
   */
  uint32_t max_reduce;
  /**
   * Quality layers.
   */
  uint32_t layers;
  /**
   * A `pnj2k_progression`: the main header's progression order.
   */
  uint32_t progression;
  /**
   * Nominal code-block width.
   */
  uint32_t code_block_width;
  /**
   * Nominal code-block height.
   */
  uint32_t code_block_height;
  /**
   * The capabilities field (Rsiz): 0 for unrestricted Part 1, 1 and 2 for
   * Profiles 0 and 1, other values for later profiles.
   */
  uint32_t profile;
  /**
   * Whether any component is signed.
   */
  bool is_signed;
  /**
   * Whether any component is subsampled.
   */
  bool subsampled;
  /**
   * Whether the main header's wavelet is the reversible 5/3.
   */
  bool reversible;
  /**
   * Whether the main header asks for a colour transform on the first
   * three components.
   */
  bool colour_transform;
  /**
   * Whether the data is a JP2 file rather than a raw codestream.
   */
  bool jp2;
  /**
   * Written zero; reserved for later fields.
   */
  uint8_t reserved[3];
} pnj2k_info;

/**
 * How to parse a codestream: its mode and the largest image allowed.
 *
 * All zeros are the defaults: lenient mode, at most 65535
 * samples per side, an area of 65535 times 65535, 16384 components,
 * 65535 tiles, 2^24 code-blocks per tile, decodes of up to 1 GiB, tile
 * layouts of up to 256 MiB and walks of up to 9361 progression volumes
 * per tile, the most one POC marker segment holds.
 */
typedef struct pnj2k_parse_params {
  /**
   * `sizeof(pnj2k_parse_params)`.
   */
  size_t struct_size;
  /**
   * The most samples on the reference grid, width times height; 0 for
   * the default, 65535 times 65535.
   */
  uint64_t max_area;
  /**
   * The most code-blocks, and the most precincts, in one tile; 0 for the
   * default, 2^24.
   */
  uint64_t max_code_blocks;
  /**
   * The most memory a decode may take for samples, in bytes; 0 for the
   * default, 1 GiB.
   */
  uint64_t max_decoded_bytes;
  /**
   * A `pnj2k_mode`.
   */
  uint32_t mode;
  /**
   * The widest image, in samples on the reference grid; 0 for the
   * default, 65535.
   */
  uint32_t max_width;
  /**
   * The tallest image; 0 for the default, 65535.
   */
  uint32_t max_height;
  /**
   * The most components; 0 for the default, 16384. Values over 65535
   * count as 65535.
   */
  uint32_t max_components;
  /**
   * The most tiles; 0 for the default, 65535.
   */
  uint32_t max_tiles;
  /**
   * The most progression volumes a tile's walk through its packets takes
   * from its POC marker segments; 0 for the default, 9361. A tile whose
   * POC segments give more is walked through that many, and the decode
   * warns `PNJ2K_WARNING_TOO_MANY_VOLUMES`, which strict mode fails on.
   */
  uint32_t max_progression_volumes;
  /**
   * The most memory the layout of code-blocks, precincts and their tag
   * trees, with the contributions its packets add, may take, in bytes:
   * one tile's, for a decode or a reach, and every tile's together for a
   * codestream that `pnj2k_codestream_extend` has read; 0 for the
   * default, 256 MiB.
   */
  uint64_t max_layout_bytes;
} pnj2k_parse_params;

/**
 * One component as the SIZ marker segment describes it.
 */
typedef struct pnj2k_component {
  /**
   * `sizeof(pnj2k_component)`.
   */
  size_t struct_size;
  /**
   * Bits per sample, 1 to 38.
   */
  uint32_t precision;
  /**
   * Horizontal subsampling: one sample every `dx` reference grid points.
   */
  uint32_t dx;
  /**
   * Vertical subsampling.
   */
  uint32_t dy;
  /**
   * Whether samples are signed.
   */
  bool is_signed;
  /**
   * Written zero; reserved for later fields.
   */
  uint8_t reserved[3];
} pnj2k_component;

/**
 * A comment (COM) marker segment.
 */
typedef struct pnj2k_comment {
  /**
   * `sizeof(pnj2k_comment)`.
   */
  size_t struct_size;
  /**
   * The comment's bytes, valid until the codestream is extended or
   * freed; not terminated.
   */
  const uint8_t *data;
  /**
   * The number of bytes.
   */
  size_t len;
  /**
   * 0 for binary data, 1 for ISO/IEC 8859-15 text; other values are
   * reserved, and passed through as read.
   */
  uint32_t registration;
  /**
   * Written zero; reserved for a later field.
   */
  uint32_t reserved;
} pnj2k_comment;

/**
 * How much of a codestream some data holds: for each number of discarded
 * levels, the quality layers whose packets are wholly present for the
 * resolutions such a decode needs.
 */
typedef struct pnj2k_reach {
  /**
   * `sizeof(pnj2k_reach)`.
   */
  size_t struct_size;
  /**
   * How many entries of `layers` mean anything: a decode can discard
   * fewer levels than this.
   */
  uint32_t levels;
  /**
   * Whether every packet of every tile was wholly present.
   */
  bool complete;
  /**
   * Written zero; reserved for a later field.
   */
  uint8_t reserved;
  /**
   * Complete layers for a decode that discards each number of levels.
   */
  uint16_t layers[PNJ2K_REACH_LEVELS];
} pnj2k_reach;

/**
 * What to decode, and into what. All zeros are the defaults: the whole
 * image, every component, at full resolution, in the format the components
 * suggest, top row first.
 */
typedef struct pnj2k_decode_params {
  /**
   * `sizeof(pnj2k_decode_params)`.
   */
  size_t struct_size;
  /**
   * How `pnj2k_decode` and `pnj2k_decode_planes` parse the data; NULL
   * for the defaults. A codestream's decodes keep its own.
   */
  const struct pnj2k_parse_params *parse;
  /**
   * A token that, once raised, ends the decode with `PNJ2K_CANCELLED`;
   * NULL for none. It is checked between code-blocks and between
   * transform levels, and must stay valid until the decode returns.
   */
  const struct pnj2k_cancel *cancel;
  /**
   * Resolution levels to discard: each side is halved that many times,
   * rounding up.
   */
  uint32_t reduce;
  /**
   * The first component to decode.
   */
  uint32_t first_component;
  /**
   * How many components to decode; 0 for every one from
   * `first_component`. Components a colour transform joins to them are
   * decoded as well, but not output.
   */
  uint32_t component_count;
  /**
   * A `pnj2k_pixel_format`; `PNJ2K_FORMAT_AUTO` for the one the
   * components suggest.
   */
  uint32_t format;
  /**
   * `PNJ2K_FLIP_ROWS`, `PNJ2K_FLOATING_POINT`, `PNJ2K_FLOAT_PLANES`,
   * `PNJ2K_SRGB`, `PNJ2K_UPSAMPLE` and `PNJ2K_INTERPOLATE`, or'd. Any
   * other bit, an encode flag's included, is `PNJ2K_INVALID_ARGUMENT`.
   */
  uint32_t flags;
  /**
   * The most quality layers to decode; 0 for all.
   */
  uint32_t layers;
  /**
   * The left column of the rectangle to decode, of the image as
   * decoded after `reduce`, in the first component's samples: the
   * pixels wherever the components are of one size, or where the
   * decode upsamples. Tiles it misses are not read, and the output is
   * the rectangle, clipped to the image.
   */
  uint32_t region_x;
  /**
   * The rectangle's top row.
   */
  uint32_t region_y;
  /**
   * The rectangle's width; with `region_height`, 0 for the whole image.
   */
  uint32_t region_width;
  /**
   * The rectangle's height; with `region_width`, 0 for the whole image.
   */
  uint32_t region_height;
  /**
   * Number of decode threads, including the caller. Other threads
   * decode blocks ahead of the transform. Output, warnings and errors
   * match a decode on the caller alone.
   *
   * 0 or 1 uses only the caller. The count is capped at the machine's
   * available parallelism. Threads start for each decode unless the
   * `parallel` feature is enabled, which retains a pool sized for the
   * last decode.
   */
  uint32_t threads;
  /**
   * Zero.
   */
  uint32_t reserved;
} pnj2k_decode_params;

/**
 * Frees memory the matching allocation function returned.
 *
 * It is given the size and alignment the allocation was asked for. `size`
 * is never 0. It runs on whichever thread frees the image, planes or
 * buffer, and must not unwind. It must not call a pnj2k function on the
 * decoder, encoder or planes whose call it runs in.
 */
typedef void (*pnj2k_free_fn)(void *user, void *ptr, size_t size, size_t align);

/**
 * An image's pixels: rows of interleaved samples `stride` bytes apart.
 *
 * To decode into a buffer of its own, the caller sets `pixels`, `size` and,
 * unless rows are packed, `stride`. To have the decoder allocate the
 * pixels, it leaves `pixels` NULL, and frees them with `pnj2k_image_free`,
 * which needs no decoder. An image holding pixels the decoder allocated
 * is freed before a decode or resize fills it again.
 */
typedef struct pnj2k_image {
  /**
   * `sizeof(pnj2k_image)`.
   */
  size_t struct_size;
  /**
   * The first row's first sample, aligned to 2 for a 16-bit format; NULL
   * on input for the decoder to allocate the pixels.
   */
  uint8_t *pixels;
  /**
   * Buffer size in bytes. Caller-owned buffers keep their supplied size
   * after decoding. For decoder-allocated pixels, this is the image's
   * byte count. After `pnj2k_codestream_image_info`, it is the size needed
   * to decode with the same parameters. `pnj2k_image_free` frees
   * `capacity` bytes and does not read this field.
   */
  size_t size;
  /**
   * Bytes from one row to the next, even for a 16-bit format; 0 on input
   * for rows packed together. `pnj2k_image_free` clears it with the
   * pixels it frees.
   */
  size_t stride;
  /**
   * Width in pixels.
   */
  uint32_t width;
  /**
   * Height in pixels.
   */
  uint32_t height;
  /**
   * The `pnj2k_pixel_format` written.
   */
  uint32_t format;
  /**
   * Must be zero, and is written zero; reserved for a later field.
   */
  uint32_t reserved;
  /**
   * Where the decoder allocated `pixels`, the function that frees them,
   * which `pnj2k_image_free` calls; NULL where the pixels are the
   * caller's. A decode or resize refuses an image whose `pixels` and
   * `release` are both set, with `PNJ2K_INVALID_ARGUMENT`, until
   * `pnj2k_image_free` frees them.
   */
  pnj2k_free_fn release;
  /**
   * The value `release` takes first.
   */
  void *release_user;
  /**
   * The library's: where the decoder allocated `pixels`, the size of the
   * allocation, which `pnj2k_image_free` gives `release`; 0 otherwise.
   * Only a decode and `pnj2k_image_free` write it, and the caller never
   * does.
   */
  size_t capacity;
} pnj2k_image;

/**
 * Allocates `size` bytes aligned to `align`, a power of two, or returns NULL.
 *
 * `size` is never 0. It runs on the thread of the call that allocates,
 * and must not unwind. It must not call a pnj2k function on the decoder or
 * encoder whose call it runs in.
 */
typedef void *(*pnj2k_alloc_fn)(void *user, size_t size, size_t align);

/**
 * Where a decoder or encoder takes the memory it hands to the caller: the
 * pixels of an image a decoder allocates, the samples of planes, and an
 * encoder's codestream bytes.
 *
 * Its working memory comes from the system allocator. The decoder or
 * encoder keeps a copy of the struct; its functions and `user` must work
 * for as long as the decoder or encoder, or anything it allocated, lives.
 */
typedef struct pnj2k_allocator {
  /**
   * `sizeof(pnj2k_allocator)`.
   */
  size_t struct_size;
  /**
   * Allocates; must not be NULL.
   */
  pnj2k_alloc_fn allocate;
  /**
   * Frees; must not be NULL.
   */
  pnj2k_free_fn deallocate;
  /**
   * Passed to both.
   */
  void *user;
} pnj2k_allocator;

/**
 * Something a parse or decode worked around.
 */
typedef struct pnj2k_warning {
  /**
   * `sizeof(pnj2k_warning)`.
   */
  size_t struct_size;
  /**
   * A description, valid until the next parse, extend, reach, image info
   * or decode on the decoder, or its free.
   */
  const char *message;
  /**
   * The byte offset the kind describes; 0 where it has none.
   */
  uint64_t offset;
  /**
   * A `pnj2k_warning_kind`.
   */
  uint32_t kind;
  /**
   * The marker code the kind describes, 0xFF00 to 0xFFFF, or the JP2
   * box type, its four bytes as a big-endian number; 0 where it has
   * none.
   */
  uint32_t code;
  /**
   * The tile the kind describes; 0 where it has none.
   */
  uint32_t tile;
  /**
   * Written zero; reserved for a later field.
   */
  uint32_t reserved;
} pnj2k_warning;

/**
 * One component's samples, at its own size, precision and signedness.
 */
typedef struct pnj2k_plane {
  /**
   * `sizeof(pnj2k_plane)`.
   */
  size_t struct_size;
  /**
   * The first row's first sample, of `sample_type`; NULL where the plane
   * is empty. Valid until the planes are freed.
   */
  const void *samples;
  /**
   * Bytes from one row to the next.
   */
  size_t stride;
  /**
   * Width in samples.
   */
  uint32_t width;
  /**
   * Height in samples.
   */
  uint32_t height;
  /**
   * Where the plane starts on the component's own grid, at the
   * resolution decoded.
   */
  uint32_t x0;
  /**
   * Where the plane starts vertically.
   */
  uint32_t y0;
  /**
   * Bits per sample.
   */
  uint32_t precision;
  /**
   * A `pnj2k_sample_type`.
   */
  uint32_t sample_type;
  /**
   * Whether samples are signed.
   */
  bool is_signed;
  /**
   * Written zero; reserved for later fields.
   */
  uint8_t reserved[7];
} pnj2k_plane;

/**
 * An image to encode: rows of interleaved samples of 8 bits, or of 9 to
 * 16 bits in `uint16_t`.
 */
typedef struct pnj2k_source {
  /**
   * `sizeof(pnj2k_source)`.
   */
  size_t struct_size;
  /**
   * The first row's first sample.
   */
  const uint8_t *pixels;
  /**
   * The buffer's size in bytes: at least `stride * (height - 1)` plus a
   * row's bytes.
   */
  size_t size;
  /**
   * Bytes from one row to the next, at least a row's; 0 for rows packed
   * together.
   */
  size_t stride;
  /**
   * Width in pixels.
   */
  uint32_t width;
  /**
   * Height in pixels.
   */
  uint32_t height;
  /**
   * Interleaved channels, 1 to 16.
   */
  uint32_t channels;
  /**
   * The samples' bits: 0 or 8 for rows of bytes; 9 to 16 for rows of
   * `uint16_t` in the machine's byte order, each sample below `1 <<
   * bits`, which an encode takes as one component of that precision a
   * channel. Resizing and filling under alpha take 8-bit rows only, and
   * an opaque alpha channel of deeper rows is kept.
   */
  uint32_t bits;
} pnj2k_source;

/**
 * A progression order change (POC).
 *
 * It gives the packets of the layers below `layer_end`, the resolutions
 * from `resolution_start` up to `resolution_end` and the components from
 * `component_start` up to `component_end`, in `progression`'s order.
 */
typedef struct pnj2k_progression_change {
  /**
   * `sizeof(pnj2k_progression_change)`.
   */
  size_t struct_size;
  /**
   * A `pnj2k_progression`.
   */
  uint32_t progression;
  /**
   * The first resolution, 0 to 32.
   */
  uint32_t resolution_start;
  /**
   * The resolution past the last, up to 33.
   */
  uint32_t resolution_end;
  /**
   * The first component.
   */
  uint32_t component_start;
  /**
   * The component past the last, up to 16,384.
   */
  uint32_t component_end;
  /**
   * The layer past the last, up to 65,535.
   */
  uint32_t layer_end;
} pnj2k_progression_change;

/**
 * One quality layer's target.
 */
typedef struct pnj2k_layer {
  /**
   * `sizeof(pnj2k_layer)`.
   */
  size_t struct_size;
  /**
   * A `pnj2k_layer_kind`.
   */
  uint32_t kind;
  /**
   * Must be zero; reserved for a later field.
   */
  uint32_t reserved;
  /**
   * The target, as `kind` says.
   */
  double value;
} pnj2k_layer;

/**
 * One component's own coding parameters (COC, QCC), in place of the
 * image's where `fields` names them.
 */
typedef struct pnj2k_component_params {
  /**
   * `sizeof(pnj2k_component_params)`.
   */
  size_t struct_size;
  /**
   * The `PNJ2K_COMP_*` members set; the rest are the image's.
   */
  uint64_t fields;
  /**
   * The component.
   */
  uint32_t component;
  /**
   * Decomposition levels, 0 to 32.
   */
  uint32_t levels;
  /**
   * Non-zero for the reversible 5/3 wavelet, zero for the 9/7.
   */
  uint32_t reversible;
  /**
   * Code-block width, a power of two from 4 to 1024.
   */
  uint32_t block_width;
  /**
   * Code-block height, likewise, at most 4096 samples a block.
   */
  uint32_t block_height;
  /**
   * `PNJ2K_BLOCK_*` flags, or'd.
   */
  uint32_t block_style;
  /**
   * A `pnj2k_quantisation_style`.
   */
  uint32_t quantisation_style;
  /**
   * The fewest guard bits, 1 to 7.
   */
  uint32_t guard_bits;
  /**
   * The base quantisation step, as a fraction of the component's range.
   */
  double step;
  /**
   * Precinct exponents, width then height, one pair a resolution from
   * the lowest, the last repeated; `2 * precinct_count` bytes.
   */
  const uint8_t *precincts;
  /**
   * The pairs `precincts` holds.
   */
  size_t precinct_count;
} pnj2k_component_params;

/**
 * A region of interest, coded first (T.800 Annex H): rectangles on the
 * reference grid, a mask over the image, or both.
 */
typedef struct pnj2k_roi {
  /**
   * `sizeof(pnj2k_roi)`.
   */
  size_t struct_size;
  /**
   * Rectangles, each `x0`, `y0`, width and height on the reference
   * grid; `4 * rect_count` values.
   */
  const uint32_t *rects;
  /**
   * The rectangles `rects` holds.
   */
  size_t rect_count;
  /**
   * A mask over the image's area, row after row, non-zero inside; NULL
   * for none.
   */
  const uint8_t *mask;
  /**
   * The mask's width, the image's.
   */
  uint32_t mask_width;
  /**
   * The mask's height, the image's.
   */
  uint32_t mask_height;
  /**
   * The components the region applies to; NULL for every one.
   */
  const uint16_t *components;
  /**
   * The components `components` holds.
   */
  size_t component_count;
} pnj2k_roi;

/**
 * The codestream's parameters, past the preset's. Each member counts
 * only where `fields` names it, so that 0 never has to mean the default
 * where 0 is a value.
 */
typedef struct pnj2k_codestream_params {
  /**
   * `sizeof(pnj2k_codestream_params)`.
   */
  size_t struct_size;
  /**
   * The `PNJ2K_CS_*` members set; the rest are the preset's.
   */
  uint64_t fields;
  /**
   * Tile width on the reference grid; 0, with `tile_height` 0, for one
   * tile.
   */
  uint32_t tile_width;
  /**
   * Tile height.
   */
  uint32_t tile_height;
  /**
   * The tile grid's origin, at or before the image's.
   */
  uint32_t tile_x0;
  /**
   * See `tile_x0`.
   */
  uint32_t tile_y0;
  /**
   * A `pnj2k_tile_parts`.
   */
  uint32_t tile_parts;
  /**
   * A `pnj2k_tile_allocation`.
   */
  uint32_t tile_allocation;
  /**
   * Decomposition levels, 0 to 32.
   */
  uint32_t levels;
  /**
   * Non-zero for the colour transform over the first three components,
   * zero for none.
   */
  uint32_t colour_transform;
  /**
   * Code-block width, a power of two from 4 to 1024.
   */
  uint32_t block_width;
  /**
   * Code-block height, likewise, at most 4096 samples a block.
   */
  uint32_t block_height;
  /**
   * `PNJ2K_BLOCK_*` flags, or'd.
   */
  uint32_t block_style;
  /**
   * A `pnj2k_progression`.
   */
  uint32_t progression;
  /**
   * A `pnj2k_quantisation_style`.
   */
  uint32_t quantisation_style;
  /**
   * The fewest guard bits, 1 to 7.
   */
  uint32_t guard_bits;
  /**
   * `PNJ2K_MARKER_*` flags, or'd.
   */
  uint32_t markers;
  /**
   * A `pnj2k_profile`.
   */
  uint32_t profile;
  /**
   * The base quantisation step, as a fraction of each component's range.
   */
  double step;
  /**
   * Precinct exponents, width then height, one pair a resolution from
   * the lowest, the last repeated; `2 * precinct_count` bytes.
   */
  const uint8_t *precincts;
  /**
   * The pairs `precincts` holds.
   */
  size_t precinct_count;
  /**
   * Progression order changes, in place of `progression`.
   */
  const struct pnj2k_progression_change *progression_changes;
  /**
   * The changes `progression_changes` holds.
   */
  size_t progression_change_count;
  /**
   * The quality layers, one target each, 1 to 65,535.
   */
  const struct pnj2k_layer *layers;
  /**
   * The layers `layers` holds.
   */
  size_t layer_count;
  /**
   * Components' own parameters, each component once.
   */
  const struct pnj2k_component_params *components;
  /**
   * The components `components` holds.
   */
  size_t component_count;
  /**
   * A region of interest; NULL for none.
   */
  const struct pnj2k_roi *roi;
  /**
   * Comments, written after `pnj2k_encode_params.comment`: each
   * `pnj2k_comment`'s `data` and `len` its bytes and `registration` 0
   * for binary data or 1 for UTF-8 text, written in ISO/IEC 8859-15.
   */
  const struct pnj2k_comment *comments;
  /**
   * The comments `comments` holds.
   */
  size_t comment_count;
  /**
   * Component registration offsets (CRG), x then y in 65,536ths of a
   * sample, one pair a component; `2 * registration_count` values.
   */
  const uint16_t *registration;
  /**
   * The pairs `registration` holds.
   */
  size_t registration_count;
} pnj2k_codestream_params;

/**
 * One channel of a JP2 file's component mapping.
 */
typedef struct pnj2k_channel_mapping {
  /**
   * `sizeof(pnj2k_channel_mapping)`.
   */
  size_t struct_size;
  /**
   * The codestream component.
   */
  uint32_t component;
  /**
   * The palette column the component indexes, or -1 for the component
   * as it is.
   */
  int32_t palette_column;
} pnj2k_channel_mapping;

/**
 * One entry of a JP2 file's channel definitions.
 */
typedef struct pnj2k_channel_definition {
  /**
   * `sizeof(pnj2k_channel_definition)`.
   */
  size_t struct_size;
  /**
   * The channel.
   */
  uint32_t channel;
  /**
   * 0 colour, 1 opacity, 2 premultiplied opacity, 65535 unspecified.
   */
  uint32_t kind;
  /**
   * The colour it is associated with, 1-based; 0 the whole image,
   * 65535 none.
   */
  uint32_t association;
  /**
   * Must be zero; reserved for a later field.
   */
  uint32_t reserved;
} pnj2k_channel_definition;

/**
 * A grid resolution: each axis's numerator, denominator and power of
 * ten, (N / D) * 10^E reference grid points per metre.
 */
typedef struct pnj2k_resolution {
  /**
   * `sizeof(pnj2k_resolution)`.
   */
  size_t struct_size;
  /**
   * The vertical numerator, up to 65,535.
   */
  uint32_t vertical_numerator;
  /**
   * The vertical denominator, 1 to 65,535.
   */
  uint32_t vertical_denominator;
  /**
   * The vertical exponent, -128 to 127.
   */
  int32_t vertical_exponent;
  /**
   * The horizontal numerator.
   */
  uint32_t horizontal_numerator;
  /**
   * The horizontal denominator.
   */
  uint32_t horizontal_denominator;
  /**
   * The horizontal exponent.
   */
  int32_t horizontal_exponent;
} pnj2k_resolution;

/**
 * The header of a JP2 file to write around the codestream.
 *
 * A zeroed struct takes the default colour specification, sRGB for three colour
 * channels or more and greyscale for fewer, defines the channels of an
 * image that has more of them than its colour space has colours, the
 * colours in order and the rest of unspecified type (T.800 I.5.3.6), and
 * writes nothing more.
 */
typedef struct pnj2k_jp2_params {
  /**
   * `sizeof(pnj2k_jp2_params)`.
   */
  size_t struct_size;
  /**
   * `PNJ2K_JP2_COLOURSPACE_UNKNOWN`, or 0.
   */
  uint32_t flags;
  /**
   * An enumerated colour space, 16 sRGB, 17 greyscale or 18 sYCC; 0 for
   * the default, or for `icc`.
   */
  uint32_t enumerated;
  /**
   * A restricted ICC profile's bytes, in place of `enumerated`; NULL for
   * none.
   */
  const uint8_t *icc;
  /**
   * The bytes `icc` holds.
   */
  size_t icc_len;
  /**
   * The palette's columns, each a precision less 1 in the low seven bits
   * with the high bit set where signed, as the palette box writes them;
   * NULL for no palette.
   */
  const uint8_t *palette_columns;
  /**
   * The columns `palette_columns` holds, 1 to 255.
   */
  size_t palette_column_count;
  /**
   * The palette's values, entry by entry, as many entries as there are
   * whole rows of columns.
   */
  const int64_t *palette_values;
  /**
   * The values `palette_values` holds.
   */
  size_t palette_value_count;
  /**
   * How the channels come from the components, with a palette.
   */
  const struct pnj2k_channel_mapping *mapping;
  /**
   * The channels `mapping` holds.
   */
  size_t mapping_count;
  /**
   * Channel definitions, or NULL if none are supplied. Undefined
   * channels are assigned unclaimed colours in order, so the written box
   * defines every channel. Supplying only the alpha definition is enough.
   */
  const struct pnj2k_channel_definition *channels;
  /**
   * The definitions `channels` holds.
   */
  size_t channel_count;
  /**
   * The resolution the image was captured at; NULL for none.
   */
  const struct pnj2k_resolution *capture_resolution;
  /**
   * The resolution it is meant to be displayed at; NULL for none.
   */
  const struct pnj2k_resolution *display_resolution;
} pnj2k_jp2_params;

/**
 * How to encode.
 *
 * All zeros but `struct_size` are Part 1's defaults
 * (`PNJ2K_PRESET_PART1`): one tile, five levels of the 9/7 wavelet, the
 * colour transform where three colour components allow it, one layer of
 * every pass, a raw codestream. `preset` set to `PNJ2K_PRESET_SECOND_LIFE`
 * takes Second Life's upload profile instead: layers of 600 and 2000
 * bytes and four times more up to an eighth of the raw image. The other
 * fields, `codestream` and `jp2` change the preset's parameters.
 */
typedef struct pnj2k_encode_params {
  /**
   * `sizeof(pnj2k_encode_params)`.
   */
  size_t struct_size;
  /**
   * The text of a comment (COM) marker segment, NUL-terminated UTF-8;
   * NULL for none. It is written in ISO/IEC 8859-15 (Latin-9), as its
   * registration says: text with a character Latin-9 does not have, or
   * of more than 65,531 characters, is `PNJ2K_INVALID_REQUEST`, and
   * bytes that are not UTF-8 are `PNJ2K_INVALID_ARGUMENT`.
   */
  const char *comment;
  /**
   * `PNJ2K_ENCODE_REVERSIBLE`, `PNJ2K_ENCODE_FLIP_ROWS`,
   * `PNJ2K_ENCODE_PACKET_LENGTHS`, `PNJ2K_ENCODE_FILL_TRANSPARENT` and
   * `PNJ2K_ENCODE_NO_QUALITY_CAP`, or'd. Any other bit, a decode flag's
   * included, is `PNJ2K_INVALID_ARGUMENT`.
   */
  uint32_t flags;
  /**
   * A `pnj2k_content`.
   */
  uint32_t content;
  /**
   * A `pnj2k_weighting`.
   */
  uint32_t weighting;
  /**
   * Texels per degree of view, above 0, for `PNJ2K_WEIGHTING_DISTANCE`.
   */
  float pixels_per_degree;
  /**
   * The size to resize the image to before coding; 0 and 0 to keep it.
   */
  uint32_t resize_width;
  /**
   * See `resize_width`.
   */
  uint32_t resize_height;
  /**
   * The last lossy layer's size as a fraction of the raw image's bytes,
   * above 0 and at most 1, a layer as large as the raw image; 0 for an
   * eighth. A fraction worth fewer than 600 bytes gives one lossy layer
   * of 600 bytes. Any other value is `PNJ2K_INVALID_ARGUMENT`.
   */
  float rate;
  /**
   * The mean squared error per sample past which no more passes are
   * added, in 8-bit code values, above 0; 0 for the default, so that a
   * zeroed struct takes it. A cap of 0 would add every pass, which
   * `PNJ2K_ENCODE_NO_QUALITY_CAP` asks for. Any other value is
   * `PNJ2K_INVALID_ARGUMENT`. Ignored under `PNJ2K_ENCODE_NO_QUALITY_CAP`
   * and when lossless.
   */
  float quality_cap;
  /**
   * A `pnj2k_preset`: the parameters the rest change.
   */
  uint32_t preset;
  /**
   * Must be zero; reserved for a later field.
   */
  uint32_t reserved;
  /**
   * The codestream's parameters, past the preset's; NULL for the
   * preset's.
   */
  const struct pnj2k_codestream_params *codestream;
  /**
   * The header of a JP2 file to write around the codestream; NULL for a
   * raw codestream.
   */
  const struct pnj2k_jp2_params *jp2;
} pnj2k_encode_params;

/**
 * Bytes an encode made, in memory from the encoder's allocator.
 *
 * They are freed with `pnj2k_buffer_free`, which needs no encoder. The
 * caller zeroes the struct but for `struct_size` before an encode fills
 * it, and frees it before another encode fills it again.
 */
typedef struct pnj2k_buffer {
  /**
   * `sizeof(pnj2k_buffer)`.
   */
  size_t struct_size;
  /**
   * The bytes; NULL where there are none.
   */
  uint8_t *data;
  /**
   * The number of bytes.
   */
  size_t len;
  /**
   * The library's: the size allocated, which `pnj2k_buffer_free` gives
   * `release`.
   */
  size_t capacity;
  /**
   * The function that frees `data`, which `pnj2k_buffer_free` calls.
   */
  pnj2k_free_fn release;
  /**
   * The value `release` takes first.
   */
  void *release_user;
} pnj2k_buffer;

/**
 * One component of a planar image to encode, at its own size.
 */
typedef struct pnj2k_component_source {
  /**
   * `sizeof(pnj2k_component_source)`.
   */
  size_t struct_size;
  /**
   * The first row's first sample.
   */
  const void *samples;
  /**
   * The buffer's size in bytes.
   */
  size_t size;
  /**
   * Bytes from one row to the next, a whole number of samples and at
   * least a row's; 0 for rows packed together.
   */
  size_t stride;
  /**
   * A `pnj2k_source_samples`.
   */
  uint32_t sample_type;
  /**
   * Width in samples.
   */
  uint32_t width;
  /**
   * Height in samples.
   */
  uint32_t height;
  /**
   * Bits per sample, 1 to 38.
   */
  uint32_t precision;
  /**
   * Non-zero where the samples are signed.
   */
  uint32_t is_signed;
  /**
   * Horizontal subsampling: one sample every `dx` reference grid points,
   * 1 to 255; 0 for 1.
   */
  uint32_t dx;
  /**
   * Vertical subsampling, likewise.
   */
  uint32_t dy;
  /**
   * Must be zero; reserved for a later field.
   */
  uint32_t reserved;
} pnj2k_component_source;

/**
 * A planar image to encode: its area on the reference grid and its
 * components, each sized from the area as T.800 B.2 has it.
 */
typedef struct pnj2k_components {
  /**
   * `sizeof(pnj2k_components)`.
   */
  size_t struct_size;
  /**
   * The image's width on the reference grid.
   */
  uint32_t width;
  /**
   * The image's height on the reference grid.
   */
  uint32_t height;
  /**
   * The image's origin on the reference grid.
   */
  uint32_t x0;
  /**
   * See `x0`.
   */
  uint32_t y0;
  /**
   * The components, 1 to 16,384.
   */
  const struct pnj2k_component_source *components;
  /**
   * The components `components` holds.
   */
  size_t count;
} pnj2k_components;

/**
 * It succeeded.
 */
#define PNJ2K_OK 0

/**
 * The data is neither a JPEG 2000 codestream nor a JP2 file.
 */
#define PNJ2K_NOT_JPEG2000 1

/**
 * The data ends before the main header does, so there is nothing to decode.
 */
#define PNJ2K_TRUNCATED 2

/**
 * A header holds a value the standard does not allow.
 */
#define PNJ2K_INVALID_DATA 3

/**
 * The codestream needs something outside Part 1, or outside what pnj2k
 * decodes.
 */
#define PNJ2K_UNSUPPORTED 4

/**
 * The image is larger than the limits allow.
 *
 * Or the pixels or planes the decoder would allocate for the caller are
 * more bytes than `max_decoded_bytes`: pixels in rows the stride asked
 * for apart, or planes copied into the caller's allocator's memory while
 * the planes decoded are still held. Nothing is asked of the allocator.
 */
#define PNJ2K_LIMIT_EXCEEDED 5

/**
 * The request cannot be met for this codestream or image.
 *
 * For a codestream: components it does not have, more levels discarded
 * than it has, a pixel format that does not fit them. Or an image the
 * encoder or a resize cannot take: no samples, a
 * side over 32768, other than 1 to 16 channels, a source buffer or stride
 * too short, a comment over 65531 bytes, a size to resize to out of range
 * or of more pixels than 64 times the image's and 2048 by 2048 together.
 * Or encode parameters that ask more of the encoder than the image allows:
 * packets, or its tiles' resolutions and subbands, past four for each of
 * its samples and 2^20 more; tile layouts of code-blocks, precincts and
 * tag trees past 16 bytes a sample and 64 MiB more; sizings of a
 * code-block or a packet by its layers' allocation past 16 a sample and
 * 2^24 more, which may be found only once the image is coded; or a region
 * of interest's rectangles, over the tile-components it applies to, past
 * as many.
 */
#define PNJ2K_INVALID_REQUEST 6

/**
 * The data is not the data the codestream was parsed from.
 *
 * The data handed to a call on a codestream must be that data or a longer
 * prefix of it: this data is shorter, or its first bytes, as many as were
 * parsed, differ from them. They are compared by a 64-bit digest of them
 * all, so data that differs is taken for the same only by a chance of
 * about one in 2^64.
 */
#define PNJ2K_DATA_MISMATCH 7

/**
 * Strict mode met something lenient mode works around.
 */
#define PNJ2K_STRICT 8

/**
 * The cancellation token was raised.
 */
#define PNJ2K_CANCELLED 9

/**
 * An argument is wrong.
 *
 * A NULL pointer, a `struct_size` this library does not take, a reserved
 * field that is not zero, a value outside its enumeration, unknown flags,
 * a stride whose rows run past any buffer, a struct still holding memory
 * the library allocated.
 */
#define PNJ2K_INVALID_ARGUMENT 10

/**
 * An output buffer is smaller than the image, or its stride shorter than
 * a row. A source buffer that is too short is `PNJ2K_INVALID_REQUEST`.
 */
#define PNJ2K_BUFFER_TOO_SMALL 11

/**
 * An allocator returned NULL: the caller's, or the system's for memory
 * handed to the caller, for a decode's samples, working planes and tile
 * layouts, or for an encode's or a resize's working memory.
 */
#define PNJ2K_OUT_OF_MEMORY 12

/**
 * A bug in pnj2k: a panic, caught at the boundary.
 *
 * The decoder or encoder it happened in has been reset and can be used
 * again. After one inside
 * `pnj2k_codestream_extend`, the codestream keeps the headers it had
 * before, and its decodes read its packets again.
 */
#define PNJ2K_INTERNAL 13

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * Returns the ABI version this library was built with.
 *
 * A program compares it with the `PNJ2K_ABI_VERSION` its header defines,
 * and refuses to use a library built for another.
 */
uint32_t pnj2k_abi_version(void);

/**
 * Returns the library's version, `major.minor.patch`, as a static string.
 */
const char *pnj2k_version(void);

/**
 * Reads a codestream's or JP2 file's size, components, levels and layers.
 *
 * Nothing is allocated or decoded. Only the main header is read, up to
 * its coding style (COD) marker segment, so a prefix holding that much is
 * enough. Takes no decoder, and so keeps no message:
 * `pnj2k_status_message` describes the status.
 *
 * # Safety
 *
 * `data` points to `len` readable bytes, or is NULL with `len` 0; `info`
 * is NULL or points to a `pnj2k_info` whose `struct_size` is set.
 */
pnj2k_status pnj2k_probe(const uint8_t *data, size_t len, struct pnj2k_info *info);

/**
 * Reads a main header as `pnj2k_probe` does, within `params`' limits.
 *
 * An image larger than `params` allows is refused as
 * `pnj2k_codestream_parse` refuses it, so that a probe ahead of a parse
 * with raised limits takes what the parse takes. `params` may be NULL for
 * the default limits; its mode is checked as a parse checks it, and
 * changes nothing, a probe warning of nothing.
 *
 * # Safety
 *
 * As `pnj2k_probe`; `params` is NULL or points to a `pnj2k_parse_params`
 * whose `struct_size` is set.
 */
pnj2k_status pnj2k_probe_with_limits(const uint8_t *data,
                                     size_t len,
                                     const struct pnj2k_parse_params *params,
                                     struct pnj2k_info *info);

/**
 * Parses a raw codestream or a JP2 file.
 *
 * The parse reads the main header, and where each tile-part's packets
 * lie. `params` may be NULL for lenient mode and the default limits,
 * which its decodes keep. `*out` is set to the codestream, or to NULL on
 * every failure; free it with `pnj2k_codestream_free`.
 *
 * `dec` keeps the failure's message and the parse's warnings.
 *
 * # Safety
 *
 * `dec` is NULL or a decoder from `pnj2k_decoder_new` that no other thread
 * is using; `data` points to `len` readable bytes, or is NULL with `len`
 * 0; `params` is NULL or points to a `pnj2k_parse_params` whose
 * `struct_size` is set; `out` is NULL or points to a writable
 * `pnj2k_codestream*`.
 */
pnj2k_status pnj2k_codestream_parse(struct pnj2k_decoder *dec,
                                    const uint8_t *data,
                                    size_t len,
                                    const struct pnj2k_parse_params *params,
                                    struct pnj2k_codestream **out);

/**
 * Frees a codestream, on any thread, once no other thread uses it. NULL
 * is ignored.
 *
 * # Safety
 *
 * `cs` is NULL or a codestream from `pnj2k_codestream_parse` that no
 * thread is using, not used again.
 */
void pnj2k_codestream_free(struct pnj2k_codestream *cs);

/**
 * Takes a longer prefix of the same data.
 *
 * Every header is parsed again, and every tile's packets are read,
 * carrying on from where the last call stopped. Decodes and reaches given
 * exactly this data then use the packets read here. Comments read from
 * the codestream before the call are no longer valid after it. On
 * failure, a panic's included, the codestream keeps the headers it had
 * before, though its decodes may read its packets again.
 *
 * `data` must be the data the codestream was parsed from, or a longer
 * prefix of it; data found to be otherwise gives `PNJ2K_DATA_MISMATCH`.
 *
 * # Safety
 *
 * As `pnj2k_codestream_parse`, `cs` a codestream from it that no other
 * thread is using.
 */
pnj2k_status pnj2k_codestream_extend(struct pnj2k_decoder *dec,
                                     struct pnj2k_codestream *cs,
                                     const uint8_t *data,
                                     size_t len);

/**
 * Fills `out` with what the codestream's main header says, and in
 * `max_reduce` the most levels a decode of it can discard. Keeps no
 * message.
 *
 * # Safety
 *
 * `cs` is NULL or a codestream from `pnj2k_codestream_parse`, not yet
 * freed, that no thread is extending; `out` is NULL or points to a
 * `pnj2k_info` whose `struct_size` is set.
 */
pnj2k_status pnj2k_codestream_info(const struct pnj2k_codestream *cs, struct pnj2k_info *out);

/**
 * Fills `out` with component `index` as the SIZ marker segment describes
 * it. Keeps no message.
 *
 * # Safety
 *
 * As `pnj2k_codestream_info`, `out` NULL or pointing to a
 * `pnj2k_component` whose `struct_size` is set.
 */
pnj2k_status pnj2k_codestream_component(const struct pnj2k_codestream *cs,
                                        size_t index,
                                        struct pnj2k_component *out);

/**
 * The number of comment (COM) marker segments; 0 for NULL.
 *
 * # Safety
 *
 * `cs` is NULL or a codestream from `pnj2k_codestream_parse`, not yet
 * freed, that no thread is extending.
 */
size_t pnj2k_codestream_comment_count(const struct pnj2k_codestream *cs);

/**
 * Fills `out` with comment `index`.
 *
 * Comments are numbered in the order they appear, the main header's
 * before the tile-parts'. The comment's bytes are valid until the
 * codestream is extended or freed. Keeps no message.
 *
 * # Safety
 *
 * As `pnj2k_codestream_info`, `out` NULL or pointing to a
 * `pnj2k_comment` whose `struct_size` is set.
 */
pnj2k_status pnj2k_codestream_comment(const struct pnj2k_codestream *cs,
                                      size_t index,
                                      struct pnj2k_comment *out);

/**
 * Fills `out` with how much of the codestream `data` holds.
 *
 * Nothing is decoded. For each number of discarded levels, `out` gives
 * the quality layers whose packets are wholly present. A decode of the
 * same data reports the same. `dec` keeps the failure's message.
 *
 * # Safety
 *
 * As `pnj2k_codestream_parse`, `cs` a codestream from it, not yet freed,
 * that no thread is extending, and `out` NULL or pointing to a
 * `pnj2k_reach` whose `struct_size` is set.
 */
pnj2k_status pnj2k_codestream_reach(struct pnj2k_decoder *dec,
                                    const struct pnj2k_codestream *cs,
                                    const uint8_t *data,
                                    size_t len,
                                    struct pnj2k_reach *out);

/**
 * Fills `out` with the shape a decode of `params` would give.
 *
 * Nothing is decoded. The call sets `out`'s width, height, format, stride
 * and size: `size` is the bytes a buffer of `stride` bytes per row needs,
 * `stride` packed where it was 0. `out`'s pixels, capacity, release and
 * `release_user` are left alone, so an image holding pixels the decoder
 * allocated still frees them. `dec` keeps the failure's message.
 *
 * # Safety
 *
 * As `pnj2k_codestream_reach`, with `params` NULL or pointing to a
 * `pnj2k_decode_params` whose `struct_size` is set and whose pointers are
 * NULL or valid, and `out` NULL or pointing to a `pnj2k_image` whose
 * `struct_size` is set.
 */
pnj2k_status pnj2k_codestream_image_info(struct pnj2k_decoder *dec,
                                         const struct pnj2k_codestream *cs,
                                         const struct pnj2k_decode_params *params,
                                         struct pnj2k_image *out);

/**
 * Decodes the codestream from `data`, the data it was parsed from or a
 * longer prefix of it, into `out`, as `pnj2k_decode` does.
 * `params->parse` is ignored: the codestream's mode and limits hold.
 *
 * # Safety
 *
 * As `pnj2k_decode`, `cs` a codestream from `pnj2k_codestream_parse`, not
 * yet freed, that no thread is extending.
 */
pnj2k_status pnj2k_codestream_decode(struct pnj2k_decoder *dec,
                                     const struct pnj2k_codestream *cs,
                                     const uint8_t *data,
                                     size_t len,
                                     const struct pnj2k_decode_params *params,
                                     struct pnj2k_image *out);

/**
 * Decodes the codestream from `data` into planes, as `pnj2k_decode_planes`
 * does: `*out` is set to the planes, or to NULL on every failure.
 * `params->parse` is ignored.
 *
 * # Safety
 *
 * As `pnj2k_decode_planes`, `cs` a codestream from
 * `pnj2k_codestream_parse`, not yet freed, that no thread is extending.
 */
pnj2k_status pnj2k_codestream_decode_planes(struct pnj2k_decoder *dec,
                                            const struct pnj2k_codestream *cs,
                                            const uint8_t *data,
                                            size_t len,
                                            const struct pnj2k_decode_params *params,
                                            struct pnj2k_planes **out);

/**
 * Makes a decoder.
 *
 * `alloc`, which may be NULL for the system allocator, provides the
 * memory the decoder hands to the caller: the pixels of images it
 * allocates and the samples of planes. The decoder keeps a copy of it, so
 * `*alloc` itself need not outlive the call.
 *
 * Returns NULL where `alloc` is malformed (a wrong `struct_size`, either
 * function NULL). Free the decoder with `pnj2k_decoder_free`.
 *
 * # Safety
 *
 * `alloc` is NULL or points to a `pnj2k_allocator` whose functions follow
 * its contract for as long as the decoder and what it allocated live.
 */
struct pnj2k_decoder *pnj2k_decoder_new(const struct pnj2k_allocator *alloc);

/**
 * Frees a decoder, and with it its message and warnings. Images and planes
 * it decoded, and codestreams it parsed, stay valid. NULL is ignored.
 *
 * # Safety
 *
 * `dec` is NULL or a decoder from `pnj2k_decoder_new`, not used again.
 */
void pnj2k_decoder_free(struct pnj2k_decoder *dec);

/**
 * The message of the last call on `dec` that failed.
 *
 * It is an empty string after a call that succeeded. It is valid until
 * the next parse, extend, reach, image info or decode on `dec`, or its
 * free. For NULL, the static string "the decoder is NULL": the failure of
 * a call given a NULL decoder.
 *
 * # Safety
 *
 * `dec` is NULL or a decoder from `pnj2k_decoder_new`.
 */
const char *pnj2k_decoder_last_error(const struct pnj2k_decoder *dec);

/**
 * A static description of a `pnj2k_status`, "unknown status" for a value
 * this library does not name. `pnj2k_decoder_last_error` and
 * `pnj2k_encoder_last_error` give the details of a failure.
 */
const char *pnj2k_status_message(uint32_t status);

/**
 * How many warnings the last successful parse, extend or decode on `dec`
 * reported: what lenient mode worked around. 0 after any other call on
 * `dec`, and for NULL.
 *
 * # Safety
 *
 * `dec` is NULL or a decoder from `pnj2k_decoder_new`.
 */
size_t pnj2k_decoder_warning_count(const struct pnj2k_decoder *dec);

/**
 * Fills `out` with warning `index` of the last successful call on `dec`.
 *
 * Its message is valid until the next parse, extend, reach, image info or
 * decode on `dec`, or its free. Keeps no message of its own failure.
 *
 * # Safety
 *
 * `dec` is NULL or a decoder from `pnj2k_decoder_new`; `out` is NULL or
 * points to a `pnj2k_warning` whose `struct_size` is set.
 */
pnj2k_status pnj2k_decoder_warning(const struct pnj2k_decoder *dec,
                                   size_t index,
                                   struct pnj2k_warning *out);

/**
 * Fills `out` with the reach of the last successful decode, or reach, on
 * `dec`. Fails with `PNJ2K_INVALID_ARGUMENT` where its last call was
 * neither. Keeps no message of its own failure.
 *
 * # Safety
 *
 * `dec` is NULL or a decoder from `pnj2k_decoder_new`; `out` is NULL or
 * points to a `pnj2k_reach` whose `struct_size` is set.
 */
pnj2k_status pnj2k_decoder_reach(const struct pnj2k_decoder *dec, struct pnj2k_reach *out);

/**
 * Makes a cancellation token, lowered. Never NULL; running out of memory
 * aborts. Free it with `pnj2k_cancel_free`.
 */
struct pnj2k_cancel *pnj2k_cancel_new(void);

/**
 * Raises `token`: decodes given it end with `PNJ2K_CANCELLED` at their
 * next check. Any thread may call it while they run. NULL is ignored.
 *
 * # Safety
 *
 * `token` is NULL or a token from `pnj2k_cancel_new`.
 */
void pnj2k_cancel_raise(const struct pnj2k_cancel *token);

/**
 * Lowers `token`, for decodes that follow. NULL is ignored.
 *
 * # Safety
 *
 * `token` is NULL or a token from `pnj2k_cancel_new`.
 */
void pnj2k_cancel_reset(const struct pnj2k_cancel *token);

/**
 * Whether `token` is raised; false for NULL.
 *
 * # Safety
 *
 * `token` is NULL or a token from `pnj2k_cancel_new`.
 */
bool pnj2k_cancel_is_raised(const struct pnj2k_cancel *token);

/**
 * Frees a token, on any thread, once no decode given it runs. NULL is
 * ignored.
 *
 * # Safety
 *
 * `token` is NULL or a token from `pnj2k_cancel_new` that no decode is
 * using, not used again.
 */
void pnj2k_cancel_free(struct pnj2k_cancel *token);

/**
 * Parses `data` and decodes it into `out` in one call: as
 * `pnj2k_codestream_parse` with `params->parse`, then
 * `pnj2k_codestream_decode`.
 *
 * `params` may be NULL for the defaults: the whole image, every component,
 * at full resolution, in the format its components suggest.
 *
 * To decode into a buffer of its own, the caller sets `out->pixels`,
 * `out->size` and, unless rows are packed, `out->stride`. With
 * `out->pixels` NULL, the decoder allocates the pixels from its allocator,
 * and `pnj2k_image_free` frees them. Pixels it allocates count toward
 * `max_decoded_bytes` in rows `out->stride` apart, and more of them are
 * `PNJ2K_LIMIT_EXCEEDED` before the allocator is asked; the bytes a stride
 * leaves between rows are zero. An image holding pixels the decoder
 * allocated (`pixels` and `release` set) is refused with
 * `PNJ2K_INVALID_ARGUMENT` until they are freed.
 *
 * The decode sets `out`'s width, height, format, stride, capacity,
 * release and release_user (NULL for the caller's buffer), and its pixels
 * and size where it allocated them. On failure `out` is unchanged, but the
 * caller's pixels may be partly written.
 *
 * # Safety
 *
 * `dec` is NULL or a decoder from `pnj2k_decoder_new` that no other
 * thread is using; `data` points to `len` readable bytes, or is NULL with
 * `len` 0; `params` is NULL or points to a `pnj2k_decode_params` whose
 * `struct_size` is set and whose pointers are NULL or valid; `out` is NULL
 * or points to a `pnj2k_image` whose `struct_size` is set, and whose
 * `pixels` are NULL or `size` writable bytes nothing else uses during the
 * call.
 */
pnj2k_status pnj2k_decode(struct pnj2k_decoder *dec,
                          const uint8_t *data,
                          size_t len,
                          const struct pnj2k_decode_params *params,
                          struct pnj2k_image *out);

/**
 * Frees the pixels a decode allocated.
 *
 * The pixels, `capacity` bytes, are freed through the function in
 * `image->release`, and `pixels`, `size`, `stride`, `capacity`, `release`
 * and `release_user` are cleared, so that the image can be handed to
 * another decode as it is, whatever that decode's row. Pixels in the
 * caller's own buffer are left alone, and only `release` and
 * `release_user` cleared. It needs no decoder, runs on any thread, and
 * ignores NULL and an image whose `struct_size` it does not take.
 *
 * # Safety
 *
 * `image` is NULL or points to a `pnj2k_image` a decode filled in, its
 * pixels not used again.
 */
void pnj2k_image_free(struct pnj2k_image *image);

/**
 * Parses `data` and decodes it into one plane per component.
 *
 * Each component keeps its precision and signedness, without scaling to a
 * pixel format. Planes use the component's size, or the image's size with
 * `PNJ2K_UPSAMPLE`. Samples of the reversible wavelet are exact, but for
 * those `PNJ2K_INTERPOLATE` makes between a component's own. The
 * irreversible wavelet of components of up to 8 bits runs in 16-bit fixed
 * point unless `PNJ2K_FLOATING_POINT` asks for floating point.
 * `*out` is set to the planes, or to NULL on every failure; free them with
 * `pnj2k_planes_free`.
 *
 * # Safety
 *
 * As `pnj2k_decode`, `out` NULL or pointing to a writable
 * `pnj2k_planes*`.
 */
pnj2k_status pnj2k_decode_planes(struct pnj2k_decoder *dec,
                                 const uint8_t *data,
                                 size_t len,
                                 const struct pnj2k_decode_params *params,
                                 struct pnj2k_planes **out);

/**
 * The number of planes; 0 for NULL.
 *
 * # Safety
 *
 * `planes` is NULL or planes from a decode, not yet freed.
 */
size_t pnj2k_planes_count(const struct pnj2k_planes *planes);

/**
 * Fills `out` with plane `index`, whose samples are valid until the
 * planes are freed. Any number of threads may call it at once.
 *
 * # Safety
 *
 * `planes` is NULL or planes from a decode, not yet freed; `out` is NULL
 * or points to a `pnj2k_plane` whose `struct_size` is set.
 */
pnj2k_status pnj2k_planes_get(const struct pnj2k_planes *planes,
                              size_t index,
                              struct pnj2k_plane *out);

/**
 * Frees planes and their samples, through the decoder's allocator. It
 * needs no decoder and runs on any thread, once no other thread reads the
 * planes. NULL is ignored.
 *
 * # Safety
 *
 * `planes` is NULL or planes from a decode, not used again.
 */
void pnj2k_planes_free(struct pnj2k_planes *planes);

/**
 * Makes an encoder.
 *
 * `alloc`, which may be NULL for the system allocator, provides the
 * memory of the codestreams it hands to the caller. The encoder keeps a
 * copy of it, so `*alloc` itself need not outlive the call.
 *
 * Returns NULL where `alloc` is malformed (a wrong `struct_size`, either
 * function NULL). Free the encoder with `pnj2k_encoder_free`.
 *
 * # Safety
 *
 * `alloc` is NULL or points to a `pnj2k_allocator` whose functions follow
 * its contract for as long as the encoder and what it allocated live.
 */
struct pnj2k_encoder *pnj2k_encoder_new(const struct pnj2k_allocator *alloc);

/**
 * Frees an encoder, and with it its message. Codestreams it made stay
 * valid. NULL is ignored.
 *
 * # Safety
 *
 * `enc` is NULL or an encoder from `pnj2k_encoder_new`, not used again.
 */
void pnj2k_encoder_free(struct pnj2k_encoder *enc);

/**
 * The message of the last call on `enc` that failed.
 *
 * It is an empty string after a call that succeeded. It is valid until
 * the next encode or resize on `enc`, or its free. For NULL, the static
 * string "the encoder is NULL": the failure of a call given a NULL
 * encoder.
 *
 * # Safety
 *
 * `enc` is NULL or an encoder from `pnj2k_encoder_new`.
 */
const char *pnj2k_encoder_last_error(const struct pnj2k_encoder *enc);

/**
 * Encodes `image` with `params` and writes the codestream to `out`.
 *
 * NULL `params` selects the defaults. The encoder's allocator supplies
 * the buffer, which `pnj2k_buffer_free` releases. Sets `data`, `len`,
 * `capacity`, `release` and `release_user`. `enc` stores any error message.
 *
 * `out` is checked first. If both `data` and `release` are set, it still
 * holds library-owned bytes and the call returns `PNJ2K_INVALID_ARGUMENT`.
 * Free the buffer with `pnj2k_buffer_free` before encoding again.
 * Failure leaves `out` unchanged.
 *
 * # Safety
 *
 * `enc` is NULL or an encoder from `pnj2k_encoder_new` that no other
 * thread is using; `image` is NULL or points to a `pnj2k_source` whose
 * `struct_size` is set and whose `pixels` are `size` readable bytes;
 * `params` is NULL or points to a `pnj2k_encode_params` whose
 * `struct_size` is set and whose `comment` is NULL or NUL-terminated; `out`
 * is NULL or points to a `pnj2k_buffer` whose `struct_size` is set.
 */
pnj2k_status pnj2k_encode(struct pnj2k_encoder *enc,
                          const struct pnj2k_source *image,
                          const struct pnj2k_encode_params *params,
                          struct pnj2k_buffer *out);

/**
 * Encodes planar `image` with `params` and returns the codestream in `out`.
 *
 * The allocation rules are those of `pnj2k_encode`. NULL `params` selects
 * the defaults. Components use their own `sample_type`, dimensions,
 * precision and subsampling.
 *
 * Planar input cannot be resized or filled under alpha. Those requests
 * return `PNJ2K_INVALID_REQUEST`. Planar input always keeps opaque alpha
 * channels, including with the Second Life preset.
 *
 * # Safety
 *
 * `enc` is NULL or an encoder from `pnj2k_encoder_new` that no other
 * thread is using; `image` is NULL or points to a `pnj2k_components` whose
 * `struct_size` is set, its `components` `count` structs of the first's
 * `struct_size` each pointing to `size` readable bytes aligned for its
 * samples; `params` and `out` as `pnj2k_encode` has them.
 */
pnj2k_status pnj2k_encode_components(struct pnj2k_encoder *enc,
                                     const struct pnj2k_components *image,
                                     const struct pnj2k_encode_params *params,
                                     struct pnj2k_buffer *out);

/**
 * Resizes the image `image` describes to `width` by `height`.
 *
 * The result goes into the caller's buffer in `out` (`pixels`, `size`
 * and, unless rows are packed, `stride`). The resize is the one an encode
 * asked to resize does: colour averaged in linear light, normal maps'
 * vectors renormalised, data averaged as it is, by `content`, a
 * `pnj2k_content`. Sets `out`'s width, height and stride, and its format
 * for one to four channels (0 for more). `enc` keeps the failure's
 * message.
 *
 * `out`'s rows are checked before the resize: `out->stride` must hold a
 * row of `width` pixels of the source's channels (`PNJ2K_BUFFER_TOO_SMALL`
 * otherwise), its rows must not run past any buffer
 * (`PNJ2K_INVALID_ARGUMENT`), and `out->size` must hold them
 * (`PNJ2K_BUFFER_TOO_SMALL`). An image holding pixels a decode allocated,
 * or none, and a buffer overlapping the source are
 * `PNJ2K_INVALID_ARGUMENT`. A source or size the encoder cannot take is
 * `PNJ2K_INVALID_REQUEST`. On failure `out` is unchanged.
 *
 * # Safety
 *
 * `enc` is NULL or an encoder from `pnj2k_encoder_new` that no other
 * thread is using; `image` is NULL or points to a `pnj2k_source` whose
 * `struct_size` is set and whose `pixels` are `size` readable bytes; `out`
 * is NULL or points to a `pnj2k_image` whose `struct_size` is set and whose
 * `pixels` are `size` writable bytes that nothing else uses during the call.
 */
pnj2k_status pnj2k_resize(struct pnj2k_encoder *enc,
                          const struct pnj2k_source *image,
                          uint32_t width,
                          uint32_t height,
                          uint32_t content,
                          struct pnj2k_image *out);

/**
 * Frees the bytes an encode gave.
 *
 * The bytes, `capacity` of them, are freed through the function in
 * `buffer->release`, and `data`, `len`, `capacity`, `release` and
 * `release_user` are cleared. It needs no encoder, runs on any thread,
 * and ignores NULL and a buffer whose `struct_size` it does not take.
 *
 * # Safety
 *
 * `buffer` is NULL or points to a `pnj2k_buffer` an encode filled in, its
 * bytes not used again.
 */
void pnj2k_buffer_free(struct pnj2k_buffer *buffer);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  /* PNJ2K_H */
