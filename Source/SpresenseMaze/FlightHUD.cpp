#include "FlightHUD.h"

#include "AttitudeInputSubsystem.h"
#include "Engine/Canvas.h"
#include "FlightCourse.h"
#include "FlightPawn.h"

namespace
{
	const FLinearColor White(1.f, 1.f, 1.f, 0.95f);
	const FLinearColor Dimmed(1.f, 1.f, 1.f, 0.65f);
	const FLinearColor Danger(1.f, 0.3f, 0.25f);
}

void AFlightHUD::DrawHUD()
{
	Super::DrawHUD();
	AFlightPawn* Ship = Cast<AFlightPawn>(GetOwningPawn());
	if (!Canvas || !Ship || !Ship->GetCourse())
	{
		return;
	}
	AFlightCourse* Course = Ship->GetCourse();

	const bool bLive = Ship->State == EFlightState::Playing || Ship->State == EFlightState::Countdown;
	if (bLive)
	{
		DrawReticles(Ship);
	}
	DrawDamageEdges(Ship);
	DrawShield(Ship);
	DrawRings(Course);
	DrawInputChip();
	DrawAttitude();
	DrawBoss(Course);
	DrawBanner(Ship);
	if (bLive)
	{
		Button(TEXT("Pause"), TEXT("一時停止"), W - 24.f * S - 170.f * S, 24.f * S, 170.f * S, 62.f * S, false);
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

void AFlightHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	AFlightPawn* Ship = Cast<AFlightPawn>(GetOwningPawn());
	if (!Ship)
	{
		return;
	}
	if (BoxName == TEXT("Reset")) { Ship->RequestNeutralReset(); }
	else if (BoxName == TEXT("Pause") || BoxName == TEXT("Resume")) { Ship->TogglePause(); }
	else if (BoxName == TEXT("Top")) { Ship->ReturnToTop(); }
	else if (BoxName == TEXT("Retry")) { Ship->RestartCourse(); }
}

void AFlightHUD::Dim(float Alpha)
{
	DrawRect(FLinearColor(0.01f, 0.02f, 0.04f, Alpha), 0.f, 0.f, W, H);
}

void AFlightHUD::DrawShield(AFlightPawn* Ship)
{
	const float X = SafeLeft(), Y = 24.f * S, PW = 380.f * S, PH = 74.f * S;
	Panel(X, Y, PW, PH);
	Text(TEXT("シールド"), Dimmed, X + 16.f * S, Y + 8.f * S, 17.f);
	const float Frac = Ship->Shield / AFlightPawn::MaxShield;
	const FLinearColor Fill = Frac > 0.5f ? Cyan : (Frac > 0.25f ? Accent : Danger);
	const int32 Segs = 20;
	const float BX = X + 16.f * S, BY = Y + 38.f * S, BW = PW - 32.f * S, BH = 22.f * S;
	const float SegW = BW / Segs;
	for (int32 i = 0; i < Segs; ++i)
	{
		const bool bOn = (i + 0.5f) / Segs <= Frac;
		FLinearColor C = bOn ? Fill : FLinearColor(1.f, 1.f, 1.f, 0.1f);
		if (bOn && Ship->HealFlash > 0.f)
		{
			C = FMath::Lerp(C, FLinearColor::White, Ship->HealFlash * 0.7f);
		}
		DrawRect(C, BX + i * SegW + 1.f, BY, SegW - 3.f * S, BH);
	}
}

void AFlightHUD::DrawRings(AFlightCourse* Course)
{
	const float CX = W * 0.5f;
	const float Y = 24.f * S;
	const FString Count = FString::Printf(TEXT("リング  %d / %d"), Course->RingsPassed, Course->RingsTotal);
	const FVector2D TS = TextSize(Count, 26.f);
	const float PW = TS.X + 60.f * S;
	Panel(CX - PW * 0.5f, Y, PW, 62.f * S);
	const float Pop = 1.f + 0.25f * Course->RingPulse;
	Text(Count, FMath::Lerp(White, FLinearColor(0.7f, 1.f, 1.f), Course->RingPulse), CX - TS.X * 0.5f, Y + 14.f * S, 26.f * Pop);
	if (Course->RingStreak >= 2)
	{
		const FString Streak = FString::Printf(TEXT("連続 %d"), Course->RingStreak);
		TextCentered(Streak, Cyan, CX, Y + 70.f * S, 20.f);
	}
}

void AFlightHUD::DrawInputChip()
{
	UAttitudeInputSubsystem* Attitude = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>();
	if (!Attitude)
	{
		return;
	}
	const FAttitudeState& Att = Attitude->GetState();
	const FString Label = UAttitudeInputSubsystem::SourceLabel(Att.Source);
	const FVector2D TS = TextSize(Label, 18.f);
	const float PW = TS.X + 56.f * S;
	const float X = W - 24.f * S - 170.f * S - 14.f * S - PW;
	const float Y = 24.f * S;
	Panel(X, Y, PW, 62.f * S);
	const bool bFresh = Att.bAvailable && Attitude->GetSampleAge() < UAttitudeInputSubsystem::SprBoxStaleSeconds;
	const FLinearColor DotColor = Att.Source == EAttitudeSource::SprBox ? FLinearColor(0.25f, 0.95f, 0.45f)
		: (bFresh ? Cyan : Danger);
	Dot(X + 22.f * S, Y + 31.f * S, 6.f * S, DotColor);
	Text(Label, White, X + 38.f * S, Y + 19.f * S, 18.f);
}

void AFlightHUD::DrawAttitude()
{
	UAttitudeInputSubsystem* Attitude = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>();
	if (!Attitude)
	{
		return;
	}
	const FAttitudeState& Att = Attitude->GetState();
	const float PW = 250.f * S, PH = 150.f * S;
	const float X = SafeLeft(), Y = H - PH - 24.f * S;
	Panel(X, Y, PW, PH);

	// Roll: an arc gauge with a needle. Pitch: a vertical bar. Both draw the
	// exact angles the flight model applies.
	const float CX = X + 80.f * S, CY = Y + 76.f * S, R = 46.f * S;
	Arc(CX, CY, R, -90.f, 90.f, FLinearColor(1.f, 1.f, 1.f, 0.25f), 2.f);
	for (const float Tick : { -60.f, -30.f, 0.f, 30.f, 60.f })
	{
		const float A = FMath::DegreesToRadians(Tick);
		DrawLine(CX + FMath::Sin(A) * R * 0.85f, CY - FMath::Cos(A) * R * 0.85f,
			CX + FMath::Sin(A) * R, CY - FMath::Cos(A) * R, FLinearColor(1.f, 1.f, 1.f, 0.4f), 2.f);
	}
	const float RollA = FMath::DegreesToRadians(FMath::Clamp(Att.RollDeg, -90.f, 90.f));
	DrawLine(CX, CY, CX + FMath::Sin(RollA) * R, CY - FMath::Cos(RollA) * R, Accent, 3.f);
	// A small level-wing symbol rotating with the roll.
	const FVector2D Wing(FMath::Cos(RollA) * 26.f * S, FMath::Sin(RollA) * 26.f * S);
	DrawLine(CX - Wing.X, CY + 22.f * S - Wing.Y, CX + Wing.X, CY + 22.f * S + Wing.Y, White, 3.f);

	const float BX = X + 160.f * S, BY = Y + 24.f * S, BH = 100.f * S;
	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.15f), BX, BY, 6.f * S, BH);
	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.35f), BX - 4.f * S, BY + BH * 0.5f, 14.f * S, 1.f);
	const float PitchT = FMath::Clamp(Att.PitchDeg / 40.f, -1.f, 1.f);
	DrawRect(Accent, BX - 5.f * S, BY + BH * 0.5f + PitchT * BH * 0.5f - 3.f * S, 16.f * S, 6.f * S);

	const FString Values = FString::Printf(TEXT("R%+4.0f  P%+4.0f  Y%+4.0f"), Att.RollDeg, Att.PitchDeg, Att.YawDeg);
	Text(Values, Dimmed, X + 14.f * S, Y + PH - 26.f * S, 14.f);
}

