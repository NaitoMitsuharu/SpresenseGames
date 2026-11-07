#include "LightBoxPointData.h"
#include "LightBoxGeometry.h"
#include "LightBoxMotion.h"
#include "LightBoxSimulation.h"
#include "LightBoxModels.h"
#include "Math/Float16Color.h"

float LightBoxPoints::Random(uint32 Index,uint32 Channel)
{
    uint32 X=Index+Channel*747796405u+2891336453u;X^=X>>16;X*=2246822519u;X^=X>>13;X*=3266489917u;X^=X>>16;
    return float(X&0x00ffffffu)/16777216.f;
}
float LightBoxPoints::Ease(uint32 I,float Phase,float Stagger)
{
    const float Delay=Random(I,1)*Stagger;
    const float U=FMath::Clamp((Phase-Delay)/(1-Delay),0.f,1.f);
    return 1-FMath::Pow(1-U,3.f);
}
FVector3f LightBoxPoints::Position(const FLightBoxPoint& A,const FLightBoxPoint& B,uint32 I,float Phase,float Arc,float Twist,float Stagger,float Burst)
{
    const float S=Ease(I,Phase,Stagger);
    const FVector3f R(Random(I,2)*2-1,Random(I,3)*2-1,Random(I,4)*2-1);
    if(B.bFollow)return B.Position+R*Burst*85;
    const FVector3f T(-R.Y,R.X,R.Z*.45f);
    const float Angle=S*Twist*.35f;
    const float Bend=FMath::Min(FVector3f::Distance(A.Position,B.Position)*.12f,Arc*.2f);
    return FMath::Lerp(A.Position,B.Position,S)+(R*FMath::Cos(Angle)+T*FMath::Sin(Angle))*(FMath::Sin(S*PI)*(1-S)*Bend)+R*Burst*85;
}
FLightBoxPoint LightBoxPoints::Scatter(uint32 I,const FLightBoxPoint& Target,const FVector& Center)
{
    FLightBoxPoint P=Target;const float Z=Random(I,5)*2-1,A=Random(I,6)*2*PI;
    const float R=FMath::Sqrt(FMath::Max(0.f,1-Z*Z));
    P.Position=Target.Position+FVector3f(R*FMath::Cos(A)*65,R*FMath::Sin(A)*65,Z*65);
    P.Radius=0;P.bFollow=false;
    return P;
}
namespace
{
    void Basis(const FVector3f& N,FVector3f& U,FVector3f& V)
    {
        U=FVector3f::CrossProduct(N,FMath::Abs(N.Z)<.9f?FVector3f::UpVector:FVector3f::ForwardVector).GetSafeNormal();V=FVector3f::CrossProduct(N,U);
    }
    float Cell(const FVector3f& P,float Size,uint32 Channel)
    {
        const FIntVector C(FMath::FloorToInt(P.X/Size),FMath::FloorToInt(P.Y/Size),FMath::FloorToInt(P.Z/Size));
        return LightBoxPoints::Random(uint32(C.X*73856093)^uint32(C.Y*19349663)^uint32(C.Z*83492791),Channel);
    }
}

namespace
{
    bool Solidity(const FLightBoxObject& O)
    {
        return O.Shape==TEXT("sphere")||O.Shape==TEXT("rock")||O.Shape==TEXT("box")||O.Shape==TEXT("cylinder")
            ||(O.Shape==TEXT("loft")&&O.Sections.Num()>=3&&O.Curve.Num()<2)||(O.Shape==TEXT("lathe")&&O.Profile.Num()>=3&&O.Curve.Num()<2);
    }
    /** U is the point in the object's unit box. A small margin keeps grains on touching surfaces. */
    bool Buried(const FLightBoxObject& O,const FVector& U,double M=.985)
    {
        if(O.Shape==TEXT("sphere"))return U.SizeSquared()<.25*M*M;
        if(O.Shape==TEXT("rock"))return U.SizeSquared()<.36*.36*M*M;
        if(O.Shape==TEXT("box"))return U.GetAbsMax()<.5*M;
        if(O.Shape==TEXT("cylinder"))return U.X*U.X+U.Y*U.Y<.25*M*M&&FMath::Abs(U.Z)<.5*M;
        if(O.Shape==TEXT("loft")){
            const auto& Q=O.Sections;if(U.X<=Q[0].X||U.X>=Q.Last().X)return false;int32 I=0;while(I+2<Q.Num()&&Q[I+1].X<U.X)++I;
            const double T=(U.X-Q[I].X)/FMath::Max(1e-6,Q[I+1].X-Q[I].X),W=FMath::Lerp(Q[I].Y,Q[I+1].Y,T)*M,H=FMath::Lerp(Q[I].Z,Q[I+1].Z,T)*M;
            return W>1e-3&&H>1e-3&&FMath::Square(U.Y/W)+FMath::Square(U.Z/H)<1;
        }
        if(O.Shape==TEXT("lathe")){
            const auto& Q=O.Profile;if(U.Z<=Q[0].X||U.Z>=Q.Last().X)return false;int32 I=0;while(I+2<Q.Num()&&Q[I+1].X<U.Z)++I;
            const double R=FMath::Lerp(Q[I].Y,Q[I+1].Y,(U.Z-Q[I].X)/FMath::Max(1e-6,Q[I+1].X-Q[I].X))*M;
            return U.X*U.X+U.Y*U.Y<R*R;
        }
        return false;
    }
}

