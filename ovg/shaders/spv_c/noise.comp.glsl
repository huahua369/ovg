#version 450
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_samplerless_texture_functions : require
layout(row_major) uniform;
layout(row_major) buffer;

#line 3 0
layout(rgba32f)
layout(binding = 0, set = 1)
uniform image2D dst_0;


#line 5
struct Params_0
{
    vec2 texel_0;
    float time_0;
    float freq_0;
};

layout(binding = 0, set = 2)
layout(scalar) uniform block_Params_0
{
    vec2 texel_0;
    float time_0;
    float freq_0;
}pu_0;

#line 14
float hash_0(vec2 p_0)
{

#line 15
    return fract(sin(dot(p_0, vec2(127.09999847412109375, 311.70001220703125))) * 43758.546875);
}

float noise_0(vec2 p_1)
{

#line 19
    vec2 i_0 = floor(p_1);

#line 19
    vec2 f_0 = fract(p_1);

#line 24
    vec2 u_0 = f_0 * f_0 * (3.0 - 2.0 * f_0);
    float _S1 = u_0.x;

#line 25
    return mix(mix(hash_0(i_0), hash_0(i_0 + vec2(1.0, 0.0)), _S1), mix(hash_0(i_0 + vec2(0.0, 1.0)), hash_0(i_0 + vec2(1.0, 1.0)), _S1), u_0.y);
}

float fbm_0(vec2 p_2)
{

#line 28
    int i_1 = 0;

#line 28
    float a_0 = 0.5;

#line 28
    vec2 _S2 = p_2;

#line 28
    float v_0 = 0.0;

    for(;;)
    {

#line 30
        if(i_1 < 5)
        {
        }
        else
        {

#line 30
            break;
        }

#line 31
        float v_1 = v_0 + a_0 * noise_0(_S2);
        vec2 _S3 = _S2 * 2.0;

#line 32
        float a_1 = a_0 * 0.5;

#line 30
        i_1 = i_1 + 1;

#line 30
        a_0 = a_1;

#line 30
        _S2 = _S3;

#line 30
        v_0 = v_1;

#line 30
    }



    return v_0;
}



layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main()
{

#line 40
    ivec2 sz_0;
    ((sz_0[0]) = imageSize((dst_0)).x), ((sz_0[1]) = imageSize((dst_0)).y);
    ivec2 _S4 = ivec2(gl_GlobalInvocationID.xy);
    if((any(bvec2((greaterThanEqual(_S4,sz_0))))))
    {

#line 43
        return;
    }

    float n_0 = fbm_0(vec2(_S4) * pu_0.texel_0 * pu_0.freq_0 * 100.0 + pu_0.time_0 * 0.05000000074505806);
    imageStore((dst_0), (ivec2(uvec2(_S4))), vec4(n_0, n_0, n_0, 1.0));
    return;
}