void AFlightHUD::DrawReticles(AFlightPawn* Ship)
{
	// Both reticles are points on the real line of fire (twin guns converge on
	// the far one), projected from path space into the camera.
	auto Screen = [&](float Dist, FVector2D& Out) -> bool
	{
		const FVector P = Ship->AimPoint(Dist);
		const FVector World = Ship->GetCourse()->Path.Frame(Ship->S + P.X).ToWorld(0, P.Y, P.Z);
		const FVector Proj = Project(World, false);
		Out = FVector2D(Proj.X, Proj.Y);
		return Proj.Z > 0.f;
	};
	FVector2D Near, Far;
	const FLinearColor C = Cyan.CopyWithNewOpacity(0.9f);
	if (Screen(Ship->AimDistance * 0.45f, Near))
	{
		const float Q = 30.f * S, L = 12.f * S;
		for (const FVector2D Sgn : { FVector2D(-1, -1), FVector2D(1, -1), FVector2D(1, 1), FVector2D(-1, 1) })
		{
			const FVector2D Corner = Near + Sgn * Q;
			DrawLine(Corner.X, Corner.Y, Corner.X - Sgn.X * L, Corner.Y, C, 2.5f);
			DrawLine(Corner.X, Corner.Y, Corner.X, Corner.Y - Sgn.Y * L, C, 2.5f);
		}
	}
	if (Screen(Ship->AimDistance, Far))
	{
		Ring(Far.X, Far.Y, 15.f * S, C, 2.5f);
		DrawRect(C, Far.X - 2.f, Far.Y - 2.f, 4.f, 4.f);
		DrawLine(Far.X - 26.f * S, Far.Y, Far.X - 18.f * S, Far.Y, C, 2.f);
		DrawLine(Far.X + 18.f * S, Far.Y, Far.X + 26.f * S, Far.Y, C, 2.f);
	}
}