void LightBoxPoints::Detail(const FString& Kind,float L,const FVector3f& N,uint32 Seed,FLightBoxPoint& P)
{
    if(Kind==TEXT("none")||N.IsNearlyZero())return;
    const float R=Random(Seed,40),A=Random(Seed,41)*2-1,B=Random(Seed,42)*2-1;
    FVector3f U,V;Basis(N,U,V);FLinearColor Shade(1,1,1,1);
    if(Kind==TEXT("fur")){
        // Hairs: a grain sits somewhere along a strand that leans and droops; roots are shadowed.
        // Neighbouring hairs share a lean (clumps), so the coat reads as combed fur rather than noise.
        const float Clump=FMath::Max(1.f,L*1.4f),CA=Cell(P.Position,Clump,46)*2-1,CB=Cell(P.Position,Clump,47)*2-1;
        // A dense undercoat closes the skin; guard hairs rise above it, so no background shows through.
        const float T=R<.4f?R*.25f:FMath::Pow((R-.4f)/.6f,.85f),Lean=L*.3f*T;
        P.Position+=N*(L*T)+(U*(CA*.5f+A*.5f)+V*(CB*.5f+B*.5f))*Lean+FVector3f(0,0,-L*.15f*T*T);
        Shade*=.78f+.3f*T;P.Radius*=1.6f;
    }else if(Kind==TEXT("feather")){
        const float T=R;P.Position+=N*(L*.3f*T)+(U*A+V*B)*(L*.45f*T);Shade*=.7f+.45f*T;
    }else if(Kind==TEXT("foliage")){
        // Leaf clumps: points gather on small lobes, each lobe gets its own green and a sunlit rim.
        const float Clump=L*1.8f,C=Cell(P.Position,Clump,43),H=Cell(P.Position,Clump,44);
        const float Out=FMath::Pow(R,.6f);P.Position+=N*(L*(.2f+1.1f*Out*C))+(U*A+V*B)*(L*.55f);
        Shade*=FLinearColor(.55f+.5f*Out+.15f*H,.6f+.5f*Out,.5f+.45f*Out-.1f*H,1);
    }else if(Kind==TEXT("bark")){
        // Furrows run along the limb: vary around its circumference (normal azimuth), wander slowly with height.
        const float Around=FMath::Atan2(N.Y,N.X);
        const float G=FMath::Sin(Around*9+FMath::Sin(P.Position.Z/(L*6))*1.5f+Random(Seed,45)*.5f)+.35f*FMath::Sin(Around*23+P.Position.Z/(L*2));
        const float Ridge=FMath::Clamp(G*.38f+.5f,0.f,1.f);P.Position+=N*(L*.4f*Ridge);Shade*=.32f+.85f*Ridge;
    }else if(Kind==TEXT("grass")){
        const float T=R;P.Position+=FVector3f(0,0,L*T)+N*(L*.08f*T)+(U*A+V*B)*(L*.18f*T*T);Shade*=.45f+.7f*T;
    }
    const float Alpha=P.Color.A;P.Color*=Shade;P.Color.A=Alpha;P.Radius*=.82f;
}

void LightBoxPoints::Engrave(const FLightBoxObject& O,const FVector3f& N,uint32 Seed,FLightBoxPoint& P)
{
    // Laser-engraved glass: cool white dots whose brightness keeps the lighting, with a faint inner volume.
    const float Lum=FMath::Clamp(P.Color.GetLuminance()*1.35f,0.f,1.f);
    const float Alpha=P.Color.A;const FLinearColor Ice(.80f,.90f,1.f,1);
    P.Color=FMath::Lerp(Ice,P.Color*(.9f/FMath::Max(.08f,P.Color.GetMax())),.14f)*(.22f+1.05f*Lum);
    P.Color.A=Alpha;P.Radius*=.58f;
    if(P.Vertices.X<0&&Random(Seed,50)<.12f){P.Position-=N*(Random(Seed,51)*FMath::Min(float(O.Size.GetMin())*.22f,8.f));P.Color*=.55f;P.Color.A=Alpha;}
}

