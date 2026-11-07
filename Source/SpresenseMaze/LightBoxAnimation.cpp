#include "LightBoxAnimation.h"

TArray<FLightBoxStyle> LightBoxAnimation::Prepare(const FLightBoxStyle& S)
{
    TArray<FLightBoxStyle> Poses;
    if(S.Story.IsEmpty())return Poses;
    for(int32 I=0;I<=S.Story.Num();++I){FLightBoxStyle P;if(!S.Pose(I,P))return {};Poses.Add(MoveTemp(P));}
    return Poses;
}

LightBoxAnimation::FInterval LightBoxAnimation::Interval(const FLightBoxStyle& S,float Time)
{
    FInterval R;if(S.Story.IsEmpty())return R;
    float Duration=S.StoryHold;for(const auto& F:S.Story)Duration+=F.Move+F.Hold;
    if(S.bStoryLoop)Duration+=S.Story[0].Move;
    Time=FMath::Max(0.f,Time);
    if(S.bStoryLoop)Time=FMath::Fmod(Time,FMath::Max(.01f,Duration));
    // Legacy "hold" contributes to a slower gesture instead of freezing a slide.
    for(int32 I=0;I<S.Story.Num();++I){const float D=FMath::Max(.01f,S.Story[I].Move+S.Story[I].Hold+(I==0?S.StoryHold:0));
        if(Time<D){R.From=I;R.To=I+1;R.Mix=FMath::SmoothStep(0.f,1.f,Time/D);return R;}Time-=D;}
    R.From=S.Story.Num();R.To=S.bStoryLoop?0:R.From;
    R.Mix=S.bStoryLoop?FMath::SmoothStep(0.f,1.f,Time/FMath::Max(.01f,S.Story[0].Move)):1;
    return R;
}

FLightBoxStyle LightBoxAnimation::Sample(const TArray<FLightBoxStyle>& Poses,const FInterval& F)
{
    if(!Poses.IsValidIndex(F.From)||!Poses.IsValidIndex(F.To))return FLightBoxStyle();
    auto R=Poses[F.From];const auto& B=Poses[F.To];const float T=F.Mix;
    for(int32 I=0;I<R.Objects.Num()&&I<B.Objects.Num();++I){auto& X=R.Objects[I];const auto& Y=B.Objects[I];
        X.Position=FMath::Lerp(X.Position,Y.Position,T);X.Size=FMath::Lerp(X.Size,Y.Size,T);
        const FQuat A0=FRotator(X.Rotation.Y,X.Rotation.Z,X.Rotation.X).Quaternion(),B0=FRotator(Y.Rotation.Y,Y.Rotation.Z,Y.Rotation.X).Quaternion();
        const auto Rot=FQuat::Slerp(A0,B0,T).Rotator();X.Rotation=FVector(Rot.Roll,Rot.Pitch,Rot.Yaw);
        X.Color=FMath::Lerp(X.Color,Y.Color,T);X.Pivot=FMath::Lerp(X.Pivot,Y.Pivot,T);X.Radius=FMath::Lerp(X.Radius,Y.Radius,T);
        X.Amplitude=FMath::Lerp(X.Amplitude,Y.Amplitude,T);X.Speed=FMath::Lerp(X.Speed,Y.Speed,T);
        for(int32 J=0;J<X.Curve.Num()&&J<Y.Curve.Num();++J)X.Curve[J]=FMath::Lerp(X.Curve[J],Y.Curve[J],T);
        for(int32 J=0;J<X.SdfParts.Num()&&J<Y.SdfParts.Num();++J){auto& A=X.SdfParts[J];const auto& Z=Y.SdfParts[J];
            A.A=FMath::Lerp(A.A,Z.A,T);A.B=FMath::Lerp(A.B,Z.B,T);A.R=FMath::Lerp(A.R,Z.R,T);
            A.Rotation=FQuat::Slerp(FQuat::MakeFromEuler(A.Rotation),FQuat::MakeFromEuler(Z.Rotation),T).Euler();
            A.Color=FMath::Lerp(A.Color,Z.Color,T);A.Blend=FMath::Lerp(A.Blend,Z.Blend,T);}
    }
    return R;
}

FLightBoxSkinBinding LightBoxAnimation::Bind(const FLightBoxObject& O,const FVector& P)
{
    FLightBoxSkinBinding B;float Score[4]={MAX_flt,MAX_flt,MAX_flt,MAX_flt};
    for(int32 I=0;I<O.SdfParts.Num();++I){const float D=FMath::Abs(LightBoxSdf::PartDistance(O.SdfParts[I],P));
        for(int32 J=0;J<4;++J)if(D<Score[J]){for(int32 K=3;K>J;--K){Score[K]=Score[K-1];B.Part[K]=B.Part[K-1];}Score[J]=D;B.Part[J]=I;break;}}
    float Sum=0;for(int32 J=0;J<4;++J)if(B.Part[J]!=INDEX_NONE){B.Weight[J]=FMath::Exp(-(Score[J]-Score[0])/FMath::Max(1.f,O.SdfParts[B.Part[J]].Blend*.3f));Sum+=B.Weight[J];}
    for(float& W:B.Weight)W/=FMath::Max(.0001f,Sum);return B;
}

