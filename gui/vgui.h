#pragma once

#include <functional>
#include <string>
#include <set>

/*
struct mouse_move_et;
struct mouse_button_et;
struct mouse_wheel_et;
struct keyboard_et;
struct text_editing_et;
struct text_input_et;
struct finger_et;
struct ole_drop_et;
*/

#ifndef BIT_INC
#define BIT_INC(x) (1<<x)
#endif

// 窗口设备事件
enum class dev_event_type_e :uint32_t {
	none = 0,
	mouse_move_e,
	mouse_button_e,
	mouse_wheel_e,
	keyboard_e,
	text_editing_e,
	text_input_e,
	finger_e,
	ole_drop_e,
	max_det
};
// 控件事件类型
enum class event_type_e :uint32_t {
	none = 0,
	mouse_move,		// 鼠标移动，
	mouse_down,		// 鼠标按下
	mouse_up,		// 鼠标弹起
	mouse_wheel,	// 滚轮消息
	// on需要拾取目标（*矩形、*圆形、自定义判断）才能触发，单击、双击、三击（指定毫秒内）
	on_keypress,	// 按键，需要输入焦点
	on_input,		// 输入法字符，需要输入焦点
	on_editing,		// 输入法: 显示编辑的字符

	on_click,		// 单击双击三击时鼠标在目标范围才能触发
	on_dblclick,
	on_tripleclick,
	on_enter,		// 鼠标进入
	on_leave,		// 鼠标离开
	on_move,		// 鼠标在元素内移动
	on_down,		// 鼠标在元素内按下
	on_up,			// 鼠标弹起，触发条件：先down，鼠标位置不限
	on_hover,		// 鼠标停留在目标区域n毫秒触发
	on_scroll,		// 滚动条，目标范围内触发

	on_drag,		// 元素被拖动时鼠标移动触发
	on_dragstart,	// 当拖拽元素开始被拖拽的时候触发的事件，此事件作用在被拖曳元素上
	on_dragend,		// 当拖拽完成后触发的事件，此事件作用在被拖曳元素上

	on_dragenter,	// 当拖曳元素进入目标元素的时候触发的事件，此事件作用在目标元素上
	on_dragleave,	// 当被鼠标拖动的对象离开其容器范围内时触发此事件，此事件作用在目标元素上
	on_dragover,	// 拖拽元素在目标元素上移动的时候触发的事件，此事件作用在目标元素上
	on_drop,		// 被拖拽的元素在目标元素上同时鼠标放开触发的事件，此事件作用在目标元素上

	on_ole_dragover,// 接收OLE拖放在目标元素上移动的时
	on_ole_drop,	// 接收OLE拖放

	on_touch,		// 触控事件

	et_max_num
};
#if 1

enum class cursor_st :uint32_t
{
	cursor_null,
	cursor_arrow,
	cursor_ibeam,
	cursor_wait,
	cursor_no,
	cursor_hand,
};

struct mouse_move_et
{
	int x, y;			// 鼠标移动坐标
	int xrel, yrel;		// The relative motion in the XY direction 
	uint8_t which;		// 鼠标实例 
	cursor_st cursor;	// 切换鼠标光标用
};
struct mouse_button_et		// 鼠标弹起
{
	uint8_t which;
	uint8_t button;       // The mouse button index 1左，3右，2中
	uint8_t down;        // ::SDL_PRESSED or ::SDL_RELEASED 
	uint8_t clicks;       // 1 for single-click, 2 for double-click, etc. 
	int x, y;
};
struct mouse_wheel_et	// 滚轮消息
{
	int x, y;
	uint8_t which;
};
#ifndef KM_CTRL
#define KM_CTRL 1
#define KM_SHIFT 2
#define KM_ALT 4
#define KM_GUI 8
#endif
struct keyboard_et
{
	uint16_t scancode;  // SDL physical key code - see ::SDL_Scancode for details 
	uint16_t mod;       // current key modifiers 
	int sym;            // SDL virtual key code - see ::SDL_Keycode for details 
	int keycode;
	int16_t kmod;		// Control=1 Shift=2 Alt=4 8Cmd/Super/Windows
	int8_t down;       // ::SDL_PRESSED or ::SDL_RELEASED 
	int8_t repeat;      // Non-zero if this is a key repeat 

};
struct text_editing_et
{
	char* text;// [32 * 8] ;  //The editing text
	int start;		// The start cursor of selected editing text 
	int length;		// The length of selected editing text 
	int x, y, w, h;
};
struct text_input_et
{
	char* text;  // The input text 
	int x, y, w, h;
};
// touch事件结构体
struct finger_et {
	int tid, touchId;
	float x, y, pressure; // 坐标、压力
	int t;	// FINGERDOWN=1 UP=2 MOTION=3 
};
// 接收ole拖动，结束数据count大于0
struct ole_drop_et
{
	const char** str;
	int count;			// 大于1一般是文件列表
	int fmt = -1;		// 0文本，1文件
	float x, y;			// 鼠标坐标