FLightBoxPointLayout LightBoxPoints::Build(const FLightBoxStyle& S,float Time)
{
    FLightBoxPointLayout L;L.Poses=LightBoxAnimation::Prepare(S);
    struct FTriangle {FIntVector IDs;FLinearColor A,B,C;double Weight,End,DensityArea;FVector3f Normal;bool bFoliage;bool bSmooth=false;};
    struct FPart {int32 First,Last;double Start,End,DensityArea=1;TSharedPtr<const FLightBoxModelCloud,ESPMode::ThreadSafe> Cloud;float ModelScale=1;};TArray<FPart> Parts;
    TArray<FTriangle> Triangles;double Area=0,Weight=0;
    // Sampling uses a fixed reference pose; changing the live wing angle must not reseed its grains.
    const auto Transforms=FLightBoxMotionSystem::Evaluate(S,0);
    const auto Objects=FLightBoxMotionSystem::ResolveBindings(S,Transforms);
    const FVector Light=FRotator(S.KeyDirection.X,S.KeyDirection.Y,0).Vector()*-1;
    auto Illuminate=[&](const FLightBoxObject& O,const FVector& N,const FVector& Position){
        // Fur and feathers scatter light: wrapped diffuse keeps the far side from reading as black speckles.
        const float Wrap=(O.Detail==TEXT("fur")||O.Detail==TEXT("feather"))?.65f:0.f;const float Diffuse=FMath::Max(0.f,(float(FVector::DotProduct(N,Light))+Wrap)/(1+Wrap));
        FLinearColor Illumination=FLinearColor::White*(.36f*S.Ambient+.24f*O.Emission)+S.KeyColor*(.68f*S.KeyIntensity/3.5f*Diffuse)+S.FillColor*.22f;
        for(const auto& A:S.Lights){const FVector Delta=A.Position-Position;const float Falloff=FMath::Square(FMath::Clamp(1-float(Delta.Size())/A.Radius,0.f,1.f));Illumination+=A.Color*(A.Intensity/2500.f*Falloff*FMath::Max(0.f,float(FVector::DotProduct(N,Delta.GetSafeNormal()))));}
        return Illumination;};
    for(int32 K=0;K<Objects.Num();++K){
        const auto& O=Objects[K];const auto Mesh=FLightBoxMesh::Build(O,0);
        const int32 Offset=L.InitialVertices.Num(),First=Triangles.Num();double ObjectArea=0;
        L.Offsets.Add(Offset);L.LocalVertices.Add(Mesh.Vertices);
        bool Moving=!L.Poses.IsEmpty()||O.IsDeforming()||O.Animation!=TEXT("none")||!O.Motions.IsEmpty()||O.Reaction!=TEXT("none");
        const FLightBoxObject* Parent=&O;for(int32 D=0;D<8&&!Parent->Parent.IsEmpty();++D){Parent=S.Objects.FindByPredicate([&](const auto& X){return X.Id==Parent->Parent;});if(!Parent)break;Moving|=Parent->Animation!=TEXT("none")||!Parent->Motions.IsEmpty();}
        L.Moving.Add(Moving);L.bAnimated|=Moving;
        FVector MeshCenter=FVector::ZeroVector;
        for(const auto& V:Mesh.Vertices){const FVector P=Transforms[K].TransformPosition(V);MeshCenter+=P;L.InitialVertices.Add(FVector4f(P.X,P.Y,P.Z,1));}
        MeshCenter/=FMath::Max(1,Mesh.Vertices.Num());
        const int32 ModelControl=O.Shape==TEXT("model")&&Moving?L.InitialVertices.Num():INDEX_NONE;L.ModelControls.Add(ModelControl);
        if(ModelControl>=0)for(const auto& V:{FVector::ZeroVector,FVector(64,0,0),FVector(0,64,0),FVector(0,0,64)}){const auto P=Transforms[K].TransformPosition(V);L.InitialVertices.Add(FVector4f(P.X,P.Y,P.Z,1));}
        TArray<FLightBoxSkinBinding> Skin;
        const bool CompatibleSkin=!L.Poses.IsEmpty()&&O.Shape==TEXT("sdf")&&!L.Poses.ContainsByPredicate([&](const auto& Pose){const auto& PartsAtPose=Pose.Objects[K].SdfParts;
            if(PartsAtPose.Num()!=O.SdfParts.Num())return true;
            for(int32 J=0;J<PartsAtPose.Num();++J)if(PartsAtPose[J].Type!=O.SdfParts[J].Type||PartsAtPose[J].bCut!=O.SdfParts[J].bCut)return true;return false;});
        if(CompatibleSkin)for(const auto& V:Mesh.Vertices)Skin.Add(LightBoxAnimation::Bind(O,V));
        L.SkinBindings.Add(MoveTemp(Skin));
        auto Color=[&](int32 I){const FVector N=Transforms[K].TransformVectorNoScale(Mesh.Normals[I]).GetSafeNormal();
            const FLinearColor Illumination=Illuminate(O,N,Transforms[K].TransformPosition(Mesh.Vertices[I]));
            FLinearColor Base=(O.Shape==TEXT("blossom")||O.Shape==TEXT("sdf"))?FLinearColor::White:O.Color;const float Luma=Base.GetLuminance();if(Luma>0&&Luma<.12f)Base*=.12f/Luma;FLinearColor Vertex=Mesh.Colors[I];Vertex.A=1;if(O.Shape==TEXT("sdf")){const float L=Vertex.GetLuminance();if(L>0&&L<.035f)Vertex*=.035f/L;}return Vertex*Base*Illumination*FMath::Sqrt(S.Glow);};
        for(int32 J=0;J<Mesh.Triangles.Num();J+=3){
            const int32 A=Mesh.Triangles[J],B=Mesh.Triangles[J+1],C=Mesh.Triangles[J+2];
            const FVector PA(L.InitialVertices[Offset+A]),PB(L.InitialVertices[Offset+B]),PC(L.InitialVertices[Offset+C]);
            const double Size=FVector::CrossProduct(PB-PA,PC-PA).Size()*.5;if(Size<.00001)continue;
            // Outward normal for surface detail: authored normals when present, otherwise away from the part's centre.
            FVector N=FVector::CrossProduct(PB-PA,PC-PA).GetSafeNormal();
            const FVector Hint=Transforms[K].TransformVectorNoScale(Mesh.Normals[A]+Mesh.Normals[B]+Mesh.Normals[C]);
            if(FVector::DotProduct(N,Hint.IsNearlyZero()?(PA+PB+PC)/3-MeshCenter:Hint)<0)N=-N;
            Area+=Size;ObjectArea+=Size;Triangles.Add({FIntVector(Offset+A,Offset+B,Offset+C),Color(A),Color(B),Color(C),Size,0,0,FVector3f(N),Mesh.Colors[A].A<.75f,O.Shape==TEXT("sdf")&&Mesh.Colors[A].A<.5f});
        }
        FPart Part{First,Triangles.Num()-1,Weight,Weight};
        if(O.Shape==TEXT("model")){
            // A real model: weighted like three times its fitted box, since its leaves/fur carry far more detail.
            Part.Cloud=LightBoxModels::Load(O.Model);
            if(Part.Cloud){Part.ModelScale=LightBoxModels::FitScale(*Part.Cloud,O.Size);const FVector E=FVector(Part.Cloud->Extent())*Part.ModelScale;ObjectArea=6*(E.X*E.Y+E.Y*E.Z+E.Z*E.X);Area+=ObjectArea;}
        }
        // Large water/terrain must not consume all the samples and erase birds or small features.
        const double Scale=(O.bFocus?1.:.3)/FMath::Sqrt(FMath::Max(1.,ObjectArea));
        for(int32 J=First;J<Triangles.Num();++J){Weight+=Triangles[J].Weight*Scale;Triangles[J].End=Weight;Triangles[J].DensityArea=1/Scale;}
        if(Part.Cloud)Weight+=ObjectArea*Scale;
        Part.End=Weight;Part.DensityArea=1/Scale;Parts.Add(Part);
    }
    const int32 Budget=FMath::Clamp(S.PointCount,256,Maximum);
    L.FieldOffset=L.InitialVertices.Num();L.FieldCount=FMath::Clamp(S.Count,0,FMath::Min(1024,Budget));L.bAnimated|=L.FieldCount>0;
    FLightBoxSimulation Field;auto InitialField=S;InitialField.FormationNext=TEXT("none");Field.Reset(InitialField);
    for(int32 I=0;I<L.FieldCount;++I){const auto P=Field.Motes[I].Position;L.InitialVertices.Add(FVector4f(P.X,P.Y,P.Z,1));}
    const bool NoSurface=Triangles.IsEmpty()&&!Parts.ContainsByPredicate([](const FPart& Q){return Q.Cloud.IsValid();});
    const int32 Background=(!NoSurface&&S.Background.GetLuminance()>.012f)?FMath::Min(2048,Budget/16):0;
    const int32 Surface=NoSurface&&L.FieldCount>0?0:Budget-L.FieldCount-Background;
    L.Points.Reserve(Surface+L.FieldCount+Background);
    const float Radius=FMath::Clamp(float(FMath::Sqrt(FMath::Max(Area,90000.)/FMath::Max(1,Surface)))*.38f,.1f,3.f);
    FLightBoxStyle Fallback=S;Fallback.FormationNext=TEXT("none");
    // Static, opaque, single-copy primitives whose interior can be tested analytically.
    struct FOccluder{int32 Object;FBox Bounds;};TArray<FOccluder> Occluders;
    for(int32 K=0;K<Objects.Num();++K){const auto& O=Objects[K];
        if(L.Moving[K]||O.Copies!=1||O.Emission>.5f||!O.bFocus||!Solidity(O))continue;
        FBox Box(ForceInit);const int32 End=K+1<L.Offsets.Num()?L.Offsets[K+1]:L.FieldOffset;for(int32 V=L.Offsets[K];V<End;++V)Box+=FVector(L.InitialVertices[V]);
        if(Box.IsValid)Occluders.Add({K,Box});
    }
    // Point at the part's surface sample, in world space.
    auto Surf=[&](const FPart& Part,uint32 Seed,int32& Low,float& A,float& B,float& C){
        const double Selected=Part.Start+Random(Seed,9)*(Part.End-Part.Start);Low=Part.First;int32 High=Part.Last;
        while(Low<High){const int32 Mid=(Low+High)/2;if(Triangles[Mid].End<Selected)Low=Mid+1;else High=Mid;}
        const auto& T=Triangles[Low];const float U=FMath::Sqrt(Random(Seed,10)),V=Random(Seed,11);A=1-U;B=U*(1-V);C=U*V;
        return FVector(L.InitialVertices[T.IDs.X])*A+FVector(L.InitialVertices[T.IDs.Y])*B+FVector(L.InitialVertices[T.IDs.Z])*C;};
    // Coat: an occluder with fur/feathers hides grains up to about half its hair length above its skin.
    auto Coat=[&](const FLightBoxObject& O){return (O.Detail==TEXT("fur")||O.Detail==TEXT("feather"))?1+O.DetailLength/FMath::Max(1.,O.Size.GetMin()*.5):1.;};
    auto Hidden=[&](int32 Owner,const FVector& World,bool Displaced=false){
        return !L.Moving[Owner]&&Occluders.ContainsByPredicate([&](const FOccluder& Q){const auto& O=Objects[Q.Object];const double Inflate=Displaced?Coat(O):1.;
            return Q.Object!=Owner&&Q.Bounds.ExpandBy(Displaced?O.DetailLength:0).IsInside(World)&&Buried(O,Transforms[Q.Object].InverseTransformPosition(World)/(O.Size*Inflate),Displaced?.97:.985);});};
    // Allocate grains by VISIBLE area: a part mostly sunk inside the torso gets only its exposed share.
    TArray<double> Share;double ShareSum=0;
    for(int32 K=0;K<Parts.Num();++K){const auto& Part=Parts[K];double Span=Part.End-Part.Start;
        if(Span>0&&!Part.Cloud&&!Occluders.IsEmpty()&&!L.Moving[K]){int32 Seen=0;constexpr int32 Probe=96;
            for(int32 J=0;J<Probe;++J){int32 Low;float A,B,C;Seen+=!Hidden(K,Surf(Part,uint32(J)*2654435761u^GetTypeHash(Objects[K].Id)^0x5bd1e995u,Low,A,B,C));}
            Span*=FMath::Max(.01,double(Seen)/Probe);}
        Share.Add(Span);ShareSum+=Span;}
    TArray<int32> Quotas;int32 Assigned=0;
    for(int32 K=0;K<Parts.Num();++K){const int32 N=ShareSum>0?FMath::FloorToInt(Surface*Share[K]/ShareSum):0;Quotas.Add(N);Assigned+=N;}
    for(int32 K=0;Assigned<Surface&&ShareSum>0;++K)if(Share[K%Parts.Num()]>0){++Quotas[K%Parts.Num()];++Assigned;}
    int32 PartIndex=0,LocalIndex=0;
    const bool Crystal=S.PointStyle==TEXT("crystal");
    for(int32 I=0;I<Surface;++I){
        FLightBoxPoint P;P.Radius=Radius;
        if(NoSurface){P.Position=FVector3f(FLightBoxSimulation::Target(I,Surface,Fallback,0));P.Color=S.ParticleColor(I,FVector(P.Position),0,0);}
        else {
            while(PartIndex<Quotas.Num()&&LocalIndex>=Quotas[PartIndex]){++PartIndex;LocalIndex=0;}
            const auto& Part=Parts[PartIndex];const uint32 Base=LocalIndex^GetTypeHash(Objects[PartIndex].Id);
            if(Part.Cloud){
                // Real model grains: the file is shuffled, so the first N points are a uniform subsample.
                const auto& Cloud=*Part.Cloud;const auto& Obj=Objects[PartIndex];const int32 M=Cloud.Positions.Num(),Index=LocalIndex%M;
                FVector Local=LightBoxModels::Local(Cloud,Index,Part.ModelScale);
                if(LocalIndex>=M)Local+=FVector(Random(Base,60)-.5f,Random(Base,61)-.5f,Random(Base,62)-.5f)*2.f; // repeats when the budget exceeds the file
                const FVector World=Transforms[PartIndex].TransformPosition(Local),N=Transforms[PartIndex].TransformVectorNoScale(FVector(Cloud.Normals[Index])).GetSafeNormal();
                P.Owner=FName(*Obj.Id);P.Sample=LocalIndex++;P.Position=FVector3f(World);
                P.Color=FLinearColor(Cloud.Colors[Index])*Illuminate(Obj,N,World)*FMath::Sqrt(S.Glow);
                P.Radius=FMath::Clamp(float(FMath::Sqrt(Part.DensityArea*Weight/FMath::Max(1,Surface)))*(Crystal?.38f:1.1f)*S.ParticleSize,.08f,9.f);
                if(Obj.Detail!=TEXT("none"))Detail(Obj.Detail,Obj.DetailLength,FVector3f(N),Base,P);
                if(Crystal)Engrave(Obj,FVector3f(N),Base,P);
                if(L.ModelControls[PartIndex]>=0){const int32 C=L.ModelControls[PartIndex];P.Vertices=FIntVector(C,C+1,C+2);P.AffineFourth=C+3;P.LocalCoordinates=FVector3f(Transforms[PartIndex].InverseTransformPosition(FVector(P.Position))/64);}
                L.Points.Add(P);continue;
            }
            // Crystal engraving stipples by light: dim surfaces keep fewer dots, so form reads through density.
            // Grains buried inside another solid part are re-drawn, so overlapping parts read as one continuous body.
            uint32 Seed=Base;int32 Low=Part.First;float A=1,B=0,C=0;bool Visible=false;
            for(int32 Try=0;Try<12&&!Visible;++Try){
                Seed=Base+uint32(Try)*0x9E3779B9u;const FVector World=Surf(Part,Seed,Low,A,B,C);const auto& Candidate=Triangles[Low];
                if(Crystal&&Try<4){const float Lit=FMath::Clamp((Candidate.A.GetLuminance()+Candidate.B.GetLuminance()+Candidate.C.GetLuminance())/3*1.4f,0.f,1.f);
                    if(Random(Seed,31)>=.18f+.82f*Lit)continue;}
                Visible=!Hidden(PartIndex,World);
            }
            const auto& T=Triangles[Low];P.Owner=FName(*Objects[PartIndex].Id);P.Sample=LocalIndex++;
            // The shader draws Radius as the quad's full WIDTH. At .38x spacing large parts (a torso among 30 parts)
            // covered ~15% and read as speckle; colour mode now closes the surface, crystal keeps fine separate dots.
            P.Radius=FMath::Clamp(float(FMath::Sqrt(T.DensityArea*Weight/FMath::Max(1,Surface)))*(Crystal?.38f:1.1f)*S.ParticleSize,.08f,9.f);
            P.Vertices=T.IDs;P.Weights=FVector2f(A,B);P.Position=Destination(P,L.InitialVertices,L.InitialVertices,0);P.Color=T.A*A+T.B*B+T.C*C;
            const auto& LocalVertices=L.LocalVertices[PartIndex];const int32 Offset=L.Offsets[PartIndex];
            const FVector Local=LocalVertices[T.IDs.X-Offset]*A+LocalVertices[T.IDs.Y-Offset]*B+LocalVertices[T.IDs.Z-Offset]*C;
            P.Color=FLightBoxMesh::PatternColor(Objects[PartIndex],Local,P.Color);
            // Static surfaces need no extra GPU lookups even in an animated scene.
            const int32 Object=L.Offsets.IndexOfByPredicate([&](int32 Offset){return Offset>T.IDs.X;});
            const int32 Owner=Object==INDEX_NONE?L.Offsets.Num()-1:Object-1;
            if(!L.Moving[Owner])P.Vertices=FIntVector(-1,-1,-1);
            // Moving parts stay bound to their animated triangle; static parts gain fur/leaf/bark relief.
            if(P.Vertices.X<0){
                const auto& Obj=Objects[PartIndex];
                // Trees always read as leaf clumps over ridged bark; an explicit detail length scales the leaves.
                if(Obj.Shape==TEXT("tree"))Detail(T.bFoliage?TEXT("foliage"):TEXT("bark"),T.bFoliage?(Obj.Detail!=TEXT("none")?Obj.DetailLength:float(Obj.Size.GetMax())*.03f):float(Obj.Size.GetMax())*.008f,T.Normal,Seed,P);
                else if(!T.bSmooth)Detail(Obj.Detail,Obj.DetailLength,T.Normal,Seed,P);
            }
            if(Crystal)Engrave(Objects[PartIndex],T.Normal,Seed,P);
            // Never let a buried grain, or one pushed out by its own fur, show through another part's coat.
            if(!Visible||(P.Vertices.X<0&&Objects[PartIndex].Detail!=TEXT("none")&&Hidden(PartIndex,FVector(P.Position),true)))P.Radius=0;
        }
        L.Points.Add(P);
    }
    for(int32 I=0;I<L.FieldCount;++I){FLightBoxPoint P;P.Owner=TEXT("__field");P.Sample=I;P.Vertices=FIntVector(L.FieldOffset+I);P.Position=FVector3f(Field.Motes[I].Position);P.Radius=FMath::Clamp(S.ParticleSize*1.7f,.3f,8.f);P.Color=S.ParticleColor(I,FVector(P.Position),0,0)*FMath::Max(.7f,S.Glow);L.Points.Add(P);}
    for(int32 I=0;I<Background;++I){FLightBoxPoint P;P.Owner=TEXT("__sky");P.Sample=I;const float Z=Random(I,20)*2-1,A=Random(I,21)*2*PI,R=FMath::Sqrt(1-Z*Z);P.Position=FVector3f(R*FMath::Cos(A)*850,R*FMath::Sin(A)*850,180+Z*650);P.Radius=1.4f+Random(I,22)*1.8f;P.Color=FMath::Lerp(S.Background,S.Accent*.18f,Random(I,23)*.3f);L.Points.Add(P);}
    // Prepare topology correspondence and colour keyframes once; playback only moves the original controls.
    for(const auto& Pose:L.Poses){TArray<TArray<FVector>> PoseParts;
        for(int32 I=0;I<S.Objects.Num();++I)PoseParts.Add(LightBoxAnimation::Remap(L.LocalVertices[I],FLightBoxMesh::Build(Pose.Objects[I],0).Vertices));
        L.PoseVertices.Add(MoveTemp(PoseParts));}
    bool Recolour=false;for(const auto& Pose:L.Poses)for(int32 I=0;I<S.Objects.Num();++I){const auto& A=S.Objects[I];const auto& B=Pose.Objects[I];Recolour|=!A.Color.Equals(B.Color);
        for(int32 J=0;J<A.SdfParts.Num()&&J<B.SdfParts.Num();++J)Recolour|=!A.SdfParts[J].Color.Equals(B.SdfParts[J].Color);}
    if(Recolour)for(const auto& Pose:L.Poses){TArray<FLinearColor> Colours;Colours.Reserve(L.Points.Num());
        for(const auto& P:L.Points){FLinearColor C=P.Color;const int32 I=S.Objects.IndexOfByPredicate([&](const auto& O){return FName(*O.Id)==P.Owner;});
            if(I>=0&&S.Objects[I].Shape!=TEXT("model")){FLinearColor A=S.Objects[I].Color,B=Pose.Objects[I].Color;
                if(S.Objects[I].Shape==TEXT("sdf")){const FVector Local=Transforms[I].InverseTransformPosition(FVector(P.Position));LightBoxSdf::Distance(S.Objects[I].SdfParts,Local,A,&A);
                    const auto Bind=LightBoxAnimation::Bind(S.Objects[I],Local);LightBoxSdf::Distance(Pose.Objects[I].SdfParts,LightBoxAnimation::Skin(S.Objects[I],Pose.Objects[I],Local,Bind),B,&B);}
                C=P.Color+(B-A)*(P.Color.GetLuminance()/FMath::Max(.03f,A.GetLuminance()));C.R=FMath::Max(0.f,C.R);C.G=FMath::Max(0.f,C.G);C.B=FMath::Max(0.f,C.B);C.A=P.Color.A;}
            Colours.Add(C);}L.PoseColours.Add(MoveTemp(Colours));}
    if(L.bAnimated&&Time!=0){
        L.InitialVertices=Frame(L,S,Time,FVector2D::ZeroVector,0,{},0);
        for(auto& P:L.Points)if(P.Vertices.X>=0)P.Position=Destination(P,L.InitialVertices,L.InitialVertices,1);
    }
    return L;
}

