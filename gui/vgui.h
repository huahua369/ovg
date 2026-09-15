#pragma once
/*
gui结构：
	事件信息：*代码实现,event_obj_t管理事件注册、事件状态保存
	布局信息：*节点布局、算法
	渲染信息：*矢量图、位图、文本、裁剪等配置信息
	动画信息：*时间线(数组),一个animation_t有多个通道，anim_ctx管理动画数据
	绑定关系：动画、属性、事件之间的绑定关系


*/
#include <functional>
#include <string>
#include <set>

#include <ovg.h>

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
struct event_obj_t
{
public:
	glm::ivec2 _pos = {};	// 控件坐标
	glm::ivec2 _size = {};	// 控件大小 
	glm::ivec2 hscroll = { 1,1 };	// x=1则受水平滚动条影响，y=1则受垂直滚动条影响 
	glm::ivec2 curpos = {};	// 当前拖动鼠标坐标 
	int _bst = 1;					// 鼠标状态
	int _old_bst = 0;				// 鼠标状态  
	std::unordered_map<int, std::function<void()>>* calls = 0;
	dev_event_t* cde = 0;		// 临时指针
	gui_io_state_t* io = 0;		// 鼠标键盘状态指针
	event_type_e etype = {};	// on事件类型
	glm::ivec2 mouse_pos = {};	// 处理后的鼠标坐标
	glm::ivec2 cb_count = {};	// 事件处理函数计数
	bool has_drag = false;			// 是否有拖动事件 
	bool is_drag = false;			// 拖动状态
	bool outer_scroll = false;		// 鼠标不在范围内也响应滚轮事件
public:
	event_obj_t();
	virtual ~event_obj_t();
	// event_type_e::none则监听所有
	void set_event_dev(dev_event_type_e e, std::function<void(dev_event_t* dv)> cb);
	void set_on_event(event_type_e e, std::function<void(event_type_e type, const glm::vec2& mps)> cb);
	void set_on_text(std::function<void(text_input_et* t)> cb);
	void set_on_editing(std::function<void(text_editing_et* te)> cb);
	void remove(dev_event_type_e e);
	void remove(event_type_e e);
	// 删除on_text和on_editing事件监听
	void remove_on_text();
	void call(int idx, int type);
	bool hittest(const glm::ivec2& mpos)const;
	std::unordered_map<int, std::function<void()>>& get_cbs(int i);
};

enum class widget_type :uint8_t
{
	WT_WIDGET,
	WT_DIV,
};
class widget_t :public event_obj_t
{
public:
	widget_type wtype = widget_type::WT_WIDGET;
	std::string _name;
	widget_t* parent = 0;
public:
	widget_t();
	widget_t(widget_type t);
	~widget_t();
	virtual widget_t* hit_test(const glm::ivec2& mpos);
	virtual bool dispatch_event(dev_event_t* e);
private:

};

class div_cx0 :public widget_t
{
public:
	std::vector<widget_t*> _v;
public:
	div_cx0();
	div_cx0(const glm::ivec4& rc);
	~div_cx0();
	void clear();
	void add(widget_t* c);
	widget_t* hit_test(const glm::ivec2& mpos);
	bool dispatch_event(dev_event_t* e) override;
private:

};

struct gui_viewport {
	gui_io_state_t io = {};
	std::string drop_text;
	glm::ivec2 _last_pos = {};
	div_cx0 _root = {};
public:
	void set_viewport(const glm::ivec4& rc);
	void clear();
	void add_div(div_cx0* c);
	void trigger(dev_event_t* e);
};
class ui_builder_cx
{
public:
	ui_builder_cx();
	~ui_builder_cx();

private:

};

#ifndef NOT_OLDGUI

class rvg_cx;

