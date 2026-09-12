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

#include <vulkan/vulkan.h>


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

	void show_window(SDL_Window* ptr, bool visible) {

		if (visible)
		{
			auto flags = SDL_GetWindowFlags(ptr);
			SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, (flags & SDL_WINDOW_TOOLTIP || flags & SDL_WINDOW_POPUP_MENU) ? "0" : "1");
			SDL_ShowWindow(ptr);
		}
		else
		{
			SDL_HideWindow(ptr);
		}
	}

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




// 窗口应用管理


#ifdef _WIN32

bool wMessageHook(void* userdata, MSG* msg) {
	auto app = (WindowMgr*)userdata;
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


//判断“点是否在某个 popup 内”
bool point_in_popup(os_window* popup, SDL_Window* target_win, const glm::ivec2& pos) {
	if (!popup || !popup->window) return false;
	if (popup->window != target_win) return false;
	int w, h;
	SDL_GetWindowSize(popup->window, &w, &h);
	return pos.x >= 0 && pos.x < w && pos.y >= 0 && pos.y < h;
}
bool is_child_popup(os_window* child, os_window* ancestor) {
	while (child) {
		if (child == ancestor) return true;
		child = child->parent;
	}
	return false;
}
void collect_popups_recursive(os_window* node, std::vector<os_window*>& out) {
	if (!node) return;

	if (node->type == WindowType::PopupMenu) {
		out.push_back(node);
	}

	for (auto* child : node->children) {
		collect_popups_recursive(child, out);
	}
}



os_window::~os_window() {
	window = 0;
}
WindowMgr::WindowMgr()
{
	SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, false);
	SDL_SetEventEnabled(SDL_EVENT_DROP_TEXT, false);
	// Enable native IME.
	SDL_SetHintWithPriority(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1", SDL_HINT_OVERRIDE);
	SDL_SetHintWithPriority(SDL_HINT_IME_IMPLEMENTED_UI, "composition", SDL_HINT_OVERRIDE);
#ifdef _DEBUG
	SDL_SetHint(SDL_HINT_RENDER_VULKAN_DEBUG, "true");
#endif
#ifdef __ANDROID__
	SDL_SetHintWithPriority(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "1", SDL_HINT_OVERRIDE);
	SDL_SetHintWithPriority(SDL_HINT_TOUCH_MOUSE_EVENTS, "1", SDL_HINT_OVERRIDE);
#endif

	UpdateMonitors();
}
WindowMgr::~WindowMgr() { shutdown(); }

bool WindowMgr::init_gpu(bool is_vulkan)
{
	bool debugmode = false;
#ifdef _DEBUG
	debugmode = true;
#endif // _DEBUG 
	if (is_vulkan)
	{
		SDL_PropertiesID props = SDL_CreateProperties();
		SDL_GPUVulkanOptions vo = {};
		VkPhysicalDeviceScalarBlockLayoutFeatures scalarFeatures = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES,
			.pNext = NULL,
			.scalarBlockLayout = VK_TRUE,
		};
		VkPhysicalDeviceVulkan12Features enabledFeatures12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };

		enabledFeatures12.scalarBlockLayout = VK_TRUE;
		// 1b. Vulkan 1.1 复合特性（包含 shaderDrawParameters） 
		VkPhysicalDeviceVulkan11Features vk11Features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,	   .pNext = &enabledFeatures12, };
		// 其他 1.1 特性默认由驱动填充，我们只关心 shaderDrawParameters
		vk11Features.shaderDrawParameters = VK_TRUE;
		// 以下字段留 0，让 SDL/Vulkan 使用默认值
		vk11Features.storageBuffer16BitAccess = VK_FALSE;
		vk11Features.uniformAndStorageBuffer16BitAccess = VK_FALSE;
		vk11Features.storagePushConstant16 = VK_FALSE;
		vk11Features.storageInputOutput16 = VK_FALSE;
		vk11Features.multiview = VK_FALSE;
		vk11Features.multiviewGeometryShader = VK_FALSE;
		vk11Features.multiviewTessellationShader = VK_FALSE;
		vk11Features.variablePointersStorageBuffer = VK_FALSE;
		vk11Features.variablePointers = VK_FALSE;
		vk11Features.protectedMemory = VK_FALSE;
		vk11Features.samplerYcbcrConversion = VK_TRUE;
		const char* devext[] = { VK_KHR_MAINTENANCE_4_EXTENSION_NAME,
			VK_KHR_MAINTENANCE_5_EXTENSION_NAME,VK_EXT_SCALAR_BLOCK_LAYOUT_EXTENSION_NAME };
		const char* insext[] = { VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
		// 2. 填充 SDL_GPUVulkanOptions
		SDL_GPUVulkanOptions vkOpts = {
			.vulkan_api_version = VK_API_VERSION_1_2,  // 必须 >= 1.2 才能启用 scalarBlockLayout
			.feature_list = &vk11Features,             // pNext 链头
			.vulkan_10_physical_device_features = NULL, // 不需要额外 1.0 特性
			.device_extension_count = 3,
			.device_extension_names = devext,
			.instance_extension_count = 1,
			.instance_extension_names = insext,
		};
		SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER, &vkOpts);
		SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, "vulkan");
		SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
		SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, debugmode);
		device = SDL_CreateGPUDeviceWithProperties(props);
		SDL_DestroyProperties(props);
	}
	else {
		device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, debugmode, nullptr);
	}
	if (!device) {
		SDL_Log("GPU device create failed: %s", SDL_GetError());
		return false;
	}
	return true;
}

