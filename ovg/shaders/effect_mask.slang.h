// effect_mask.slang -- render the effect mask into an R8 target.
// The VG vertex shader feeds the fill color at location 1; only its alpha
// contributes to the effect.  RGB is not needed (effect_present reads R).

struct FSIn {
	[[vk::location(1)]] float4 src : TEXCOORD0;
};

[shader("fragment")]
float4 fs(FSIn input) : SV_Target {
	return float4(input.src.a);
}
