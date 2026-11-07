#include "LightBoxSdf.h"
#include "Misc/ScopeLock.h"
#include "Async/ParallelFor.h"

namespace
{
    float SmoothMin(float A,float B,float K)
    {
        if(K<=0)return FMath::Min(A,B);
        const float H=FMath::Max(K-FMath::Abs(A-B),0.f)/K;return FMath::Min(A,B)-H*H*K*.25f;
    }
    FVector Into(const FLightBoxSdfPart& S,const FVector& P){return FQuat::MakeFromEuler(S.Rotation).UnrotateVector(P-S.A);}
    // Distance functions after Inigo Quilez (iquilezles.org/articles/distfunctions).
    float Primitive(const FLightBoxSdfPart& S,const FVector& P)
    {
        if(S.Type==TEXT("capsule")){
            const FVector BA=S.B-S.A,PA=P-S.A;const double L2=BA.SizeSquared(),R1=S.R.X,R2=S.R.Y;
            if(L2<1e-6)return float(PA.Size()-FMath::Max(R1,R2));
            const double RR=R1-R2,A2=L2-RR*RR,IL2=1/L2,Y=FVector::DotProduct(PA,BA),Z=Y-L2;
            const double X2=(PA*L2-BA*Y).SizeSquared(),Y2=Y*Y*L2,Z2=Z*Z*L2,K=FMath::Sign(RR)*RR*RR*X2;
            if(FMath::Sign(Z)*A2*Z2>K)return float(FMath::Sqrt(X2+Z2)*IL2-R2);
            if(FMath::Sign(Y)*A2*Y2<K)return float(FMath::Sqrt(X2+Y2)*IL2-R1);
            return float((FMath::Sqrt(X2*FMath::Max(A2,0.)*IL2)+Y*RR)*IL2-R1);
        }
        const FVector Q=Into(S,P);
        if(S.Type==TEXT("box")){
            const FVector D=Q.GetAbs()-(S.R-FVector(S.Round)).ComponentMax(FVector(.01));
            return float(D.ComponentMax(FVector::ZeroVector).Size()+FMath::Min(D.GetMax(),0.)-S.Round);
        }
        if(S.Type==TEXT("torus"))return float(FVector2D(FVector2D(Q.X,Q.Y).Size()-S.R.X,Q.Z).Size()-S.R.Y);
        // Ellipsoid (bound, exact on the surface).
        const FVector R=S.R.ComponentMax(FVector(.01));const double K0=(Q/R).Size(),K1=(Q/(R*R)).Size();
        return K1<1e-9?-float(R.GetMin()):float(K0*(K0-1)/K1);
    }
    FBox PartBox(const FLightBoxSdfPart& S)
    {
        if(S.Type==TEXT("capsule")){FBox B(ForceInit);B+=S.A;B+=S.B;return B.ExpandBy(FMath::Max(S.R.X,S.R.Y));}
        const double Reach=S.Type==TEXT("torus")?S.R.X+S.R.Y:S.Type==TEXT("box")?S.R.Size():S.R.GetMax();
        return FBox(S.A-FVector(Reach),S.A+FVector(Reach));
    }
    FString Key(const TArray<FLightBoxSdfPart>& Parts,const FLinearColor& Base)
    {
        FString K=Base.ToString();
        for(const auto& S:Parts)K+=FString::Printf(TEXT("|%s%s%s%s%s%.2f,%.2f,%d%d%d%s"),*S.Type,*S.A.ToString(),*S.B.ToString(),*S.R.ToString(),*S.Rotation.ToString(),S.Blend,S.Round,S.bCut,S.bSmooth,S.bHasColor,*S.Color.ToString());
        return K;
    }
    FCriticalSection CacheLock;
    TMap<FString,TSharedRef<const FLightBoxSdfMesh,ESPMode::ThreadSafe>> Cache;
}

FBox LightBoxSdf::Bounds(const TArray<FLightBoxSdfPart>& Parts)
{
    FBox Box(ForceInit);float Blend=0;
    for(const auto& S:Parts)if(!S.bCut){Box+=PartBox(S);Blend=FMath::Max(Blend,S.Blend);}
    return Box.IsValid?Box.ExpandBy(2+Blend*.5f):Box;
}

