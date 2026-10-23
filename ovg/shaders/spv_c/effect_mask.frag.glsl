#version 450
#extension GL_EXT_scalar_block_layout : require
layout(row_major) uniform;
layout(row_major) buffer;

#line 5 0
layout(location = 0)
out vec4 entryPointParam_fs_0;


#line 5
layout(location = 1)
in vec4 input_src_0;



void main()
{

#line 10
    entryPointParam_fs_0 = vec4(input_src_0.w);

#line 10
    return;
}

