/*
动画

创建时间 2026-9-9
*/

#include "pch.h"
#include "anim.h"
#include <mapView.h>

float apply_easing(easing_e easing, float t) {
	switch (easing) {
	case easing_e::EASE_IN_QUAD:		t = t * t; break;
	case easing_e::EASE_OUT_QUAD:		t = 1.0f - (1.0f - t) * (1.0f - t); break;
	case easing_e::EASE_IN_OUT_QUAD:	t = t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2) * 0.5f; break;
	case easing_e::EASE_IN_CUBIC:		t = t * t * t; break;
	case easing_e::EASE_OUT_CUBIC:		t = 1.0f - std::pow(1.0f - t, 3); break;
	case easing_e::EASE_STEP:			t = t < 1.0f ? 0.0f : 1.0f; break;
	default: break;
	}
	return t;
}
// 插值器，支持多维度linear/cubicSpline，四元数slerp/cubicSpline
namespace interpolator_ns
{

	glm::vec4 step(size_t prevKey, float* output, int stride, float* rb)
	{
		glm::vec4 result = {};
		for (size_t i = 0; i < stride; ++i)
		{
			rb[i] = output[prevKey * stride + i];
		}
		memcpy(&result, rb, std::min(4, stride) * sizeof(float));
		return result;
	}

	glm::vec4 linear(size_t prevKey, size_t nextKey, float* output, float t, int stride, float* rb)
	{
		glm::vec4 result = {};
		for (size_t i = 0; i < stride; ++i)
		{
			rb[i] = output[prevKey * stride + i] * (1 - t) + output[nextKey * stride + i] * t;
		}
		memcpy(&result, rb, std::min(4, stride) * sizeof(float));
		return result;
	}
	// https://github.khronos.org/glTF-Tutorials/gltfTutorial/gltfTutorial_007_Animations.html
	template <typename T>
	T cubicSpline(const T& vert0, const T& tang0, const T& vert1, const T& tang1, float t) {
		float tt = t * t, ttt = tt * t;
		float s2 = -2 * ttt + 3 * tt, s3 = ttt - tt;
		float s0 = 1 - s2, s1 = s3 - tt + t;
		T p0 = vert0;
		T m0 = tang0;
		T p1 = vert1;
		T m1 = tang1;
		return s0 * p0 + s1 * m0 * t + s2 * p1 + s3 * m1 * t;
	}
	glm::vec4 cubicSpline(size_t prevKey, size_t nextKey, float* output, float keyDelta, float t, int stride, float* rb)
	{
		// stride: Count of components (4 in a quaternion).
		// Scale by 3, because each output entry consist of two tangents and one data-point.
		auto prevIndex = prevKey * stride * 3;
		auto nextIndex = nextKey * stride * 3;
		size_t A = 0;
		size_t V = 1 * stride;
		size_t B = 2 * stride;

		glm::vec4 result = {};
		float tSq = t * t;
		float tCub = tSq * t;
		// We assume that the components in output are laid out like this: in-tangent, point, out-tangent.
		// https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#appendix-c-spline-interpolation
		for (size_t i = 0; i < stride; ++i)
		{
			auto v0 = output[prevIndex + i + V];
			auto a = keyDelta * output[nextIndex + i + A];
			auto b = keyDelta * output[prevIndex + i + B];
			auto v1 = output[nextIndex + i + V];
			rb[i] = ((2.0 * tCub - 3.0 * tSq + 1.0) * v0) + ((tCub - 2.0 * tSq + t) * b) + ((-2.0 * tCub + 3.0 * tSq) * v1) + ((tCub - tSq) * a);
		}
		memcpy(&result, rb, std::min(4, stride) * sizeof(float));
		return result;
	}

	void resetKey()
	{
		//prevKey = 0;
	}
	glm::vec4 get_v4(float* v, int idx, int stride, float* rb) {
		auto& r = rb;
		v += idx;
		glm::vec4 result = {};
		for (size_t i = 0; i < stride; i++)
		{
			r[i] = v[i];
		}
		memcpy(&result, rb, std::max(4, stride) * sizeof(float));
		return result;
	}

