/*
vgui sdl3实现

创建日期：2026-9-12
*/

#include <pch.h>
#include <SDL3/SDL.h>
#ifdef _WIN32
#include <WinSock2.h>
#include <windows.h>
#include <dwmapi.h>
#include <imm.h>
#pragma comment(lib,"Imm32.lib")
#pragma comment(lib, "dwmapi")
#include <commctrl.h>
#include <ole2.h>
#include <win_core.h>
#endif  

#include <mutex>
#include <vgui.h>
#include "vgui_sdl3.h"
#include <stb_image.h>



std::string get_clipboard()
{
	std::string ret = {};
	if (SDL_HasClipboardText())
	{
		auto p = SDL_GetClipboardText();
		if (p)
		{
			ret = p;
			SDL_free((void*)p);
		}
	}
	return ret;
}

void set_clipboard(const char* str)
{
	if (str)
		SDL_SetClipboardText(str);
}

void set_col_u8()
{
#ifdef _WIN32 
	system("color 00");
	system("CHCP 65001");
#endif

}
namespace pce {

	void set_property(SDL_Window* w, const char* str, void* p);
	void* get_property(SDL_Window* w, const char* str);


	void set_property(SDL_Window* w, const char* str, void* p)
	{
		SDL_SetPointerProperty(SDL_GetWindowProperties(w), str, p);
	}
	void* get_property(SDL_Window* w, const char* str)
	{
		return SDL_GetPointerProperty(SDL_GetWindowProperties(w), str, 0);
	}

	void* get_windowptr(SDL_Window* w)
	{
#ifdef _WIN32
		return get_property(w, SDL_PROP_WINDOW_WIN32_HWND_POINTER);// "SDL.window.win32.hwnd");
		//return systemInfo.info.win.window;
#elif __ANDROID__
		//return systemInfo.info.android.window;
		return get_property(w, "SDL.window.android.window", );
#endif
		//return &systemInfo.info;
		return 0;
	}


}//!pce

SDL_HitTestResult HitTestCallback2(SDL_Window* win, const SDL_Point* area, void* data);



class Timer
{
public:
	float target_fps = 0.0;
	float screen_ticks_per_frame = 0.0f;
	Uint64 started_ticks = 0;
	float extra_time = 0.0;
	Timer()
		:started_ticks{ SDL_GetPerformanceCounter() }
	{}

	void restart() {
		started_ticks = SDL_GetPerformanceCounter();
	}

	float get_time() {
		return (static_cast<float>(SDL_GetPerformanceCounter() - started_ticks) / static_cast<float>(SDL_GetPerformanceFrequency()) * 1000.0f);
	}
	void set_fps(float f) {

		target_fps = f;
		screen_ticks_per_frame = 1000.0f / static_cast<float>(target_fps);
	}
	void fps_sleep()
	{}
};


#ifdef _WIN32

bool wMessageHook(void* userdata, MSG* msg) {
	auto app = (app_ctx*)userdata;
	if (app && msg)
	{
		switch (msg->message)
		{
		case WM_NCLBUTTONDBLCLK:
			return app->has_maximized(msg->hwnd);	// 禁用无边框双击最大化
		case WM_NCLBUTTONDOWN:
		case WM_NCRBUTTONDOWN:
		case WM_NCMBUTTONDOWN:
			app->nc_down = true;
			app->kncdown();
			break;
		default:
			break;
		}
	}
	return 1;
}
#endif



// 窗口应用管理

app_ctx::app_ctx()
{
	set_col_u8();
	fct = new Timer();
	uint32_t f = SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS;
#ifdef __ANDROID__
	f |= SDL_INIT_HAPTIC;
#endif
	int kr = SDL_Init(f);
	SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, false);
	SDL_SetEventEnabled(SDL_EVENT_DROP_TEXT, false);
	// Enable native IME.
	SDL_SetHintWithPriority(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1", SDL_HINT_OVERRIDE);
	SDL_SetHintWithPriority(SDL_HINT_IME_IMPLEMENTED_UI, "composition", SDL_HINT_OVERRIDE);
	SDL_SetHint(SDL_HINT_RENDER_VULKAN_DEBUG, "true");
#ifdef __ANDROID__
	SDL_SetHintWithPriority(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "1", SDL_HINT_OVERRIDE);
	SDL_SetHintWithPriority(SDL_HINT_TOUCH_MOUSE_EVENTS, "1", SDL_HINT_OVERRIDE);
#endif

#ifdef _WIN32
	{
		auto ctxe = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;
		//auto tr = SetThreadDpiAwarenessContext(ctxe);
		// 设置当前进程的DPI感知等级。
		auto pd = SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		auto td = SetThreadDpiHostingBehavior(DPI_HOSTING_BEHAVIOR_MIXED);
		auto k = GetThreadDpiAwarenessContext();
		if (k)
		{
			printf("%p\n", k);
		}
	}
#endif // _WIN32

#if _WIN32
	auto hr = OleInitialize(NULL);
	SDL_SetWindowsMessageHook(wMessageHook, this);
#endif
#ifdef EVWATCH
	SDL_AddEventWatch([](void* userdata, SDL_Event* e) {
		auto ctx = (app_ctx*)userdata;
		if (ctx && e) {
			ctx->call_cb(e);
		}
		return 0;
		}, this);
#endif 
	audio_device = open_audio(0, 0, 0);


#ifdef __ANDROID__
	auto idp = SDL_GetPrefPath(0, "nluna");
	if (idp) {
		internalDataPath = get_dir(idp);
		SDL_free((char*)idp);
	}
	auto edp = SDL_AndroidGetExternalStoragePath();
	if (edp)
	{
		externalDataPath = get_dir(edp);
		SDL_free((char*)edp);
	}
	externalCachePath = externalDataPath + "cache/";
#else
	externalDataPath = "ext_data\\";
	externalCachePath = externalDataPath + "cache\\";
	auto idp = SDL_GetPrefPath(0, "nluna");
	if (idp) {
		internalDataPath = idp;
		SDL_free(idp);
	}
#endif
	UpdateMonitors();
}

app_ctx::~app_ctx()
{
	close_audio(audio_device);
	for (size_t i = 0; i < SDL_SystemCursor::SDL_SYSTEM_CURSOR_COUNT; i++)
	{
		if (system_cursor[i]) {
			SDL_DestroyCursor(system_cursor[i]);
		}
	}
	memset(system_cursor, 0, sizeof(system_cursor) * SDL_SystemCursor::SDL_SYSTEM_CURSOR_COUNT);

	if (fct)
	{
		delete fct; fct = 0;
	}
	SDL_Quit();
#if _WIN32
	OleUninitialize();
#endif 
}


#if 1
#if 1 