void WindowMgr::kncdown()
{
	if (nc_down)
	{
		for (auto& it : windows_) {
			if (it->parent) {
				pce::show_window(it->window, false);
			}
		}
		nc_down = false;
	}
}
bool WindowMgr::has_maximized(void* nwptr)
{
	int ret = 1;
	for (auto& it : windows_) {
		if (pce::get_windowptr(it->window) == nwptr) {
			auto f = SDL_GetWindowFlags(it->window);
			bool v = (f & SDL_WINDOW_BORDERLESS);
			if (v)
			{
				ret = 0; break;
			}
		}
	}
	return ret;
}
os_window* WindowMgr::create(const char* title, int w, int h, uint32_t iflags) {
	SDL_Window* handle = 0;
	auto flags = pce::get_flags(iflags);
	SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "1");
	handle = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	os_window* p = 0;
	if (handle)
	{
		auto win = std::make_unique<os_window>();
		win->window = handle;
		pce::set_property(handle, "osw.ptr", win.get());
		win->id = SDL_GetWindowID(handle);

		SDL_ClaimWindowForGPUDevice(device, handle);
		p = win.get();
		windows_.push_back(std::move(win));
	}
	return p;
}
os_window* WindowMgr::create2(const char* title, int x, int y, int w, int h, uint32_t iflags, os_window* parent) {
	SDL_Window* handle = 0;
	auto flags = pce::get_flags(iflags);
	if (iflags & ef_tooltip || iflags & ef_popup)
	{
		SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
		handle = SDL_CreatePopupWindow(parent ? parent->window : nullptr, x, y, w, h, flags | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_NOT_FOCUSABLE);
		SDL_SetWindowAlwaysOnTop(handle, true);
	}
	else {
		SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "1");
		handle = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	}
	os_window* p = 0;
	if (handle)
	{
		auto win = std::make_unique<os_window>();
		win->window = handle;
		p = win.get();
		pce::set_property(handle, "osw.ptr", p);
		win->id = SDL_GetWindowID(handle);
		if (iflags & ef_popup)
			win->type = WindowType::PopupMenu;
		if (iflags & ef_tooltip)
			win->type = WindowType::Tooltip;

		if (parent) {
			parent->children.push_back(p);
			win->parent = parent;
		}
		SDL_ClaimWindowForGPUDevice(device, handle);
		windows_.push_back(std::move(win));
	}
	return p;
}


os_window* WindowMgr::find(uint32_t id) {
	for (auto& w : windows_)
		if (w->id == id) return w.get();
	return nullptr;
}

void WindowMgr::destroy(uint32_t id) {
	for (auto it = windows_.begin(); it != windows_.end(); ++it) {
		if ((*it)->id == id) {
			SDL_ReleaseWindowFromGPUDevice(device, it->get()->window);
			if ((*it)->parent == 0)
				SDL_DestroyWindow(it->get()->window);
			windows_.erase(it);
			return;
		}
	}
}

void WindowMgr::shutdown() {
	if (device)
		SDL_WaitForGPUIdle(device);
	for (auto& w : windows_) {
		SDL_ReleaseWindowFromGPUDevice(device, w->window);
		if (!w->parent)
			SDL_DestroyWindow(w->window);
	}
	windows_.clear();
	if (device) {
		SDL_DestroyGPUDevice(device);
		device = nullptr;
	}
}

size_t WindowMgr::window_count() const { return windows_.size(); }

std::vector<std::unique_ptr<os_window>>& WindowMgr::windows() { return windows_; }

void WindowMgr::UpdateMonitors()
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
void WindowMgr::process_event(const SDL_Event& e)
{

}
int WindowMgr::get_event()
{
	int ts = 0;
	SDL_Event e = {};
	while (SDL_PollEvent(&e) != 0)
	{
		if (e.type == SDL_EVENT_QUIT) {
			ts = -1; break;
		}
		if (e.type == SDL_EVENT_KEY_DOWN/* && e.key.repeat*/)
		{
			SDL_SetEventEnabled(SDL_EVENT_KEY_DOWN, 0);
			ts = 2;
		}
		if (e.type == SDL_EVENT_KEY_UP) {
			SDL_SetEventEnabled(SDL_EVENT_KEY_DOWN, 1);
		}
		switch (e.type)
		{
		case SDL_EVENT_DISPLAY_ORIENTATION:
		case SDL_EVENT_DISPLAY_ADDED:
		case SDL_EVENT_DISPLAY_REMOVED:
		case SDL_EVENT_DISPLAY_MOVED:
		case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
		{
			WantUpdateMonitors = true;
			UpdateMonitors();
			break;
		}
		}
		process_event(e);
	}
	return ts;
}