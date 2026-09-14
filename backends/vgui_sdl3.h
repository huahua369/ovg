#pragma once
/*
vgui sdl3

创建日期：2026-9-12
*/
#include <mutex>
#include <vector>
#include <string>
#include <vgui.h>


namespace hz {
	// 获取监视器缩放比例,glm::ivec2*
	float get_monitor_scale(void* pels);
	// 获取打印机名称
	std::vector<std::string> get_print_devname();
	std::string get_temp_path();
	class drop_info_cx;
	class drop_regs;
}


enum form_flags_e
{
	ef_null = 0,					// ef_default
	ef_fullscreen = BIT_INC(0),		// 全屏
	ef_utility = BIT_INC(1),		// 不出现在任务栏
	ef_resizable = BIT_INC(2),		// 可以拉伸大小
	ef_transparent = BIT_INC(3),	// 透明窗口
	ef_borderless = BIT_INC(4),		// 无系统边框
	ef_popup = BIT_INC(5),			// 弹出式窗口，需要有父窗口
	ef_tooltip = BIT_INC(6),		// 工具提示窗口，需要有父窗口 
	ef_gpu = BIT_INC(7),			// gpu渲染 
	ef_vulkan = BIT_INC(8),			// vk渲染
	ef_dx11 = BIT_INC(11), 			// dx11渲染
	ef_vsync = BIT_INC(13),
	ef_minimized = BIT_INC(14),
	ef_maximized = BIT_INC(15),
	ef_default = ef_resizable | ef_vulkan
};
enum class fcv_type {
	e_null,
	e_show,
	e_hide,
	e_visible_rev,
	e_size,
	e_pos
};
enum class WindowType :uint8_t {
	Regular,
	Tooltip,
	PopupMenu,
	Modal
};
struct PlatformMonitor
{
	glm::vec2 MainPos, MainSize;	// Coordinates of the area displayed on this monitor (Min = upper left, Max = bottom right)
	glm::vec2 WorkPos, WorkSize;	// Coordinates without task bars / side bars / menu bars. Used to avoid positioning popups/tooltips inside this region. If you don't have this info, please copy the value for MainPos/MainSize.
	float DpiScale;					// 1.0f = 96 DPI
	void* PlatformHandle;			// Backend dependant data (e.g. HMONITOR, GLFWmonitor*, SDL Display Index, NSScreen*)
};
struct os_window {
	SDL_Window* window = nullptr;
	os_window* parent = 0;      // 0 = 顶级窗口
	gui_viewport* viewport = nullptr;
	uint32_t id = 0;
	WindowType type = WindowType::Regular;
	bool close_with_parent = true;
	bool pending_close = false;          // ★ 新增
	bool mouse_in_client = false;        // ★ 新增（可选）

	// UI 回调
	std::function<void()> on_close;
	std::vector<os_window*> children;
	~os_window();
	// 锁定/释放鼠标
	void set_capture();
	void release_capture();
	// 开始输入法
	void start_text_input();
	void stop_text_input();
	bool text_input_active();
	// 设置输入法坐标
	void set_ime_pos(const glm::ivec4& r);
	// 禁用窗口鼠标键盘操作。模态窗口用
	void enable_window(bool bEnable);
	// 移动鼠标到窗口指定位置
	void set_mouse_pos(const glm::ivec2& pos);
	void set_mouse_pos_global(const glm::ivec2& pos);
	// 显示/隐藏鼠标
	void show_cursor();
	void hide_cursor();
};
struct PointerState {
	SDL_Window* window = nullptr;
	glm::ivec2  pos{};
	bool pressed = false;
};
class docking_mgr {
public:
	bool viewports_enabled = false; // 默认关闭，Docking 才开
public:
	void set_viewports_enabled(bool v) { viewports_enabled = v; }

private:
};
class app_mgr
{
public:
	SDL_GPUDevice* device = nullptr;
	SDL_Cursor* system_cursor[SDL_SYSTEM_CURSOR_COUNT] = {};
	std::vector<PlatformMonitor> monitors;
	os_window* main_window = 0;
	gui_io_mk_state _mouse_key = {};
	gui_viewport* active_viewport_ = 0;
	docking_mgr docking_ = {};
	bool nc_down = false;
	bool WantUpdateMonitors = true;
	bool capture_type = true;
private:
	std::vector<std::unique_ptr<os_window>> windows_;
public:
	app_mgr();
	~app_mgr();

	bool init_gpu(bool is_vulkan);
	// 创建主窗口、普通窗口
	os_window* create(const char* title, int w, int h, uint32_t flags);
	// 创建普通窗口、菜单、工具提示、实用窗口、可指定父级的窗口
	os_window* create2(const char* title, int x, int y, int w, int h, uint32_t flags, os_window* parent);
	os_window* find(uint32_t id);
	void destroy(uint32_t id);

	void shutdown();

	size_t window_count() const;
	std::vector<std::unique_ptr<os_window>>& windows();

	void kncdown();
	bool has_maximized(void* nwptr);
	void UpdateMonitors();
	int get_event();
	void process_event(const SDL_Event* e);
	gui_io_state_t* io();
	docking_mgr& docking();
	void set_defcursor(cursor_st t);
private:
	void set_syscursor(int type);
};