uint32_t get_flags(int fgs)
{
	uint32_t flags = 0;
	if (fgs == 0)fgs = ef_default;
	if (fgs & ef_borderless)
	{
		flags |= SDL_WINDOW_BORDERLESS;
		SDL_SetHintWithPriority("SDL_BORDERLESS_RESIZABLE_STYLE", "1", SDL_HINT_OVERRIDE);
		SDL_SetHintWithPriority("SDL_BORDERLESS_WINDOWED_STYLE", "1", SDL_HINT_OVERRIDE);

	}
	if (fgs & ef_fullscreen)
		flags |= SDL_WINDOW_FULLSCREEN;
	if (fgs & ef_resizable)
		flags |= SDL_WINDOW_RESIZABLE;
	//flags |= SDL_WINDOW_MOUSE_GRABBED;//锁定鼠标在窗口内
	flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_INPUT_FOCUS;

	if (fgs & ef_transparent)
		flags |= SDL_WINDOW_TRANSPARENT;
	if (fgs & ef_vulkan)
		flags |= SDL_WINDOW_VULKAN;
	//if (fgs & ef_metal)
	//	flags |= SDL_WINDOW_METAL;
	if (fgs & ef_tooltip)
		flags |= SDL_WINDOW_TOOLTIP;
	if (fgs & ef_popup)
		flags |= SDL_WINDOW_POPUP_MENU;
	else if (fgs & ef_utility)
		flags |= SDL_WINDOW_UTILITY;
	else if (fgs & ef_minimized)
		flags |= SDL_WINDOW_MINIMIZED;
	else if (fgs & ef_maximized)
		flags |= SDL_WINDOW_MAXIMIZED;
	return flags;
}




SDL_Renderer* newRenderer(SDL_Window* window, const char* name, void* instance, void* physical_device, void* device)
{
	SDL_Renderer* renderer;
	SDL_PropertiesID props = SDL_CreateProperties();
	if (window)
		SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
	if (instance)
		SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_VULKAN_INSTANCE_POINTER, instance);
	if (physical_device)
		SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_VULKAN_PHYSICAL_DEVICE_POINTER, physical_device);
	if (device)
		SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_VULKAN_DEVICE_POINTER, device);
	SDL_SetStringProperty(props, SDL_PROP_RENDERER_CREATE_NAME_STRING, name);
	renderer = SDL_CreateRendererWithProperties(props);
	SDL_DestroyProperties(props);
	return renderer;
}

SDL_Renderer* newRenderer(int width, int height, const char* name, void* instance, void* physical_device, void* device)
{
	SDL_Renderer* renderer;
	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PixelFormat::SDL_PIXELFORMAT_ABGR8888);
	if (surface)
		SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_SURFACE_POINTER, surface);
	if (instance)
		SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_VULKAN_INSTANCE_POINTER, instance);
	if (physical_device)
		SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_VULKAN_PHYSICAL_DEVICE_POINTER, physical_device);
	if (device)
		SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_VULKAN_DEVICE_POINTER, device);
	SDL_SetStringProperty(props, SDL_PROP_RENDERER_CREATE_NAME_STRING, name);
	renderer = SDL_CreateRendererWithProperties(props);
	SDL_DestroyProperties(props);
	return renderer;
}

void app_ctx::call_cb(SDL_Event* e)
{
	auto fwp = SDL_GetWindowFromID(e->window.windowID);
	auto fw = (form_x*)pce::get_property(fwp, "form_x");
	int rw = 0;

	if (e->type == SDL_EVENT_SYSTEM_THEME_CHANGED) {
		auto st = SDL_GetSystemTheme();
		printf("");
	}
	if (fw) {
		rw = on_call_we(e, fw);
	}
}
void app_ctx::sleep_ms(int ms)
{
	//SDL_Delay(ms);
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
void app_ctx::sleep_ns(int ns)
{
	std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
	//SDL_DelayNS(ns);
}
uint64_t app_ctx::get_ticks() {
	return SDL_GetTicks();
}
void app_ctx::set_fps(int n) {
	//_fps = n;
	//if (n > 0)
	//{
	//	fms = 1000.0 / n;
	//	fct->set_fps(n);
	//}
}

void app_ctx::set_syscursor(int type)
{
	if (type < 0)type = 0;
	if (type < SDL_SYSTEM_CURSOR_COUNT)
	{
		auto& psc = system_cursor[type];
		if (!psc)
			psc = SDL_CreateSystemCursor((SDL_SystemCursor)type);
		if (psc)
		{
			SDL_SetCursor(psc);
		}
	}
}

void app_ctx::set_defcursor(cursor_st t)
{
	switch (t)
	{
	case cursor_st::cursor_arrow:
		set_syscursor(SDL_SYSTEM_CURSOR_DEFAULT);
		break;
	case cursor_st::cursor_ibeam:
		set_syscursor(SDL_SYSTEM_CURSOR_TEXT);
		break;
	case cursor_st::cursor_wait:
		set_syscursor(SDL_SYSTEM_CURSOR_WAIT);
		break;
	case cursor_st::cursor_no:
		set_syscursor(SDL_SYSTEM_CURSOR_NOT_ALLOWED);
		break;
	case cursor_st::cursor_hand:
		set_syscursor(SDL_SYSTEM_CURSOR_POINTER);
		break;
	default:
		break;
	}
}

void app_ctx::remove(form_x* fw)
{
	auto& v = forms;
	if (fw && v.size())
	{
		if (v[0] == fw) {
			for (auto it : v)
			{
				reforms.push(it);
			}
			v.clear();
		}
		else
		{
			v.erase(std::remove(v.begin(), v.end(), fw), v.end());
			if (!fw->_ref)
			{
				fw->_ref = 1;
				reforms.push(fw);
			}
		}
	}
}

void app_ctx::clearf()
{
	for (; reforms.size();) {
		auto it = reforms.front();
		if (it)
		{
			if (it == main)
				main = 0;
			delete it;
		}
		reforms.pop();
	}
}
const char* power_str(SDL_PowerState t) {
	const char* r = 0;
	switch (t)
	{
	case SDL_POWERSTATE_ERROR:
		r = (char*)u8"确定电源状态时出错";
		break;
	case SDL_POWERSTATE_UNKNOWN:r = (char*)u8"无法确定电源状态";
		break;
	case SDL_POWERSTATE_ON_BATTERY:r = (char*)u8"未插入电源，使用电池运行";
		break;
	case SDL_POWERSTATE_NO_BATTERY:r = (char*)u8"无电池";
		break;
	case SDL_POWERSTATE_CHARGING:r = (char*)u8"充电中";
		break;
	case SDL_POWERSTATE_CHARGED:r = (char*)u8"接通电源，电池已充满";
		break;
	default:
		break;
	}
	return r;
}
glm::vec3 app_ctx::get_power_info()
{
	glm::vec3 v = {};
	int s = 0, p = 0;
	v.z = SDL_GetPowerInfo(&s, &p);
	v.x = s < 0 ? 0 : s;
	v.y = p < 0 ? 0.0 : 0.01 * p;
	return v;
}

const char* app_ctx::get_power_str()
{
	auto p = get_power_info();
	return power_str((SDL_PowerState)p.z);
}
void app_ctx::kncdown()
{
	if (nc_down)
	{
		for (auto it : forms) {
			it->hide_child();
		}
		nc_down = false;
	}
}

int app_ctx::has_maximized(void* hwnd)
{
	int ret = 1;
	for (auto it : forms)
	{
		if (it->get_nptr() == hwnd && it->_flags & ef_borderless)
		{
			ret = 0;
		}
	}
	return ret;
}

SDL_AudioSpec get_spec(int format_idx, int channels, int freq) {

	SDL_AudioSpec spec = {};
	SDL_AudioFormat format[] = { SDL_AUDIO_S16, SDL_AUDIO_S32, SDL_AUDIO_F32 };
	format_idx = std::clamp(format_idx, 0, 2);
	spec.format = format[format_idx];
	spec.channels = channels;
	spec.freq = freq;
	return spec;
}
// 音频
uint32_t app_ctx::open_audio(int format, int channels, int freq)
{
	auto spec = get_spec(format, channels, freq);
	auto audio_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, freq && channels ? &spec : NULL);
	if (audio_device == 0) {
		SDL_Log("error\tCouldn't open audio device: %s", SDL_GetError());
	}
	return audio_device;
}
void app_ctx::close_audio(uint32_t dev)
{
	if (dev)
		SDL_CloseAudioDevice(dev);
}
uint32_t app_ctx::get_audio_device() {
	return audio_device;
}