TArray<FLightBoxPoint> LightBoxPoints::Sample(const FLightBoxStyle& S){return Build(S).Points;}

TArray<FLightBoxPointBinding> LightBoxPoints::Bindings(const TArray<FLightBoxPoint>& Points)
{
    TArray<FLightBoxPointBinding> R;R.Reserve(Points.Num());
    auto Half=[](float V){return FFloat16(V).GetFloat();};
    for(const auto& P:Points){FLightBoxPointBinding B;B.Vertices=P.Vertices;B.Fourth=P.AffineFourth;
        B.Weights=B.Fourth>=0?FVector4f(Half(P.LocalCoordinates.X),Half(P.LocalCoordinates.Y),Half(P.LocalCoordinates.Z),1):FVector4f(Half(P.Weights.X),Half(P.Weights.Y),0,0);
        B.Rest=FVector3f(Half(P.Position.X),Half(P.Position.Y),Half(P.Position.Z));R.Add(B);}
    return R;
}

TArray<FVector4f> LightBoxPoints::Bake(const TArray<FLightBoxPointBinding>& Bindings,const TArray<FVector4f>& Vertices)
{
    TArray<FVector4f> HalfVertices;HalfVertices.SetNumUninitialized(Vertices.Num());
    for(int32 I=0;I<Vertices.Num();++I){const auto& V=Vertices[I];const FFloat16Color Q(FLinearColor(V.X,V.Y,V.Z,V.W));HalfVertices[I]=FVector4f(Q.R.GetFloat(),Q.G.GetFloat(),Q.B.GetFloat(),Q.A.GetFloat());}
    TArray<FVector4f> R;R.SetNumUninitialized(Bindings.Num());
    for(int32 I=0;I<Bindings.Num();++I){const auto& B=Bindings[I];const auto W=B.Weights;FVector4f P(0,0,0,1);
        if(B.Vertices.X>=0){const auto A=HalfVertices[B.Vertices.X],C=HalfVertices[B.Vertices.Y],D=HalfVertices[B.Vertices.Z];
            P=B.Fourth>=0?A*(1-W.X-W.Y-W.Z)+C*W.X+D*W.Y+HalfVertices[B.Fourth]*W.Z:A*W.X+C*W.Y+D*(1-W.X-W.Y);
            P.X-=B.Rest.X;P.Y-=B.Rest.Y;P.Z-=B.Rest.Z;}
        R[I]=P;}
    return R;
}