struct drag_v6
{
	glm::ivec2 pos;
	glm::ivec2 size;
	glm::ivec2 tp, cp0, cp1;
	int ck = 0;
	int z = 0;
};
class scroll_bar;
struct scroll2_t
{
	scroll_bar* h = 0,		// 水平
		* v = 0;			// 垂直
};
class div_cx;
struct div_ev
{
	div_cx* p;
	int down;		// 是否按下
	int clicks;		// 单击次数
	glm::ivec2 mpos;// 鼠标坐标
	bool drag;		// 是否拖动
};
class div_cx :public widget_t
{
public:
	glm::dvec4 _hover_eq = { 0,0.5,0,0 };	// 时间
	glm::ivec4 border = {};	// 颜色，线粗，圆角，背景色 
	glm::ivec2 _move_pos = {};
	int evupdate = 0;
	int ckinc = 0;
	int ckup = 0;
	flex_data flex = {};
	flex_data flex_child = {};
	scroll_bar* horizontal = 0, * vertical = 0;//水平滚动条 ，垂直滚动条 
	std::vector<widget_t*> widgets, event_wts, event_wts1;
	std::vector<widget_t*> tadd, tremove;
	std::vector<widget_t*> sort_draw;	// 排序渲染
	std::vector<glm::ivec2> lines;	// 控件分行
	std::vector<drag_v6> drags;	// 拖动坐标
	std::vector<drag_v6*> dragsp;	// 拖动区域
	std::function<void(div_ev* e)> on_click;
	std::function<void(div_ev* e)> on_click_outer;//模态窗口点中外围时
	std::string editingstr;						// 编辑状态文本
	uint32_t editing_color = 0xff121212;		// 编辑状态文本颜色
	int line_height = 0;
	glm::ivec2 editpos = {};
	std::vector<node_dt> tempfv;
	flex_run* lctx = 0;
	int order = 0;
	bool update_drag = false;		// 是否更新拖动坐标
	bool draggable = false;
	bool docking = false;
public:
	div_cx();
	~div_cx();
	void add_widget(widget_t* p);
	void remove_widget(widget_t* p);
	// 设置本面板滚动条，pos_width每次滚动量,垂直vnpos,水平hnpos为滚动条容器内偏移
	void set_scroll(int width, int rcw, const glm::ivec2& pos_width, const glm::ivec2& vnpos = {}, const glm::ivec2& hnpos = {});
	void set_scroll_hide(bool is);// 是否隐藏滚动条
	void set_scroll_pos(const glm::ivec2& ps, bool v);
	void set_scroll_size(const glm::ivec2& ps, bool v);
	void set_view(const glm::ivec2& view_size, const glm::ivec2& content_size);
	void set_scroll_visible(const glm::ivec2& hv);
	glm::ivec2 get_scroll_range();
	// 设置位置，t=0设置，1加减
	void set_scroll_pts(const glm::ivec2& pts, int t);
	// 创建滚动条
	scroll_bar* new_scroll_bar(const glm::ivec2& size, int vs, int cs, int rcw, bool v, const glm::ivec2& npos = {});
	scroll2_t new_scroll2(const glm::ivec2& viewsize, int width, int rcw, const glm::ivec2& pos_width, const glm::ivec2& vnpos, const glm::ivec2& hnpos);
public:
	//void on_event(uint32_t type, et_un_t* ep);
	bool on_mevent(int type, const glm::vec2& mps, void* e);
	// 返回是否命中ui
	bool hittest(const glm::ivec2& pos);
	bool press_test();
	size_t add_dragpos(const glm::ivec2& pos, const glm::ivec2& size = {});
	void remove_dragpos(size_t idx);
	glm::ivec3 get_dragpos(size_t idx);
	drag_v6* get_dragv6(size_t idx);

	bool update(float delta);
	void draw(rvg_cx* rv);
	void draw_last(rvg_cx* rv);
	void set_editing(const std::string& str, const glm::ivec2& cpos, int lineheight);
	void clayout();
private:
	void sortdg();
	void bind_scroll_bar(scroll_bar* p, bool v);	// 绑定到面板	
	void on_motion(const glm::vec2& pos);
	// 	idx=1左，3右，2中
	void on_button(int idx, int down, const glm::vec2& pos, int clicks, int r);
	void on_wheel(double x, double y);
};

#if 1


// 控件相关

