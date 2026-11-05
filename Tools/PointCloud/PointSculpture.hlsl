// Precompiled GPU vertex animation. Positions remain in textures between shape changes.
struct PointRandom {
    float H(uint i,uint channel) {
        uint x=i+channel*747796405u+2891336453u;x^=x>>16;x*=2246822519u;x^=x>>13;x*=3266489917u;x^=x>>16;
        return float(x&0x00ffffffu)/16777216.0;
    }
};
PointRandom rng;
uint id=(uint)(Index+.5);
float2 dataUV=(float2(id%(uint)DataWidth,id/(uint)DataWidth)+.5)/DataWidth;
float4 b=Texture2DSampleLevel(ToTex,ToTexSampler,dataUV,0);
float follow=b.w<0?1:0;b.w=abs(b.w);
[branch] if(Animated>.5) {
    float3 links=Texture2DSampleLevel(LinksTex,LinksTexSampler,dataUV,0).xyz;
    [branch] if(links.x>=0) {
        float2 weights=Texture2DSampleLevel(WeightsTex,WeightsTexSampler,dataUV,0).xy;
        float4 target=0;
        [unroll] for(uint k=0;k<3;k++) {
            uint v=(uint)(links[k]+.5);
            float2 uv=(float2(v%(uint)VertexWidth,v/(uint)VertexWidth)+.5)/VertexWidth;
            float4 vertex=lerp(Texture2DSampleLevel(VertexFrom,VertexFromSampler,uv,0),Texture2DSampleLevel(VertexTo,VertexToSampler,uv,0),FrameMix);
            target+=vertex*(k==0?weights.x:k==1?weights.y:1-weights.x-weights.y);
        }
        b.xyz=target.xyz;b.w*=target.w;
    }
}
float3 p=b.xyz;float radius=b.w;float visibility=1;
[branch] if(Phase<1 && follow<.5) {
    float4 a=Texture2DSampleLevel(FromTex,FromTexSampler,dataUV,0);
    float delay=rng.H(id,1)*Path.z;
    float u=saturate((Phase-delay)/(1-delay));float s=1-pow(1-u,3);
    float3 r=float3(rng.H(id,2),rng.H(id,3),rng.H(id,4))*2-1;
    float3 tangent=float3(-r.y,r.x,r.z*.45);
    float angle=s*Path.y*.35;
    float bend=min(length(a.xyz-b.xyz)*.12,Path.x*.2);
    p=lerp(a.xyz,b.xyz,s)+(r*cos(angle)+tangent*sin(angle))*(sin(s*3.141592654)*(1-s)*bend)+r*Path.w*85;
    radius=lerp(a.w,b.w,s);
    if(a.w<.00001||b.w<.00001)visibility=radius/max(a.w+b.w,.00001);
} else if(Path.w>0) {
    p+=(float3(rng.H(id,2),rng.H(id,3),rng.H(id,4))*2-1)*Path.w*85;
}
float3 q=p-Center;
p=Center+AxisX*q.x+AxisY*q.y+AxisZ*q.z;
p=Origin+ObjectX*p.x+ObjectY*p.y+ObjectZ*p.z;
// Keep subpixel points visible on mobile displays, independently of camera distance.
radius=(radius>.00001?max(radius,2*HalfFov.x*length(p-Eye)/max(Viewport.x,1)*1.8):0)*visibility;
return p+(Right*(QuadUV.x-.5)+Up*(QuadUV.y-.5))*radius-World;
