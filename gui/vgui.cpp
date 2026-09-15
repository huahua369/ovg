/*
gui实现

创建日期：2026-9-12
*/

#include "pch.h"
#include "vgui.h"

event_obj_t::event_obj_t()
{}

event_obj_t::~event_obj_t()
{
	if (calls)delete[] calls;
	calls = 0;
}
void event_obj_t::set_event_dev(dev_event_type_e e, std::function<void(dev_event_t* dv)> cb)
{
	auto cbs = get_cbs(0);
	if (cb)
	{
		cbs[(int)e] = [=]() { cb(cde); };
	}
	else
		cbs.erase((int)e);
	cb_count.x = cbs.size();
}
void event_obj_t::set_on_event(event_type_e e, std::function<void(event_type_e type, const glm::vec2& mps)> cb)
{
	auto cbs = get_cbs(1);
	if (cb)
	{
		if (e == event_type_e::on_drag || e == event_type_e::on_dragstart || e == event_type_e::on_dragend)
			has_drag = true;
		cbs[(int)e] = [=]() { cb(etype, mouse_pos); };
	}
	cb_count.y = cbs.size();
}

void event_obj_t::set_on_text(std::function<void(text_input_et*)> cb)
{
	if (cb)
		set_event_dev(dev_event_type_e::text_input_e, [=](dev_event_t* dv) {cb(dv->v.t); });
}

void event_obj_t::set_on_editing(std::function<void(text_editing_et*)> cb)
{
	if (cb)
		set_event_dev(dev_event_type_e::text_editing_e, [=](dev_event_t* dv) {cb(dv->v.e); });
}

void event_obj_t::remove(dev_event_type_e e)
{
	if (calls)
		calls[0].erase((int)e);
	cb_count.x = calls[0].size();
}

void event_obj_t::remove(event_type_e e)
{
	if (calls)
		calls[1].erase((int)e);
	cb_count.y = calls[1].size();
}

void event_obj_t::remove_on_text()
{
	if (calls)
	{
		calls[0].erase((int)dev_event_type_e::text_input_e);
		calls[0].erase((int)dev_event_type_e::text_editing_e);
		cb_count.x = calls[0].size();
	}
}

void event_obj_t::call(int idx, int type)
{
	if (calls && idx >= 0 && idx < 2) {
		if (cb_count[idx] > 0)
		{
			auto& c = calls[idx];
			auto it = c.find(type);
			if (it != c.end() && it->second) {
				it->second();
			}
		}
	}
}

// rc= left,top,right,bottom
bool rect_includes(const glm::vec4& rc, const glm::vec2& p)
{
	return (p.x >= rc.x) && (p.x <= rc.z) && (p.y >= rc.y) && (p.y <= rc.w);
}


bool in_rect_box(const glm::ivec4& rect, const glm::ivec2& mousePos) {
	return mousePos.x >= rect.x && mousePos.x <= (rect.x + rect.z) && mousePos.y >= rect.y && mousePos.y <= (rect.y + rect.w);
}
glm::ivec2 check_box_cr1(const glm::vec2& p, const glm::vec4* d, size_t count, int stride)
{
	bool ret = false;
	if (!d)
	{
		return {};
	}
	glm::ivec2 rs = {};
	auto t = (char*)d;
	for (size_t i = 0; i < count; i++, t += stride)
	{
		auto c = (glm::vec4*)t;
		if ((int)c->w > 0)
		{
			auto r = *c;
			ret = in_rect_box(r, p); //!((p.x < r.x) || (p.y < r.y) || (p.x > r.x + r.z /*- 1*/) || (p.y > r.y + r.w /*- 1*/));
			if (ret)
			{
				rs.x = ret;
				rs.y = i;
				break;
			}
		}
		else {
			//计算点p和 当前圆圆心c 的距离
			int dis = distance(p, glm::vec2(c->x, c->y));
			auto r = c->z /*- 1*/; if (ret)
			{
				//和半径比较
				ret = (dis <= r * r);
				rs.x = ret;
				rs.y = i;
				break;
			}
		}
	}
	return  rs;
}