struct btn_cols_t {
	uint32_t font_color = 0xFF222222;
	uint32_t background_color = 0;
	uint32_t border_color = 0;
	uint32_t hover_color = 0;
	uint32_t active_font_color = 0xFFe6e6e6;
	uint32_t active_background_color = 0;
	uint32_t active_border_color = 0;// 0xFF3c3c3c;
	uint32_t hover_border_color = 0;
};
// todo tag 标签,用于标记和选择。0蓝，1青，2灰，3橙，4红
enum class uType :uint8_t {
	primary, success, info, warning, danger
};
// 填充颜色字白色，边框字颜色中间半透明，边框字颜色
enum class uTheme :uint8_t {
	dark, light, plain,
};
/*
1.普通状态2，鼠标hover状态  3.active 点击状态  4.focus 取得焦点状态  4.disable禁用状态
*/
enum class BTN_STATE0 :uint8_t
{
	STATE_NOMAL = BIT_INC(0),
	STATE_HOVER = BIT_INC(1),
	STATE_ACTIVE = BIT_INC(2),
	STATE_FOCUS = BIT_INC(3),
	STATE_DISABLE = BIT_INC(4),
};

// todo图片按钮
struct image_btn :public widget_t {
	std::string str;
	union {
		void* vkimage;	// vk图像
		image_ptr_t* img;
		void* surf;			// vkvg表面
	}imgptr = {};
	ovg_image_r state_img[5] = {};
	image_ptr_t st = {};
	std::vector<glm::ivec4> data;
	int show_idx = 0;	// 参考BTN_STATE
	int img_type = 0;	//  0 image_ptr_t, 1 VkvgSurface, 2 vkimage
	int multi = 0;		// 多状态按钮，0=单状态，1=多状态
public:
	image_btn();
	~image_btn();
	void set_size(const glm::vec2& ss);
	// 设置按钮状态分区，rc空则平分
	void set_state(BTN_STATE m, const glm::ivec2& rc);
	// 设置单个状态区域 
	void set_state1(BTN_STATE m, const glm::ivec4& rc);

	void set_image(image_ptr_t* img);
	void set_vkimage(void* vkimage, int width, int height, int type);
	void set_surface(void* surf, int width, int height);
	bool on_mevent(int type, const glm::vec2& mps, void* e);
	bool update(float delta);
	void draw(rvg_cx* rv);
};
struct color_style {
	btn_cols_t pdc = {};			// 颜色配置
	uint32_t dfill = 0, dcol = 0;	// 渲染用
	uint32_t dtext_color = 0;		// 渲染用
	float light = 0.512;
	glm::vec2 pushedps = {};
	int rounding = 0;
	int thickness = 1;
	text_style_t* ptext_style = 0;
	double dtime = 0.0;
	const char* str = 0;
	int str_len = 0;
	int _bst = 1;					// 鼠标状态	
	int _old_bst = 0;			// 鼠标状态
	uTheme effect = uTheme::dark;
	uint8_t disabled_alpha = 0x30;
	bool circle = false;			// 圆形按钮
	bool mPushed = false;
	bool _disabled = false;
	bool hover = false;
};
// 纯色按钮
struct color_btn :public widget_t
{
	std::string str;
	color_style cs = {};
public:
	color_btn();
	~color_btn();
	btn_cols_t* set_btn_color_bgr(size_t idx);
	bool update(float delta);
	void draw(rvg_cx* rv);
};
// 渐变按钮
struct gradient_btn :public widget_t
{
	std::string str;
	uint32_t back_color = 0;
	uint32_t text_color_shadow = 0x88111111;
	double opacity = 1.0;
	// x=默认，y=鼠标进入，z=按下
	glm::uvec3 gradTop = { 0xff4a4a4a,0x80404040,0xff292929 }, gradBot = { 0xff3a3a3a,0x80303030,0xff1d1d1d };
	// private
	uint32_t _gradTop = 0;
	uint32_t _gradBot = 0;
	uint32_t borderLight = 0;
	uint32_t borderDark = 0;
	uTheme effect = uTheme::light;	// dark
	bool mPushed = false;
	bool mChecked = false;
	bool mMouseFocus = false;
	bool mEnabled = true;
	bool is_muilt = true;
public:
	gradient_btn();
	~gradient_btn();
	const char* c_str();
	void init(glm::ivec4 rect, const std::string& text, uint32_t back_color = 0, uint32_t text_color = -1);

	bool update(float delta);
	void draw(rvg_cx* rv);
};
std::string save_color_style(const color_style* data, int indent);