void AFlightHUD::DrawBoss(AFlightCourse* Course)
{
	if (!Course->bBossActive)
	{
		return;
	}
	const float BW = W * 0.36f, BH = 14.f * S;
	const float X = (W - BW) * 0.5f, Y = 118.f * S;
	TextCentered(TEXT("大型機"), Danger, W * 0.5f, Y - 30.f * S, 18.f);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), X - 2.f, Y - 2.f, BW + 4.f, BH + 4.f);
	DrawRect(Danger, X, Y, BW * Course->BossHealth01, BH);
}

void AFlightHUD::DrawBanner(AFlightPawn* Ship)
{
	if (Ship->BannerTime <= 0.f || Ship->Banner.IsEmpty())
	{
		return;
	}
	const float A = FMath::Clamp(Ship->BannerTime / 0.4f, 0.f, 1.f);
	const FVector2D TS = TextSize(Ship->Banner, 46.f);
	const float Y = H * 0.28f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.35f * A), 0.f, Y - 18.f * S, W, TS.Y + 36.f * S);
	DrawRect(Accent.CopyWithNewOpacity(0.8f * A), W * 0.5f - TS.X * 0.5f, Y + TS.Y + 12.f * S, TS.X, 3.f * S);
	Text(Ship->Banner, FLinearColor(1.f, 1.f, 1.f, A), W * 0.5f - TS.X * 0.5f, Y, 46.f);
}

void AFlightHUD::DrawDamageEdges(AFlightPawn* Ship)
{
	if (Ship->HitFlash <= 0.f)
	{
		return;
	}
	const float A = Ship->HitFlash * 0.45f;
	const float E = 60.f * S;
	for (int32 i = 0; i < 4; ++i)
	{
		const float K = A * (1.f - i / 4.f);
		const float O = i * E / 4.f;
		const FLinearColor C(1.f, 0.1f, 0.08f, K * 0.5f);
		DrawRect(C, O, O, W - O * 2.f, E / 4.f);
		DrawRect(C, O, H - O - E / 4.f, W - O * 2.f, E / 4.f);
		DrawRect(C, O, O, E / 4.f, H - O * 2.f);
		DrawRect(C, W - O - E / 4.f, O, E / 4.f, H - O * 2.f);
	}
}

