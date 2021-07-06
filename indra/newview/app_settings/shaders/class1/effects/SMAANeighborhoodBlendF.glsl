/** 
 * @file SMAANeighborhoodBlendF.glsl
 */

/*[EXTRA_CODE_HERE]*/

#ifdef DEFINE_GL_FRAGCOLOR
out vec4 frag_color;
#else
#define frag_color gl_FragColor
#endif

VARYING vec2 vary_texcoord0;
VARYING vec4 vary_offset;

uniform sampler2D tex0;
uniform sampler2D tex1;
#if SMAA_REPROJECTION
uniform sampler2D tex2;
#endif

#define float4 vec4
#define float2 vec2
#define SMAATexture2D(tex) sampler2D tex

float4 SMAANeighborhoodBlendingPS(float2 texcoord,
                                  float4 offset,
                                  SMAATexture2D(colorTex),
                                  SMAATexture2D(blendTex)
                                  #if SMAA_REPROJECTION
                                  , SMAATexture2D(velocityTex)
                                  #endif
                                  );

void main()
{
	frag_color = SMAANeighborhoodBlendingPS(vary_texcoord0,
											vary_offset,
											tex0,
											tex1
											#if SMAA_REPROJECTION
											, tex2
											#endif
											);
}

