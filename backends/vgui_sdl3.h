#pragma once
/*
vgui sdl3

创建日期：2026-9-12
*/
#include <mutex>
#include <vector>
#include <string>
#include <vgui.h>
class app_ctx;
class form_x;
class Timer;

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
	glm::vec2 MainPos, MainSize;      // Coordinates of the area displayed on this monitor (Min = upper left, Max = bottom right)
	glm::vec2 WorkPos, WorkSize;      // Coordinates without task bars / side bars / menu bars. Used to avoid positioning popups/tooltips inside this region. If you don't have this info, please copy the value for MainPos/MainSize.
	float   DpiScale;               // 1.0f = 96 DPI
	void* PlatformHandle;         // Backend dependant data (e.g. HMONITOR, GLFWmonitor*, SDL Display Index, NSScreen*)
};

class app_ctx
{
public:
	SDL_Cursor* system_cursor[SDL_SYSTEM_CURSOR_COUNT] = {};
	Timer* fct = 0;
	std::vector<form_x*> forms;		// 窗口列表
	std::queue<form_x*> reforms;
	std::vector<PlatformMonitor> monitors;
	form_x* main = 0;

	// 内部数据路径、外部数据路径、外部缓存路径；
	std::string internalDataPath;
	std::string externalDataPath;
	std::string externalCachePath;

	uint32_t audio_device = 0;

	glm::ivec2 mouse_pos = {};

	uint32_t prev_time = 0;
	float avg_delta = 0.0f;
	float talpha = 0.05f;  // 采样系数（调整平滑度）
	int _fps = 60;
	int fms = 0;
	int waitms = 1;
	int cfps = 0;
	int cfps1 = 0;

	bool nc_down = false;
	bool WantUpdateMonitors = true;
public:
	app_ctx();
	~app_ctx();

	// 更新事件，返回一帧的毫秒数
	float update_event();
	size_t form_count();
	int get_fps();
	void call_cb(SDL_Event* e);
	// 睡眠毫秒
	static void sleep_ms(int ms);
	// 睡眠纳秒
	static void sleep_ns(int ns);
	static uint64_t get_ticks();
	void set_fps(int n);
	void set_syscursor(int type);
	void set_defcursor(cursor_st t);
	void remove(form_x* f);
	void clearf();
	// 获取电池信息
	glm::vec3 get_power_info();
	const char* get_power_str();
	// 隐藏子窗口
	void kncdown();
	int has_maximized(void* hwnd);
public:

	// 音频播放
	uint32_t open_audio(int format, int channels, int freq);
	void close_audio(uint32_t dev);
	uint32_t get_audio_device();
	// 音频	format	0=S16, 1=S32, 2=F32
	void* new_audio_stream0(int format, int channels, int freq);
	static void* new_audio_stream(uint32_t dev, int format, int channels, int freq);
	static void free_audio_stream(void* st);
	static void bindaudio(uint32_t dev, void* st);
	static void unbindaudio(void* st);
	static void unbindaudios(void** st, int count);
	static int get_audio_stream_queued(void* st);
	static int get_audio_dst_framesize(void* st);
	static int get_audio_stream_available(void* st);
	static void put_audio(void* stream, void* data, int len);
	static void set_audio_gain(void* stream, float v);
	// v=0播放、1暂停
	static void pause_audio(void* st_, int v);
	// format：0=S16 , 1=S32 ,2=F32，volume取0-1
	static bool mix_audio(uint8_t* dst, uint8_t* src, int format, size_t len, float volume);
	static void clear_audio(void* st);
private:
	int get_event();
	void render(double delta);
	int on_call_we(const SDL_Event* e, form_x* pw);
	bool on_call_emit(const SDL_Event* e, form_x* pw);
	void UpdateMonitors();
};

class form_x
{
public:
	SDL_Window* _ptr = 0;
	app_ctx* app = 0;				// 应用ctx
	glm::ivec2 _pos = {};			// 窗口位置
	glm::ivec2 _size = {};			// 窗口大小
	glm::ivec2 display_size = {};	// 窗口显示大小
	glm::ivec2 save_size = {};		// 保存窗口大小
	glm::vec2 display_framebuffer_scale = {};

