#pragma once
/*
  vg_renderer.c

  SDL3 GPU vector-graphics renderer built around vg.slang.h

   Supports: SOLID / SURFACE / LINEAR / RADIAL / SWEEP paints
   Uses:    SDL_PushGPUVertexUniformData  (push constants = UBO + PushConsts)
矢量图：预乘混合渲染
位图：支持none = -1, 不混合
	normal = 0,	 普通混合
	additive,
	multiply,
	modulate,
	screen,
	normal_prem,	 预乘alpha
	additive_prem,
模板状态：
管线配置：

*/

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <queue>
#include <vector>

/* ── 3.  Global renderer state ────────────────────────────────── */
typedef struct VGState {
	SDL_Window* window;
	SDL_GPUDevice* device;
	SDL_GPUGraphicsPipeline* pipeline;
	SDL_GPUBuffer* vertexBuffer;
	SDL_GPUSampler* linearSampler;
	uint32_t cap_v = 0;
	/* CPU-side data mirrors */
	int width = 0, height = 0;
	SDL_GPUTexture* emptyImg;
	SDL_GPUSampler* sampler;
	SDL_GPURenderPass* pass;
	SDL_GPUCommandBuffer* cmd;
	std::queue<SDL_GPUTransferBuffer*> rq;

	SDL_GPUTexture* msaaColor = nullptr;
	SDL_GPUTexture* msaaDepth = nullptr;

	int msaaWidth = 0;
	int msaaHeight = 0;

	SDL_GPUSampleCount msaaSampleCount = SDL_GPU_SAMPLECOUNT_4;
	ovg_draw_data_t* data;
} VGState;


/* ── 4.  Load a pre-compiled SPIR-V file ─────────────────────── */
void* LoadSPIRV(const char* path, size_t* outSize);
/* ── 5.  Create an SDL_GPUShader from a .spv file ────────────── */
SDL_GPUShader* CreateShader(VGState* g, const char* spvPath, SDL_GPUShaderStage stage,
	Uint32 numSamplers, Uint32 numStorageTex, Uint32 numStorageBuf, Uint32 numUniformBuf);

/* ── 6.  Init ─────────────────────────────────────────────────── */
bool VG_Init(VGState* g, int width, int height);

/* ── 7.  Push uniforms for the current draw ─────────────────────── */
/*
 *  Binding map (matches the GLSL shaders):
 *
 *  Vertex stage
 *    set=1, binding=0 : uboGrad  → SDL_PushGPUVertexUniformData, slot 0
 *    push constants    : (auto)    → SDL_PushGPUVertexUniformData, slot 0
 *                                  BUT SDL3 GPU treats push constants
 *                                  separately, so we use the same call
 *                                  with the PushConsts struct.
 *
 *  Fragment stage
 *    set=3, binding=0 : uboGrad  → SDL_PushGPUFragmentUniformData, slot 0
 *    set=2, binding=0 : sourceTex → SDL_BindGPUFragmentSamplers
 *    push constants    : (auto)    → SDL_PushGPUFragmentUniformData
 *
 *  Since both push constants and uboGrad live in the same uniform
 *  slot range, we push them sequentially.  SDL3 GPU internally
 *  allocates separate buffer regions, so this is safe.
 */
void VG_PushDrawUniforms(VGState* g, SDL_GPUCommandBuffer* cmd, float vpW, float vpH, int patType, float opacity);
/* ── 8.  Draw a filled rectangle (2 triangles, 6 vertices) ──────── */
#define PAT_SOLID  0
#define PAT_SURFACE 1
#define PAT_LINEAR 2
#define PAT_RADIAL 3
#define PAT_MESH   4
#define PAT_RASTER 5
#define PAT_SWEEP  6

#define STENCIL_FILL_BIT              0x1
#define STENCIL_CLIP_BIT              0x2
#define STENCIL_ALL_BIT               0x3


void VG_RenderFrame(VGState* g, ovg_draw_data_t* data);
