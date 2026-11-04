#include "FlightHUD.h"
#include "AttitudeInputSubsystem.h"
#include "Engine/Canvas.h"
#include "FlightCourse.h"
#include "FlightPawn.h"

void AFlightHUD::DrawHUD()
{
	Super::DrawHUD();
	AFlightPawn* Ship = Cast<AFlightPawn>(GetOwningPawn());
	if (!Canvas || !Ship || !Ship->GetCourse()) { return; }
	AFlightCourse* Course = Ship->GetCourse();
	const float Dt = FMath::Min(GetWorld()->GetDeltaSeconds(), 0.05f);
	UiTime += Dt;
	if (!bUiInitialised) { ShownShield = Ship->Shield; ShownScore = Course->Score.Total(); bUiInitialised = true; }
	ShownShield = FMath::FInterpTo(ShownShield, Ship->Shield, Dt, 4.f);
	ShownScore = FMath::FInterpTo(ShownScore, float(Course->Score.Total()), Dt, 15.f);
	FadeEdges();
	const bool Live = Ship->State == EFlightState::Playing || Ship->State == EFlightState::Countdown;
	if (Live)
	{
		DrawDamageEdges(Ship); DrawScore(Course); DrawRings(Course); DrawShield(Ship);
		DrawAttitude(); DrawBoss(Course); DrawBanner(Ship); DrawReticles(Ship); DrawDodge(Ship);
		if (Ship->State == EFlightState::Playing) { DrawThreats(Ship); }
		const float X = W-104*S, Y = 32*S, Size = 68*S;
		RoundRect(X,Y,Size,Size,Size*0.5f,Ink.CopyWithNewOpacity(0.55f));
		RoundRect(X+25*S,Y+23*S,5*S,22*S,2*S,Paper);
		RoundRect(X+38*S,Y+23*S,5*S,22*S,2*S,Paper);
		AddHitBox(FVector2D(X,Y),FVector2D(Size,Size),TEXT("Pause"),true);
	}
	switch (Ship->State)
	{
	case EFlightState::AwaitNeutral: DrawAwaitNeutral(Ship); break;
	case EFlightState::Countdown: DrawCountdown(Ship); break;
	case EFlightState::Paused: DrawPaused(); break;
	case EFlightState::Destroyed: DrawDestroyed(); break;
	case EFlightState::Cleared: DrawCleared(Course); break;
	default: break;
	}
}

void AFlightHUD::NotifyHitBoxClick(FName Id)
{
	Super::NotifyHitBoxClick(Id);
	AFlightPawn* Ship = Cast<AFlightPawn>(GetOwningPawn());
	if (!Ship) { return; }
	if (Id == TEXT("Reset")) { Ship->RequestNeutralReset(); }
	else if (Id == TEXT("Pause") || Id == TEXT("Resume")) { Ship->TogglePause(); }
	else if (Id == TEXT("Top")) { Ship->ReturnToTop(); }
	else if (Id == TEXT("Retry")) { Ship->RestartCourse(); }
	else if (Id == TEXT("DodgeLeft")) { Ship->RequestDodge(-1); }
	else if (Id == TEXT("DodgeRight")) { Ship->RequestDodge(1); }
	else if (Id == TEXT("Gesture")) { Ship->ToggleGestureDodge(); }
}

void AFlightHUD::Dim(float Alpha)
{
	DrawRect(FLinearColor(0.012f,0.022f,0.035f,Alpha),0,0,W,H);
}

void AFlightHUD::DrawShield(AFlightPawn* Ship)
{
	const float X = SafeLeft(), Y = H-126*S, BW = 294*S;
	const float Health = FMath::Clamp(Ship->Shield / AFlightPawn::MaxShield,0.f,1.f);
	const FLinearColor Color = Health <= 0.25f ? Coral : Mint;
	// A small shield outline; a continuous fill with delayed damage makes loss legible.
	const FVector2D P[] = { {X,Y+2*S},{X+12*S,Y-3*S},{X+24*S,Y+2*S},{X+22*S,Y+17*S},{X+12*S,Y+26*S},{X+2*S,Y+17*S} };
	for (int32 I=0; I<6; ++I) { DrawLine(P[I].X,P[I].Y,P[(I+1)%6].X,P[(I+1)%6].Y,Color,1.8f*S); }
	Label(Health <= 0.25f ? TEXT("シールド低下") : TEXT("シールド"),X+38*S,Y,17,Muted);
	const FString Value = FString::FromInt(FMath::CeilToInt(Ship->Shield));
	Label(Value,X+BW-Measure(Value,25,true).X,Y-5*S,25,Paper,true);
	RoundRect(X,Y+42*S,BW,7*S,3.5f*S,Paper.CopyWithNewOpacity(0.16f));
	RoundRect(X,Y+42*S,BW*ShownShield/AFlightPawn::MaxShield,7*S,3.5f*S,Color.CopyWithNewOpacity(0.32f));
	RoundRect(X,Y+42*S,BW*Health,7*S,3.5f*S,FMath::Lerp(Color,Paper,Ship->HealFlash*0.7f));
}

