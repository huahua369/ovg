// ovg.cpp: 定义应用程序的入口点。
//

#include "ovg_main.h" 
#include "ovg_renderer_sdl3.h"
#include <Windows.h>
#include <cmath>
#include <unordered_map>

#ifndef fseeki64
#ifdef _WIN32
#define fseeki64 _fseeki64
#define ftelli64 _ftelli64
#else			
#define fseeki64 fseeko64
#define ftelli64 ftello64
#endif // _WIN32
#endif

using namespace std;
#include "ovg_fonts.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <vgui_sdl3.h>


int main()
{
	//LoadLibraryA(R"(E:\Program Files\RenderDoc_1.37_64\renderdoc.dll)");
	cout << "Hello ovg." << endl;
	glm::ivec2 surfsize = { 1024,800 };

	font_cache_cx* font_ctx = new_font_cache();
	font_familys_t* familys = new_font_family(font_ctx, (char*)u8"微软雅黑,Segoe UI Emoji,Consolas,Times New Roman,Tahoma,Calibri,Noto Serif Devanagari", 0);

	auto cb = new_ctx_cb();
	auto vg = cb->new_rvg(cb->ac);

	uint32_t f = SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS;
#ifdef __ANDROID__
	f |= SDL_INIT_HAPTIC;
#endif
	int kr = SDL_Init(f);

	auto wg = new app_mgr();
	if (!wg->init_gpu(true))return -1;
	auto vp = new gui_viewport();
	auto form1 = wg->create("SDL3 GPU Vector Graphics", surfsize.x, surfsize.y, 0);
	form1->viewport = vp;


	//if (!vg_sdl3_init(g, surfsize.x, surfsize.y, true)) {
	//	SDL_Log("Init failed: %s", SDL_GetError());
	//	return 1;
	//}
	auto dev = new_sdl3gpu_device(wg->device);
	assert(dev);
	auto format = SDL_GetGPUSwapchainTextureFormat(wg->device, form1->window);
	ovg_ctx_t* ctx = new_ovgctx_sdl3(dev, format ? format : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, SDL_GPU_SAMPLECOUNT_4);
	assert(ctx);

	vg_fbo_t fbo = new_vgfbo_sdl3(ctx, surfsize.x, surfsize.y, form1->window);
	bool running = true;
	runtime_cx rtc = {};

	// 渲染 
	auto fp = fopen("E:\\1.txt", "r");
	std::string buff;
	if (fp) {
		fseeki64(fp, 0L, SEEK_END);
		auto size = ftelli64(fp);
		fseeki64(fp, 0L, SEEK_SET);
		buff.resize(size);
		auto retval = fread(buff.data(), size, 1, fp);
		assert(retval == 1);
		fclose(fp);
	}
	bool testvg = 0;
	ovg_image_data img[1] = {};
	int channels = 0;
	//img->data = (uint32_t*)stbi_load("./temp/nig.png", &img->width, &img->height, &channels, 4);
	img->data = (uint32_t*)stbi_load("./temp/button.png", &img->width, &img->height, &channels, 4);

	SDL_ShowWindow(form1->window);

	while (running) {
		if (wg->get_event() < 0)
		{
			running = false;
		}
		if (ovg_get_window_swapchain(ctx, &fbo))
		{
			rtc.begin();
			cb->clear(vg);
			cb->set_fill_rule(vg, VG_FILL_RULE_NON_ZERO);
			glm::vec2 sf = fbo.display_size;
			draw_grid_fill(vg, sf, glm::ivec2(-1, 0xffdfdfdf), 20);
			cb->reset_clip(vg, 1);

			vg->width = fbo.display_size.x; vg->height = fbo.display_size.y;

			//draw_test3d(&fbo, cb, vg);
			text_style_t style4 = {};
			style4.family = familys;
			style4.fontsize = 58;
			style4.color = 0xff0080f0;
			style4.color_stroke = 0xFF0000f0;
			style4.min_subpixel = 0;
			//style4.stroke = 1;
			//style4.color_shadow = 0xa6000000;
			style4.shadow_pos = { 2.0f, 2.0f };

			text_st_t text4 = {};
			text4.text = (char*)u8"🍕➗☂️-abgyh彩色渐变字体";
			//text4.text = (char*)buff.c_str();
			text4.text_len = -1;

			text4.pos = { 10.0f, 200.0f };

			cb->move_to(vg, 0, text4.pos.y + 0.5);
			cb->rel_line_to(vg, 1800, 0);
			cb->set_source_color(vg, 0xff00ff00);
			cb->set_line_width(vg, 1);
			cb->stroke(vg);

			cb->add_text(vg, &text4, &style4, nullptr);

			style4.min_subpixel = 0;
			text4.text = (char*)u8"-+abg➗🍕☂️灰度+彩色渐变字体\n右起سأصبح غنياً";


			//style4.stroke = -1;
			text4.pos = { 10.0f, 120 + 200.0f };

			cb->move_to(vg, 0, text4.pos.y + 0.5);
			cb->rel_line_to(vg, 1800, 0);
			cb->set_source_color(vg, 0xff00ff00);
			cb->set_line_width(vg, 1);
			cb->stroke(vg);
			cb->rectangle(vg, 0, text4.pos.y - 20, 200, 200);
			cb->set_source_color(vg, 0xff000000);
			cb->set_source_color(vg, -1);
			cb->fill(vg);
			cb->add_text(vg, &text4, &style4, nullptr);
			text4.text = (char*)u8"./+*@#!@#$%^&*()_+[];'/.,";
			text4.pos.y += 260;
			cb->add_text(vg, &text4, &style4, nullptr);
			ovg_image_r rimg = {};
			rimg.img = img;
			rimg.dst = { 108,108,img->width * 2.8,img->height * 1.5 };
			rimg.rc = { 0,0,img->width,img->height };
			rimg.sliced = { 4,4,4,4 };
			rimg.color = -1;
			cb->add_image(vg, &rimg);
			if (img->valid)
			{
				vg_image_desc_t desc = {};
				desc.width = img->width;
				desc.height = img->height;
				desc.format = VG_FORMAT_RGBA8;
				desc.stride = desc.width * sizeof(int);
				desc.pixels = img->data;
				desc.x = 0, desc.y = 0, desc.w = img->width, desc.h = img->height;		// 更新矩形区域
				desc.is_copy = true;
				img->valid = false;
				cb->image_update(vg, img, &desc);
			}
			int ms = rtc.end();
			//if (ms > 0)
			//	printf("draw build ms: %d\n", ms);
			ovg_draw_data_t dlist[] = { get_draw_list(vg) };
			rtc.begin();
			ovg_render_frame(ctx, &fbo, dlist, sizeof(dlist) / sizeof(ovg_draw_data_t));// 提交渲染 
			ms = rtc.end();
			//if (ms > 0)
			//	printf("submit draw ms: %d\n", ms);
		}
		SDL_Delay(16);  /* ~60 FPS */
	}

	SDL_WaitForGPUIdle(wg->device);
	/* Cleanup */

	free_vgfbo_sdl3(&fbo);
	free_ovgctx_sdl3(ctx);
	free_sdl3gpu_device(dev);

	delete_font_family(familys);
	free_font_cache(font_ctx);
	// 删除vg对象
	cb->destroy_rvg(vg);
	if (cb)free_ctx_cb(cb);
	delete wg;
	SDL_Quit();

	return 0;
}