TArray<float> LightBoxPoints::DepthOrder(TConstArrayView<FVector3f> Positions,const FVector3f& Forward)
{
    constexpr int32 Bins=1024;int32 Counts[Bins]={};TArray<float> Depth;Depth.SetNumUninitialized(Positions.Num());float Min=MAX_flt,Max=-MAX_flt;
    for(int32 I=0;I<Positions.Num();++I){const float D=FVector3f::DotProduct(Positions[I],Forward);Depth[I]=D;Min=FMath::Min(Min,D);Max=FMath::Max(Max,D);}
    const float Scale=(Bins-1)/FMath::Max(.001f,Max-Min);TArray<uint16> Keys;Keys.SetNumUninitialized(Positions.Num());
    for(int32 I=0;I<Depth.Num();++I){const int32 K=FMath::Clamp(FMath::FloorToInt((Depth[I]-Min)*Scale),0,Bins-1);Keys[I]=uint16(K);++Counts[K];}
    int32 At=0;for(int32& N:Counts){const int32 Next=At+N;N=At;At=Next;}
    TArray<float> Order;Order.SetNumUninitialized(Positions.Num());for(int32 I=0;I<Keys.Num();++I)Order[Counts[Keys[I]]++]=float(I);
    return Order;
}

FVector3f LightBoxPoints::Destination(const FLightBoxPoint& P,const TArray<FVector4f>& A,const TArray<FVector4f>& B,float Mix)
{
    if(P.Vertices.X<0)return P.Position;
    auto At=[&](int32 I){return FVector3f(FMath::Lerp(A[I],B[I],Mix));};
    if(P.AffineFourth>=0){const auto W=P.LocalCoordinates;return At(P.Vertices.X)*(1-W.X-W.Y-W.Z)+At(P.Vertices.Y)*W.X+At(P.Vertices.Z)*W.Y+At(P.AffineFourth)*W.Z;}
    return At(P.Vertices.X)*P.Weights.X+At(P.Vertices.Y)*P.Weights.Y+At(P.Vertices.Z)*(1-P.Weights.X-P.Weights.Y);
}

