// displace.slang -- feDisplacementMap
[[vk::binding(0, 0)]] Sampler2D src;
[[vk::binding(1, 0)]] Sampler2D map;
[[vk::binding(0, 1)]] RWTexture2D<float4> dst;

struct Params {
	float2 texel;
	float  scale;
	float  time;
};

[[vk::binding(0, 2)]]
ConstantBuffer<Params> pu;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 gid : SV_DispatchThreadID) {
	int2 sz;
	dst.GetDimensions(sz.x, sz.y);
	int2 ip = int2(gid.xy);
	if (any(ip >= sz)) return;

	float2 uv = (ip + 0.5) * pu.texel;
	float4 m = map.SampleLevel(uv, 0);

	float2 off = (m.rg - 0.5) * 2.0 * pu.scale;
	float2 suv = uv + off * pu.texel;

	suv = clamp(suv, 0.0, 1.0 - pu.texel);
	dst[ip] = src.SampleLevel(suv, 0);
}
