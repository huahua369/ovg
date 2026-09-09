/*
动画

创建时间 2026-9-9
*/

#ifndef GLM_FORCE_XYZW_ONLY 
#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_XYZW_ONLY
#include <glm/glm.hpp> 
#include <glm/gtx/intersect.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <glm/gtx/closest_point.hpp>
#include <glm/gtc/type_ptr.hpp> 
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp> 
#include <glm/gtx/matrix_transform_2d.hpp>
#include <glm/gtx/euler_angles.hpp>
#endif


#include "vgui.h"

// 插值器，支持多维度linear/cubicSpline，四元数slerp/cubicSpline
class interpolator_cx
{
public:
	size_t prevKey = 0;
	double prevT = 0.0; 
public:

	glm::vec4 step(size_t prevKey, float* output, int stride, std::vector<float>& rb)
	{
		glm::vec4 result = {};
		for (size_t i = 0; i < stride; ++i)
		{
			rb[i] = output[prevKey * stride + i];
		}
		memcpy(&result, rb.data(), std::min(4, stride) * sizeof(float));
		return result;
	}

	glm::vec4 linear(size_t prevKey, size_t nextKey, float* output, float t, int stride, std::vector<float>& rb)
	{
		glm::vec4 result = {};
		for (size_t i = 0; i < stride; ++i)
		{
			rb[i] = output[prevKey * stride + i] * (1 - t) + output[nextKey * stride + i] * t;
		}
		memcpy(&result, rb.data(), std::min(4, stride) * sizeof(float));
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
	glm::vec4 cubicSpline(size_t prevKey, size_t nextKey, float* output, float keyDelta, float t, int stride, std::vector<float>& rb)
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
		memcpy(&result, rb.data(), std::min(4, stride) * sizeof(float));
		return result;
	}

	void resetKey()
	{
		prevKey = 0;
	}
	glm::vec4 get_v4(float* v, int idx, int stride, std::vector<float>& rb) {
		auto& r = rb;
		v += idx;
		glm::vec4 result = {};
		for (size_t i = 0; i < stride; i++)
		{
			r[i] = v[i];
		}
		memcpy(&result, rb.data(), std::max(4, stride) * sizeof(float));
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
	glm::vec4 interpolate(sampler_t* sampler, float t, float maxTime, std::vector<float>* rb)
	{
		if (!(t > 0) || !sampler)
		{
			return {};
		}
		int stride = sampler->dim;
		size_t ilength = sampler->count;
		size_t vlength = sampler->count;
		float* input = (float*)sampler->input;
		float* output = (float*)sampler->output;

		std::vector<float> rb1;
		if (!rb)rb = &rb1;
		rb->resize(stride);

		if (vlength == 1) // no interpolation for single keyFrame animations
		{
			return get_v4(output, 0, stride, *rb);
		}
		// Wrap t around, so the animation loops.
		// Make sure that t is never earlier than the first keyframe and never later then the last keyframe.
		t = fmod(t, maxTime);
		t = glm::clamp(t, input[0], input[ilength - 1]);
		if (prevT > t)
		{
			prevKey = 0;
		}
		prevT = t;
		// Find next keyframe: min{ t of input | t > prevKey }
		size_t nextKey = 0;
		for (size_t i = prevKey; i < ilength; ++i)
		{
			if (t <= input[i])
			{
				nextKey = glm::clamp(i, (size_t)1, ilength - 1);
				break;
			}
		}
		prevKey = glm::clamp(nextKey - 1, (size_t)0, nextKey);

		auto keyDelta = input[nextKey] - input[prevKey];

		// Normalize t: [t0, t1] -> [0, 1]
		float tn = 0.0;
		if (nextKey != prevKey)
			tn = (t - input[prevKey]) / keyDelta;
		else
			tn = tn;
		// 0"translation",1"rotation",2"scale",3"weights";
		if (sampler->path == 1)
		{
			glm::quat qr = {};
			//linear=0，step=1，cubicspline=2
			switch (sampler->interpolation)
			{
			case interpolation_e::linear:
			{
				auto q0 = getQuat(output, prevKey);
				auto q1 = getQuat(output, nextKey);
				auto r = glm::normalize(glm::slerp(q0, q1, tn));
				glm::quat q(r.w, r.x, r.y, r.z);
				qr = q;
			}
			break;
			case interpolation_e::step:
			{
				qr = glm::normalize(getQuat(output, prevKey));
			}
			break;
			case interpolation_e::cubicspline:
			{
				// GLTF requires cubic spline interpolation for quaternions.
				// https://github.com/KhronosGroup/glTF/issues/1386
				auto r = cubicSpline(prevKey, nextKey, output, keyDelta, tn, 4, *rb);
				glm::quat q(r.w, r.x, r.y, r.z);
				qr = glm::normalize(q);
			}
			break;
			default:
				break;
			}
			*rb = { qr.x, qr.y, qr.z, qr.w };
			return glm::vec4(qr.x, qr.y, qr.z, qr.w);
		}
		glm::vec4 ret = {};
		switch (sampler->interpolation)
		{
			case interpolation_e::step:
			ret = step(prevKey, output, stride, *rb);
			break;
		case interpolation_e::cubicspline:
			ret = cubicSpline(prevKey, nextKey, output, keyDelta, tn, stride, *rb);
			break;
		default:
			ret = linear(prevKey, nextKey, output, tn, stride, *rb);
			break;
		}
		return ret;
	}

};