struct radio_style_t
{
	uint32_t col = 0xffFF9E40, innc = 0xffffffff, text_col = 0xff666666;
	uint32_t line_col = 0xff4c4c4c;
	float radius = 7;
	float thickness = 1.0;
	float duration = 0.28;	// 动画时间 
};

struct check_style_t {
	uint32_t col = 0xffFF9E40;
	uint32_t fill = 0xffff8020;
	uint32_t check_col = 0xffffffff;
	uint32_t line_col = 0xff4c4c4c;
	uint32_t text_col = 0xff666666;
	float rounding = 2;
	float square_sz = 14;
	float thickness = 1.0;
	float duration = 0.28;	// 动画时间 
};
struct radio_info_t
{
	glm::vec2 pos = {};	// 坐标
	std::string text;
	std::function<void(void* p, bool v)> on_change_cb;
	float swidth = 0.;
	float dt = 0;	// 动画进度
	bool* pv = 0;
	bool value = 0; // 选中值
	bool value1 = 1; // 选中值动画
};
struct checkbox_info_t
{
	glm::vec2 pos = {};	// 坐标
	std::string text;
	std::function<void(void* p, bool v)> on_change_cb;
	float dt = 0;	// 动画进度
	float duration = 0.28;	// 动画时间
	float new_alpha = -1;		// 动画控制	
	bool* pv = 0;
	bool mixed = false;			// 混合状态，不满时用
	bool value = 0; // 选中值
	bool value1 = 1; // 选中值动画
};
struct radio_tl;
struct group_radio_t
{
	radio_tl* active = 0;	// 激活的radio
	int ct = 0;				// 引用计数
};
// 单选
struct radio_tl :public widget_t
{
	radio_style_t _style = {};	// 风格id
	radio_info_t v = {};
private:
	group_radio_t* gr = 0;		// 组 
public:
	radio_tl();
	~radio_tl();
public:
	void set_group(group_radio_t* p);
	void bind_ptr(bool* p);
	void set_value(const std::string& str, bool v);
	void set_value(bool v);
	void set_value();

	bool on_mevent(int type, const glm::vec2& mps, void* e);
	bool update(float delta);
	void draw(rvg_cx* rv);
};
// 复选
struct checkbox_tl :public widget_t
{
	check_style_t _style = {};	// 风格id
	checkbox_info_t v = {};
public:
	checkbox_tl();
	~checkbox_tl();
	void bind_ptr(bool* p);
	void set_value(const std::string& str, bool v);
	void set_value(bool v);
	void set_value();

	bool on_mevent(int type, const glm::vec2& mps, void* e);
	bool update(float delta);
	void draw(rvg_cx* rv);
};

// 开关
struct switch_tl :public widget_t
{
	glm::ivec3 color = { 0xffff9e40, 0xff4c4c4c,-1 }; // 开/关/圆点颜色 { 0xff66ce13, 0xff4949ff };
	glm::ivec2 text_color = { 0xffff9e40, 0xff4c4c4c }; // 文本颜色;
	uint32_t dcol = 0;	// 渲染的颜色 
	float cpos = 0;		// 动画坐标
	float cv = 0.7;		// 圆点大小 
	float height = 20;
	float wf = 2.1;		// 宽比例 
	checkbox_info_t v = {};
	bool inline_prompt = false;
public:
	switch_tl();
	~switch_tl();
	void bind_ptr(bool* p);
	void set_value(bool b);
	void set_value();

	bool on_mevent(int type, const glm::vec2& mps, void* e);
	bool update(float delta);
	void draw(rvg_cx* rv);
};
// 进度条
struct progress_tl :public widget_t
{
	std::string format = "%";				// 格式
	glm::vec2 vr = { 0, 100 };		// 范围
	glm::ivec2 color = { 0xffff9e40, 0x806c6c6c };//前景色，背景色

	double value = 0.0;				// 当前进度
	int width = 0;					// 宽度
	int height = 0;					// 高度
	bool right_inside = false;			// 右对齐
	bool text_inside = true;
public:
	progress_tl();
	~progress_tl();
	void set_value(double b);
	void set_vr(const glm::ivec2& r);
	double get_v();