TArray<FVector4f> LightBoxPoints::Frame(const FLightBoxPointLayout& L,const FLightBoxStyle& S,float Time,const FVector2D& Tilt,float Burst,const TArray<FVector4f>& Field,float StoryTime)
{
    auto Result=L.InitialVertices;
    const auto Interval=LightBoxAnimation::Interval(S,StoryTime<0?Time:StoryTime);
    const auto Pose=L.Poses.IsEmpty()?S:LightBoxAnimation::Sample(L.Poses,Interval);
    const auto World=FLightBoxMotionSystem::Evaluate(Pose,Time,Tilt,Burst);const auto Resolved=FLightBoxMotionSystem::ResolveBindings(Pose,World);
    for(int32 I=0;I<S.Objects.Num();++I)if(L.Moving[I]){
        auto Vertices=Resolved[I].IsDeforming()?FLightBoxMesh::Build(Resolved[I],Time).Vertices:L.LocalVertices[I];
        if(!L.Poses.IsEmpty()&&!Resolved[I].IsDeforming()){
            if(!L.SkinBindings[I].IsEmpty()){const auto Palette=LightBoxAnimation::Palette(S.Objects[I],Resolved[I]);
                for(int32 J=0;J<Vertices.Num();++J)Vertices[J]=LightBoxAnimation::Skin(L.LocalVertices[I][J],L.SkinBindings[I][J],Palette);}
            else for(int32 J=0;J<Vertices.Num();++J)Vertices[J]=FMath::Lerp(L.PoseVertices[Interval.From][I][J],L.PoseVertices[Interval.To][I][J],Interval.Mix);
        }
        if(Vertices.Num()!=L.LocalVertices[I].Num())continue;
        for(int32 J=0;J<Vertices.Num();++J){const FVector P=World[I].TransformPosition(Vertices[J]);Result[L.Offsets[I]+J]=FVector4f(P.X,P.Y,P.Z,1);}
        if(L.ModelControls[I]>=0){const FVector Scale=Resolved[I].Size/S.Objects[I].Size;const FVector Axes[]={FVector::ZeroVector,FVector(Scale.X*64,0,0),FVector(0,Scale.Y*64,0),FVector(0,0,Scale.Z*64)};
            for(int32 J=0;J<4;++J){const auto P=World[I].TransformPosition(Axes[J]);Result[L.ModelControls[I]+J]=FVector4f(P.X,P.Y,P.Z,1);}}
    }
    for(int32 I=0;I<FMath::Min(L.FieldCount,Field.Num());++I)Result[L.FieldOffset+I]=Field[I];
    return Result;
}
