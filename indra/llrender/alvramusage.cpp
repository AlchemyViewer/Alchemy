/**
 * @file alvramusage.cpp
 * @brief What the viewer holds in video memory, by what it is for.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "alvramusage.h"

#include "algpubuffer.h"
#include "aluniformbuffer.h"
#include "llimagegl.h"
#include "llvertexbuffer.h"

ALVRAMUsage ALVRAMUsage::current()
{
    ALVRAMUsage usage;
    usage.mStreamedTextures = LLImageGL::getTextureBytesAllocated(LLImageGLMemory::STREAMED_TEXTURE);
    usage.mOtherTextures    = LLImageGL::getTextureBytesAllocated(LLImageGLMemory::OTHER_TEXTURE);
    usage.mRenderTargets    = LLImageGL::getTextureBytesAllocated(LLImageGLMemory::RENDER_TARGET);
    usage.mReflectionProbes = LLImageGL::getTextureBytesAllocated(LLImageGLMemory::REFLECTION_PROBE);
    usage.mGeometry         = LLVertexBuffer::getBytesAllocated();
    usage.mGeometryPooled   = LLVertexBuffer::getBytesPooled();
    usage.mGPUBuffers       = ALGPUBuffer::sBytesAllocated + ALUniformBuffer::getStoreBytes() + LLImageGL::getScratchBufferBytes();
    return usage;
}