void AFlightHUD::DrawRings(AFlightCourse* Course)
{
	const float X = W-320*S, Y = 36*S;
	// Airy, split-ring symbol beside an unpadded count.
	Arc(X+18*S,Y+28*S,13*S,12,78,Mint,2*S);
	Arc(X+18*S,Y+28*S,13*S,102,168,Mint,2*S);
	Arc(X+18*S,Y+28*S,13*S,192,258,Mint,2*S);
	Arc(X+18*S,Y+28*S,13*S,282,348,Mint,2*S);
	Label(TEXT("リング"),X+48*S,Y,16,Muted);
	const FString Count = FString::FromInt(Course->RingsPassed);
	const float Size = 33.f + Course->RingPulse * 2.f;
	Label(Count,X+48*S,Y+23*S,Size,Paper,true);
	Label(FString::Printf(TEXT("/ %d"),Course->RingsTotal),X+48*S+Measure(Count,Size,true).X+9*S,Y+35*S,18,Muted);
	if (Course->RingStreak >= 2)
	{
		Label(FString::Printf(TEXT("%d 連続"),Course->RingStreak),X+48*S,Y+69*S,16,Mint);
	}
	const AFlightPawn* Ship = Cast<AFlightPawn>(GetOwningPawn());
	const int32 Section = int32(Course->SectionAt(Ship ? Ship->S : 0));
	CenterLabel(AFlightCourse::SectionName(EFlightSection(Section)),W*0.5f,39*S,19,Paper,true);
	for (int32 I=0; I<4; ++I)
	{
		RoundRect(W*0.5f-93*S+I*49*S,72*S,39*S,3*S,1.5f*S,I<=Section?Mint:Paper.CopyWithNewOpacity(0.2f));
	}
}

void AFlightHUD::DrawInputChip()
{
	UAttitudeInputSubsystem* Input = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>();
	if (!Input) { return; }
	const EAttitudeSource Source = Input->GetState().Source;
	const FString LabelText = Source == EAttitudeSource::SprBox ? TEXT("Spresenseで操作") : Source == EAttitudeSource::DeviceMotion ? TEXT("端末の傾きで操作") : Source == EAttitudeSource::Keyboard ? TEXT("キーボードで操作") : TEXT("入力の接続待ち");
	const AFlightPawn* Ship = Cast<AFlightPawn>(GetOwningPawn());
	const float Y = Ship && Ship->State == EFlightState::Paused ? H*0.5f+252*S : H*0.5f+218*S;
	CenterLabel(LabelText,W*0.5f,Y,16,Muted);
}

void AFlightHUD::DrawAttitude()
{
	const AFlightPawn* Ship = Cast<AFlightPawn>(GetOwningPawn());
	if (!Ship) { return; }
	const float CX = W*0.5f, Y = H-47*S;
	RoundRect(CX-68*S,Y,136*S,3*S,1.5f*S,Paper.CopyWithNewOpacity(0.18f));
	DrawLine(CX,Y-5*S,CX,Y+8*S,Paper.CopyWithNewOpacity(0.5f),S);
	const float Input = FMath::Clamp(Ship->SteeringAmount(),-1.f,1.f);
	RoundRect(CX+Input*62*S-4*S,Y-2.5f*S,8*S,8*S,4*S,Mint);
}

void AFlightHUD::DrawReticles(AFlightPawn* Ship)
{
	const FVector Aim = Ship->AimPoint(Ship->AimDistance);
	const FVector P = Project(Ship->GetCourse()->Path.Frame(Ship->S+Aim.X).ToWorld(0,Aim.Y,Aim.Z),false);
	if (P.Z <= 0) { return; }
	const FLinearColor Color = FMath::Lerp(Paper,Mint,Ship->DodgePulse());
	const float Gap = 9*S, Reach = 17*S;
	DrawLine(P.X-Reach,P.Y,P.X-Gap,P.Y,Color,1.5f*S);
	DrawLine(P.X+Gap,P.Y,P.X+Reach,P.Y,Color,1.5f*S);
	DrawLine(P.X,P.Y-Reach,P.X,P.Y-Gap,Color,1.5f*S);
	DrawLine(P.X,P.Y+Gap,P.X,P.Y+Reach,Color,1.5f*S);
	RoundRect(P.X-2*S,P.Y-2*S,4*S,4*S,2*S,Mint);
}

