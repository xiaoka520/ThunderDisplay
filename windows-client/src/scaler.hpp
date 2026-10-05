#pragma once
// Two separable, scale-aware Lanczos-2 passes. Conversion remains full-precision
// RGB8/RGB10 on the GPU; the video processor never resizes the source first.
inline constexpr const char* ScalingShader=R"hlsl(
Texture2D<float4> image : register(t0);
cbuffer Scaling : register(b0) {
    float2 sourceSize; float2 targetSize;
    float2 cropOrigin; float2 cropSize;
    float2 axis; float2 padding;
};
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex vs(uint id : SV_VertexID) {
    Vertex v; v.uv=float2((id<<1)&2,id&2);
    v.position=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v;
}
float weight(float x) {
    x=abs(x); if(x<0.00001) return 1; if(x>=2) return 0;
    float p=3.141592653589793*x; return sin(p)*sin(p*0.5)/(p*p*0.5);
}
float4 readPixel(float2 p) { return image.Load(int3(clamp(int2(floor(p+0.5)),int2(cropOrigin),int2(cropOrigin+cropSize)-1),0)); }
float4 ps(Vertex v) : SV_Target {
    float2 p=cropOrigin+v.uv*cropSize-0.5;
    float ratio=max(1,dot(cropSize/targetSize,axis));
    float along=dot(p,axis), start=floor(along);
    float4 sum=0; float total=0;
    [unroll] for(int k=-7;k<=8;++k) {
        float sampleAt=start+k, w=weight((sampleAt-along)/ratio);
        sum+=readPixel(p+axis*(sampleAt-along))*w; total+=w;
    }
    float4 result=sum/max(total,0.00001);
    // Restrict overshoot to nearby source values to avoid halos around text.
    float4 center=readPixel(p), before=readPixel(p-axis*ratio), after=readPixel(p+axis*ratio);
    result.rgb=clamp(result.rgb,min(center.rgb,min(before.rgb,after.rgb)),max(center.rgb,max(before.rgb,after.rgb)));
    return float4(saturate(result.rgb),1);
}
)hlsl";