bool event_obj_t::hittest(const glm::ivec2& mpos)const
{
	glm::vec4 rc = { _pos, _pos + _size };
	return rect_includes(rc, mpos);
}

std::unordered_map<int, std::function<void()>>& event_obj_t::get_cbs(int i)
{
	if (!calls)
		calls = new std::unordered_map<int, std::function<void()>>[2]();
	return calls[i];
}

// 通用控件鼠标事件处理 type有on_move/on_scroll/on_drag/on_down/on_up/on_click/on_dblclick/on_tripleclick
bool widget_on_move(event_obj_t* wp, dev_event_t* dv, const glm::vec2& pos) {
	bool hover = false;
	if (!wp)return hover;
	auto e = &dv->v;
	auto t = dv->type;
	if (t == dev_event_type_e::mouse_move_e)
	{
		auto p = e->m;
		glm::ivec2 mps = { p->x,p->y }; mps -= pos;
		// 判断是否鼠标进入
		auto k = wp->hittest(mps);
		if (k) {
			bool hoverold = wp->_bst & (int)BTN_STATE::STATE_HOVER;
			wp->_bst |= (int)BTN_STATE::STATE_HOVER;   hover = true;
			if (!(wp->_bst & (int)BTN_STATE::STATE_ACTIVE))// 不是鼠标则独占
				dv->ret = 1;
			if (!hoverold)
			{
				// 鼠标进入
				wp->etype = event_type_e::on_enter;
				wp->mouse_pos = mps;
				wp->call(1, (int)wp->etype);
			}
		}
		else {
			if (wp->_bst & (int)BTN_STATE::STATE_HOVER)
			{
				wp->_bst &= ~(int)BTN_STATE::STATE_HOVER;
				// 鼠标离开
				wp->etype = event_type_e::on_leave;
				wp->mouse_pos = mps;
				wp->call(1, (int)wp->etype);
			}
		}
		{
			if (wp->_bst & (int)BTN_STATE::STATE_HOVER)
			{
				wp->etype = event_type_e::on_move;
				wp->mouse_pos = mps;
				wp->call(1, (int)wp->etype);
			}
			if (wp->has_drag && wp->_bst & (int)BTN_STATE::STATE_ACTIVE) {
				auto dps = mps - wp->curpos;
				bool first = !wp->is_drag;
				wp->is_drag = true;
				if (first) {
					wp->etype = event_type_e::on_dragstart;
					wp->mouse_pos = mps;
					wp->call(1, (int)wp->etype);
				}
				else
				{
					wp->etype = event_type_e::on_drag;
					wp->mouse_pos = mps;
					wp->call(1, (int)wp->etype);
				}
			}
		}
	}
	return hover;
}