	std::function<int()> on_close_cb;		// 关闭事件 
	std::string title;
	char* clipstr = 0;

	glm::ivec4 ime_pos = {};
	void* activate_ptr = 0;	// 激活的对象

	gui_io_state_t _io = {};	// 默认io
	gui_io_state_t* io = {};	// 自定义io

	std::vector<form_x*> childfs;
	form_x* parent = 0;
	form_x* tooltip = 0;	// 提示窗口
	// 接收拖动OLE管理
	hz::drop_regs* dragdrop = 0;
	// 默认接收ole
	hz::drop_info_cx* _oledrop = 0;
	std::string drop_text;
	int _dx = -1, _dy = -1;
	glm::ivec2 _last_pos = { -1,-1 };	// 上次鼠标位置
	void* uptr = 0;						// 用户设置指针

	std::queue<glm::ivec4> qcmd_value;	// 操作列表
	std::mutex lkqcv;

	// 标题栏高度
	int titlebarheight = 0;
	int _flags = 0;
	int uct = 0;// 更新计数
	// 锁定鼠标
	bool capture_type = true;
	bool close_type = true;		// 关闭按钮风格：true关闭退出，false则隐藏窗口
	bool mmove_type = true;		// 鼠标拖动
	bool _HitTest = false;
	int8_t _ref = false;
	bool _focus_lost_hide = false;	// 失去焦点隐藏 
	bool viewports_enable = false;	// docking用
	bool is_render = true;
private:
	bool visible = true;
	bool visible_old = true;
	bool _rmode = false;
public:
	form_x();
	~form_x();
	void init_dragdrop();
	void set_curr_drop(hz::drop_info_cx* p);
	void add_event(void* ud, std::function<void(dev_event_t* e, void* ud)> cb);
	size_t remove_event(void* ud);
	void move2end_e(void* ud);
	void trigger(uint32_t etype, void* e);

	void set_capture();
	void release_capture();
	void on_size(const glm::ivec2& ss);
	void on_moved(const glm::ivec2& ss);
	//销毁窗口
	void destroy();
	// 关闭窗口
	void close();
	// 显示/隐藏窗口
	void show();
	void hide();
	void show_reverse();
	// 置顶窗口
	void raise();
	bool get_visible();
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
	void flash_window(int opera);
	// 设置窗口图标
	void set_icon(const char* fn);
	void set_icon(const uint32_t* d, int w, int h);
	void set_alpha(bool is);

	glm::ivec2 get_size();
	glm::ivec2 get_pos();
	void set_size(const glm::vec2& v);
	void set_pos(const glm::vec2& pos);

	void remove_f(form_x* c);

	// 获取hwnd
	void* get_nptr();
	void draw_rects(const glm::vec4* rects, int n, const glm::vec4& color);
	// 鼠标移到窗口坐标-1中心
	void warp_mouse_in_window(float x, float y);
	// 限制鼠标在窗口内grab_enable，相对窗口隐藏鼠标rmode
	void set_mouse_mode(bool grab_enable, bool rmode);

	// 同步
	void add_vk_semaphores(int64_t wait_semaphore, int64_t signal_semaphore, uint32_t wait_stage_mask);
public:
	gui_io_state_t* get_io();
	void update_w();
	void update(float delta);
	void set_state();
	void present();
	void present_e();
	bool is_minimized();

	// 获取粘贴板文本
	char* get_clipboard0();
	// 释放SDL申请的内存
	void sdlfree(void* p);
	void sdlfree(void** p);
	// 设置粘贴板文本
	void set_clipboard(const char* str);
	bool do_dragdrop_begin(const wchar_t* str, size_t size);
	void new_tool_tip(const glm::ivec2& pos, const void* str);
	// 返回是否命中ui
	//bool hittest(const glm::ivec2& pos);
	void focus_lost();
	void hide_child();
	// 反截图
	void set_displayaffinity(bool v);
private:
};