// 类型、通道数、采样数
void* app_ctx::new_audio_stream0(int format_idx, int channels, int freq)
{
	return new_audio_stream(audio_device, format_idx, channels, freq);
}
void* app_ctx::new_audio_stream(uint32_t dev, int format_idx, int channels, int freq)
{
	auto spec = get_spec(format_idx, channels, freq);
	auto stream = SDL_CreateAudioStream(&spec, 0);
	if (!SDL_BindAudioStream(dev, stream)) {  /* once bound, it'll start playing when there is data available! */
		SDL_Log("Failed to bind stream to device: %s", SDL_GetError());
	}
	return stream;
}
void app_ctx::bindaudio(uint32_t dev, void* st) {
	SDL_AudioStream* as = (SDL_AudioStream*)st;
	auto d = SDL_GetAudioStreamDevice(as);
	if (d == 0 && !SDL_BindAudioStream(dev, as)) {  /* once bound, it'll start playing when there is data available! */
		SDL_Log("Failed to bind stream to device: %s", SDL_GetError());
	}
}
void app_ctx::unbindaudio(void* st) {
	SDL_AudioStream* as = (SDL_AudioStream*)st;
	if (st)
		SDL_UnbindAudioStream(as);
}
void app_ctx::unbindaudios(void** st, int count) {
	SDL_AudioStream** as = (SDL_AudioStream**)st;
	if (st && *as && count > 0)
		SDL_UnbindAudioStreams(as, count);
}
void app_ctx::free_audio_stream(void* st) {
	if (st)
	{
		unbindaudio(st);
		SDL_DestroyAudioStream((SDL_AudioStream*)st);
	}
}
int app_ctx::get_audio_dst_framesize(void* st)
{
	auto stream = (SDL_AudioStream*)st;
	SDL_AudioSpec dst_spec = {};
	SDL_GetAudioStreamFormat(stream, 0, &dst_spec);
	return SDL_AUDIO_FRAMESIZE(dst_spec);
}
int app_ctx::get_audio_stream_queued(void* st)
{
	return st ? SDL_GetAudioStreamQueued((SDL_AudioStream*)st) : 0;
}
int app_ctx::get_audio_stream_available(void* st)
{
	return st ? SDL_GetAudioStreamAvailable((SDL_AudioStream*)st) : 0;
}
void app_ctx::put_audio(void* stream, void* data, int len)
{
	auto st = (SDL_AudioStream*)stream;
	if (stream && data && len > 0) {
		SDL_PutAudioStreamData((SDL_AudioStream*)st, data, (int)len);
	}
}
void app_ctx::set_audio_gain(void* stream, float v)
{
	auto st = (SDL_AudioStream*)stream;
	if (stream) {
		SDL_SetAudioStreamGain(st, v);
	}
}

void app_ctx::pause_audio(void* st_, int v)
{
	auto st = (SDL_AudioStream*)st_;
	if (st)
	{
		if (v)
			SDL_PauseAudioStreamDevice(st);
		else
			SDL_ResumeAudioStreamDevice(st);
	}
}
bool app_ctx::mix_audio(uint8_t* dst, uint8_t* src, int format, size_t len, float volume)
{
	SDL_AudioFormat fmt[] = { SDL_AUDIO_S16 , SDL_AUDIO_S32 , SDL_AUDIO_F32 };
	format = glm::clamp(format, 0, 2);
	return SDL_MixAudio(dst, src, fmt[format], len, volume);
}
void app_ctx::clear_audio(void* st_)
{
	auto st = (SDL_AudioStream*)st_;
	if (st)
	{
		SDL_ClearAudioStream(st);
	}
}

int app_ctx::get_event()
{
	int64_t ts = 0;
#if 1
	SDL_Event e = {};
	while (SDL_PollEvent(&e) != 0)
	{
		if (e.type == SDL_EVENT_KEY_DOWN/* && e.key.repeat*/)
		{
			SDL_SetEventEnabled(SDL_EVENT_KEY_DOWN, 0);
			ts = 2;
		}
		if (e.type == SDL_EVENT_KEY_UP) {
			SDL_SetEventEnabled(SDL_EVENT_KEY_DOWN, 1);
		}
#ifndef EVWATCH
		call_cb(&e);
#endif 
	}
#endif
	clearf();
	if (forms.empty())
	{
		prev_time = 0;
	}
	if (e.type)
	{
		for (auto it : forms)
		{
			it->update_w();
		}
	}
	return ts;
}

void app_ctx::render(double delta)
{
	for (auto it : forms)
	{
		it->present();
	}
}

float app_ctx::update_event()
{
	float delta = 0.0f;
	do {
		int gev = get_event();
		if (forms.empty())break;
		fct->restart();
		uint32_t curr_time = SDL_GetTicks();
		if (prev_time > 0)
		{
			if (WantUpdateMonitors)
				UpdateMonitors();
			delta = (float)(curr_time - prev_time) / 1000.0f;
			avg_delta = avg_delta * (1.0 - talpha) + delta * talpha;
			cfps = 1.0 / avg_delta;
			cfps1 = 1.0 / delta;
		}
		prev_time = curr_time;
		if (_fps > 0 && gev < 1)
		{
			if (cfps < _fps)
				break;
			while ((fct->get_time() + fct->extra_time) < fct->screen_ticks_per_frame)
			{
				get_event();
				sleep_ms(waitms);
			}
			if (fct->get_time() < (fct->screen_ticks_per_frame)) {
				fct->extra_time -= fct->screen_ticks_per_frame - fct->get_time();
			}
			else {
				fct->extra_time += fct->get_time() - fct->screen_ticks_per_frame;
			}
		}
	} while (0);//prev_time > 0
	return delta;
}

size_t app_ctx::form_count()
{
	return forms.size();
}

int app_ctx::get_fps()
{
	return cfps;
}

#endif


// 窗口
#if 1

template<class T>
class lock_auto_x
{
public:
	T* p = 0;
public:
	lock_auto_x(T* k) {
		if (k)
		{
			k->lock();
			p = k;
		}
	}
	~lock_auto_x() {
		if (p)
			p->unlock();
	}

private:

};
form_x::form_x()
{
	io = &_io;
}

form_x::~form_x()
{
	for (auto it : childfs) {
		it->_ptr = 0;
		app->remove(it);
	}
	childfs.clear();
	_ref = 1;
	app->remove(this);
	destroy();
	sdlfree(clipstr); clipstr = 0;
	_ptr = 0;
	if (dragdrop)
	{
		delete dragdrop;
		dragdrop = 0;
	}
	if (_oledrop)
	{
		delete _oledrop;
		_oledrop = 0;
	}
}