void AFlightHUD::DrawBoss(AFlightCourse* Course)
{
	if (!Course->bBossActive) { return; }
	const float BW = 390*S, X=(W-BW)*0.5f,Y=115*S;
	CenterLabel(TEXT("大型機"),W*0.5f,Y,15,Coral);
	RoundRect(X,Y+29*S,BW,5*S,2.5f*S,Paper.CopyWithNewOpacity(0.15f));
	RoundRect(X,Y+29*S,BW*Course->BossHealth01,5*S,2.5f*S,Coral);
}

void AFlightHUD::DrawBanner(AFlightPawn* Ship)
{
	if (Ship->BannerTime <= 0 || Ship->Banner.IsEmpty()) { return; }
	const float A = FMath::Min(Ship->BannerTime/0.4f,1.f);
	const FVector2D T = Measure(Ship->Banner,22,true);
	const float Y=Ship->GetCourse()->bBossActive?172*S:118*S;
	RoundRect(W*0.5f-T.X*0.5f-22*S,Y,T.X+44*S,45*S,22*S,Ink.CopyWithNewOpacity(A*0.72f));
	CenterLabel(Ship->Banner,W*0.5f,Y+9*S,22,(Ship->Banner.Contains(TEXT("警告"))?Coral:Paper).CopyWithNewOpacity(A),true);
}

void AFlightHUD::DrawDamageEdges(AFlightPawn* Ship)
{
	if (Ship->HitFlash <= 0) { return; }
	for (int32 I=0; I<12; ++I)
	{
		const float A=(1-I/12.f)*Ship->HitFlash*0.24f;
		DrawRect(Coral.CopyWithNewOpacity(A),I*3*S,0,3*S,H);
		DrawRect(Coral.CopyWithNewOpacity(A),W-(I+1)*3*S,0,3*S,H);
	}
}

void AFlightHUD::DrawAwaitNeutral(AFlightPawn* Ship)
{
	Dim(0.24f);
	const FAttitudeState& Input = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>()->GetState();
	const float PW=660*S,X=(W-PW)*0.5f,Y=H*0.5f-260*S;
	Modal(X,Y,PW,520*S);
	Label(TEXT("FLIGHT / READY"),X+42*S,Y+35*S,15,Mint,true);
	Label(Ship->bInputLost?TEXT("持ち方をもう一度合わせよう"):TEXT("空へ、飛び出そう。"),X+42*S,Y+74*S,36,Paper,true);
	Label(Input.bAvailable?TEXT("いまの持ち方を基準にしてスタート。"):TEXT("Spresenseか端末の姿勢入力を接続してください。"),X+42*S,Y+143*S,21,Muted);
	const bool Keys=Input.Source==EAttitudeSource::Keyboard;
	const FString Guide=Keys?TEXT("WASDで移動。Spaceでクイック回避。"):Ship->bGestureDodge?TEXT("傾けて移動。短く傾けて戻すと回避。"):TEXT("傾けて移動。右下のボタンで回避。");
	Label(Guide,X+42*S,Y+193*S,22,Paper);
	Label(TEXT("レーザーは自動。リングと撃破をつなごう。"),X+42*S,Y+235*S,20,Muted);
	if (Input.bAvailable) { PillButton(TEXT("Reset"),TEXT("姿勢を合わせて開始"),X+42*S,Y+307*S,PW-84*S,74*S,true); }
	else { CenterLabel(TEXT("接続を待っています"),W*0.5f,Y+329*S,24,Muted); }
	PillButton(TEXT("Top"),TEXT("トップへ"),X+42*S,Y+397*S,PW-84*S,62*S);
	DrawInputChip();
}

void AFlightHUD::DrawCountdown(AFlightPawn* Ship)
{
	const int32 N=FMath::Max(1,FMath::CeilToInt(Ship->CountdownLeft));
	const float Frac=Ship->CountdownLeft-FMath::FloorToFloat(Ship->CountdownLeft);
	const float R=(52+Frac*4)*S;
	RoundRect(W*0.5f-R,H*0.39f-R,R*2,R*2,R,Ink.CopyWithNewOpacity(0.65f));
	CenterLabel(FString::FromInt(N),W*0.5f,H*0.39f-39*S,68,Paper,true);
	CenterLabel(TEXT("まもなくスタート"),W*0.5f,H*0.39f+78*S,20,Paper);
}

