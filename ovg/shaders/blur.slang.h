// blur.slang -- separable Gaussian blur (feGaussianBlur)
// SDL3 GPU compute: Sampler2D + RWTexture2D + ConstantBuffer

[[vk::binding(0, 0)]]
Sampler2D src;

[[vk::binding(0, 1)]]
RWTexture2D<float4> dst;

struct Params {
	float2 texel;   // 1.0 / texture_size
	float  sigma;
	int    axis;    // 0 = horizontal, 1 = vertical
};

[[vk::binding(0, 2)]]
ConstantBuffer<Params> pu;

float gauss(float x, float s) {
	return exp(-x * x / (2.0 * s * s)) / (s * 2.50662827);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 gid : SV_DispatchThreadID) {
	int2 p = int2(gid.xy);
	int2 sz;
	dst.GetDimensions(sz.x, sz.y);
	if (any(p >= sz)) return;
	int R = max(1, int(ceil(pu.sigma * 3.0)));
	float4 sum = float4(0, 0, 0, 0);
	float wsum = 0.0;
	for (int i = -R; i <= R; i++) {
		float ww = gauss(float(i), pu.sigma);
		int2 sp = p;
		if (pu.axis == 0) sp.x += i;
		else              sp.y += i;
		sp = clamp(sp, int2(0, 0), sz - int2(1, 1));
		float2 sampleUV = (float2(sp) + 0.5) * pu.texel;
		float4 c = src.SampleLevel(sampleUV, 0.0);
		sum += c * ww;
		wsum += ww;
	}
	dst[p] = sum / wsum;
}