	int* has;			// 是否接收, 0不接收，1接收

};
struct gui_io_state_t;

struct dev_event_t
{
	union
	{
		struct mouse_move_et* m;
		struct mouse_button_et* b;
		struct mouse_wheel_et* w;
		struct keyboard_et* k;
		struct text_editing_et* e;
		struct text_input_et* t;
		struct finger_et* f;
		struct ole_drop_et* d;
	}v = {};
	gui_io_state_t* io = 0;
	dev_event_type_e type = dev_event_type_e::none;
	uint8_t ret = 0;
};

#endif // 1

// gui事件管理
struct gui_io_mk_state {
	bool KeysDown[512];
	bool KeyCtrl : 1;
	bool KeyShift : 1;
	bool KeyAlt : 1;
	bool KeySuper : 1;
};
struct gui_io_state_t
{
	gui_io_mk_state* mks;
	glm::vec2 MouseDelta;
	glm::vec2 MousePos;
	glm::vec2 wheel;
	glm::ivec4* ime_rect = 0;
	bool MouseDown[8] = {};
	float deltaTime = 0.0f;
	int8_t clicks = 0;
	bool WantCaptureMouse : 1 = false;
	bool WantCaptureKey : 1 = false;
	bool WantTextInput : 1 = false;
};

// 发起拖放文件
void do_dragdrop_file(const char** fn, int count);
// 发起拖放文本
void do_dragdrop_str(const char* str, int count);

struct font_family_t;
struct ovg_ctx_cb;
struct rvg_t;
/*
1.普通状态2，鼠标hover状态  3.active 点击状态  4.focus 取得焦点状态  4.disable禁用状态
*/
enum class BTN_STATE :uint8_t
{
	STATE_NOMAL = BIT_INC(0),
	STATE_HOVER = BIT_INC(1),
	STATE_ACTIVE = BIT_INC(2),
	STATE_FOCUS = BIT_INC(3),
	STATE_DISABLE = BIT_INC(4),
};
struct event_entity_t
{
public:
	void* ptr = 0;
	glm::ivec2 _pos = {};	// 控件坐标
	glm::ivec2 _size = {};	// 控件大小 
	glm::ivec2 curpos = {};	// 当前拖动鼠标坐标 
	glm::ivec2 hscroll = { 1,1 };	// x=1则受水平滚动条影响，y=1则受垂直滚动条影响
	int _bst = 1;					// 鼠标状态
	int _old_bst = 0;				// 鼠标状态  
	glm::ivec2 cursor = { 0,-1 };	// 光标坐标
	bool has_drag = false;			// 是否有拖动事件 
	bool outer_scroll = false;		// 鼠标不在范围内也响应滚轮事件
	std::unordered_map<int, std::function<void()>> calls[2];
	dev_event_t* cde = 0;		// 临时指针
	gui_io_state_t* io = 0;		// 鼠标键盘状态指针
	event_type_e etype = {};	// on事件类型
	glm::ivec2 mouse_pos = {};	// 处理后的鼠标坐标
public:
	event_entity_t();
	virtual ~event_entity_t();
	// event_type_e::none则监听所有
	void set_event_dev(dev_event_type_e e, std::function<void(dev_event_t* dv)> cb);
	void set_on_event(event_type_e e, std::function<void(event_type_e type, const glm::vec2& mps)> cb);
	void set_on_text(std::function<void(text_input_et* t)> cb);
	void set_on_editing(std::function<void(text_editing_et* te)> cb);
	void remove(dev_event_type_e e);
	void remove(event_type_e e);
	// 删除on_text和on_editing事件监听
	void remove_text();
	void call(int idx, int type);
	bool hittest(const glm::ivec2& mpos);
};
class dispatcher_cx
{
public:
	glm::ivec2 pos = {};			// 这批事件区的父级坐标
	std::vector<event_entity_t*> v;
public:
	dispatcher_cx();
	~dispatcher_cx();
	void add(event_entity_t* p);
	bool trigger(dev_event_t* d);
private:

};

struct gui_viewport {
	gui_io_state_t io = {};
	std::string drop_text;
	glm::ivec2 _last_pos = {};
	std::vector<dispatcher_cx*> _v;
public:
	void trigger(dev_event_t* e);
};