	bool on_mevent(int type, const glm::vec2& mps, void* e);
	bool update(float delta);
	void draw(rvg_cx* rv);
};
// 滑块
struct slider_tl :public widget_t
{
	glm::vec2 vr = { 0, 100 };		// 范围
	glm::ivec2 color = { 0xffff9e40, 0x806c6c6c };//前景色，背景色 
	glm::ivec2 sl = { 6,0xff363636 };	// 滑块半径颜色
	int wide = 0;
	double value = 0.0;				// 当前进度
	double* pv = 0;
	int vertical = 0;				// 垂直模式1
	bool reverse_color = 0;
public:
	slider_tl();
	~slider_tl();
	void bind_ptr(double* p);
	void set_value(double b);
	void set_vr(const glm::ivec2& r);
	// 设置圆大小
	void set_cw(int cw);
	double get_v();

	bool on_mevent(int type, const glm::vec2& mps, void* e);
	bool update(float delta);
	void draw(rvg_cx* rv);
};
// 颜色控件
struct colorpick_tl :public widget_t
{
	glm::ivec2 color = { -1, -1 };	//当前颜色，旧颜色
	glm::vec4 hsv = {}, oldhsv = {};// 0-1保存hsv
	uint32_t bc_color = 0xff232323;	//边框 
	int width = 0;					// 宽度
	int height = 0;					// 单行高度 
	int step = 4;					// 行间隔 
	int colorw = 100;				// 颜色宽
	int cpx = 0;					// 颜色x坐标
	int dx = -1;
	std::string hsvstr, colorstr;
	std::function<void(colorpick_tl* p, uint32_t col)> on_change_cb;
	bool alpha = true;				// 显示透明通道
public:
	colorpick_tl();
	~colorpick_tl();
	void init(uint32_t c, int w, int h, bool alpha);
	uint32_t get_color();	// 获取颜色
	void set_color2hsv(uint32_t c);
	void set_hsv(const glm::vec3& c);
	void set_hsv(const glm::vec4& c);
	void set_posv(int poss_x);

	bool on_mevent(int type, const glm::vec2& mps, void* e);
	bool update(float delta);
	void draw(rvg_cx* rv);
};
class colorpick_cx :public div_cx
{
public:
	glm::ivec2 color = { -1, -1 };	//当前颜色，旧颜色
	glm::vec4 hsv = {}, oldhsv = {};// 0-1保存hsv
	uint32_t bc_color = 0xff232323;	//边框 
	int width = 0;					// 宽度
	int height = 0;					// 单行高度 
	int step = 4;					// 行间隔 
	int colorw = 100;				// 颜色宽
	int cpx = 0;					// 颜色x坐标
	int dx = -1;
	std::string hsvstr, colorstr;
	std::function<void(colorpick_cx* p, uint32_t col)> on_change_cb;
public:
	colorpick_cx();
	~colorpick_cx();
	void init(uint32_t c, int w, int h);

	uint32_t get_color();	// 获取颜色
	void set_color2hsv(uint32_t c);
	void set_hsv(const glm::vec3& c);
	void set_hsv(const glm::vec4& c);
	void set_posv(const glm::ivec2& poss);
	bool on_mevent(int type, const glm::vec2& mps, void* e);
	bool update(float delta);
private:

};

// 滚动条
struct scroll_bar :public widget_t
{
	int64_t _view_size = 0;			// 视图大小
	int64_t _content_size = 0;		// 内容大小 
	int _rc_width = 0;			// 滑块宽度
	int _dir = 0;				// 方向，0=水平，1=垂直
	glm::ivec3 thumb_size_m = {};// 滚动范围
	glm::vec2 tps = {};
	glm::ivec4 _color = { 0xff363636,0xffcccccc,0xffffffff,0xffC8641E };		// 背景色，滑块颜色，滑块高亮颜色，激活颜色
	uint32_t _tcc = 0;			// 滑块当前颜色
	float _pos_width = 1;		// 滚动宽度
	int t_offset = 0;			// 偏移量
	float scale_w = 1.0;		// 滚动比例
	float scale_s = 0.6;		// 显示比例
	glm::vec2 scale_s0 = { 0.8,0.8 };	// 显示比例，用于鼠标进入变形
	bool hover = 0;				// 保存鼠标进入状态
	bool hover_sc = 0;
	bool hideble = 0;			// 隐藏滚动条
	bool limit = 1;				// 是否限制在滚动范围
	bool valid = 1;				// 是否重新渲染
	bool d_drag = 0;
private:
	int64_t _offset = 0;			// 偏移量
	int64_t c_offset = 0;			// 内容偏移量
public:
	scroll_bar();
	~scroll_bar();
	void set_viewsize(int64_t vs, int64_t cs, int rcw);
	bool on_mevent(int type, const glm::vec2& mps, void* e);
	bool update(float delta);