float LightBoxSdf::PartDistance(const FLightBoxSdfPart& Part,const FVector& Point){return Primitive(Part,Point);}

float LightBoxSdf::Distance(const TArray<FLightBoxSdfPart>& Parts,const FVector& P,const FLinearColor& Base,FLinearColor* Color,bool* bSmooth)
{
    float D=TNumericLimits<float>::Max();bool bAny=false;
    float Nearest=TNumericLimits<float>::Max();
    TArray<float,TInlineAllocator<LightBoxSdf::MaxParts>> Own;
    for(const auto& S:Parts){
        const float Di=Primitive(S,P);Own.Add(Di);
        if(S.bCut){if(bAny)D=-SmoothMin(-D,Di,S.Blend);continue;}
        D=bAny?SmoothMin(D,Di,S.Blend):Di;bAny=true;Nearest=FMath::Min(Nearest,Di);
    }
    if(Color||bSmooth){
        // Colour follows the nearest parts, softly mixed across each blend seam.
        FLinearColor Sum=FLinearColor::Transparent;float Weight=0,BestSmooth=TNumericLimits<float>::Max();bool Smooth=false;
        for(int32 I=0;I<Parts.Num();++I){const auto& S=Parts[I];if(S.bCut)continue;
            const float W=FMath::Exp(-(Own[I]-Nearest)/FMath::Max(.6f,S.Blend*.12f));
            Sum+=(S.bHasColor?S.Color:Base)*W;Weight+=W;
            if(Own[I]<BestSmooth){BestSmooth=Own[I];Smooth=S.bSmooth;}}
        if(Color)*Color=Weight>0?Sum/Weight:Base;
        if(bSmooth)*bSmooth=Smooth;
    }
    return bAny?D:TNumericLimits<float>::Max();
}

