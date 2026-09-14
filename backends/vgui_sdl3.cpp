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
#include <typeindex>
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
	auto app = (app_mgr*)userdata;
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
void os_window::set_capture()
{
	SDL_CaptureMouse(1);
}
void os_window::release_capture()
{
	SDL_CaptureMouse(0);
}
void os_window::start_text_input()
{
	if (!SDL_TextInputActive(window))
		SDL_StartTextInput(window);
}
void os_window::stop_text_input()
{
	if (SDL_TextInputActive(window))
		SDL_StopTextInput(window);
}
bool os_window::text_input_active()
{
	return SDL_TextInputActive(window);
}
// 设置输入法坐标
void os_window::set_ime_pos(const glm::ivec4& r) {
	do
	{
		if (r.w < 1 || r.z < 1)break;
#ifdef _WIN320
		auto hWnd = (HWND)pce::get_windowptr(window);
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
		SDL_SetTextInputArea(window, &rect, 0);
#endif
	} while (0);
}
// 禁用窗口鼠标键盘操作。模态窗口用
void os_window::enable_window(bool bEnable)
{
	//#ifdef _WIN32
	//	auto hWnd = (HWND)pce::get_windowptr(window);
	//	EnableWindow(hWnd, bEnable);
	//#endif
	SDL_SetWindowModal(window, bEnable);
}
// 移动鼠标到窗口指定位置
void os_window::set_mouse_pos(const glm::ivec2& pos)
{
	SDL_WarpMouseInWindow(window, pos.x, pos.y);
}
void os_window::set_mouse_pos_global(const glm::ivec2& pos)
{
	SDL_WarpMouseGlobal(pos.x, pos.y);
}
// 显示/隐藏鼠标
void os_window::show_cursor()
{
	SDL_ShowCursor();
}
void os_window::hide_cursor()
{
	SDL_HideCursor();
}


app_mgr::app_mgr()
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
app_mgr::~app_mgr() { shutdown(); }

bool app_mgr::init_gpu(bool is_vulkan)
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