void form_x::init_dragdrop()
{
	// OLE拖放
	if (!dragdrop)
	{
		dragdrop = new hz::drop_regs();
		dragdrop->init((HWND)pce::get_windowptr(_ptr));
		_oledrop = new hz::drop_info_cx();
		_oledrop->on_drop_cb = [=](int type, int idx)
			{
				ole_drop_et t = {};
				t.x = _dx; t.y = _dy;
				t.fmt = _oledrop->_type;
				//printf("drag ole :%d\t%d\n", t.x, t.y);
				if (type == 0)
				{
					_oledrop->_tmp.resize(1);
					_oledrop->_tmp[0] = _oledrop->_str.c_str();
				}
				else
				{
					auto ct = _oledrop->_files.size();
					t.count = ct;
					if (ct)
					{
						_oledrop->_tmp.resize(ct);
						int i = 0;
						for (auto& it : _oledrop->_files) {

							_oledrop->_tmp[i++] = it.c_str();
						}
					}
				}
				t.str = _oledrop->_tmp.data();
				t.count = _oledrop->_tmp.size();
				trigger((uint32_t)dev_event_type_e::ole_drop_e, &t);
			};
	}
	dragdrop->set_over([=](int x, int y, int fmt)
		{
			POINT pt = { x, y };
			ScreenToClient((HWND)pce::get_windowptr(_ptr), &pt);
			//printf("on drag ole :%d\t%d\tpos:%d\t%d\n", pt.x, pt.y, x, y);
			if (_dx != pt.x || _dy != pt.y)
			{
				_dx = pt.x;
				_dy = pt.y;
				hz::drop_info_cx* rt = 0;
				//if (_hot_em)
				//{
				//	//trigger((uint32_t)dev_event_type_e::ole_drop_e, &t);
				//	//rt = _hot_em->call_mouse_move_oledrop(pt.x, pt.y);
				//}
				if (!rt)
				{
					ole_drop_et t = {};
					t.x = pt.x; t.y = pt.y;
					{
						t.count = 0;
						t.fmt = fmt;
						t.has = &_oledrop->has;
						(*t.has) = 0;
						trigger((uint32_t)dev_event_type_e::ole_drop_e, &t);
						if (_oledrop->has == 1)
							rt = _oledrop;
					}
				}
				//if (rt)
				dragdrop->set_target(rt);
			}

		});

}
void form_x::set_curr_drop(hz::drop_info_cx* p)
{
#ifdef _WIN32
	if (dragdrop)
		dragdrop->set_target(p);
#endif
}


void form_x::trigger(uint32_t etype, void* e)
{
#if 0
	dev_event_type_e type = (dev_event_type_e)etype;
	lkecb.lock();
	auto cbs0 = *events_a;
	auto iptr = input_ptr;
	lkecb.unlock();
	et_un_t et = {};
	et.form = this;
	et.v.b = (mouse_button_et*)e;
	bool btn = !(type == dev_event_type_e::mouse_button_e && et.v.b->down == 0);
	do
	{
		if (type == dev_event_type_e::text_input_e || dev_event_type_e::text_editing_e == type)
		{
			if (iptr)
			{
				iptr->cb((uint32_t)type, &et, iptr->ptr);
			}
			return;
		}
		if (et.ret)
		{
			break;
		}
		if (cbs0.size())
		{
			for (auto it = cbs0.rbegin(); it != cbs0.rend(); it++)
			{
				if (it->cb) {
					it->cb((uint32_t)type, &et, it->ptr);
					if (et.ret && btn)
					{
						break;
					}
				}
			}
			if (et.ret && btn)
			{
				break;
			}
		}
	} while (0);
#endif
}
void form_x::set_capture()
{
	SDL_CaptureMouse(1);
}

void form_x::release_capture()
{
	SDL_CaptureMouse(0);
}
// 关闭窗口
void form_x::close() {
	SDL_Event e = {};
	e.type = e.window.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
	e.window.windowID = SDL_GetWindowID(_ptr);
	SDL_PushEvent(&e);
	//SDL_SendWindowEvent(_ptr, SDL_EVENT_WINDOW_CLOSE_REQUESTED, 0, 0);
}
// 显示/隐藏窗口
void form_x::show() {
	lock_auto_x lx(&lkqcv);
	qcmd_value.push({ (int)fcv_type::e_show,0,0,0 });
}
void form_x::hide() {
	printf("hide %p\n", this);
	lock_auto_x lx(&lkqcv);
	qcmd_value.push({ (int)fcv_type::e_hide,0,0,0 });
}
void form_x::show_reverse() {

	lock_auto_x lx(&lkqcv);
	qcmd_value.push({ (int)fcv_type::e_visible_rev,0,0,0 });
}
void form_x::raise()
{
	SDL_RaiseWindow(_ptr);
}
bool form_x::get_visible()
{
	auto f = SDL_GetWindowFlags(_ptr);
	bool v = !(f & SDL_WINDOW_HIDDEN);
	return v && _size.x > 0 && _size.y > 0;
}


void et2key(const SDL_Event* e, keyboard_et* ekm)
{
	if (!e || !(e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_KEY_UP))return;
	int ks = 0;
	auto pk = SDL_GetKeyboardState(&ks);
	int key = (int)e->key.scancode;
	ekm->sym = (e->key.key);
	ekm->keycode = SDL_GetKeyFromScancode(e->key.scancode, e->key.mod, 1);
	ekm->scancode = key;      /**< SDL physical key code - see ::SDL_Scancode for details */
	ekm->mod = e->key.mod;                 /**< current key modifiers */
	ekm->down = e->key.down;        /**< ::SDL_PRESSED or ::SDL_RELEASED */
	ekm->repeat = e->key.repeat;       /**< Non-zero if this is a key repeat */
	static int64_t ts = 0, ts1 = 0;

	if (ekm->repeat > 0) {
		ts += (e->button.timestamp - ts1) * 0.000001;
		ekm->repeat = ekm->repeat;
		//printf("ms: %d\n", ts); ts = 0; ts1 = e->button.timestamp;
	}
	else {
		ts = 0; ts1 = e->button.timestamp;
	}
	int f1 = SDLK_F1;
	int ms = SDL_GetModState();
	static int kcs[] = { SDLK_END, SDLK_DOWN, SDLK_PAGEDOWN, SDLK_LEFT, 0, SDLK_RIGHT, SDLK_HOME, SDLK_UP, SDLK_PAGEUP, SDLK_INSERT, SDLK_DELETE };
	if ((!(key<SDL_SCANCODE_KP_1 || key> SDL_SCANCODE_KP_PERIOD)) && !(ms & SDL_KMOD_NUM))
	{
		ekm->keycode = kcs[key - SDL_SCANCODE_KP_1];
	}
	//ekm->kn = SDL_GetKeyName(ekm->keycode);
	if (ms & SDL_KMOD_LCTRL || ms & SDL_KMOD_RCTRL)
	{
		ekm->kmod |= KM_CTRL;
	}
	if (ms & SDL_KMOD_LSHIFT || ms & SDL_KMOD_RSHIFT)
	{
		ekm->kmod |= KM_SHIFT;
	}
	if (ms & SDL_KMOD_LALT || ms & SDL_KMOD_RALT)
	{
		ekm->kmod |= KM_ALT;
	}
	if (ms & SDL_KMOD_LGUI || ms & SDL_KMOD_RGUI)
	{
		ekm->kmod |= KM_GUI;
	}

}

