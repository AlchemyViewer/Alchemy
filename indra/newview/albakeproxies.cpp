/**
 * @file albakeproxies.cpp
 * @brief The bakes a face names through bake proxies (IMG_USE_BAKED_*)
 *
 * Copyright (c) 2026, Rye Mutt <rye@alchemyviewer.org>
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#include "linden_common.h"

#include "albakeproxies.h"

#include "llgltfmaterial.h"
#include "lltextureentry.h"

using namespace LLAvatarAppearanceDefines;

namespace
{
    // The bake a texture ID names, where it is a bake proxy
    ALBakeProxies::bake_set_t named_by(const LLUUID& id)
    {
        ALBakeProxies::bake_set_t bakes;
        const EBakedTextureIndex index = LLAvatarAppearanceDictionary::assetIdToBakedTextureIndex(id);
        if (index != BAKED_NUM_INDICES)
        {
            bakes.set(index);
        }
        return bakes;
    }
}

namespace ALBakeProxies
{

bool anyNamedBy(const LLGLTFMaterial& material)
{
    for (const LLUUID& id : material.mTextureId)
    {
        if (LLAvatarAppearanceDictionary::isBakedImageId(id))
        {
            return true;
        }
    }
    return false;
}

bake_set_t drawnBy(const LLGLTFMaterial* material)
{
    return material ? named_by(material->mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR]) : bake_set_t();
}

bake_set_t drawnBy(const LLTextureEntry& te, bool hud)
{
    bake_set_t bakes = named_by(te.getID());
    if (!hud)
    {
        bakes |= drawnBy(te.getGLTFRenderMaterial());
    }
    return bakes;
}

}
