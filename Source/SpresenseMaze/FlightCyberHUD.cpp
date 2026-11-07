#include "FlightHUD.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "FlightCourse.h"
#include "FlightPawn.h"

void AFlightHUD::CyberPanel(float X, float Y, float Width, float Height, const FLinearColor& Color, float Alpha)
{
	if (!Canvas || !Canvas->DefaultTexture) { return; }
	const float Cut=FMath::Min(16*S,Height*0.22f);
	const FVector2D P[]={{X,Y},{X+Width-Cut,Y},{X+Width,Y+Cut},{X+Width,Y+Height},{X+Cut,Y+Height},{X,Y+Height-Cut}};
	TArray<FCanvasUVTri> Triangles;
	for (int32 I=0; I<6; ++I)
	{
		FCanvasUVTri T;
		T.V0_Pos=FVector2D(X+Width*0.5f,Y+Height*0.5f); T.V1_Pos=P[I]; T.V2_Pos=P[(I+1)%6];
		T.V0_UV=T.V1_UV=T.V2_UV=FVector2D(0.5,0.5);
		T.V0_Color=T.V1_Color=T.V2_Color=Ink.CopyWithNewOpacity(0.92f*Alpha);
		Triangles.Add(T);
	}
	FCanvasTriangleItem Item(Triangles,Canvas->DefaultTexture->GetResource()); Item.BlendMode=SE_BLEND_Translucent; Canvas->DrawItem(Item);
	for (int32 I=0; I<6; ++I) { DrawLine(P[I].X,P[I].Y,P[(I+1)%6].X,P[(I+1)%6].Y,Color.CopyWithNewOpacity(0.38f*Alpha),S); }
	DrawLine(X,Y,X+FMath::Min(Width*0.30f,110*S),Y,Color.CopyWithNewOpacity(Alpha),3*S);
	DrawLine(X+Width,Y+Height,X+Width-32*S,Y+Height,Color.CopyWithNewOpacity(Alpha),3*S);
}

void AFlightHUD::DrawCyberMotion(AFlightPawn* Ship, AFlightCourse* Course)
{
	const float Chain=FMath::Clamp(Course->Score.Combo/20.f,0.f,1.f);
	const float Dodge=Ship->DodgePulse();
	// Bounded screen-space streaks: no extra particles or full-screen post passes.
	// Keep the flight corridor, reticle and enemy bullets clear.
	if (Ship->State==EFlightState::Playing)
	{
		for (int32 I=0; I<22; ++I)
		{
			const float Phase=FMath::Frac(UiTime*(0.45f+Chain*0.3f)+I*0.618034f);
			const float Side=I%2?1.f:-1.f;
			const float X=W*0.5f+Side*W*(0.34f+0.21f*Phase);
			const float Y=H*(0.22f+FMath::Frac(I*0.3819f)*0.59f);
			const FVector2D Dir=FVector2D(X-W*0.5f,(Y-H*0.43f)*1.6f).GetSafeNormal();
			const float Len=(20+Phase*65)*(1+Chain+Dodge)*S;
			const float A=FMath::Sin(Phase*PI)*(0.10f+Chain*0.13f+Dodge*0.35f);
			DrawLine(X,Y,X+Dir.X*Len,Y+Dir.Y*Len,Mint.CopyWithNewOpacity(A),1.4f*S);
		}
	}
	// Narrow animated edge rails communicate energy without covering the view.
	for (int32 Side=0; Side<2; ++Side)
	{
		const float X=Side?W-14*S:14*S;
		DrawLine(X,200*S,X,H-200*S,Mint.CopyWithNewOpacity(0.12f),S);
		const float Y=FMath::Lerp(210*S,H-270*S,FMath::Frac(UiTime*0.24f+Side*0.5f));
		DrawLine(X,Y,X,Y+58*S,(Side?Coral:Mint).CopyWithNewOpacity(0.65f),3*S);
	}
	if (Course->RingPulse>0.01f)
	{
		const float P=1-Course->RingPulse;
		const float R=(125+P*220)*S;
		for (int32 I=0; I<10; ++I)
		{
			const float A=I*36.f+P*55;
			Arc(W*0.5f,H*0.43f,R,A,A+12,Mint.CopyWithNewOpacity(Course->RingPulse*0.65f),2*S);
		}
		const float Y=H*0.29f-24*S*P;
		CenterLabel(TEXT("RING CHAIN"),W*0.5f,Y,16,Mint.CopyWithNewOpacity(Course->RingPulse),true);
	}
	if (Dodge>0.05f)
	{
		const float Angle=UiTime*240;
		Arc(W*0.5f,H*0.62f,138*S,Angle,Angle+115,Mint.CopyWithNewOpacity(Dodge*0.8f),3*S);
		Arc(W*0.5f,H*0.62f,150*S,Angle+180,Angle+250,Coral.CopyWithNewOpacity(Dodge*0.6f),2*S);
		CenterLabel(TEXT("ROLL / SHIELD ACTIVE"),W*0.5f,H-202*S,19,Mint.CopyWithNewOpacity(Dodge),true);
	}
}