bool app_ctx::on_call_emit(const SDL_Event* e, form_x* pw)
{
	if (!pw)return false;
	switch (e->type)
	{
	case SDL_EVENT_DISPLAY_ORIENTATION:
	case SDL_EVENT_DISPLAY_ADDED:
	case SDL_EVENT_DISPLAY_REMOVED:
	case SDL_EVENT_DISPLAY_MOVED:
	case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
	{
		WantUpdateMonitors = true;
		return true;
	}
	case SDL_EVENT_MOUSE_MOTION:
	{
		//auto afp = ctx->get_activate_form();
		mouse_move_et mt = {};
		mouse_pos = { e->motion.x,e->motion.y };
		mt.xrel = e->motion.xrel;
		mt.yrel = e->motion.yrel;		// The relative motion in the XY direction 
		mt.which = e->motion.which;		// 鼠标实例 
		if (pw->viewports_enable)
		{
			int window_x = 0, window_y = 0;
			SDL_GetWindowPosition(SDL_GetWindowFromID(e->motion.windowID), &window_x, &window_y);
			mouse_pos.x += window_x;
			mouse_pos.y += window_y;
		}
		mt.x = mouse_pos.x;
		mt.y = mouse_pos.y;			// 鼠标移动坐标

		//pw->hittest(mouse_pos);
		if (pw->io) {
			pw->io->MousePos = mouse_pos;
			pw->io->MouseDelta = { mt.xrel,mt.yrel };
			int ms = SDL_GetModState();
			pw->io->KeyCtrl = (ms & SDL_KMOD_LCTRL || ms & SDL_KMOD_RCTRL);
			pw->io->KeyShift = (ms & SDL_KMOD_LSHIFT || ms & SDL_KMOD_RSHIFT);
			pw->io->KeyAlt = (ms & SDL_KMOD_LALT || ms & SDL_KMOD_RALT);
			pw->io->KeySuper = (ms & SDL_KMOD_LGUI || ms & SDL_KMOD_RGUI);
		}
		if (pw->_last_pos != mouse_pos)
		{
			pw->trigger((uint32_t)dev_event_type_e::mouse_move_e, &mt);
			//printf("win:%p\t%d %d\n", pw, (int)mt.x, (int)mt.y);
		}
		else
		{
			//printf("win: \t%d %d\n", (int)mt.x, (int)mt.y);
		}

		pw->_last_pos = mouse_pos;


		if (mt.cursor > cursor_st::cursor_null) {
			pw->app->set_defcursor(mt.cursor);
		}
	}
	break;
	case SDL_EVENT_MOUSE_WHEEL:
	{
		mouse_wheel_et wt = {};
		wt.which = e->wheel.which;
		int dir = e->wheel.direction;
		wt.x = e->wheel.x;
		wt.y = e->wheel.y;
		float preciseX = e->wheel.mouse_x;// preciseX;
		float preciseY = e->wheel.mouse_y;
		if (pw->io && !pw->_HitTest) {
			pw->io->wheel = { wt.x,wt.y };
		}
		pw->trigger((uint32_t)dev_event_type_e::mouse_wheel_e, &wt);
	}
	break;
#if 0
	case SDL_MULTIGESTURE:
	{
		auto& mg = e->mgesture;
		mgesture_et t = {};
		t.touchId = mg.touchId;
		t.dDist = mg.dDist;
		t.dTheta = mg.dTheta;
		t.numFingers = mg.numFingers;
		t.x = mg.x;
		t.y = mg.y;
		pw->trigger(t);
	}
	break;
#endif
	case SDL_EVENT_FINGER_DOWN:
	case SDL_EVENT_FINGER_UP:
	case SDL_EVENT_FINGER_MOTION:
	{
		finger_et ft = {};
		ft.t = e->type - SDL_EVENT_FINGER_DOWN + 1;
		ft.tid = e->tfinger.fingerID;
		ft.touchId = e->tfinger.fingerID;
		ft.x = e->tfinger.x; ft.y = e->tfinger.y;
		ft.pressure = e->tfinger.pressure;
		if (e->type == SDL_EVENT_FINGER_DOWN)
		{
			pw->hide_child();
		}
		pw->trigger((uint32_t)dev_event_type_e::finger_e, &ft);

	}break;
	//case SDL_TOUCH_MOUSEID: 
	case SDL_EVENT_MOUSE_BUTTON_DOWN:	//1
	case SDL_EVENT_MOUSE_BUTTON_UP:		//0
	{
		mouse_button_et t = {};
		t.which = e->button.which;
		t.button = e->button.button;
		t.down = e->button.down; //SDL_PRESSED; SDL_RELEASED;
		t.clicks = e->button.clicks;
		t.x = mouse_pos.x;// e->button.x;
		t.y = mouse_pos.y;// e->button.y;
		if (t.down)
		{
			pw->hide_child();
		}
		//pw->hittest({ t.x,t.y });
		if (pw->io && !pw->_HitTest) {
			pw->io->MouseDown[t.button - 1] = t.down;
		}
		else {
			pw->io->MouseDown[t.button - 1] = 0;
		}
		pw->trigger((uint32_t)dev_event_type_e::mouse_button_e, &t);

		if (pw && pw->capture_type)
		{
			if (t.down)
			{
				pw->set_capture();		// 锁定鼠标	
			}
			else {
				pw->release_capture();	// 释放鼠标			
				// 开始输入法
				if (pw->io && pw->io->ime_rect)
				{
					pw->start_text_input();
					pw->set_ime_pos(*pw->io->ime_rect);
				}
			}
		}
	}
	break;
	case SDL_EVENT_TEXT_INPUT:
	{
		text_input_et t = {};
		t.text = (char*)e->text.text;
		pw->trigger((uint32_t)dev_event_type_e::text_input_e, &t);
		auto irc = (glm::ivec4*)&t.x;
		pw->set_ime_pos(*irc);
	}
	break;
	case SDL_EVENT_TEXT_EDITING:
	{
		text_editing_et t = {};
		t.text = (char*)e->edit.text;
		t.start = e->edit.start;
		t.length = e->edit.length;
		pw->trigger((uint32_t)dev_event_type_e::text_editing_e, &t);
		auto irc = (glm::ivec4*)&t.x;
		pw->set_ime_pos(*irc);
	}
	break;
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
	{
		keyboard_et t = {};
		et2key(e, &t);
		auto kn = SDL_GetKeyName(t.keycode);
		pw->io->KeysDown[*kn] = t.down;
		pw->io->KeysDown[VK_SHIFT] = (t.kmod & KM_SHIFT);
		pw->io->KeyShift = (t.kmod & KM_SHIFT);
		pw->io->KeyAlt = (t.kmod & KM_ALT);
		pw->io->KeyCtrl = (t.kmod & KM_CTRL);
		pw->io->KeySuper = (t.kmod & KM_GUI);
		pw->trigger((uint32_t)dev_event_type_e::keyboard_e, &t);
	}
	break;
	case SDL_EVENT_DROP_BEGIN:
		pw->drop_text.clear();
		break;
	case SDL_EVENT_DROP_POSITION:
	{
		ole_drop_et t = {};
		t.x = e->drop.x;
		t.y = e->drop.y;
		if (pw->viewports_enable)
		{
			int window_x = 0, window_y = 0;
			SDL_GetWindowPosition(SDL_GetWindowFromID(e->motion.windowID), &window_x, &window_y);
			t.x += window_x;
			t.y += window_y;
		}
		pw->trigger((uint32_t)dev_event_type_e::ole_drop_e, &t);
		printf("pos\n");
	}break;
	case SDL_EVENT_DROP_COMPLETE:
	{
		ole_drop_et t = {};
		t.x = e->drop.x;
		t.y = e->drop.y;//结束
		if (pw->viewports_enable)
		{
			int window_x = 0, window_y = 0;
			SDL_GetWindowPosition(SDL_GetWindowFromID(e->motion.windowID), &window_x, &window_y);
			t.x += window_x;
			t.y += window_y;
		}
		if (pw->drop_text.size()) {
			if ('\n' == *pw->drop_text.rbegin())
				pw->drop_text.pop_back();
			auto str = pw->drop_text.data();
			t.str = (const char**)&str;
			t.count = 1;
			pw->trigger((uint32_t)dev_event_type_e::ole_drop_e, &t);
		}
	}break;
	case SDL_EVENT_DROP_TEXT:
	{
		if (e->drop.data)
		{
			pw->drop_text += (char*)e->drop.data;	pw->drop_text.push_back('\n');
		}
	}
	break;
	case SDL_EVENT_DROP_FILE:
	{
		if (e->drop.data)
		{
			pw->drop_text += (char*)e->drop.data;	pw->drop_text.push_back('\n');
		}
	}
	break;
	}
	return false;
}
int app_ctx::on_call_we(const SDL_Event* e, form_x* pw)
{
	if (!pw)return 0;
	int r = 0;
	switch (e->type)
	{
	case SDL_EVENT_WINDOW_DESTROYED:
	{
	}break;
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
	{
		int cbr = 0;
		if (pw->on_close_cb)
		{
			cbr = pw->on_close_cb();
		}
		if (pw->close_type || cbr == 1)
		{
			if (pw->parent)
			{
				pw->parent->remove_f(pw);
			}
			else {
				//pw->destroy();
				pw->app->remove(pw);
			}
		}
		else {
			pw->hide();
			r = 1;
		}
	}
	break;
	case SDL_EVENT_WINDOW_MOUSE_ENTER:
	{
	}break;
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
	{
	}break;
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
	{
	}break;
	case SDL_EVENT_WINDOW_FOCUS_LOST:
	{
		pw->focus_lost();
	}break;
	case SDL_EVENT_WINDOW_MINIMIZED:
	{
		pw->save_size = pw->_size;
		pw->on_size({});
	}break;
	case SDL_EVENT_WINDOW_RESTORED:
	{
		pw->on_size(pw->save_size); pw->present_e();
	}break;
	case SDL_EVENT_WINDOW_RESIZED:
	{
		pw->save_size = pw->_size;
		pw->on_size({ e->window.data1,e->window.data2 });
	}break;
	case SDL_EVENT_WINDOW_MOVED:
	{
		pw->on_moved({ e->window.data1,e->window.data2 });
	}break;
	default:
		break;
	}
	if (e->type < SDL_EVENT_WINDOW_FIRST || e->type > SDL_EVENT_WINDOW_LAST)
	{
		on_call_emit(e, pw);
	}
	else
	{
		//if (e_window_cb)
		//	e_window_cb(pw, e->type);
	}
	return 0;
}

