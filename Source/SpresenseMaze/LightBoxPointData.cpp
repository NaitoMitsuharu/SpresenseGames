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
    return U*U*(3-2*U);
}
FVector3f LightBoxPoints::Position(const FLightBoxPoint& A,const FLightBoxPoint& B,uint32 I,float Phase,float Arc,float Twist,float Stagger,float Burst)
{
    const float S=Ease(I,Phase,Stagger);
    const FVector3f R(Random(I,2)*2-1,Random(I,3)*2-1,Random(I,4)*2-1);
    const FVector3f T(-R.Y,R.X,R.Z*.45f);
    const float Angle=S*Twist*2*PI;
    return FMath::Lerp(A.Position,B.Position,S)+(R*FMath::Cos(Angle)+T*FMath::Sin(Angle))*(FMath::Sin(S*PI)*Arc)+R*Burst*85;
}
FLightBoxPoint LightBoxPoints::Scatter(uint32 I,const FLightBoxPoint& Target,const FVector& Center)
{
    FLightBoxPoint P=Target;const float Z=Random(I,5)*2-1,A=Random(I,6)*2*PI;
    const float R=FMath::Sqrt(FMath::Max(0.f,1-Z*Z));
    P.Position=FVector3f(Center)+FVector3f(R*FMath::Cos(A)*230,R*FMath::Sin(A)*170,Z*170);
    return P;
}
TArray<FLightBoxPoint> LightBoxPoints::Sample(const FLightBoxStyle& S)
{
    struct FTriangle {FVector3f A,B,C;FLinearColor CA,CB,CC;double End;};
    TArray<FTriangle> Triangles;double Area=0;
    const auto Transforms=FLightBoxMotionSystem::Evaluate(S,0);
    const auto Objects=FLightBoxMotionSystem::ResolveBindings(S,Transforms);
    const FVector Light=FRotator(S.KeyDirection.X,S.KeyDirection.Y,0).Vector()*-1;
    for(int32 K=0;K<Objects.Num();++K){
        const auto& O=Objects[K];if(O.Shape==TEXT("group"))continue;
        const auto Mesh=FLightBoxMesh::Build(O,0);
        auto C=[&](int32 I){const FVector N=Transforms[K].TransformVectorNoScale(Mesh.Normals[I]).GetSafeNormal();
            const float Diffuse=FMath::Max(0.f,float(FVector::DotProduct(N,Light)));
            FLinearColor Illumination=FLinearColor::White*(.28f*S.Ambient+.2f*O.Emission)+S.KeyColor*(.68f*S.KeyIntensity/3.5f*Diffuse)+S.FillColor*(.2f*(1-Diffuse*.5f));
            const FVector Position=Transforms[K].TransformPosition(Mesh.Vertices[I]);
            for(const auto& L:S.Lights){const FVector Delta=L.Position-Position;const float Falloff=FMath::Square(FMath::Clamp(1-float(Delta.Size())/L.Radius,0.f,1.f));Illumination+=L.Color*(L.Intensity/2500.f*Falloff*FMath::Max(0.f,float(FVector::DotProduct(N,Delta.GetSafeNormal()))));}
            const FLinearColor Base=O.Shape==TEXT("blossom")?FLinearColor::White:O.Color;
            return Mesh.Colors[I]*Base*Illumination;};
        for(int32 J=0;J<Mesh.Triangles.Num();J+=3){
            const int32 A=Mesh.Triangles[J],B=Mesh.Triangles[J+1],CIndex=Mesh.Triangles[J+2];
            const FVector PA=Transforms[K].TransformPosition(Mesh.Vertices[A]),PB=Transforms[K].TransformPosition(Mesh.Vertices[B]),PC=Transforms[K].TransformPosition(Mesh.Vertices[CIndex]);
            const double Weight=FVector::CrossProduct(PB-PA,PC-PA).Size()*.5;if(Weight<.00001)continue;
            Area+=Weight;Triangles.Add({FVector3f(PA),FVector3f(PB),FVector3f(PC),C(A),C(B),C(CIndex),Area});
        }
    }
    const int32 Count=FMath::Clamp(S.PointCount,256,Maximum);
    TArray<FLightBoxPoint> Points;Points.SetNumUninitialized(Count);
    const float Radius=FMath::Clamp(float(FMath::Sqrt(FMath::Max(Area,90000.)/Count))*.62f,.12f,4.f);
    FLightBoxStyle Fallback=S;Fallback.Formation=S.Formation==TEXT("volume")?TEXT("sphere"):S.Formation;
    for(int32 I=0;I<Count;++I){
        auto& P=Points[I];P.Radius=Radius;
        if(Triangles.IsEmpty()){
            P.Position=FVector3f(FLightBoxSimulation::Target(I,Count,Fallback,0));P.Color=S.ParticleColor(I,FVector(P.Position),0,0);continue;
        }
        const double Selected=Random(I,9)*Area;int32 Low=0,High=Triangles.Num()-1;
        while(Low<High){const int32 Mid=(Low+High)/2;if(Triangles[Mid].End<Selected)Low=Mid+1;else High=Mid;}
        const auto& T=Triangles[Low];const float U=FMath::Sqrt(Random(I,10)),V=Random(I,11),A=1-U,B=U*(1-V),C=U*V;
        P.Position=T.A*A+T.B*B+T.C*C;P.Color=T.CA*A+T.CB*B+T.CC*C;
    }
    return Points;
}