	void draw(rvg_cx* rv);
	int64_t get_offset();			// 获取滚动偏移
	int64_t get_offset_ns();			// 获取滚动偏移
	int get_range();			// 获取滚动偏移最大范围
	void set_offset(int pts);			// 设置滚动偏移
	void set_offset_inc(int inc);			// 增加滚动偏移
	void set_posv(const glm::ivec2& poss);
};

struct text_control;
// 输入框：单行/多行
class edit_cx :public widget_t
{
public:
	std::function<void(edit_cx* ptr)> change_cb;	// 文本改变时执行回调函数 
	std::function<void(edit_cx* ptr, std::string& str)> input_cb;	// 文本输入时执行回调函数，可修改此字符串返回
	text_control* ctx = 0;			// stb_textedit	
	std::string stext;				// 显示的文本，密码显示用
	std::string editingstr;			// 输入中的文本，输入法编辑时用
	std::string placeholder;		// 占位符
	glm::ivec4 _color = { 0xff282828, 0xffffffff, 0xf0ff7a4d, 0xff2c2c2c };				// 背景色、文本颜色、选择背景色、输入法编辑文本颜色
	glm::ivec3 _cursor = { 1,-1,500 };				// 闪烁光标。宽度、颜色、毫秒
	glm::ivec2 _cmpos = {};				// 当前鼠标坐标
	std::wstring wstr;
	glm::ivec3 _cursor_px = {};		// 光标坐标xy，当前行z
	char pwdch = {};				// 密码显示字符
	int _istate = 0;
	int fix_line_height = 0;		// 固定行高，0则自动计算
	int _baseline = 0;
	bool mdown = false;
	bool _read_only = false;
	bool is_input = false;
	bool show_input_cursor = true;
	bool roundselect = true;	// 圆角选区
	bool up_text = true;	// 更新文本了
	bool first_height = false;
public:
	edit_cx();
	~edit_cx();
	void set_single(bool is);
	// 设置为密码框比如'*'
	void set_pwd(char ch);
	// 设置utf8文本
	void set_text(const void* str, int len);
	void add_text(const void* str, int len);
	// 设置文本框大小
	void set_size(const glm::ivec2& ss);
	// 设置文本框坐标
	void set_pos(const glm::ivec2& pos);
	void set_align_pos(const glm::vec2& pos);
	void set_align(const glm::vec2& a);
	// 闪烁光标。宽度、颜色、毫秒
	void set_cursor(const glm::ivec3& c);
	// 背景色、文本颜色、选择背景色、输入法编辑文本颜色
	void set_color(const glm::ivec4& c);
	void set_family(font_family_t* family, int fontsize);
	// 设置是否显示输入光标
	void set_show_input_cursor(bool ab);
	// 设置自动换行
	void set_autobr(bool ab);
	void set_round_path(float v);
	// 删除位置，字符数量
	void remove_char(size_t idx, int count);
	// 删除选择的文本
	bool remove_bounds();
	// 发送事件到本edit
	//void on_event_e(uint32_t type, et_un_t* e);
	bool on_mevent(int type, const glm::vec2& mps, void* e);
	//void on_keyboard(et_un_t* ep);
	// 更新渲染啥的
	bool update(float delta);
	void draw(rvg_cx* rv);
	glm::ivec4 input_pos();
	int get_cursor_idx();
	std::string get_select_str();
	std::wstring get_select_wstr();
	glm::ivec2 get_bounds();
	std::vector<glm::ivec4> get_bounds_px();
	glm::ivec2 get_pixel_size(const char* str, int len);
	size_t get_xy_to_index(int x, int y, const char* str);
	glm::ivec3 get_line_length(int idx);
	void up_caret();
	void up_cursor(bool is);
};

#endif // 1

#endif // !NOT_OLDGUI
