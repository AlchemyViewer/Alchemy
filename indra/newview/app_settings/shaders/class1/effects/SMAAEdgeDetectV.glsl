/** 
 * @file SMAAEdgeDetectV.glsl
 */

/*[EXTRA_CODE_HERE]*/

uniform mat4 modelview_projection_matrix;

ATTRIBUTE vec3 position;

VARYING vec2 vary_texcoord0;
VARYING vec4 vary_offset[3];

#define float4 vec4
#define float2 vec2
void SMAAEdgeDetectionVS(float2 texcoord,
                         out float4 offset[3]);
						 
void main()
{
	gl_Position = modelview_projection_matrix * vec4(position.xyz, 1.0);	
	vary_texcoord0 = (gl_Position.xy*0.5+0.5);
	
	SMAAEdgeDetectionVS(vary_texcoord0, vary_offset);
}
