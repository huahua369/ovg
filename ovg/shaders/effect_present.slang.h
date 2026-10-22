// effect_present.slang -- compose and draw an effect layer onto the main framebuffer
// mode: 0 = outer glow, 1 = inner glow, 2 = shadow
// The mask/effect textures are stored with the flipped 2D viewport, so the
// vertical offset is applied in texture space.

struct VSOut {
	float4 pos : SV_Position;
	float2 uv  : TEXCOORD0;
};

[shader("vertex")]
VSOut vs(uint vid : SV_VertexID)
{
	float2 p = float2((vid == 1) ? 3 : -1, (vid == 2) ? 3 : -1);
	VSOut o;
	o.pos = float4(p, 0, 1);
	o.uv = p * 0.5 + 0.5;
	return o;
}

struct Params {
	int    mode;		// 0 outer, 1 inner, 2 shadow
	float  spread;		// -1..1
	float  opacity;		// 0..1
	float  pad0;
	float2 offset;		// shadow offset in device pixels
	float4 color;		// straight RGBA, 0..1
	float4 rect;		// x, y, width, height in device pixels
};

[[vk::binding(0, 2)]]
Sampler2D maskSrc;

[[vk::binding(1, 2)]]
Sampler2D blurSrc;

[[vk::binding(0, 3)]]
ConstantBuffer<Params> pu;

[shader("fragment")]
float4 fs(VSOut input) : SV_Target
{
	float2 uv = (input.pos.xy - pu.rect.xy) / pu.rect.zw;
	float2 texel = 1.0 / pu.rect.zw;

	float m = maskSrc.SampleLevel(uv, 0.0).a;
	float2 off = float2(-pu.offset.x, pu.offset.y) * texel;
	float b = blurSrc.SampleLevel(uv + off, 0.0).a;

	float a;
	if (pu.mode == 1) {
		// inner glow: (1 - blurred coverage) clipped to the shape interior
		float s = saturate(1.0 - b);
		a = s * m;
	}
	else {
		// outer glow / shadow: use the blurred coverage directly; the shape
		// itself hides the interior, so only the halo remains visible.
		float s = saturate(b * (1.0 + max(pu.spread, 0.0) * 3.0));
		a = (pu.mode == 0) ? s : s;
	}

	a *= pu.color.a * pu.opacity;
	return float4(pu.color.rgb * a, a);
}