void widget_on_event(event_obj_t* wp, dev_event_t* dv, const glm::vec2& pos) {
	if (!wp)return;
	auto e = &dv->v;
	auto t = dv->type;
	switch (t)
	{
	case dev_event_type_e::mouse_move_e:
		widget_on_move(wp, dv, pos);
		break;
	case dev_event_type_e::mouse_button_e:
	{
		auto p = e->b;
		glm::ivec2 mps = { p->x,p->y }; mps -= pos;
		void* cs = 0;
		if (!cs && wp->_bst & (int)BTN_STATE::STATE_HOVER) {
			if (p->down == 1)
			{
				dv->ret = 1;
			}
			if (p->button == 1) {
				if (p->down == 1) {
					wp->_bst |= (int)BTN_STATE::STATE_ACTIVE;
					wp->curpos = mps - (glm::ivec2)wp->_pos;
					wp->etype = event_type_e::on_down;
					wp->mouse_pos = mps;
					wp->call(1, (int)wp->etype);
				}
				else {
					if (wp->_bst & (int)BTN_STATE::STATE_ACTIVE)
					{
						if (!wp->has_drag || !wp->is_drag)// 没有拖动时执行点击事件
						{
							wp->etype = event_type_e::on_up;
							wp->mouse_pos = mps;
							wp->call(1, (int)wp->etype);
							event_type_e tc = event_type_e::on_click; //左键单击
							if (p->clicks == 2) { tc = event_type_e::on_dblclick; }
							else if (p->clicks == 3) { tc = event_type_e::on_tripleclick; }
							wp->etype = tc;
							wp->mouse_pos = mps;
							wp->call(1, (int)wp->etype);
						}
					}
					wp->_bst &= ~(int)BTN_STATE::STATE_ACTIVE;
				}
			}
		}
		if (p->down == 0) {
			wp->_bst &= ~(int)BTN_STATE::STATE_ACTIVE;
			wp->_bst |= (int)BTN_STATE::STATE_NOMAL;
			if (wp->has_drag && wp->is_drag)
			{
				wp->etype = event_type_e::on_dragend;
				wp->mouse_pos = mps;
				wp->call(1, (int)wp->etype);
			}
			wp->is_drag = false;
			wp->etype = event_type_e::mouse_up;
			wp->mouse_pos = mps;
			wp->call(1, (int)wp->etype);
		}
	}
	break;
	case dev_event_type_e::mouse_wheel_e:
	{
		auto p = e->w;
		glm::vec2 mps = { p->x, p->y };
		if (wp->_bst & (int)BTN_STATE::STATE_HOVER || wp->outer_scroll)
		{
			wp->etype = event_type_e::on_scroll;
			wp->mouse_pos = mps;
			wp->call(1, (int)wp->etype);
			dv->ret = 1;
		}
	}
	break;
	default:
		break;
	}

}

bool on_gui_event(event_obj_t* pw, dev_event_t* dv, const glm::ivec2& ppos)
{
	bool r = false;
	auto e = &dv->v;
	auto t = dv->type;
	pw->cde = dv;
	pw->call(0, (int)dv->type);
	widget_on_event(pw, dv, ppos);
	return (dv->ret);
}

void gui_viewport::set_viewport(const glm::ivec4& rc)
{
	_root._pos = { rc.x,rc.y };
	_root._size = { rc.z,rc.w };
}

void gui_viewport::clear()
{
	_root.clear();
}

void gui_viewport::add_div(div_cx0* c)
{
	_root.add(c);
}

void gui_viewport::trigger(dev_event_t* e)
{
	//hit_test_visitor v{ io.MousePos };
	//_root.accept(&v);
	//v.result;
	auto hr = _root.hit_test(io.MousePos);
	_root.dispatch_event(e);
}

widget_t::widget_t()
{}

widget_t::widget_t(widget_type t) :wtype(t)
{}

widget_t::~widget_t()
{}

widget_t* widget_t::hit_test(const glm::ivec2& mpos) {
	return hittest(mpos) ? this : nullptr;
}

bool widget_t::dispatch_event(dev_event_t* e) {
	return on_gui_event(this, e, {});
}

div_cx0::div_cx0() :widget_t(widget_type::WT_DIV)
{}

div_cx0::div_cx0(const glm::ivec4& rc)
{
	_pos = { rc.x,rc.y };
	_size = { rc.z,rc.w };
}

div_cx0::~div_cx0()
{}

void div_cx0::clear()
{
	_v.clear();
}

void div_cx0::add(widget_t* c)
{
	if (c)
		_v.push_back(c);
}


widget_t* div_cx0::hit_test(const glm::ivec2& mpos) {
	if (!hittest(mpos))
		return nullptr;
	auto mps = mpos - _pos;
	for (auto it = _v.rbegin(); it != _v.rend(); ++it) {
		widget_t* child = *it;
		if (child->hit_test(mps)) {
			return child;
		}
	}
	return this;
}

bool div_cx0::dispatch_event(dev_event_t* e) {
	for (auto it = _v.rbegin(); it != _v.rend(); ++it) {
		if ((*it)->dispatch_event(e))
			return true;
	}
	return on_gui_event(this, e, {});
}


ui_builder_cx::ui_builder_cx()
{}

ui_builder_cx::~ui_builder_cx()
{}
