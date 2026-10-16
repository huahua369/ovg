// noise.slang -- procedural turbulence (feTurbulence-like)
[[vk::binding(0, 1)]]
RWTexture2D<float4> dst;

struct Params {
	float2 texel;
	float  time;
	float  freq;
};

[[vk::binding(0, 2)]]
ConstantBuffer<Params> pu;

float hash(float2 p) {
	return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float noise(float2 p) {
	float2 i = floor(p), f = frac(p);
	float a = hash(i);
	float b = hash(i + float2(1, 0));
	float c = hash(i + float2(0, 1));
	float d = hash(i + float2(1, 1));
	float2 u = f * f * (3 - 2 * f);
	return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float fbm(float2 p) {
	float v = 0, a = 0.5;
	for (int i = 0; i < 5; i++) {
		v += a * noise(p);
		p *= 2.0; a *= 0.5;
	}
	return v;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 gid : SV_DispatchThreadID) {
	int2 sz;
	dst.GetDimensions(sz.x, sz.y);
	int2 ip = int2(gid.xy);
	if (any(ip >= sz)) return;

	float2 uv = float2(ip) * pu.texel;
	float n = fbm(uv * pu.freq * 100.0 + pu.time * 0.05);
	dst[ip] = float4(n, n, n, 1);
}