TArray<LightBoxAnimation::FSkinTransform> LightBoxAnimation::Palette(const FLightBoxObject& Rest,const FLightBoxObject& Pose)
{
    TArray<FSkinTransform> R;
    for(int32 I=0;I<FMath::Min(Rest.SdfParts.Num(),Pose.SdfParts.Num());++I){const auto& A=Rest.SdfParts[I];const auto& Z=Pose.SdfParts[I];FSkinTransform T;
        T.A=A.A;T.Axis=A.B-A.A;T.Next=Z.B-Z.A;T.Offset=Z.A;T.bCapsule=A.Type==TEXT("capsule")&&Z.Type==A.Type;
        if(T.bCapsule){T.InvLength=1/FMath::Max(.0001,T.Axis.SizeSquared());T.RestRadius=FVector2D(A.R.X,A.R.Y);T.NextRadius=FVector2D(Z.R.X,Z.R.Y);
            T.Rotation=T.Axis.IsNearlyZero()||T.Next.IsNearlyZero()?FQuat::Identity:FQuat::FindBetweenNormals(T.Axis.GetSafeNormal(),T.Next.GetSafeNormal());}
        else{FVector Scale=Z.R/A.R.ComponentMax(FVector(.01));if(A.Type==TEXT("torus"))Scale=FVector(Scale.X,Scale.X,Scale.Y);
            const auto Q0=FQuat::MakeFromEuler(A.Rotation),Q1=FQuat::MakeFromEuler(Z.Rotation);
            auto Map=[&](const FVector& V){return Q1.RotateVector(Q0.UnrotateVector(V)*Scale);};
            T.X=Map(FVector(1,0,0));T.Y=Map(FVector(0,1,0));T.Z=Map(FVector(0,0,1));T.Offset-=Map(A.A);}
        R.Add(T);}
    return R;
}

FVector LightBoxAnimation::Skin(const FVector& P,const FLightBoxSkinBinding& B,const TArray<FSkinTransform>& Palette)
{
    FVector Out=FVector::ZeroVector;float Sum=0;
    for(int32 J=0;J<4;++J){const int32 I=B.Part[J];if(!Palette.IsValidIndex(I)||B.Weight[J]==0)continue;const auto& T=Palette[I];FVector Q;
        if(T.bCapsule){const double U=FMath::Clamp(FVector::DotProduct(P-T.A,T.Axis)*T.InvLength,0.,1.);
            const double Scale=FMath::Lerp(T.NextRadius.X,T.NextRadius.Y,U)/FMath::Max(.01,FMath::Lerp(T.RestRadius.X,T.RestRadius.Y,U));
            Q=T.Offset+T.Next*U+T.Rotation.RotateVector(P-T.A-T.Axis*U)*Scale;}
        else Q=T.Offset+T.X*P.X+T.Y*P.Y+T.Z*P.Z;
        Out+=Q*B.Weight[J];Sum+=B.Weight[J];}
    return Sum>.0001f?Out/Sum:P;
}

FVector LightBoxAnimation::Skin(const FLightBoxObject& Rest,const FLightBoxObject& Pose,const FVector& P,const FLightBoxSkinBinding& B)
{
    return Skin(P,B,Palette(Rest,Pose));
}

TArray<FVector> LightBoxAnimation::Remap(const TArray<FVector>& Rest,const TArray<FVector>& Target)
{
    // Rare topology-changing legacy poses: cache spatial correspondence once, never per animation tick.
    if(Rest.Num()==Target.Num())return Target;
    if(Target.IsEmpty())return Rest;
    FBox A(Rest),B(Target);const FVector AS=A.GetSize().ComponentMax(FVector(.01)),BS=B.GetSize().ComponentMax(FVector(.01));
    TMap<FIntVector,TArray<int32>> Cells;
    auto Cell=[](FVector V){return FIntVector(FMath::FloorToInt(V.X*12),FMath::FloorToInt(V.Y*12),FMath::FloorToInt(V.Z*12));};
    for(int32 I=0;I<Target.Num();++I)Cells.FindOrAdd(Cell((Target[I]-B.Min)/BS)).Add(I);
    TArray<FVector> R;R.Reserve(Rest.Num());
    for(const auto& P:Rest){const FVector U=(P-A.Min)/AS;const auto C=Cell(U);int32 Best=INDEX_NONE;double Distance=MAX_dbl;
        for(int32 Ring=0;Ring<=12&&Best==INDEX_NONE;++Ring)for(int32 X=-Ring;X<=Ring;++X)for(int32 Y=-Ring;Y<=Ring;++Y)for(int32 Z=-Ring;Z<=Ring;++Z){if(FMath::Max3(FMath::Abs(X),FMath::Abs(Y),FMath::Abs(Z))!=Ring)continue;
            if(const auto* At=Cells.Find(C+FIntVector(X,Y,Z)))for(int32 I:*At){const double D=FVector::DistSquared(U,(Target[I]-B.Min)/BS);if(D<Distance){Distance=D;Best=I;}}}
        R.Add(Target[Best==INDEX_NONE?0:Best]);}
    return R;
}