	glm::quat getQuat(float* output, size_t index)
	{
		auto x = output[4 * index];
		auto y = output[4 * index + 1];
		auto z = output[4 * index + 2];
		auto w = output[4 * index + 3];
		return glm::quat(w, x, y, z);
	}
	float apply_loop(float t, float start, float end, int loop) {
		if (t <= start) return start;
		if (t >= end) {
			float dur = end - start;
			if (dur <= 0.0f) return start;

			switch (loop) {
			default:
			case 0: // none
				return end;

			case 1: // loop
				return start + std::fmod(t - start, dur);

			case 2: // pingpong
			{
				float tt = std::fmod(t - start, dur * 2.0f);
				if (tt > dur)
					tt = dur * 2.0f - tt;
				return start + tt;
			}
			}
		}
		return t;
	}
	// 0普通，1四元数
	glm::vec4 interpolate(sampler_t* sampler, float t, float start_time, float maxTime, int stride, int type, int loop, float* rb)
	{
		if (!(t > 0) || !sampler)
		{
			return {};
		}
		size_t ilength = sampler->count;
		size_t vlength = sampler->count;
		float* input = (float*)sampler->input;
		float* output = (float*)sampler->output;

		std::vector<float> rb1;
		if (!rb) { rb1.resize(stride); rb = rb1.data(); }

		if (vlength == 1)
		{
			return get_v4(output, 0, stride, rb);
		}
		//t = fmod(t, maxTime);
		t = apply_loop(t, input[0], input[ilength - 1], loop);
		t = glm::clamp(t, input[0], input[ilength - 1]);
		if (sampler->prevT > t)
		{
			sampler->prevKey = 0;
		}
		sampler->prevT = t;
		// Find next keyframe: min{ t of input | t > prevKey }
		size_t nextKey = 0;
		for (size_t i = sampler->prevKey; i < ilength; ++i)
		{
			if (t <= input[i])
			{
				nextKey = glm::clamp(i, (size_t)1, ilength - 1);
				break;
			}
		}
		sampler->prevKey = glm::clamp(nextKey - 1, (size_t)0, nextKey);

		auto keyDelta = input[nextKey] - input[sampler->prevKey];

		// Normalize t: [t0, t1] -> [0, 1]
		float tn = 0.0;
		if (nextKey != sampler->prevKey)
			tn = (t - input[sampler->prevKey]) / keyDelta;
		// 扭曲时间
		tn = apply_easing(sampler->easing, tn);

		if (type == 1)
		{
			glm::quat qr = {};
			//linear=0，step=1，cubicspline=2
			switch (sampler->interpolation)
			{
			case interpolation_e::linear:
			{
				auto q0 = getQuat(output, sampler->prevKey);
				auto q1 = getQuat(output, nextKey);
				auto r = glm::normalize(glm::slerp(q0, q1, tn));
				glm::quat q(r.w, r.x, r.y, r.z);
				qr = q;
			}
			break;
			case interpolation_e::step:
			{
				qr = glm::normalize(getQuat(output, sampler->prevKey));
			}
			break;
			case interpolation_e::cubicspline:
			{
				// GLTF requires cubic spline interpolation for quaternions.
				// https://github.com/KhronosGroup/glTF/issues/1386
				auto r = cubicSpline(sampler->prevKey, nextKey, output, keyDelta, tn, 4, rb);
				glm::quat q(r.w, r.x, r.y, r.z);
				qr = glm::normalize(q);
			}
			break;
			default:
				break;
			}
			memcpy(rb, &qr, sizeof(glm::quat));
			return glm::vec4(qr.x, qr.y, qr.z, qr.w);
		}
		glm::vec4 ret = {};
		switch (sampler->interpolation)
		{
		case interpolation_e::step:
			ret = step(sampler->prevKey, output, stride, rb);
			break;
		case interpolation_e::cubicspline:
			ret = cubicSpline(sampler->prevKey, nextKey, output, keyDelta, tn, stride, rb);
			break;
		default:
			ret = linear(sampler->prevKey, nextKey, output, tn, stride, rb);
			break;
		}
		return ret;
	}

};

int FindClosestFloatIndex(sampler_t* sampler, float val)
{
	int ini = 0;
	int fin = sampler->count - 1;

	while (ini <= fin)
	{
		int mid = (ini + fin) / 2;
		float v = sampler->input[mid];
		if (val < v)
			fin = mid - 1;
		else if (val > v)
			ini = mid + 1;
		else
			return mid;
	}

	return fin;
}
glm::ivec2 get_tidx(sampler_t* sampler, float time, float* frac)
{
	int curr_index = FindClosestFloatIndex(sampler, time);
	int next_index = std::min<int>(curr_index + 1, sampler->count - 1);

	if (curr_index < 0) curr_index++;

	if (curr_index == next_index)
	{
		*frac = 0;
		return { curr_index,next_index };
	}
	float curr_time = sampler->input[curr_index];
	float next_time = sampler->input[next_index];
	*frac = (time - curr_time) / (next_time - curr_time);
	assert(*frac >= 0 && *frac <= 1.0);
	return { curr_index,next_index };
}



void update_animation(animation_t* anim, float deltaTime, float* dst, int dst_count)
{
	if (!anim || !dst || dst_count <= 0)
		return;
	auto sampler = anim->samplers.data();
	int inc = dst_count; float duration = anim->end_time - anim->start_time;
	for (size_t i = 0; i < anim->channels.size(); i++)
	{
		auto& it = anim->channels[i];
		auto& s = sampler[it.sampler];
		auto v = interpolator_ns::interpolate(&s, deltaTime, anim->start_time, duration, it.dim, it.type, anim->loop, dst);
		dst += it.dim;
		inc -= it.dim;
		if (inc <= 0)
		{
			dst = 0;// 超出输出缓冲区了
		}
	}
}


anim_ctx::anim_ctx()
{}

anim_ctx::~anim_ctx()
{}

void anim_ctx::clear()
{
	for (auto& pair : as)
	{
		delete pair.second;
	}
	as.clear();
	data_input.clear();
	data_output.clear();
}

animation_t* anim_ctx::add_anim(const std::string& name)
{
	if (name.empty() || as.find(name) != as.end())
		return nullptr;
	auto p = new animation_t();
	as[name] = p;
	return p;
}

animation_t* anim_ctx::find_anim(const std::string& name)
{
	auto it = as.find(name);
	if (it != as.end())
		return it->second;
	return nullptr;
}

void anim_ctx::load(const std::string& file)
{
	hz::mfile_t mf;
}

void anim_ctx::save(const std::string& file)
{
	if (file.empty() || !file[0] || data_input.empty() || data_output.empty() || as.empty())
		return;
}