void app_ctx::UpdateMonitors()
{
	WantUpdateMonitors = false;
	int display_count;
	SDL_DisplayID* displays = SDL_GetDisplays(&display_count);
	for (int n = 0; n < display_count; n++)
	{
		// Warning: the validity of monitor DPI information on Windows depends on the application DPI awareness settings, which generally needs to be set in the manifest or at runtime.
		SDL_DisplayID display_id = displays[n];
		PlatformMonitor monitor = {};
		SDL_Rect r;
		SDL_GetDisplayBounds(display_id, &r);
		monitor.MainPos = monitor.WorkPos = glm::vec2((float)r.x, (float)r.y);
		monitor.MainSize = monitor.WorkSize = glm::vec2((float)r.w, (float)r.h);
		if (SDL_GetDisplayUsableBounds(display_id, &r) && r.w > 0 && r.h > 0)
		{
			monitor.WorkPos = glm::vec2((float)r.x, (float)r.y);
			monitor.WorkSize = glm::vec2((float)r.w, (float)r.h);
		}
		monitor.DpiScale = SDL_GetDisplayContentScale(display_id); // See https://wiki.libsdl.org/SDL3/README-highdpi for details.
		monitor.PlatformHandle = (void*)(intptr_t)n;
		if (monitor.DpiScale <= 0.0f)
			continue; // Some accessibility applications are declaring virtual monitors with a DPI of 0, see #7902.
		monitors.push_back(monitor);
	}
	SDL_free(displays);
}

void form_x::on_size(const glm::ivec2& ss)
{
	if (ss != _size)
		_size = ss;
}

void form_x::on_moved(const glm::ivec2& ss)
{
	if (ss != _pos)
	{
		_pos = ss;
	}
}


void form_x::destroy()
{
	if (_ptr)
	{
		SDL_DestroyWindow(_ptr); _ptr = 0;
	}

}
void show_window(SDL_Window* ptr, bool visible) {

	if (visible)
	{
		auto flags = SDL_GetWindowFlags(ptr);
		//SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN, flags & SDL_WINDOW_TOOLTIP || flags & SDL_WINDOW_POPUP_MENU ? "1" : "0");
		SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, (flags & SDL_WINDOW_TOOLTIP || flags & SDL_WINDOW_POPUP_MENU) ? "0" : "1");
		SDL_ShowWindow(ptr);
	}
	else
	{
		SDL_HideWindow(ptr);
	}
}
//void form_x::show_menu(mnode_t* m)
//{
//	if (m && m->child.size())
//	{
//		if (m->indep) {
//			form_x* f = new_form_popup(this, m->fsize.x, m->fsize.y);
//			f->bind(m->ui);
//			f->set_size(m->fsize);
//			f->set_pos(m->fpos);
//		}
//		else {
//			m->ui->set_pos(m->fpos);
//			bind(m->ui);
//		}
//	}
//}
void form_x::remove_f(form_x* p)
{
	if (p)
	{
		auto& v = childfs;
		v.erase(std::remove_if(v.begin(), v.end(), [p](form_x* r) {return r == p; }), v.end());
		app->remove(p);
	}
}

void* form_x::get_nptr()
{
	void* p = 0;
#ifdef _WIN32
	p = (HWND)pce::get_windowptr(_ptr);
#endif
	return p;
}


void form_x::add_vk_semaphores(int64_t wait_semaphore, int64_t signal_semaphore, uint32_t wait_stage_mask)
{
	//if (wait_semaphore || signal_semaphore)
	//{
	//	if (!wait_stage_mask)
	//		wait_stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	//	if (renderer)
	//		SDL_AddVulkanRenderSemaphores(renderer, wait_stage_mask, wait_semaphore, signal_semaphore);
	//}
}

gui_io_state_t* form_x::get_io()
{
	return io;
}

