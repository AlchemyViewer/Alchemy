/**
 * @file blurLightF.glsl
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
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

/*[EXTRA_CODE_HERE]*/

out vec4 frag_color;

uniform sampler2D lightMap;

uniform float dist_factor;
uniform vec2 delta;
uniform vec2 screen_res;
uniform vec3 kern[4];
uniform float kern_scale;

in vec2 vary_fragcoord;

vec4 getPosition(vec2 pos_screen);
vec4 getNorm(vec2 pos_screen);

void main()
{
    vec2 tc = vary_fragcoord.xy;
    vec4 norm = getNorm(tc);
    vec3 pos = getPosition(tc).xyz;
    vec4 ccol = texture(lightMap, tc).rgba;

    vec2 dlt = kern_scale * delta / (1.0+norm.xy*norm.xy);
    dlt /= max(-pos.z*dist_factor, 1.0);

    // AO uses the y weight of the kernel. The shadow term, which used the x weight, is no
    // longer blurred here: it is a Gaussian-weighted PCF (deferred/shadowUtil.glsl) that is
    // smooth at the source, so a second screen-space blur only widened its penumbra and let
    // the bilateral tolerance bleed it across depth edges the PCF had already resolved. AO is
    // still stochastic per pixel and wants the denoise, so it keeps it. This is why shadow
    // quality no longer depends on SSAO -- the pass that hosts this blur -- being enabled.
    float defined_weight = kern[0].y; // special case the first (centre) sample's weight in the blur; we have to sample it anyway so we get it for 'free'
    float ao = defined_weight * ccol.g;

    // relax tolerance according to distance to avoid speckling artifacts, as angles and distances are a lot more abrupt within a small screen area at larger distances
    float pointplanedist_tolerance_pow2 = pos.z*pos.z*0.00005;

    // perturb sampling origin slightly in screen-space to hide edge-ghosting artifacts where smoothing radius is quite large
    tc *= screen_res;
    float tc_mod = 0.5*(tc.x + tc.y);
    tc_mod -= floor(tc_mod);
    tc_mod *= 2.0;
    tc += ( (tc_mod - 0.5) * kern[1].z * dlt * 0.5 );

    // TODO: move this to kern instead of building kernel per pixel
    vec3 k[7];
    k[0] = kern[0];
    k[2] = kern[1];
    k[4] = kern[2];
    k[6] = kern[3];

    k[1] = (k[0]+k[2])*0.5f;
    k[3] = (k[2]+k[4])*0.5f;
    k[5] = (k[4]+k[6])*0.5f;

    for (int i = 1; i < 7; i++)
    {
        vec2 samptc = tc + k[i].z*dlt*2.0;
        samptc /= screen_res;
        vec3 samppos = getPosition(samptc).xyz;

        float d = dot(norm.xyz, samppos.xyz-pos.xyz);// dist from plane

        if (d*d <= pointplanedist_tolerance_pow2)
        {
            ao += texture(lightMap, samptc).g * k[i].y;
            defined_weight += k[i].y;
        }
    }

    for (int i = 1; i < 7; i++)
    {
        vec2 samptc = tc - k[i].z*dlt*2.0;
        samptc /= screen_res;
        vec3 samppos = getPosition(samptc).xyz;

        float d = dot(norm.xyz, samppos.xyz-pos.xyz);// dist from plane

        if (d*d <= pointplanedist_tolerance_pow2)
        {
            ao += texture(lightMap, samptc).g * k[i].y;
            defined_weight += k[i].y;
        }
    }

    ao /= defined_weight;

    // shadow (r) and spot shadows (b, a) pass through unblurred; only AO (g) is smoothed
    frag_color = vec4(ccol.r, max(ao, 0.0), ccol.b, ccol.a);
}

