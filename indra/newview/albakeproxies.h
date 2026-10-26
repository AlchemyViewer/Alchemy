/**
 * @file albakeproxies.h
 * @brief The bakes a face names through bake proxies (IMG_USE_BAKED_*): those an avatar wearing it hides
 *        the system body parts of, and whether its PBR material resolves one for its own avatar.
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
#ifndef AL_BAKEPROXIES_H
#define AL_BAKEPROXIES_H

#include "llavatarappearancedefines.h"

#include <bitset>

class LLGLTFMaterial;
class LLTextureEntry;

namespace ALBakeProxies
{
    // A set of bakes, by EBakedTextureIndex
    typedef std::bitset<LLAvatarAppearanceDefines::BAKED_NUM_INDICES> bake_set_t;

    // Whether any slot of a PBR material names a bake proxy, which the face drawing it resolves for its own
    // object's avatar, and so draws from a copy of its own (LLViewerObject::initRenderMaterial)
    bool anyNamedBy(const LLGLTFMaterial& material);

    // The bake a PBR material draws a body with: the one its base colour names, the slot that stands for a
    // face's image. A bake in its other slots shades the surface without drawing a body
    bake_set_t drawnBy(const LLGLTFMaterial* material);

    // The bakes a face draws a body with, whose system body parts an avatar wearing it hides
    // (LLVOAvatar::updateMeshVisibility): its image's and its PBR material's. A HUD's face resolves no bake
    // (LLViewerObject::getBakedTextureForMagicId): its image counts, as Linden's viewer counts it, and its
    // material does not
    bake_set_t drawnBy(const LLTextureEntry& te, bool hud);
}

#endif // AL_BAKEPROXIES_H
