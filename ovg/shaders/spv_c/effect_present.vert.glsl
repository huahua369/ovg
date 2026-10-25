#version 460
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_shader_draw_parameters : require
layout(row_major) uniform;
layout(row_major) buffer;

#line 9 0
layout(location = 0)
out vec2 entryPointParam_vs_uv_0;


#line 9
struct VSOut_0
{
    vec4 pos_0;
    vec2 uv_0;
};

void main()
{

#line 15
    uint _S1 = uint(gl_VertexIndex - gl_BaseVertex);

#line 15
    int _S2;

    if(_S1 == 1U)
    {

#line 17
        _S2 = 3;

#line 17
    }
    else
    {

#line 17
        _S2 = -1;

#line 17
    }

#line 17
    float _S3 = float(_S2);

#line 17
    if(_S1 == 2U)
    {

#line 17
        _S2 = 3;

#line 17
    }
    else
    {

#line 17
        _S2 = -1;

#line 17
    }

#line 17
    vec2 p_0 = vec2(_S3, float(_S2));
    VSOut_0 o_0;
    o_0.pos_0 = vec4(p_0, 0.0, 1.0);
    o_0.uv_0 = p_0 * 0.5 + 0.5;
    VSOut_0 _S4 = o_0;

#line 21
    gl_Position = o_0.pos_0;

#line 21
    entryPointParam_vs_uv_0 = _S4.uv_0;

#line 21
    return;
}

