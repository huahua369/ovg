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

#ifndef BIT_INC
#define BIT_INC(x) (1<<x)
#endif

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
	ef_vulkan = BIT_INC(8),			// vk渲染
	ef_gpu = BIT_INC(9),			// gpu渲染 
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

struct PlatformMonitor
{
	glm::vec2 MainPos, MainSize;	// Coordinates of the area displayed on this monitor (Min = upper left, Max = bottom right)
	glm::vec2 WorkPos, WorkSize;	// Coordinates without task bars / side bars / menu bars. Used to avoid positioning popups/tooltips inside this region. If you don't have this info, please copy the value for MainPos/MainSize.
	float DpiScale;				// 1.0f = 96 DPI
	void* PlatformHandle;			// Backend dependant data (e.g. HMONITOR, GLFWmonitor*, SDL Display Index, NSScreen*)
};
struct os_window {
	SDL_Window* window = nullptr;
	uint32_t id = 0;

	uint32_t parent_id = 0;      // 0 = 顶级窗口
	bool is_popup = false;       // 菜单 / tooltip / popup
	bool close_with_parent = true;
	bool should_close = false;
};

class WindowMgr
{
public:
	SDL_GPUDevice* device = nullptr;
	SDL_Cursor* system_cursor[SDL_SYSTEM_CURSOR_COUNT] = {};
	std::vector<PlatformMonitor> monitors;
	bool nc_down = false;
private:
	std::vector<std::unique_ptr<os_window>> windows_;
public:
	WindowMgr();
	~WindowMgr();

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
};