void form_x::update_w()
{
	int w, h;
	SDL_GetWindowSize(_ptr, &w, &h);
	auto flags = SDL_GetWindowFlags(_ptr);
	if (flags & SDL_WINDOW_MINIMIZED)
		w = h = 0;
	_size.x = w;
	_size.y = h;
	if (flags & SDL_WINDOW_MOUSE_FOCUS)
	{
		// 有鼠标焦点
	}
	int qc = qcmd_value.size();
	if (qcmd_value.size() && _ptr) {
		//printf("mm%d\n", qc);
		lock_auto_x lx(&lkqcv);
		glm::ivec2 oldsize = { w,h }, old_pos = get_pos(), ss = oldsize, ps = old_pos;
		for (; qcmd_value.size();)
		{
			auto v = qcmd_value.front(); qcmd_value.pop();
			auto t = (fcv_type)v.x;
			switch (t)
			{
			case fcv_type::e_null:
				break;
			case fcv_type::e_show:
				visible = true; visible_old = false;
				break;
			case fcv_type::e_hide:
				visible = false; visible_old = true;
				break;
			case fcv_type::e_visible_rev:
				visible = !visible_old;
				break;
			case fcv_type::e_size:
				ss = { v.y, v.z };
				break;
			case fcv_type::e_pos:
				ps = { v.y, v.z };
				break;
			default:
				break;
			}
		}
		if (oldsize != ss)
		{
			SDL_SetWindowSize(_ptr, ss.x, ss.y);
		}
		if (visible_old != visible)
		{
			visible_old = visible;
			if (visible && old_pos != ps)
				SDL_SetWindowPosition(_ptr, ps.x, ps.y);// 显示前移动
			show_window(_ptr, visible);
			if (!visible && old_pos != ps)
				SDL_SetWindowPosition(_ptr, ps.x, ps.y);// 先隐藏再移动
		}
		else {
			if (old_pos != ps)
			{
				SDL_SetWindowPosition(_ptr, ps.x, ps.y);
			}
		}
	}

	do
	{
		auto r = glm::ivec4(0);
		if (io)
		{
			if (io->ime_rect)
			{
				r = *io->ime_rect;
				io->ime_rect->w = 0;
			}
		}
		if (!(r.w >= 0 && r.z > 0))break;
#ifdef _WIN320
		auto hWnd = (HWND)pce::get_windowptr(_ptr);
		if (!hWnd)break;
		HIMC hIMC = ::ImmGetContext(hWnd);
		if (hIMC)
		{
			COMPOSITIONFORM cf;
			cf.dwStyle = CFS_POINT;
			RECT rc = { 0 };
			if (r.x > 0 || r.y > 0)
			{
				rc.left = r.x;
				rc.top = r.y;
			}
			cf.rcArea.top = cf.rcArea.left = cf.rcArea.right = cf.rcArea.bottom = 0;
			cf.ptCurrentPos.x = rc.left;//输入法坐标
			cf.ptCurrentPos.y = rc.top;
			::ImmSetCompositionWindow(hIMC, &cf);
			::ImmReleaseContext(hWnd, hIMC);
		}
#else 
		SDL_Rect rect = { r.x,r.y, r.z, r.w };
		SDL_SetTextInputArea(_ptr, &rect, 0);
#endif
	} while (0);

}
void form_x::update(float delta)
{
	is_render = false;
	if (!visible)return;
	// Setup display size (every frame to accommodate for window resizing)

	int display_w, display_h;

	SDL_GetWindowSizeInPixels(_ptr, &display_w, &display_h);
	//float wx, wy;
	//SDL_RenderCoordinatesToWindow(renderer, 100,200,&wx, &wy);
	display_size = _size;
	if (_size.x > 0 && _size.y > 0)
	{
		display_framebuffer_scale = glm::vec2((float)display_w / _size.x, (float)display_h / _size.y);
	}
	else
	{
		return;
	}
	int dwt = 0;
	if (io) {
		io->DeltaTime = delta;
		io->WantCaptureMouse = false;
	}
	//for (auto it : _draw_data) {
	//	dwt += render_update(*it, delta);
	//	if (it->press_test() && io)
	//		io->WantCaptureMouse = true;
	//}
	//if (up_cb)
	//{
	//	up_cb(delta, &dwt);
	//}
	//for (auto it : _draw_data) {
	//	dwt += render_build(*it);
	//} 
}
void form_x::set_state()
{
	if (!visible || !app || display_size.x < 1 || display_size.y < 1)
		return;

	is_render = true;
}

void form_x::warp_mouse_in_window(float x, float y)
{
	if (x < 0 || y < 0)
	{
		x = _size.x / 2;
		y = _size.y / 2;
	}
	SDL_WarpMouseInWindow(_ptr, x, y);
}
void form_x::set_mouse_mode(bool grab_enable, bool rmode)
{
	_rmode = rmode;
	SDL_SetWindowRelativeMouseMode(_ptr, rmode);	//设置窗口的相对鼠标模式。
	SDL_SetWindowMouseGrab(_ptr, grab_enable);		// 设置鼠标范围在窗口内
	if (rmode)warp_mouse_in_window(-1, -1);
}


bool form_x::is_minimized()
{
	return !visible || (SDL_GetWindowFlags(_ptr) & SDL_WINDOW_MINIMIZED);
}


char* form_x::get_clipboard0()
{
	char* ret = 0;
	if (SDL_HasClipboardText())
	{
		ret = (char*)SDL_GetClipboardText();
		if (clipstr)
			SDL_free(clipstr);
		clipstr = ret;
	}
	return ret;
}
void form_x::sdlfree(void* p)
{
	if (p)
	{
		SDL_free(p);
	}
}
void form_x::sdlfree(void** p)
{
	if (p && *p)
	{
		SDL_free(*p); *p = 0;
	}
}
void form_x::set_clipboard(const char* str)
{
	if (str)
		SDL_SetClipboardText(str);
}

bool dragdrop_begin(const wchar_t* str, size_t size)
{
	if (size == -1)size = wcslen(str);
	return str && size ? hz::do_dragdrop_begin(str, size) : false;
}
bool form_x::do_dragdrop_begin(const wchar_t* str, size_t size)
{
	return dragdrop_begin(str, size);
}
void form_x::new_tool_tip(const glm::ivec2& pos, const void* str)
{


}

//bool form_x::hittest(const glm::ivec2& pos)
//{
//	_HitTest = false;
//	for (auto it : _draw_data) {
//		if (it->hittest(pos)) {
//			_HitTest = true;
//			break;
//		}
//	}
//	return _HitTest;
//}

void form_x::focus_lost()
{
	if (_focus_lost_hide)
	{
		hide();
	}
	else
	{
		hide_child();
	}
}

void form_x::hide_child()
{
	for (auto it : childfs) {
		if (it->_focus_lost_hide)
			it->hide();
	}
}
void form_x::set_displayaffinity(bool v)
{
#ifdef _WIN32
	auto hwnd = (HWND)get_nptr();
	if (hwnd)
		SetWindowDisplayAffinity(hwnd, v ? WDA_MONITOR : WDA_NONE);// 反截图 
#endif
}



#if 1
SDL_Surface* SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth, Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
	return SDL_CreateSurface(width, height,
		SDL_GetPixelFormatForMasks(depth, Rmask, Gmask, Bmask, Amask));
}

SDL_Surface* SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int width, int height, int depth, Uint32 format)
{
	return SDL_CreateSurface(width, height, (SDL_PixelFormat)format);
}

SDL_Surface* SDL_CreateRGBSurfaceFrom(void* pixels, int width, int height, int depth, int pitch, Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
	return SDL_CreateSurfaceFrom(width, height, SDL_GetPixelFormatForMasks(depth, Rmask, Gmask, Bmask, Amask), pixels, pitch);
}

