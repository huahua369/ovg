slangc -lang slang avg.slang.h -target glsl -profile glsl_450 -force-glsl-scalar-layout -entry main -o spv_c/a_vg.vert.glsl
slangc -lang slang avg.slang.h -target glsl -profile glsl_450 -force-glsl-scalar-layout -entry fragMain -o spv_c/a_vg.frag.glsl 

slangc -lang slang base3d.slang.h -target glsl -profile glsl_450 -force-glsl-scalar-layout -entry main -o spv_c/a_base3d.vert.glsl  
slangc -lang slang base3d.slang.h -target glsl -profile glsl_450 -force-glsl-scalar-layout -entry fragMain -o spv_c/a_base3d.frag.glsl 

slangc -lang slang base3d.slang.h -target glsl -profile glsl_450 -force-glsl-scalar-layout -entry main -D COLOR_MASK=1 -o spv_c/a_base3d_mask.vert.glsl  
slangc -lang slang base3d.slang.h -target glsl -profile glsl_450 -force-glsl-scalar-layout -entry fragMain -D COLOR_MASK=1 -o spv_c/a_base3d_mask.frag.glsl 

slangc -lang slang base3d.slang.h -target glsl -profile glsl_450 -force-glsl-scalar-layout -entry main -D DOUBLESIDEDCOLOR -o spv_c/a_base3d_dsc.vert.glsl  
slangc -lang slang base3d.slang.h -target glsl -profile glsl_450 -force-glsl-scalar-layout -entry fragMain -D DOUBLESIDEDCOLOR -o spv_c/a_base3d_dsc.frag.glsl 

slangc -lang slang base3d.slang.h -target glsl -profile glsl_450 -force-glsl-scalar-layout -entry main -D ID_INSTANCING=1 -o spv_c/a_base3d_inst.vert.glsl  
slangc -lang slang base3d.slang.h -target glsl -profile glsl_450 -force-glsl-scalar-layout -entry fragMain -D ID_INSTANCING=1 -o spv_c/a_base3d_inst.frag.glsl 

slangc -lang slang base3d.slang.h -target glsl -profile glsl_450 -force-glsl-scalar-layout -entry main -D DOUBLESIDEDCOLOR -D ID_INSTANCING=1 -o spv_c/a_base3d_dsc_inst.vert.glsl  
slangc -lang slang base3d.slang.h -target glsl -profile glsl_450 -force-glsl-scalar-layout -entry fragMain -D DOUBLESIDEDCOLOR -D ID_INSTANCING=1 -o spv_c/a_base3d_dsc_inst.frag.glsl 

slangc -lang slang blur.slang.h -target glsl -profile glsl_450 -force-glsl-scalar-layout -stage compute -entry main -o spv_c/blur.comp.glsl
slangc -lang slang displace.slang.h -target glsl -profile glsl_450 -force-glsl-scalar-layout -stage compute -entry main -o spv_c/displace.comp.glsl
slangc -lang slang noise.slang.h -target glsl -profile glsl_450 -force-glsl-scalar-layout -stage compute -entry main -o spv_c/noise.comp.glsl

slangc -lang slang blit.slang.h -target glsl -profile glsl_450 -force-glsl-scalar-layout -entry vs -o spv_c/blit.vert.glsl  
slangc -lang slang blit.slang.h -target glsl -profile glsl_450 -force-glsl-scalar-layout -entry fs -o spv_c/blit.frag.glsl 

glslangValidator spv_c/a_vg.vert.glsl -V --vn vg_vert -o spv_c/a_vg.vert.h
glslangValidator spv_c/a_vg.frag.glsl -V --vn vg_frag -o spv_c/a_vg.frag.h

glslangValidator spv_c/a_base3d.vert.glsl -V --vn a_base3d_vert -o spv_c/a_base3d.vert.h
glslangValidator spv_c/a_base3d.frag.glsl -V --vn a_base3d_frag -o spv_c/a_base3d.frag.h

glslangValidator spv_c/a_base3d_mask.vert.glsl -V --vn a_base3d_mask_vert -o spv_c/a_base3d_mask.vert.h
glslangValidator spv_c/a_base3d_mask.frag.glsl -V --vn a_base3d_mask_frag -o spv_c/a_base3d_mask.frag.h

glslangValidator spv_c/a_base3d_dsc.vert.glsl -V --vn a_base3d_dsc_vert -o spv_c/a_base3d_dsc.vert.h
glslangValidator spv_c/a_base3d_dsc.frag.glsl -V --vn a_base3d_dsc_frag -o spv_c/a_base3d_dsc.frag.h

glslangValidator spv_c/a_base3d_inst.vert.glsl -V --vn a_base3d_inst_vert -o spv_c/a_base3d_inst.vert.h
glslangValidator spv_c/a_base3d_inst.frag.glsl -V --vn a_base3d_inst_frag -o spv_c/a_base3d_inst.frag.h

glslangValidator spv_c/a_base3d_dsc_inst.vert.glsl -V --vn a_base3d_dsc_inst_vert -o spv_c/a_base3d_dsc_inst.vert.h
glslangValidator spv_c/a_base3d_dsc_inst.frag.glsl -V --vn a_base3d_dsc_inst_frag -o spv_c/a_base3d_dsc_inst.frag.h

glslangValidator spv_c/blit.vert.glsl -V --vn a_blit_vert -o spv_c/blit.vert.h
glslangValidator spv_c/blit.frag.glsl -V --vn a_blit_frag -o spv_c/blit.frag.h

glslangValidator -V -S comp spv_c/blur.comp.glsl --vn a_blur_comp -o spv_c/blur.spv.h
glslangValidator -V -S comp spv_c/displace.comp.glsl --vn a_displace_comp -o spv_c/displace.spv.h
glslangValidator -V -S comp spv_c/noise.comp.glsl --vn a_noise_comp -o spv_c/noise.spv.h
