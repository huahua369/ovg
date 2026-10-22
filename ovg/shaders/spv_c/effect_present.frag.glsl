#version 450
#extension GL_EXT_scalar_block_layout : require
layout(row_major) uniform;
layout(row_major) buffer;

#line 21 0
struct Params_0
{
    int mode_0;
    float spread_0;
    float opacity_0;
    float pad0_0;
    vec2 offset_0;
    vec4 color_0;
    vec4 rect_0;
};


#line 38
layout(binding = 0, set = 3)
layout(scalar) uniform block_Params_0
{
    int mode_0;
    float spread_0;
    float opacity_0;
    float pad0_0;
    vec2 offset_0;
    vec4 color_0;
    vec4 rect_0;
}pu_0;

#line 32
layout(binding = 0, set = 2)
uniform sampler2D maskSrc_0;

layout(binding = 1, set = 2)
uniform sampler2D blurSrc_0;


#line 14019 1
float saturate_0(float x_0)
{

#line 14027
    return clamp(x_0, 0.0, 1.0);
}


#line 14027
layout(location = 0)
out vec4 entryPointParam_fs_0;


#line 41 0
void main()
{
    vec2 uv_0 = (gl_FragCoord.xy - pu_0.rect_0.xy) / pu_0.rect_0.zw;


    float m_0 = (textureLod((maskSrc_0), (uv_0), (0.0))).w;

    float b_0 = (textureLod((blurSrc_0), (uv_0 + vec2(- pu_0.offset_0.x, pu_0.offset_0.y) * (1.0 / pu_0.rect_0.zw)), (0.0))).w;

#line 48
    float a_0;


    if((pu_0.mode_0) == 1)
    {

#line 51
        a_0 = saturate_0(1.0 - b_0) * m_0;

#line 51
    }
    else
    {

#line 51
        a_0 = saturate_0(b_0 * (1.0 + max(pu_0.spread_0, 0.0) * 3.0));

#line 51
    }

#line 63
    float a_1 = a_0 * (pu_0.color_0.w * pu_0.opacity_0);

#line 63
    entryPointParam_fs_0 = vec4(pu_0.color_0.xyz * a_1, a_1);

#line 63
    return;
}

