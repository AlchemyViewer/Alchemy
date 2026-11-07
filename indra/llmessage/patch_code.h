/**
 * @file patch_code.h
 * @brief Function declarations for encoding and decoding patches.
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_PATCH_CODE_H
#define LL_PATCH_CODE_H

class LLBitPack;
class LLGroupHeader;
class LLPatchHeader;

void    init_patch_coding(LLBitPack &bitpack);
void    code_patch_group_header(LLBitPack &bitpack, LLGroupHeader *gopp);
// False, writing nothing, where the header's quantization byte comes to
// END_OF_PATCHES, which every decoder takes for the end of the layer.
bool    code_patch_header(LLBitPack &bitpack, LLPatchHeader *ph, S32 *patch);
void    code_end_of_data(LLBitPack &bitpack);
// False, writing nothing, where a coefficient it would write is past the word
// bits of the header before it.
bool    code_patch(LLBitPack &bitpack, S32 *patch, S32 postquant);
void    end_patch_coding(LLBitPack &bitpack);

void    init_patch_decoding(LLBitPack &bitpack);
void    decode_patch_group_header(LLBitPack &bitpack, LLGroupHeader *gopp);
// Reads the group header of a layer off the wire, and says whether the layer
// may be decoded: only where the data holds the whole header and its patch size
// is NORMAL_PATCH_SIZE. The decoders write as many coefficients and rows as the
// header says into what they are handed, and the land and wind that receive
// them are laid out for that size.
[[nodiscard]] bool decode_layer_group_header(LLBitPack &bitpack, LLGroupHeader *gopp);
// A header the data ends inside reads as END_OF_PATCHES.
void    decode_patch_header(LLBitPack &bitpack, LLPatchHeader *ph);
// A patch the data ends inside reads as zeros from there, and is not to be kept:
// bitpack.readPastEnd() says so.
void    decode_patch(LLBitPack &bitpack, S32 *patches);
// Reads a layer's next patch, its header and then its coefficients. False,
// having read no coefficients, where the header is the layer's end code:
// decode_patch would read them with the word bits of the patch before. False
// too where the data ends inside the patch, which is not to be kept.
[[nodiscard]] bool decode_expected_patch(LLBitPack &bitpack, LLPatchHeader *ph, S32 *patches);

#endif
