#include "LightBoxPointData.h"
#include "LightBoxGeometry.h"
#include "LightBoxMotion.h"
#include "LightBoxSimulation.h"

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
FLightBoxPointLayout LightBoxPoints::Build(const FLightBoxStyle& S,float Time)
{
    FLightBoxPointLayout L;
    struct FTriangle {FIntVector IDs;FLinearColor A,B,C;double Weight,End,DensityArea;};
    struct FPart {int32 First,Last;double Start,End;};TArray<FPart> Parts;
    TArray<FTriangle> Triangles;double Area=0,Weight=0;
    // Sampling uses a fixed reference pose; changing the live wing angle must not reseed its grains.
    const auto Transforms=FLightBoxMotionSystem::Evaluate(S,0);
    const auto Objects=FLightBoxMotionSystem::ResolveBindings(S,Transforms);
    const FVector Light=FRotator(S.KeyDirection.X,S.KeyDirection.Y,0).Vector()*-1;
    for(int32 K=0;K<Objects.Num();++K){
        const auto& O=Objects[K];const auto Mesh=FLightBoxMesh::Build(O,0);
        const int32 Offset=L.InitialVertices.Num(),First=Triangles.Num();double ObjectArea=0;
        L.Offsets.Add(Offset);L.LocalVertices.Add(Mesh.Vertices);
        bool Moving=O.IsDeforming()||O.Animation!=TEXT("none")||!O.Motions.IsEmpty()||O.Reaction!=TEXT("none");
        const FLightBoxObject* Parent=&O;for(int32 D=0;D<8&&!Parent->Parent.IsEmpty();++D){Parent=S.Objects.FindByPredicate([&](const auto& X){return X.Id==Parent->Parent;});if(!Parent)break;Moving|=Parent->Animation!=TEXT("none")||!Parent->Motions.IsEmpty();}
        L.Moving.Add(Moving);L.bAnimated|=Moving;
        for(const auto& V:Mesh.Vertices){const FVector P=Transforms[K].TransformPosition(V);L.InitialVertices.Add(FVector4f(P.X,P.Y,P.Z,1));}
        auto Color=[&](int32 I){const FVector N=Transforms[K].TransformVectorNoScale(Mesh.Normals[I]).GetSafeNormal();
            const float Diffuse=FMath::Max(0.f,float(FVector::DotProduct(N,Light)));
            FLinearColor Illumination=FLinearColor::White*(.36f*S.Ambient+.24f*O.Emission)+S.KeyColor*(.68f*S.KeyIntensity/3.5f*Diffuse)+S.FillColor*.22f;
            const FVector Position=Transforms[K].TransformPosition(Mesh.Vertices[I]);
            for(const auto& A:S.Lights){const FVector Delta=A.Position-Position;const float Falloff=FMath::Square(FMath::Clamp(1-float(Delta.Size())/A.Radius,0.f,1.f));Illumination+=A.Color*(A.Intensity/2500.f*Falloff*FMath::Max(0.f,float(FVector::DotProduct(N,Delta.GetSafeNormal()))));}
            FLinearColor Base=O.Shape==TEXT("blossom")?FLinearColor::White:O.Color;const float Luma=Base.GetLuminance();if(Luma>0&&Luma<.12f)Base*=.12f/Luma;return Mesh.Colors[I]*Base*Illumination*FMath::Sqrt(S.Glow);};
        for(int32 J=0;J<Mesh.Triangles.Num();J+=3){
            const int32 A=Mesh.Triangles[J],B=Mesh.Triangles[J+1],C=Mesh.Triangles[J+2];
            const FVector PA(L.InitialVertices[Offset+A]),PB(L.InitialVertices[Offset+B]),PC(L.InitialVertices[Offset+C]);
            const double Size=FVector::CrossProduct(PB-PA,PC-PA).Size()*.5;if(Size<.00001)continue;
            Area+=Size;ObjectArea+=Size;Triangles.Add({FIntVector(Offset+A,Offset+B,Offset+C),Color(A),Color(B),Color(C),Size,0,0});
        }
        // Large water/terrain must not consume all the samples and erase birds or small features.
        const double Scale=(O.bFocus?1.:.3)/FMath::Sqrt(FMath::Max(1.,ObjectArea));
        const double Start=Weight;
        for(int32 J=First;J<Triangles.Num();++J){Weight+=Triangles[J].Weight*Scale;Triangles[J].End=Weight;Triangles[J].DensityArea=1/Scale;}
        Parts.Add({First,Triangles.Num()-1,Start,Weight});
    }
    const int32 Budget=FMath::Clamp(S.PointCount,256,Maximum);
    L.FieldOffset=L.InitialVertices.Num();L.FieldCount=FMath::Clamp(S.Count,0,FMath::Min(1024,Budget));L.bAnimated|=L.FieldCount>0;
    FLightBoxSimulation Field;auto InitialField=S;InitialField.FormationNext=TEXT("none");Field.Reset(InitialField);
    for(int32 I=0;I<L.FieldCount;++I){const auto P=Field.Motes[I].Position;L.InitialVertices.Add(FVector4f(P.X,P.Y,P.Z,1));}
    const int32 Background=(!Triangles.IsEmpty()&&S.Background.GetLuminance()>.012f)?FMath::Min(2048,Budget/16):0;
    const int32 Surface=Triangles.IsEmpty()&&L.FieldCount>0?0:Budget-L.FieldCount-Background;
    L.Points.Reserve(Surface+L.FieldCount+Background);
    const float Radius=FMath::Clamp(float(FMath::Sqrt(FMath::Max(Area,90000.)/FMath::Max(1,Surface)))*.38f,.1f,3.f);
    FLightBoxStyle Fallback=S;Fallback.FormationNext=TEXT("none");
    TArray<int32> Quotas;int32 Assigned=0;
    for(const auto& Part:Parts){const int32 N=Weight>0?FMath::FloorToInt(Surface*(Part.End-Part.Start)/Weight):0;Quotas.Add(N);Assigned+=N;}
    for(int32 K=0;Assigned<Surface&&Weight>0;++K)if(Parts[K%Parts.Num()].End>Parts[K%Parts.Num()].Start){++Quotas[K%Parts.Num()];++Assigned;}
    int32 PartIndex=0,LocalIndex=0;
    for(int32 I=0;I<Surface;++I){
        FLightBoxPoint P;P.Radius=Radius;
        if(Triangles.IsEmpty()){P.Position=FVector3f(FLightBoxSimulation::Target(I,Surface,Fallback,0));P.Color=S.ParticleColor(I,FVector(P.Position),0,0);}
        else {
            while(PartIndex<Quotas.Num()&&LocalIndex>=Quotas[PartIndex]){++PartIndex;LocalIndex=0;}
            const auto& Part=Parts[PartIndex];const uint32 Seed=LocalIndex^GetTypeHash(Objects[PartIndex].Id);
            const double Selected=Part.Start+Random(Seed,9)*(Part.End-Part.Start);int32 Low=Part.First,High=Part.Last;
            while(Low<High){const int32 Mid=(Low+High)/2;if(Triangles[Mid].End<Selected)Low=Mid+1;else High=Mid;}
            const auto& T=Triangles[Low];P.Owner=FName(*Objects[PartIndex].Id);P.Sample=LocalIndex++;
            P.Radius=FMath::Clamp(float(FMath::Sqrt(T.DensityArea*Weight/FMath::Max(1,Surface)))*.38f*S.ParticleSize,.08f,6.f);const float U=FMath::Sqrt(Random(Seed,10)),V=Random(Seed,11),A=1-U,B=U*(1-V),C=U*V;
            P.Vertices=T.IDs;P.Weights=FVector2f(A,B);P.Position=Destination(P,L.InitialVertices,L.InitialVertices,0);P.Color=T.A*A+T.B*B+T.C*C;
            const auto& LocalVertices=L.LocalVertices[PartIndex];const int32 Offset=L.Offsets[PartIndex];
            const FVector Local=LocalVertices[T.IDs.X-Offset]*A+LocalVertices[T.IDs.Y-Offset]*B+LocalVertices[T.IDs.Z-Offset]*C;
            P.Color=FLightBoxMesh::PatternColor(Objects[PartIndex],Local,P.Color);
            // Static surfaces need no extra GPU lookups even in an animated scene.
            const int32 Object=L.Offsets.IndexOfByPredicate([&](int32 Offset){return Offset>T.IDs.X;});
            const int32 Owner=Object==INDEX_NONE?L.Offsets.Num()-1:Object-1;
            if(!L.Moving[Owner])P.Vertices=FIntVector(-1,-1,-1);
        }
        L.Points.Add(P);
    }
    for(int32 I=0;I<L.FieldCount;++I){FLightBoxPoint P;P.Owner=TEXT("__field");P.Sample=I;P.Vertices=FIntVector(L.FieldOffset+I);P.Position=FVector3f(Field.Motes[I].Position);P.Radius=FMath::Clamp(S.ParticleSize*1.7f,.3f,8.f);P.Color=S.ParticleColor(I,FVector(P.Position),0,0)*FMath::Max(.7f,S.Glow);L.Points.Add(P);}
    for(int32 I=0;I<Background;++I){FLightBoxPoint P;P.Owner=TEXT("__sky");P.Sample=I;const float Z=Random(I,20)*2-1,A=Random(I,21)*2*PI,R=FMath::Sqrt(1-Z*Z);P.Position=FVector3f(R*FMath::Cos(A)*850,R*FMath::Sin(A)*850,180+Z*650);P.Radius=1.4f+Random(I,22)*1.8f;P.Color=FMath::Lerp(S.Background,S.Accent*.18f,Random(I,23)*.3f);L.Points.Add(P);}
    if(L.bAnimated&&Time!=0){
        L.InitialVertices=Frame(L,S,Time,FVector2D::ZeroVector,0,{});
        for(auto& P:L.Points)if(P.Vertices.X>=0)P.Position=Destination(P,L.InitialVertices,L.InitialVertices,1);
    }
    return L;
}

