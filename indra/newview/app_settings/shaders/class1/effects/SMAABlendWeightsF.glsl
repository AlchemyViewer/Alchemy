/** 
 * @file SMAABlurWeightsF.glsl
 */

/*[EXTRA_CODE_HERE]*/

#ifdef DEFINE_GL_FRAGCOLOR
out vec4 frag_color;
#else
#define frag_color gl_FragColor
#endif

VARYING vec2 vary_texcoord0;
VARYING vec2 vary_pixcoord;
VARYING vec4 vary_offset[3];

uniform sampler2D tex0;
uniform sampler2D tex1;
uniform sampler2D tex2;

#define float4 vec4
#define float2 vec2
#define SMAATexture2D(tex) sampler2D tex

float4 SMAABlendingWeightCalculationPS(float2 texcoord,
                                       float2 pixcoord,
                                       float4 offset[3],
                                       SMAATexture2D(edgesTex),
                                       SMAATexture2D(areaTex),
                                       SMAATexture2D(searchTex),
                                       float4 subsampleIndices);

void main()
{
	frag_color = SMAABlendingWeightCalculationPS(vary_texcoord0,
												 vary_pixcoord,
												 vary_offset,
												 tex0,
												 tex1,
												 tex2,
												 float4(0.0)
												 );
}

