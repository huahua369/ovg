#version 450
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_samplerless_texture_functions : require
layout(row_major) uniform;
layout(row_major) buffer;

#line 8 0
layout(rgba32f)
layout(binding = 0, set = 1)
uniform image2D dst_0;


#line 10
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


#line 19
float gauss_0(float x_0, float s_0)
{

#line 20
    return exp(- x_0 * x_0 / (2.0 * s_0 * s_0)) / (s_0 * 2.50662827491760254);
}



layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
void main()
{

#line 26
    ivec2 _S1 = ivec2(gl_GlobalInvocationID.xy);
    ivec2 sz_0;
    ((sz_0[0]) = imageSize((dst_0)).x), ((sz_0[1]) = imageSize((dst_0)).y);
    if((any(bvec2((greaterThanEqual(_S1,sz_0))))))
    {

#line 29
        return;
    }

#line 30
    int _S2 = max(1, int(ceil(pu_0.sigma_0 * 3.0)));
    const vec4 _S3 = vec4(0.0, 0.0, 0.0, 0.0);

#line 31
    int i_0 = - _S2;

#line 31
    vec4 sum_0 = _S3;

#line 31
    float wsum_0 = 0.0;

    for(;;)
    {

#line 33
        if(i_0 <= _S2)
        {
        }
        else
        {

#line 33
            break;
        }

#line 34
        float ww_0 = gauss_0(float(i_0), pu_0.sigma_0);
        ivec2 sp_0 = _S1;
        if((pu_0.axis_0) == 0)
        {

#line 36
            sp_0[0] = sp_0[0] + i_0;

#line 36
        }
        else
        {

#line 37
            sp_0[1] = sp_0[1] + i_0;

#line 36
        }

        ivec2 _S4 = clamp(sp_0, ivec2(0, 0), sz_0 - ivec2(1, 1));

#line 38
        sp_0 = _S4;


        vec4 sum_1 = sum_0 + (textureLod((src_0), ((vec2(_S4) + 0.5) * pu_0.texel_0), (0.0))) * ww_0;
        float wsum_1 = wsum_0 + ww_0;

#line 33
        i_0 = i_0 + 1;

#line 33
        sum_0 = sum_1;

#line 33
        wsum_0 = wsum_1;

#line 33
    }

#line 44
    imageStore((dst_0), (ivec2(uvec2(_S1))), sum_0 / wsum_0);
    return;
}

