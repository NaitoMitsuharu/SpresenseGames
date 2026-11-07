#include "LightBoxPerformance.h"

namespace LightBoxPerformance
{
    // A cold phone measures faster than it runs after minutes of display, so keep 12% headroom, and allow
    // occasional slow frames (60 Hz v-sync quantises frames to 16.7/33.3/50 ms).
    bool Holds(const FSample& S,float TargetFps){return S.Fps>=TargetFps*1.12f&&S.P95Ms<=1000.f/TargetFps*1.6f;}

    int32 Choose(const TArray<FSample>& Samples)
    {
        int32 Best45=0,Best30=0;
        for(const auto& S:Samples){if(Holds(S,45))Best45=FMath::Max(Best45,S.Count);if(Holds(S,30))Best30=FMath::Max(Best30,S.Count);}
        if(Best45>=DensityFloor)return Best45;
        if(Best30>0)return Best30;
        return Levels()[0];
    }

    FSample Measure(TArray<float> Ms,int32 Count)
    {
        FSample S;S.Count=Count;if(Ms.IsEmpty())return S;
        float Sum=0;for(float M:Ms)Sum+=M;S.Fps=Ms.Num()*1000.f/FMath::Max(1.f,Sum);
        Ms.Sort();S.P95Ms=Ms[FMath::Clamp(FMath::FloorToInt(Ms.Num()*.95f),0,Ms.Num()-1)];return S;
    }

    FLightBoxStyle Scene(int32 Count)
    {
        FLightBoxStyle S;S.Title=TEXT("性能の確認");S.Count=0;S.PointCount=Count;S.AssembleSeconds=1;S.bShowBox=false;
        S.Background=FLinearColor(.002f,.005f,.012f);S.Note=TEXT("端末の性能を確認しています（初回のみ）");
        FLightBoxObject O;O.Id=TEXT("calibration");O.Shape=TEXT("sdf");O.Position=FVector(0,0,120);O.Size=FVector(170,90,130);O.Detail=TEXT("fur");O.DetailLength=4;
        auto Part=[&](const TCHAR* Type,FVector A,FVector R,float K,FLinearColor C,FVector B=FVector::ZeroVector){FLightBoxSdfPart P;P.Type=Type;P.A=A;P.B=B;P.R=R;P.Blend=K;P.bHasColor=true;P.Color=C;O.SdfParts.Add(P);};
        const FLinearColor White(.9f,.88f,.84f),Black(.02f,.02f,.02f);
        Part(TEXT("ellipsoid"),FVector(0,0,0),FVector(60,38,38),12,White);
        Part(TEXT("ellipsoid"),FVector(58,0,20),FVector(28,26,25),10,White);
        Part(TEXT("ellipsoid"),FVector(25,0,10),FVector(18,40,30),8,Black);
        for(float X:{-35.f,35.f})for(float Y:{-24.f,24.f})Part(TEXT("capsule"),FVector(X,Y,-15),FVector(14,10,0),8,Black,FVector(X+4,Y,-62));
        for(float Y:{-15.f,15.f})Part(TEXT("ellipsoid"),FVector(62,Y,44),FVector(8,5,8),4,Black);
        // Slow turn: animated targets cost the same per-frame work as typical AI scenes.
        FLightBoxMotion Turn;Turn.Type=TEXT("rotate");Turn.Axis=FVector(0,0,1);Turn.Amplitude=360;Turn.Period=20;Turn.Wave=TEXT("saw");O.Motions.Add(Turn);
        S.Objects={O};return S;
    }
}
