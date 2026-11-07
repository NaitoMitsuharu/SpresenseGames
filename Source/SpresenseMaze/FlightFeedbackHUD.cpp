#include "FlightHUD.h"
#include "Engine/Canvas.h"
#include "FlightCourse.h"
#include "FlightPawn.h"

void AFlightHUD::DrawScore(AFlightCourse* Course)
{
	const float X=SafeLeft()+24*S,Y=44*S;
	CyberPanel(X-24*S,Y-14*S,340*S,Course->Score.Combo>=2?242*S:174*S,Mint);
	Label(TEXT("NEON BREAKER / SCORE"),X,Y,14,Muted,true);
	const float Kick=Course->RingPulse*3.f;
	Label(FText::AsNumber(FMath::RoundToInt(ShownScore)).ToString(),X,Y+23*S,48+Kick,Paper,true);
	if (Course->NoticeTime>0)
	{
		const float A=FMath::Min(Course->NoticeTime*3,1.f);
		Label(Course->ScoreNotice,X,Y+92*S+(1-A)*8*S,20,Mint.CopyWithNewOpacity(A));
	}
	if (Course->Score.Combo>=2)
	{
		Label(FString::Printf(TEXT("%d CHAIN"),Course->Score.Combo),X,Y+145*S,25,Paper,true);
		Label(FString::Printf(TEXT("×%.1f"),Course->Score.Multiplier()),X+215*S,Y+145*S,25,Coral,true);
		RoundRect(X,Y+198*S,284*S,4*S,2*S,Paper.CopyWithNewOpacity(0.12f));
		RoundRect(X,Y+198*S,284*S*Course->Score.ComboTime/6,4*S,2*S,Mint);
	}
	if (Course->IncomingTime>0)
	{
		const float Size=20,A=FMath::Min(Course->IncomingTime*2,1.f);
		const FVector2D T=Measure(Course->IncomingFormation,Size);
		const float CX=W*0.5f,YB=170*S;
		CyberPanel(CX-T.X*0.5f-23*S,YB,T.X+46*S,42*S,Coral,A);
		CenterLabel(Course->IncomingFormation,CX,YB+9*S,Size,Paper.CopyWithNewOpacity(A));
	}
}

void AFlightHUD::DrawThreats(AFlightPawn* Ship)
{
	int32 Drawn=0;
	for (const FFlightThreat& T:Ship->GetCourse()->Threats)
	{
		if (++Drawn>3) { break; }
		const FVector Enemy=Project(Ship->GetCourse()->Path.Frame(T.S).ToWorld(0,T.Off.X,T.Off.Y),false);
		const FVector Aim=Project(Ship->GetCourse()->Path.Frame(Ship->S+2100).ToWorld(0,T.Target.X,T.Target.Y),false);
		const FLinearColor Color=FMath::Lerp(Paper,Coral,T.Charge01);
		if (Enemy.Z>0 && Enemy.X>0 && Enemy.X<W && Enemy.Y>100*S && Enemy.Y<H)
		{
			const float R=(20-5*T.Charge01)*S,L=6*S;
			for (float Side:{-1.f,1.f})
			{
				DrawLine(Enemy.X+Side*R,Enemy.Y-L,Enemy.X+Side*R,Enemy.Y+L,Color,1.5f*S);
			}
		}
		if (Drawn==1 && Aim.Z>0 && Aim.X>40*S && Aim.X<W-40*S && Aim.Y>170*S && Aim.Y<H-130*S)
		{
			const float R=(27-7*T.Charge01)*S;
			const FVector2D P[]={{Aim.X,Aim.Y-R},{Aim.X+R,Aim.Y},{Aim.X,Aim.Y+R},{Aim.X-R,Aim.Y}};
			for (int32 I=0; I<4; ++I) { DrawLine(P[I].X,P[I].Y,P[(I+1)%4].X,P[(I+1)%4].Y,Coral.CopyWithNewOpacity(0.7f),1.5f*S); }
			CenterLabel(TEXT("回避"),Aim.X,Aim.Y+34*S,16,Coral,true);
		}
	}
}

void AFlightHUD::DrawDodge(AFlightPawn* Ship)
{
	const float Size=82*S,Gap=14*S,X=W-36*S-Size*2-Gap,Y=H-130*S;
	if (Ship->bSplitAim)
	{
		// Spresense mode: no buttons, the tilt-and-return gesture rolls. Show its stamina instead.
		const float BW=Size*2+Gap, BY=Y+Size-12*S, Fill=Ship->RollStamina.Value/FlightHandling::FRollStamina::Max;
		const bool CanRoll=Ship->RollStamina.CanSpend();
		const FLinearColor Bar=Ship->RollDenied>0?Coral:(CanRoll?Mint:Muted);
		CenterLabel(Ship->IsRolling()?TEXT("ロール中・シールド"):CanRoll?TEXT("傾けて戻すとロール"):TEXT("スタミナ回復中"),X+BW*0.5f,BY-34*S,16,Ship->IsRolling()?Mint:(CanRoll?Paper:Muted));
		RoundRect(X,BY,BW,8*S,4*S,Paper.CopyWithNewOpacity(0.16f));
		RoundRect(X,BY,BW*Fill,8*S,4*S,Bar);
		const float Mark=X+BW*FlightHandling::FRollStamina::Cost/FlightHandling::FRollStamina::Max;
		DrawLine(Mark,BY-3*S,Mark,BY+11*S,Paper.CopyWithNewOpacity(0.55f),1.5f*S);
		return;
	}
	const bool Ready=Ship->DodgeCooldown<=0 && Ship->State==EFlightState::Playing;
	CenterLabel(Ship->DodgeCooldown>0?TEXT("チャージ中"):TEXT("クイック回避"),X+Size+Gap*0.5f,Y-27*S,16,Ready?Paper:Muted);
	for (int32 I=0; I<2; ++I)
	{
		const float BX=X+I*(Size+Gap),CX=BX+Size*0.5f,CY=Y+Size*0.5f,Side=I?1.f:-1.f;
		const bool Active=Ship->DodgeTime>0 && Ship->DodgeDirection==int32(Side);
		const bool SideReady=Ship->CanDodge(int32(Side));
		RoundRect(BX,Y,Size,Size,Size*0.5f,Active?Mint:Ink.CopyWithNewOpacity(SideReady?0.64f:0.35f));
		const FLinearColor Symbol=Active?Ink:(SideReady?Paper:Muted.CopyWithNewOpacity(0.55f));
		for (int32 K=0; K<2; ++K)
		{
			const float PX=CX+(K*10-7)*S;
			DrawLine(PX-Side*6*S,CY-9*S,PX+Side*2*S,CY,Symbol,2*S);
			DrawLine(PX+Side*2*S,CY,PX-Side*6*S,CY+9*S,Symbol,2*S);
		}
		if (!Ready && Ship->DodgeCooldown>0)
		{
			Arc(CX,CY,Size*0.5f-3*S,0,360*(1-Ship->DodgeCooldown/AFlightPawn::DodgeRecharge),Mint.CopyWithNewOpacity(0.65f),2*S);
		}
		if (Ship->State==EFlightState::Playing) { AddHitBox(FVector2D(BX,Y),FVector2D(Size,Size),I?TEXT("DodgeRight"):TEXT("DodgeLeft"),true); }
	}
}