void AFlightHUD::DrawAwaitNeutral(AFlightPawn* Ship)
{
	Dim(0.45f);
	UAttitudeInputSubsystem* Attitude = GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>();
	const FAttitudeState& Att = Attitude->GetState();
	const float CX = W * 0.5f;
	float Y = H * 0.3f;
	if (Ship->bInputLost)
	{
		TextCentered(TEXT("入力元が切り替わりました"), Accent, CX, Y - 56.f * S, 26.f);
	}
	if (!Att.bAvailable)
	{
		TextCentered(TEXT("姿勢入力が見つかりません"), Danger, CX, Y, 40.f);
		TextCentered(TEXT("Spresenseを接続してください"), Dimmed, CX, Y + 64.f * S, 22.f);
	}
	else
	{
		TextCentered(TEXT("持ち方を整えて姿勢リセット"), White, CX, Y, 40.f);
		TextCentered(FString::Printf(TEXT("入力  %s"), *UAttitudeInputSubsystem::SourceLabel(Att.Source)), Dimmed, CX, Y + 64.f * S, 22.f);
		const float BW = 380.f * S, BH = 76.f * S;
		Button(TEXT("Reset"), TEXT("姿勢リセット"), CX - BW * 0.5f, H * 0.52f, BW, BH, true);
	}
	const float SW = 220.f * S, SH = 60.f * S;
	Button(TEXT("Top"), TEXT("トップへ"), CX - SW * 0.5f, H * 0.52f + 100.f * S, SW, SH, false);
	TextCentered(TEXT("傾けて上下左右へ移動  レーザーは自動"), Dimmed, CX, H * 0.52f + 180.f * S, 18.f);
}

void AFlightHUD::DrawCountdown(AFlightPawn* Ship)
{
	const int32 N = FMath::CeilToInt(Ship->CountdownLeft);
	const float Frac = Ship->CountdownLeft - FMath::FloorToFloat(Ship->CountdownLeft);
	const FString Num = FString::FromInt(FMath::Max(N, 1));
	TextCentered(Num, FLinearColor(1.f, 1.f, 1.f, 0.4f + 0.6f * Frac), W * 0.5f, H * 0.36f, 96.f * (0.8f + 0.3f * Frac));
}

void AFlightHUD::DrawPaused()
{
	Dim(0.55f);
	const float CX = W * 0.5f;
	TextCentered(TEXT("一時停止"), White, CX, H * 0.24f, 52.f);
	const float BW = 340.f * S, BH = 70.f * S, Gap = 16.f * S;
	float Y = H * 0.42f;
	Button(TEXT("Resume"), TEXT("再開"), CX - BW * 0.5f, Y, BW, BH, true);
	Y += BH + Gap;
	Button(TEXT("Reset"), TEXT("姿勢リセット"), CX - BW * 0.5f, Y, BW, BH, false);
	Y += BH + Gap;
	Button(TEXT("Top"), TEXT("トップへ"), CX - BW * 0.5f, Y, BW, BH, false);
}

void AFlightHUD::DrawDestroyed()
{
	Dim(0.3f);
	TextCentered(TEXT("撃墜"), Danger, W * 0.5f, H * 0.34f, 64.f);
	TextCentered(TEXT("チェックポイントから再開します"), Dimmed, W * 0.5f, H * 0.34f + 90.f * S, 22.f);
}

void AFlightHUD::DrawCleared(AFlightCourse* Course)
{
	Dim(0.5f);
	const float CX = W * 0.5f;
	TextCentered(TEXT("コースクリア"), Accent, CX, H * 0.2f, 60.f);
	const float PW = 520.f * S, PH = 150.f * S;
	Panel(CX - PW * 0.5f, H * 0.36f, PW, PH);
	TextCentered(FString::Printf(TEXT("リング  %d / %d"), Course->RingsPassed, Course->RingsTotal), White, CX, H * 0.36f + 26.f * S, 30.f);
	TextCentered(FString::Printf(TEXT("撃破  %d"), Course->Kills), White, CX, H * 0.36f + 82.f * S, 30.f);
	const float BW = 260.f * S, BH = 70.f * S;
	Button(TEXT("Retry"), TEXT("もう一度"), CX - BW - 10.f * S, H * 0.66f, BW, BH, true);
	Button(TEXT("Top"), TEXT("トップへ"), CX + 10.f * S, H * 0.66f, BW, BH, false);
}