TSharedRef<const FLightBoxSdfMesh,ESPMode::ThreadSafe> LightBoxSdf::Mesh(const TArray<FLightBoxSdfPart>& Parts,const FLinearColor& Base)
{
    const FString K=Key(Parts,Base);
    {FScopeLock Lock(&CacheLock);if(const auto* Found=Cache.Find(K))return *Found;}
    auto Out=MakeShared<FLightBoxSdfMesh,ESPMode::ThreadSafe>();
    const FBox Box=Bounds(Parts);
    if(Box.IsValid){
        // Naive surface nets: one vertex per sign-changing cell, projected onto the surface.
        double H=FMath::Max(.4,Box.GetSize().GetMax()/88);
        for(int32 Attempt=0;Attempt<6;++Attempt,H*=1.3){
            Out->Vertices.Reset();Out->Normals.Reset();Out->Colors.Reset();Out->Triangles.Reset();
            const FIntVector N(FMath::Max(2,FMath::CeilToInt(Box.GetSize().X/H)),FMath::Max(2,FMath::CeilToInt(Box.GetSize().Y/H)),FMath::Max(2,FMath::CeilToInt(Box.GetSize().Z/H)));
            const FVector O=Box.Min;auto Corner=[&](int32 X,int32 Y,int32 Z){return O+FVector(X,Y,Z)*H;};
            const int32 SX=N.X+1,SY=N.Y+1;TArray<float> Field;Field.SetNumUninitialized(SX*SY*(N.Z+1));
            ParallelFor(N.Z+1,[&](int32 Z){for(int32 Y=0;Y<SY;++Y)for(int32 X=0;X<SX;++X)Field[(Z*SY+Y)*SX+X]=Distance(Parts,Corner(X,Y,Z),Base);});
            auto F=[&](int32 X,int32 Y,int32 Z){return Field[(Z*SY+Y)*SX+X];};
            TArray<int32> Cell;Cell.Init(-1,N.X*N.Y*N.Z);auto CellIndex=[&](int32 X,int32 Y,int32 Z){return (Z*N.Y+Y)*N.X+X;};
            static const int32 Edges[12][2]={{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
            bool bTooMany=false;
            for(int32 Z=0;Z<N.Z&&!bTooMany;++Z)for(int32 Y=0;Y<N.Y&&!bTooMany;++Y)for(int32 X=0;X<N.X&&!bTooMany;++X){
                float V[8];int32 Inside=0;
                for(int32 C=0;C<8;++C){V[C]=F(X+(C&1),Y+((C>>1)&1),Z+((C>>2)&1));Inside+=V[C]<0;}
                if(Inside==0||Inside==8)continue;
                FVector Sum=FVector::ZeroVector;int32 Count=0;
                for(const auto& E:Edges){const float A=V[E[0]],B=V[E[1]];if((A<0)==(B<0))continue;
                    const float T=A/(A-B);const FVector PA=Corner(X+(E[0]&1),Y+((E[0]>>1)&1),Z+((E[0]>>2)&1)),PB=Corner(X+(E[1]&1),Y+((E[1]>>1)&1),Z+((E[1]>>2)&1));
                    Sum+=FMath::Lerp(PA,PB,double(T));++Count;}
                Cell[CellIndex(X,Y,Z)]=Out->Vertices.Add(Sum/Count);
                bTooMany=Out->Vertices.Num()>MaxVertices;
            }
            if(bTooMany)continue;
            auto Quad=[&](int32 A,int32 B,int32 C,int32 D,bool Flip){if(A<0||B<0||C<0||D<0)return;
                if(Flip)Out->Triangles.Append({A,C,B,A,D,C});else Out->Triangles.Append({A,B,C,A,C,D});};
            for(int32 Z=1;Z<N.Z;++Z)for(int32 Y=1;Y<N.Y;++Y)for(int32 X=0;X<N.X;++X){const float A=F(X,Y,Z),B=F(X+1,Y,Z);if((A<0)!=(B<0))
                Quad(Cell[CellIndex(X,Y-1,Z-1)],Cell[CellIndex(X,Y,Z-1)],Cell[CellIndex(X,Y,Z)],Cell[CellIndex(X,Y-1,Z)],A<0);}
            for(int32 Z=1;Z<N.Z;++Z)for(int32 Y=0;Y<N.Y;++Y)for(int32 X=1;X<N.X;++X){const float A=F(X,Y,Z),B=F(X,Y+1,Z);if((A<0)!=(B<0))
                Quad(Cell[CellIndex(X-1,Y,Z-1)],Cell[CellIndex(X-1,Y,Z)],Cell[CellIndex(X,Y,Z)],Cell[CellIndex(X,Y,Z-1)],A<0);}
            for(int32 Z=0;Z<N.Z;++Z)for(int32 Y=1;Y<N.Y;++Y)for(int32 X=1;X<N.X;++X){const float A=F(X,Y,Z),B=F(X,Y,Z+1);if((A<0)!=(B<0))
                Quad(Cell[CellIndex(X-1,Y-1,Z)],Cell[CellIndex(X,Y-1,Z)],Cell[CellIndex(X,Y,Z)],Cell[CellIndex(X-1,Y,Z)],A<0);}
            // Project onto the true surface, then take the gradient as the normal and the local part colour.
            Out->Normals.SetNum(Out->Vertices.Num());Out->Colors.SetNum(Out->Vertices.Num());
            const double Eps=H*.25;
            ParallelFor(Out->Vertices.Num(),[&](int32 I){
                FVector P=Out->Vertices[I],G=FVector::UpVector;
                for(int32 Step=0;Step<3;++Step){
                    const float D=Distance(Parts,P,Base);
                    G=FVector(Distance(Parts,P+FVector(Eps,0,0),Base)-Distance(Parts,P-FVector(Eps,0,0),Base),Distance(Parts,P+FVector(0,Eps,0),Base)-Distance(Parts,P-FVector(0,Eps,0),Base),Distance(Parts,P+FVector(0,0,Eps),Base)-Distance(Parts,P-FVector(0,0,Eps),Base))/(2*Eps);
                    if(G.SizeSquared()<1e-8)break;const FVector Move=G*(D/G.SizeSquared());P-=Move.GetClampedToMaxSize(H);
                }
                FLinearColor C;bool bSmooth=false;Distance(Parts,P,Base,&C,&bSmooth);C.A=bSmooth?.25f:1.f;
                Out->Vertices[I]=P;Out->Normals[I]=G.GetSafeNormal(1e-8,FVector::UpVector);Out->Colors[I]=C;});
            break;
        }
    }
    TSharedRef<const FLightBoxSdfMesh,ESPMode::ThreadSafe> Result=Out;
    FScopeLock Lock(&CacheLock);if(Cache.Num()>24)Cache.Reset();Cache.Add(K,Result);return Result;
}
