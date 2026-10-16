#version 450
#extension GL_EXT_scalar_block_layout : require
layout(row_major) uniform;
layout(row_major) buffer;

#line 2 0
layout(binding = 0)
uniform texture2D tex_0;


#line 3
layout(binding = 1)
uniform sampler samp_0;


#line 5
struct SLANG_ParameterGroup_B_0
{
    vec2 res_0;
    float time_0;
    float rim_0;
};


#line 5
layout(binding = 2)
layout(scalar) uniform block_SLANG_ParameterGroup_B_0
{
    vec2 res_0;
    float time_0;
    float rim_0;
}B_0;

#line 11821 1
layout(location = 0)
out vec4 entryPointParam_fs_0;


#line 11821
layout(location = 0)
in vec2 i_uv_0;


#line 26 0
void main()
{

    vec2 p_0 = i_uv_0 * 2.0 - 1.0;

    float mask_0 = smoothstep(0.75, 0.64999997615814209, length(p_0));

#line 36
    vec3 col_0 = (textureLod(sampler2D(tex_0,samp_0), (i_uv_0), (0.0))).xyz;

#line 36
    entryPointParam_fs_0 = vec4(mix(col_0, col_0 + pow(max(0.0, dot(normalize(p_0 + 0.00000999999974738), normalize(vec2(-0.60000002384185791, -0.80000001192092896)))), 2.0) * mask_0 * B_0.rim_0, vec3(mask_0)) + (1.0 - mask_0) * vec3(0.01999999955296516, 0.01999999955296516, 0.02999999932944775), 1.0);

#line 36
    return;
}