void AFlightHUD::DrawPaused()
{
	Dim(0.35f);
	const AFlightPawn* Ship=Cast<AFlightPawn>(GetOwningPawn());
	const float PW=660*S,X=(W-PW)*0.5f,Y=H*0.5f-305*S;
	Modal(X,Y,PW,610*S);
	Label(TEXT("TAKE A BREATH"),X+42*S,Y+34*S,15,Mint,true);
	Label(TEXT("ひと休み。"),X+42*S,Y+70*S,42,Paper,true);
	PillButton(TEXT("Resume"),TEXT("フライトを続ける"),X+42*S,Y+149*S,PW-84*S,74*S,true);
	PillButton(TEXT("Reset"),TEXT("姿勢を合わせ直す"),X+42*S,Y+239*S,PW-84*S,64*S);
	PillButton(TEXT("Gesture"),Ship&&Ship->bGestureDodge?TEXT("傾けて戻す回避：オン"):TEXT("傾けて戻す回避：オフ"),X+42*S,Y+325*S,PW-84*S,64*S);
	PillButton(TEXT("Top"),TEXT("トップへ"),X+42*S,Y+418*S,PW-84*S,64*S);
	DrawInputChip();
}

void AFlightHUD::DrawDestroyed()
{
	Dim(0.25f);
	const float PW=580*S,X=(W-PW)*0.5f,Y=H*0.5f-145*S;
	Modal(X,Y,PW,290*S);
	Label(TEXT("もう一度、飛ぼう。"),X+42*S,Y+56*S,38,Paper,true);
	Label(TEXT("チェックポイントからまもなく再開します。"),X+42*S,Y+129*S,21,Muted);
	RoundRect(X+42*S,Y+209*S,PW-84*S,4*S,2*S,Mint.CopyWithNewOpacity(0.7f));
}

void AFlightHUD::DrawCleared(AFlightCourse* Course)
{
	Dim(0.32f);
	const float PW=760*S,X=(W-PW)*0.5f,Y=H*0.5f-320*S;
	Modal(X,Y,PW,640*S);
	Label(TEXT("FLIGHT COMPLETE"),X+42*S,Y+31*S,15,Mint,true);
	Label(TEXT("コースクリア"),X+42*S,Y+66*S,37,Paper,true);
	Label(Course->Score.Grade(),X+40*S,Y+130*S,112,Mint,true);
	Label(FText::AsNumber(Course->Score.Total()).ToString(),X+220*S,Y+154*S,51,Paper,true);
	const FString Record=Course->bRecordSaveFailed?TEXT("記録を保存できませんでした"):Course->bNewBest?TEXT("自己ベスト更新"):TEXT("ベスト ")+FText::AsNumber(Course->BestScore).ToString();
	Label(Record,X+224*S,Y+225*S,19,Course->bRecordSaveFailed?Coral:Mint);
	DrawRect(Paper.CopyWithNewOpacity(0.1f),X+42*S,Y+284*S,PW-84*S,S);
	const FString Values[]={FString::Printf(TEXT("%d / %d"),Course->RingsPassed,Course->RingsTotal),FString::FromInt(Course->Kills),FString::FromInt(Course->Score.MaxCombo),FString::FromInt(Course->Score.Grazes),FString::FromInt(Course->Score.DamageCount),FString::FromInt(Course->Score.Retries)};
	const TCHAR* Names[]={TEXT("リング"),TEXT("撃破"),TEXT("最大コンボ"),TEXT("かすめ回避"),TEXT("被弾"),TEXT("再挑戦")};
	for (int32 I=0; I<6; ++I)
	{
		const float CX=X+(150+(I%3)*230)*S, RowY=Y+(307+(I/3)*89)*S;
		CenterLabel(Values[I],CX,RowY,29,Paper,true);
		CenterLabel(Names[I],CX,RowY+42*S,16,Muted);
	}
	const FString Breakdown=FString::Printf(TEXT("リング %d   戦闘 %d   飛行 %d   ボーナス %d"),Course->Score.RingPoints,Course->Score.CombatPoints,Course->Score.FlyingPoints,Course->Score.BonusPoints);
	CenterLabel(Breakdown,W*0.5f,Y+492*S,16,Muted);
	PillButton(TEXT("Retry"),TEXT("もう一度飛ぶ"),X+42*S,Y+548*S,332*S,66*S,true);
	PillButton(TEXT("Top"),TEXT("トップへ"),X+390*S,Y+548*S,328*S,66*S);
}
