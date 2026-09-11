#pragma once
/*
动画
	结构：json+数据
*/
#include <string>
#include <vector>

// 动画
enum class interpolation_e :uint8_t
{
	linear = 0,
	step = 1,
	cubicspline = 2
};
enum class easing_e :uint8_t
{
	EASE_LINEAR = 0,
	EASE_IN_QUAD,
	EASE_OUT_QUAD,
	EASE_IN_OUT_QUAD,
	EASE_IN_CUBIC,
	EASE_OUT_CUBIC,
	EASE_STEP,
};
struct sampler_t
{
	float* input;		// 时间点
	float* output;		// 采样值
	size_t count;		// 采样点数量
	easing_e easing;	// 0=linear, 1=ease_in_quad, 2=ease_out_quad, 3=ease_in_out_quad, 4=ease_in_cubic, 5=ease_out_cubic, 6=step
	interpolation_e interpolation;	// 0=linear, 1=step, 2=cubicspline 
	// 私有
	size_t prevKey = 0;
	double prevT = 0.0;
};
struct channel_t
{
	int sampler;		// 采样数据索引
	int path;			// 类型 property
	int target_node;	// 节点索引 target
	int dim;			// 维度
	int type;			// 0普通浮点数，1四元数
};
struct animation_t
{
	std::string name;
	std::vector<channel_t> channels;
	std::vector<sampler_t> samplers;
	int loop = 0;	// 0=none, 1=loop, 2=pingpong
	float start_time = 0.0;	// 开始时间
	float end_time = 0.0;	// 结束时间 
};
class anim_ctx
{
public:
	std::unordered_map<std::string, animation_t*> as;
	std::vector<float> data_input;		// 时间点
	std::vector<float> data_output;		// 采样值
public:
	anim_ctx();
	~anim_ctx();
	// 清空所有数据
	void clear();
	// 增加一个空动画
	animation_t* add_anim(const std::string& name);
	animation_t* find_anim(const std::string& name);
	void load(const std::string& file);
	void save(const std::string& file);
private:

};

void update_animation(animation_t* anim, float deltaTime, float* dst, int dst_count);