SDL_Surface* SDL_CreateRGBSurfaceWithFormatFrom(void* pixels, int width, int height, int depth, int pitch, Uint32 format)
{
	return SDL_CreateSurfaceFrom(width, height, (SDL_PixelFormat)format, pixels, pitch);
}
#else
SDL_Surface* SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth, Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);

SDL_Surface* SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int width, int height, int depth, Uint32 format);

SDL_Surface* SDL_CreateRGBSurfaceFrom(void* pixels, int width, int height, int depth, int pitch, Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);

SDL_Surface* SDL_CreateRGBSurfaceWithFormatFrom(void* pixels, int width, int height, int depth, int pitch, Uint32 format);

#endif

void form_x::set_icon(const char* fn)
{
	if (!fn || !*fn)return;
	auto window = _ptr;
	SDL_Surface* icon = nullptr;
	uint32_t rmask, gmask, bmask, amask;
	//rmask = 0xFF000000; gmask = 0x00FF0000; bmask = 0x0000FF00; amask = 0x000000FF;	// RGBA8888模式
	rmask = 0x000000FF; gmask = 0x0000FF00; bmask = 0x00FF0000; amask = 0xFF000000;	// RGBA8888模式
	//rmask = 0xFF000000; gmask = 0x00FF0000; bmask = 0x0000FF00; amask = 0x00000000;	// RGB8888模式
	//加载图片文件创建一个RGB Surface
	int width = 0, height = 0, channels = 0;
	auto data = (uint32_t*)stbi_load(fn, &width, &height, &channels, 4);
	int depth = 32, pitch = width * 4;
	if (data)
	{
		icon = SDL_CreateRGBSurfaceFrom(data, width, height, depth, pitch, rmask, gmask, bmask, amask);
		if (NULL == icon) return;
		SDL_SetWindowIcon(window, icon);
		SDL_DestroySurface(icon);
		stbi_image_free(data);
	}
}

void form_x::set_icon(const uint32_t* d, int w, int h)
{
	if (!d || w < 6 || h < 6)return;
	auto window = _ptr;
	SDL_Surface* icon = nullptr;
	uint32_t rmask, gmask, bmask, amask;
	rmask = 0x000000FF; gmask = 0x0000FF00; bmask = 0x00FF0000; amask = 0xFF000000;	// RGBA8888模式
	//加载图片文件创建一个RGB Surface
	int depth = 32, pitch = w * 4;
	icon = SDL_CreateRGBSurfaceFrom((void*)d, w, h, depth, pitch, rmask, gmask, bmask, amask);
	if (NULL == icon) return;
	SDL_SetWindowIcon(window, icon);
	SDL_DestroySurface(icon);
}

void form_x::set_alpha(bool is)
{

}
#endif


SDL_HitTestResult HitTestCallback2(SDL_Window* win, const SDL_Point* area, void* data)
{
	int winWidth = 0, winHeight = 0;
	SDL_GetWindowSize(win, &winWidth, &winHeight);
	SDL_HitTestResult ret = SDL_HITTEST_NORMAL;
#if 0
	const int RESIZE_AREA = 8;
	const int RESIZE_AREAC = RESIZE_AREA * 2;
	auto fw = (form_x*)data;
	int tbh = 0;
	do
	{
		if (fw && fw->mmove_type == false)break;
		// Resize top
		if (area->x < RESIZE_AREAC && area->y < RESIZE_AREAC)
		{
			ret = SDL_HITTEST_RESIZE_TOPLEFT; break;
		}
		if (area->x > winWidth - RESIZE_AREAC && area->y < RESIZE_AREAC)
		{
			ret = SDL_HITTEST_RESIZE_TOPRIGHT; break;
		}
		if (area->x < RESIZE_AREA)
		{
			ret = SDL_HITTEST_RESIZE_LEFT; break;
		}
		if (area->y < RESIZE_AREA)
		{
			ret = SDL_HITTEST_RESIZE_TOP; break;
		}


		if (area->x < RESIZE_AREAC && area->y > winHeight - RESIZE_AREAC)
		{
			ret = SDL_HITTEST_RESIZE_BOTTOMLEFT; break;
		}
		if (area->x > winWidth - RESIZE_AREAC && area->y > winHeight - RESIZE_AREAC)
		{
			ret = SDL_HITTEST_RESIZE_BOTTOMRIGHT; break;
		}
		if (area->x > winWidth - RESIZE_AREA)
		{
			ret = SDL_HITTEST_RESIZE_RIGHT; break;
		}
		if (area->y > winHeight - RESIZE_AREA)
		{
			ret = SDL_HITTEST_RESIZE_BOTTOM; break;
		}
		if (fw)
		{
			tbh = fw->titlebarheight;
			if (tbh > 0) {
				if (area->y < tbh && area->x < winWidth - 128)
				{
					// Title bar
					ret = SDL_HITTEST_DRAGGABLE; break;
				}
			}
			else if (!fw->hittest({ area->x,area->y }))
			{
				ret = SDL_HITTEST_DRAGGABLE; break;
			}
		}

	} while (0);
#endif
	//printf("%d\n", ret);
	return ret;
}

glm::ivec2 form_x::get_size()
{
	glm::ivec2 r = {};
	SDL_GetWindowSize(_ptr, &r.x, &r.y);
	return r;
}
glm::ivec2 form_x::get_pos()
{
	glm::ivec2 r = {};
	SDL_GetWindowPosition(_ptr, &r.x, &r.y);
	return r;
}
void form_x::set_size(const glm::vec2& v)
{
	lock_auto_x lx(&lkqcv);
	qcmd_value.push({ (int)fcv_type::e_size,v,0 });
}
void form_x::set_pos(const glm::vec2& v)
{
	lock_auto_x lx(&lkqcv);
	qcmd_value.push({ (int)fcv_type::e_pos,v,0 });

}


void form_x::start_text_input()
{
	if (!SDL_TextInputActive(_ptr))
		SDL_StartTextInput(_ptr);
}
void form_x::stop_text_input()
{
	if (SDL_TextInputActive(_ptr))
		SDL_StopTextInput(_ptr);
}
bool form_x::text_input_active()
{
	return SDL_TextInputActive(_ptr);
}
void form_x::set_ime_pos(const glm::ivec4& r)
{
	ime_pos = r;
}
void form_x::enable_window(bool bEnable)
{
#ifdef _WIN32
	auto hWnd = (HWND)pce::get_windowptr(_ptr);
	EnableWindow(hWnd, bEnable);
#endif
}
void form_x::set_mouse_pos(const glm::ivec2& pos)
{
	SDL_WarpMouseInWindow(_ptr, pos.x, pos.y);
}
void form_x::set_mouse_pos_global(const glm::ivec2& pos)
{
	SDL_WarpMouseGlobal(pos.x, pos.y);
}
void form_x::show_cursor()
{
	SDL_ShowCursor();
}
void form_x::hide_cursor()
{
	SDL_HideCursor();
}
void form_x::flash_window(int opera)
{
	auto o = std::clamp((SDL_FlashOperation)opera, SDL_FlashOperation::SDL_FLASH_CANCEL, SDL_FlashOperation::SDL_FLASH_UNTIL_FOCUSED);
	if (_ptr)
	{
		SDL_FlashWindow(_ptr, o);
	}
}


#endif