void app_mgr::kncdown()
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
bool app_mgr::has_maximized(void* nwptr)
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
os_window* app_mgr::create(const char* title, int w, int h, uint32_t iflags) {
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
os_window* app_mgr::create2(const char* title, int x, int y, int w, int h, uint32_t iflags, os_window* parent) {
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


os_window* app_mgr::find(uint32_t id) {
	for (auto& w : windows_)
		if (w->id == id) return w.get();
	return nullptr;
}

void app_mgr::destroy(uint32_t id) {
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

void app_mgr::shutdown() {
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

size_t app_mgr::window_count() const { return windows_.size(); }

std::vector<std::unique_ptr<os_window>>& app_mgr::windows() { return windows_; }

void app_mgr::UpdateMonitors()
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
// todo event

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
gui_io_state_t* app_mgr::io()
{
	return active_viewport_ ? &active_viewport_->io : nullptr;
}
void app_mgr::process_event(const SDL_Event* e)
{
	dev_event_t dev = {};
	dev.type = dev_event_type_e::none;
	auto pw = find(e->window.windowID);
	if (pw) {
		active_viewport_ = pw->viewport;
	}
	auto pwio = io();
	bool viewports_enabled = docking().viewports_enabled;
	auto viewport = active_viewport_;
	if (!viewport || !pwio)
		return;
	dev.io = pwio;
	switch (e->type) {
	case SDL_EVENT_MOUSE_MOTION:
	{
		mouse_move_et mt = {};
		glm::ivec2 mouse_pos = { e->motion.x,e->motion.y };
		mt.xrel = e->motion.xrel;
		mt.yrel = e->motion.yrel;		// The relative motion in the XY direction 
		mt.which = e->motion.which;		// 鼠标实例 
		dev.type = dev_event_type_e::mouse_move_e;
		if (viewports_enabled)
		{
			int window_x = 0, window_y = 0;
			SDL_GetWindowPosition(SDL_GetWindowFromID(e->motion.windowID), &window_x, &window_y);
			mouse_pos.x += window_x;
			mouse_pos.y += window_y;
		}
		mt.x = mouse_pos.x;
		mt.y = mouse_pos.y;			// 鼠标移动坐标
		//pw->hittest(mouse_pos);
		if (pwio) {
			pwio->MousePos = mouse_pos;
			pwio->MouseDelta = { mt.xrel,mt.yrel };
			//int ms = SDL_GetModState();
			//pwio->mks->KeyCtrl = (ms & SDL_KMOD_LCTRL || ms & SDL_KMOD_RCTRL);
			//pwio->mks->KeyShift = (ms & SDL_KMOD_LSHIFT || ms & SDL_KMOD_RSHIFT);
			//pwio->mks->KeyAlt = (ms & SDL_KMOD_LALT || ms & SDL_KMOD_RALT);
			//pwio->mks->KeySuper = (ms & SDL_KMOD_LGUI || ms & SDL_KMOD_RGUI);
		}
		if (viewport->_last_pos != mouse_pos)
		{
			dev.v.m = &mt;
			viewport->trigger(&dev);
		}
		viewport->_last_pos = mouse_pos;
		if ((int)mt.cursor > 0) {
			set_defcursor(mt.cursor);
		}
	}
	break;
	case SDL_EVENT_MOUSE_WHEEL:
	{
		mouse_wheel_et t = {};
		t.which = e->wheel.which;
		int dir = e->wheel.direction;
		t.x = e->wheel.x;
		t.y = e->wheel.y;
		float preciseX = e->wheel.mouse_x;
		float preciseY = e->wheel.mouse_y;
		if (pwio /*&& !pw->_HitTest*/) {
			pwio->wheel = { t.x,t.y };
		}
		dev.v.w = &t;
		dev.type = dev_event_type_e::mouse_wheel_e;
		viewport->trigger(&dev);
	}
	break;
	case SDL_EVENT_FINGER_DOWN:
	case SDL_EVENT_FINGER_UP:
	case SDL_EVENT_FINGER_MOTION:
	{
		finger_et ft = {};
		dev.type = dev_event_type_e::finger_e;
		ft.t = e->type - SDL_EVENT_FINGER_DOWN + 1;
		ft.tid = e->tfinger.fingerID;
		ft.touchId = e->tfinger.fingerID;
		ft.x = e->tfinger.x; ft.y = e->tfinger.y;
		ft.pressure = e->tfinger.pressure;
		if (e->type == SDL_EVENT_FINGER_DOWN)
		{
			//pw->hide_child();
		}
		dev.v.f = &ft;
		viewport->trigger(&dev);

	}break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:	//1
	case SDL_EVENT_MOUSE_BUTTON_UP:		//0
	{
		mouse_button_et t = {};
		dev.type = dev_event_type_e::mouse_button_e;
		t.which = e->button.which;
		t.button = e->button.button;
		t.down = e->button.down; //SDL_PRESSED; SDL_RELEASED;
		t.clicks = e->button.clicks;
		t.x = e->button.x;
		t.y = e->button.y;
		if (t.down)
		{
			pwio->clicks = 0;
		}
		else {
			pwio->clicks = t.clicks;
		}
		pwio->MouseDown[t.button - 1] = t.down;
		dev.v.b = &t;
		viewport->trigger(&dev);
		if (pw && capture_type)
		{
			if (t.down)
			{
				pw->set_capture();		// 锁定鼠标	
			}
			else {
				pw->release_capture();	// 释放鼠标			
				// 开始输入法
				if (pwio->WantTextInput)
				{
					pw->start_text_input();
					pw->set_ime_pos(*pwio->ime_rect);
				}
			}
		}
	}
	break;
	case SDL_EVENT_TEXT_INPUT:
	{
		text_input_et t = {};
		dev.type = dev_event_type_e::text_input_e;
		t.text = (char*)e->text.text;
		dev.v.t = &t;
		viewport->trigger(&dev);
		auto irc = (glm::ivec4*)&t.x;
		pw->set_ime_pos(*irc);
	}
	break;
	case SDL_EVENT_TEXT_EDITING:
	{
		text_editing_et t = {};
		dev.type = dev_event_type_e::text_editing_e;
		t.text = (char*)e->edit.text;
		t.start = e->edit.start;
		t.length = e->edit.length;
		dev.v.e = &t;
		viewport->trigger(&dev);
		auto irc = (glm::ivec4*)&t.x;
		pw->set_ime_pos(*irc);
	}
	break;
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
	{
		keyboard_et t = {};
		et2key(e, &t);
		dev.type = dev_event_type_e::keyboard_e;
		auto kn = SDL_GetKeyName(t.keycode);
		pwio->mks->KeysDown[*kn] = t.down;
		pwio->mks->KeysDown[VK_SHIFT] = (t.kmod & KM_SHIFT);
		pwio->mks->KeyShift = (t.kmod & KM_SHIFT);
		pwio->mks->KeyAlt = (t.kmod & KM_ALT);
		pwio->mks->KeyCtrl = (t.kmod & KM_CTRL);
		pwio->mks->KeySuper = (t.kmod & KM_GUI);

		dev.v.k = &t;
		viewport->trigger(&dev);
	}
	break;
	case SDL_EVENT_DROP_BEGIN:
		viewport->drop_text.clear();
		break;
	case SDL_EVENT_DROP_POSITION:
	{
		ole_drop_et t = {};
		t.x = e->drop.x;
		t.y = e->drop.y;
		dev.type = dev_event_type_e::ole_drop_e;
		if (viewports_enabled)
		{
			int window_x = 0, window_y = 0;
			SDL_GetWindowPosition(SDL_GetWindowFromID(e->motion.windowID), &window_x, &window_y);
			t.x += window_x;
			t.y += window_y;
		}
		dev.v.d = &t;
		viewport->trigger(&dev);
		//printf("pos\n");
	}break;
	case SDL_EVENT_DROP_COMPLETE:
	{
		ole_drop_et t = {};
		t.x = e->drop.x;
		t.y = e->drop.y;//结束

		dev.type = dev_event_type_e::ole_drop_e;
		if (viewports_enabled)
		{
			int window_x = 0, window_y = 0;
			SDL_GetWindowPosition(SDL_GetWindowFromID(e->motion.windowID), &window_x, &window_y);
			t.x += window_x;
			t.y += window_y;
		}
		if (viewport->drop_text.size()) {
			if ('\n' == *viewport->drop_text.rbegin())
				viewport->drop_text.pop_back();
			auto str = viewport->drop_text.data();
			t.str = (const char**)&str;
			t.count = 1;
			dev.v.d = &t;
			viewport->trigger(&dev);
		}
	}break;
	case SDL_EVENT_DROP_TEXT:
	{
		if (e->drop.data)
		{
			viewport->drop_text += (char*)e->drop.data;	viewport->drop_text.push_back('\n');
		}
	}
	break;
	case SDL_EVENT_DROP_FILE:
	{
		if (e->drop.data)
		{
			viewport->drop_text += (char*)e->drop.data;	viewport->drop_text.push_back('\n');
		}
	}
	break;
	}

}
int app_mgr::get_event()
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
		process_event(&e);
	}
	return ts;
}

docking_mgr& app_mgr::docking() { return docking_; }

void app_mgr::set_syscursor(int type)
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
void app_mgr::set_defcursor(cursor_st t)
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