TArray<FLightBoxPoint> LightBoxPoints::Sample(const FLightBoxStyle& S){return Build(S).Points;}

FVector3f LightBoxPoints::Destination(const FLightBoxPoint& P,const TArray<FVector4f>& A,const TArray<FVector4f>& B,float Mix)
{
    if(P.Vertices.X<0)return P.Position;
    auto At=[&](int32 I){return FVector3f(FMath::Lerp(A[I],B[I],Mix));};
    return At(P.Vertices.X)*P.Weights.X+At(P.Vertices.Y)*P.Weights.Y+At(P.Vertices.Z)*(1-P.Weights.X-P.Weights.Y);
}

TArray<FVector4f> LightBoxPoints::Frame(const FLightBoxPointLayout& L,const FLightBoxStyle& S,float Time,const FVector2D& Tilt,float Burst,const TArray<FVector4f>& Field)
{
    auto Result=L.InitialVertices;
    const auto World=FLightBoxMotionSystem::Evaluate(S,Time,Tilt,Burst);const auto Resolved=FLightBoxMotionSystem::ResolveBindings(S,World);
    for(int32 I=0;I<S.Objects.Num();++I)if(L.Moving[I]){
        const auto Vertices=S.Objects[I].IsDeforming()?FLightBoxMesh::Build(Resolved[I],Time).Vertices:L.LocalVertices[I];
        if(Vertices.Num()!=L.LocalVertices[I].Num())continue;
        for(int32 J=0;J<Vertices.Num();++J){const FVector P=World[I].TransformPosition(Vertices[J]);Result[L.Offsets[I]+J]=FVector4f(P.X,P.Y,P.Z,1);}
    }
    for(int32 I=0;I<FMath::Min(L.FieldCount,Field.Num());++I)Result[L.FieldOffset+I]=Field[I];
    return Result;
}
