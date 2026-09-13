/*
gui实现

创建日期：2026-9-12
*/
 
#include "pch.h"
#include "vgui.h"

widget_t::widget_t()
{}

widget_t::~widget_t()
{}
void widget_t::set_pos(const glm::ivec2 & ps)
{}
void widget_t::set_size(const glm::vec2 & ss)
{}
glm::vec2 widget_t::get_size()
{
	return glm::vec2();
}
bool widget_t::on_mevent(event_type_e type, const glm::vec2& mps, void* e)
{
	return false;
}
void widget_t::on_event(uint32_t type, dev_event_t* ep)
{}
bool widget_t::update(float delta)
{
	return false;
}
void widget_t::draw(ovg_ctx_cb* rv, rvg_t* p)
{}
glm::ivec4 widget_t::input_pos()
{
	return glm::ivec4();
}

void widget_t::add_text(const char* str, int len)
{}

void widget_t::set_editing(const char* str, int len, int start)
{}

void widget_t::set_family(font_family_t * family, int fontsize)
{}

