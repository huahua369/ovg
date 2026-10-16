// blit.slang -- composite + glass rim highlight
Texture2D<float4> tex;
SamplerState samp;

cbuffer B {
    float2 res;
    float  time;
    float  rim;
};

struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
VSOut vs(uint vid : SV_VertexID){
    float2 p = float2((vid == 1) ? 3 : -1, (vid == 2) ? 3 : -1);
    VSOut o;
    o.pos = float4(p, 0, 1);
    o.uv = p * 0.5 + 0.5;
    return o;
}

[shader("fragment")]
float4 fs(VSOut i) : SV_Target {
    float4 c = tex.SampleLevel(samp, i.uv, 0);

    float2 p = i.uv * 2 - 1;
    float r = length(p);
    float mask = smoothstep(0.75, 0.65, r);

    float2 n = normalize(p + 1e-5);
    float light = pow(max(0.0, dot(n, normalize(float2(-0.6, -0.8)))), 2.0);

    float3 col = c.rgb;
    col += light * mask * rim;
    col = lerp(c.rgb, col, mask);
    col += (1 - mask) * float3(0.02, 0.02, 0.03);

    return float4(col, 1);
}
