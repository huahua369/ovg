#version 450
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_samplerless_texture_functions : require
layout(row_major) uniform;
layout(row_major) buffer;

#line 4 0
layout(rgba32f)
layout(binding = 0, set = 1)
uniform image2D dst_0;


#line 6
struct Params_0
{
    vec2 texel_0;
    float scale_0;
    float time_0;
};

layout(binding = 0, set = 2)
layout(scalar) uniform block_Params_0
{
    vec2 texel_0;
    float scale_0;
    float time_0;
}pu_0;

#line 3
layout(binding = 1)
uniform sampler2D map_0;


#line 2
layout(binding = 0)
uniform sampler2D src_0;


#line 17
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main()
{

#line 18
    ivec2 sz_0;
    ((sz_0[0]) = imageSize((dst_0)).x), ((sz_0[1]) = imageSize((dst_0)).y);
    ivec2 _S1 = ivec2(gl_GlobalInvocationID.xy);
    if((any(bvec2((greaterThanEqual(_S1,sz_0))))))
    {

#line 21
        return;
    }
    vec2 uv_0 = (vec2(_S1) + 0.5) * pu_0.texel_0;

#line 30
    imageStore((dst_0), (ivec2(uvec2(_S1))), (textureLod((src_0), (clamp(uv_0 + ((textureLod((map_0), (uv_0), (0.0))).xy - 0.5) * 2.0 * pu_0.scale_0 * pu_0.texel_0, vec2(0.0), 1.0 - pu_0.texel_0)), (0.0))));
    return;
}

