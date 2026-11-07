// Precompiled GPU vertex animation. Positions remain in textures between shape changes.
struct PointRandom {
    float H(uint i,uint channel) {
        uint x=i+channel*747796405u+2891336453u;x^=x>>16;x*=2246822519u;x^=x>>13;x*=3266489917u;x^=x>>16;
        return float(x&0x00ffffffu)/16777216.0;
    }
};
PointRandom rng;
uint id=(uint)(Index+.5);
// Both texture widths are powers of two. Reciprocal addressing is exact for the
// supported point indices and avoids mobile dynamic integer divide/modulo.
float2 dataUV=float2(frac((Index+.5)/DataWidth),(floor(Index/DataWidth)+.5)/DataWidth);
float4 b=Texture2DSampleLevel(ToTex,ToTexSampler,dataUV,0);
float follow=b.w<0?1:0;b.w=abs(b.w);
[branch] if(Animated>1.5) {
    float2 uv=float2(dataUV.x,(floor(Index/DataWidth)*2+.5+FrameMix)/(DataWidth*2));
    float4 delta=Texture2DSampleLevel(VertexTo,VertexToSampler,uv,0);
    b.xyz+=delta.xyz;b.w*=delta.w;
} else [branch] if(Animated>.5) {
    float4 links=Texture2DSampleLevel(LinksTex,LinksTexSampler,dataUV,0);
    [branch] if(links.x>=0) {
        float4 weights=Texture2DSampleLevel(WeightsTex,WeightsTexSampler,dataUV,0);
        bool affine=weights.w>.5;
        float4 target=0;
        [unroll] for(uint k=0;k<3;k++) {
            uint v=(uint)(links[k]+.5);
            float2 uv=float2(frac((float(v)+.5)/VertexWidth),(floor(float(v)/VertexWidth)*2+.5+FrameMix)/(VertexWidth*2));
            float4 vertex=Texture2DSampleLevel(VertexTo,VertexToSampler,uv,0);
            target+=vertex*(affine?(k==0?1-weights.x-weights.y-weights.z:k==1?weights.x:weights.y):(k==0?weights.x:k==1?weights.y:1-weights.x-weights.y));
        }
        [branch] if(affine){float v=links.w;float2 uv=float2(frac((v+.5)/VertexWidth),(floor(v/VertexWidth)*2+.5+FrameMix)/(VertexWidth*2));
            target+=Texture2DSampleLevel(VertexTo,VertexToSampler,uv,0)*weights.z;}
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
// Sensor forces on the formed sculpture, all relative to its size (WaveForce.w = reach).
// Waves (clap / shake): a front crosses the sculpture; each grain is pushed as it passes, then rings out.
// WaveN.xyz = travel direction (zero = radial from the centre), WaveN.w = age; WaveForce.xy = signed strength, z = speed.
float reach=max(WaveForce.w,1.);
float3 c=p-Center;float3 sway=0;
[unroll] for(uint k=0;k<2;k++) {
    float4 w=k==0?Wave0:Wave1;float s=k==0?WaveForce.x:WaveForce.y;
    [branch] if(w.w>-5 && abs(s)>.001) {
        bool radial=dot(w.xyz,w.xyz)<.25;
        float3 dir=radial?c/max(length(c),1.):w.xyz;
        float x=radial?length(c):dot(c,w.xyz)+reach;
        float tau=w.w-x/max(WaveForce.z,1.);
        [branch] if(tau>0) {
            float a=abs(s);float env=exp(-tau*3.4);
            float amp=sign(s)*reach*(.025+.10*a)*(.8+.4*rng.H(id,7))*env;
            float ph=tau*13.8;
            sway+=dir*amp*sin(ph)+float3(0,0,abs(amp)*.35*sin(ph+1.3));
            // Only a sharp, loud clap shakes grains loose for a moment.
            sway+=(float3(rng.H(id,2),rng.H(id,3),rng.H(id,4))*2-1)*reach*.09*saturate((a-.6)/.4)*env*saturate(tau*8);
        }
    }
}
float h=saturate(c.z/(2*reach)+.5);
// Breath: the upper grains bend away from the microphone, a ripple running up through them.
[branch] if(Wind.w>.001) {
    sway+=Wind.xyz*Wind.w*reach*(.07*h*h+.018*sin(SensorClock*3.4-h*5+rng.H(id,8)*1.5));
}
// Magnet: the near side of the sculpture is drawn out toward it, grains streaming slightly.
[branch] if(Pull.w>.001) {
    float facing=saturate(.5+.5*dot(c,Pull.xyz)/reach);facing*=facing;
    sway+=Pull.xyz*Pull.w*reach*facing*(.16*(.85+.3*rng.H(id,9))+.025*sin(SensorClock*3+rng.H(id,10)*6.283));
}
// Warmth: grains around the touched point rise and shimmer like air over a flame.
[branch] if(Heat.w>.001) {
    float f=saturate(1-length(p-Heat.xyz)/(reach*.55));f*=f;
    float r9=rng.H(id,11)*6.283;
    sway+=float3(sin(SensorClock*2.3+r9)*.012,cos(SensorClock*2.9+r9)*.012,.05+.02*sin(SensorClock*3.1+r9))*Heat.w*f*reach;
}
p+=sway;
float3 q=p-Center;
p=Center+AxisX*q.x+AxisY*q.y+AxisZ*q.z;
p=Origin+ObjectX*p.x+ObjectY*p.y+ObjectZ*p.z;
// Keep subpixel points visible on mobile displays, independently of camera distance.
radius=(radius>.00001?max(radius,2*HalfFov.x*length(p-Eye)/max(Viewport.x,1)*1.8):0)*visibility;
return p+(Right*(QuadUV.x-.5)+Up*(QuadUV.y-.5))*radius-World;
