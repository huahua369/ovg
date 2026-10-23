#version 450
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_samplerless_texture_functions : require
layout(row_major) uniform;
layout(row_major) buffer;

#line 9 0
layout(r16f)
layout(binding = 0, set = 1)
uniform image2D dst_0;


#line 11
struct Params_0
{
    vec2 texel_0;
    float sigma_0;
    int axis_0;
};

layout(binding = 0, set = 2)
layout(scalar) uniform block_Params_0
{
    vec2 texel_0;
    float sigma_0;
    int axis_0;
}pu_0;

#line 5
layout(binding = 0)
uniform sampler2D src_0;


#line 20
float gauss_0(float x_0, float s_0)
{

#line 21
    return exp(- x_0 * x_0 / (2.0 * s_0 * s_0)) / (s_0 * 2.50662827491760254);
}



layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main()
{

#line 27
    ivec2 _S1 = ivec2(gl_GlobalInvocationID.xy);
    ivec2 sz_0;
    ((sz_0[0]) = imageSize((dst_0)).x), ((sz_0[1]) = imageSize((dst_0)).y);
    if((any(bvec2((greaterThanEqual(_S1,sz_0))))))
    {

#line 30
        return;
    }

#line 31
    int _S2 = max(1, int(ceil(pu_0.sigma_0 * 3.0)));

#line 31
    int i_0 = - _S2;

#line 31
    float sum_0 = 0.0;

#line 31
    float wsum_0 = 0.0;


    for(;;)
    {

#line 34
        if(i_0 <= _S2)
        {
        }
        else
        {

#line 34
            break;
        }

#line 35
        float ww_0 = gauss_0(float(i_0), pu_0.sigma_0);
        ivec2 sp_0 = _S1;
        if((pu_0.axis_0) == 0)
        {

#line 37
            sp_0[0] = sp_0[0] + i_0;

#line 37
        }
        else
        {

#line 38
            sp_0[1] = sp_0[1] + i_0;

#line 37
        }

        ivec2 _S3 = clamp(sp_0, ivec2(0, 0), sz_0 - ivec2(1, 1));

#line 39
        sp_0 = _S3;


        float sum_1 = sum_0 + (textureLod((src_0), ((vec2(_S3) + 0.5) * pu_0.texel_0), (0.0))).x * ww_0;
        float wsum_1 = wsum_0 + ww_0;

#line 34
        i_0 = i_0 + 1;

#line 34
        sum_0 = sum_1;

#line 34
        wsum_0 = wsum_1;

#line 34
    }

#line 45
    imageStore((dst_0), (ivec2(uvec2(_S1))), vec4(sum_0 / wsum_0, float(0), float(0), float(0)));
    return;
}

