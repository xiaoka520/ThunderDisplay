#pragma once
// The negotiated desktop stream contains sRGB-encoded channels with a BT.709
// YCbCr matrix. Decode the matrix/range explicitly, without driver gamma edits.
inline constexpr const char* DesktopColorShader=R"hlsl(
Texture2D<float> luma : register(t0);
Texture2D<float2> chroma : register(t1);
Texture2D<float4> rgbImage : register(t2);
SamplerState linearClamp : register(s0);
cbuffer ColorParameters : register(b0) {
    float2 textureSize; float tenBit; float unused;
    float4 cropRect;
};
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex vs(uint id : SV_VertexID) {
    Vertex v; v.uv=float2((id<<1)&2,id&2);
    v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v;
}
float4 convert(Vertex v) : SV_Target {
    int2 p=int2(v.position.xy);
    float y=luma.Load(int3(p,0));
    // Left-sited horizontal chroma, centered vertically. Clamp to visible
    // pixels, excluding the decoder's padded right/bottom border.
    float2 uv=clamp((float2(p)+float2(1,0.5))/textureSize,
        1/textureSize,(cropRect.zw-1)/textureSize);
    float2 c=chroma.SampleLevel(linearClamp,uv,0);
    if(tenBit>0.5) {
        y=(y*(65535.0/64.0)-64)/876;
        c=(c*(65535.0/64.0)-512)/896;
    } else {
        y=(y*255-16)/219; c=(c*255-128)/224;
    }
    float3 rgb=float3(y+1.5748*c.y,y-0.187324273*c.x-0.468124273*c.y,y+1.8556*c.x);
    // No transfer-function correction: capture and presentation both use sRGB.
    return float4(saturate(rgb),1);
}
float4 blit(Vertex v) : SV_Target {
    float2 uv=(cropRect.xy+v.uv*cropRect.zw)/textureSize;
    return float4(rgbImage.SampleLevel(linearClamp,uv,0).rgb,1);
}
)hlsl";
