#include "FlightPawn.h"
#include "FlightCourse.h"
#include "Misc/ConfigCacheIni.h"

bool AFlightPawn::CanDodge(int32 Direction) const
{
	if (State != EFlightState::Playing || !Course || Course->Score.bFinished || (Direction != -1 && Direction != 1))
	{
		return false;
	}
	if (bSplitAim)
	{
		// A roll is allowed at the course edge too; it just travels less sideways.
		return DodgeTime <= 0 && RollStamina.CanSpend();
	}
	return DodgeCooldown <= 0 && AFlightCourse::LimitX - Direction * Off.X > 300;
}

bool AFlightPawn::RequestDodge(int32 Direction)
{
	if (!CanDodge(Direction))
	{
		if (bSplitAim && State == EFlightState::Playing && DodgeTime <= 0) { RollDenied = 0.6f; }
		return false;
	}
	DodgeDirection = Direction;
	const float Room = float(AFlightCourse::LimitX - Direction * Off.X) - 60.f;
	if (bSplitAim)
	{
		RollStamina.TrySpend();
		bBarrelRoll = true;
		DodgeLength = RollDuration;
		DodgeTravel = FMath::Clamp(Room, 0.f, 520.f);
		DodgeCooldown = 0;
	}
	else
	{
		bBarrelRoll = false;
		DodgeLength = DodgeDuration;
		DodgeTravel = FMath::Min(640.f, Room);
		DodgeCooldown = DodgeRecharge;
	}
	DodgeTime = DodgeLength;
	TrailTime = FMath::Max(TrailTime, bBarrelRoll ? 0.7f : 0.45f); TrailStrength = FMath::Max(TrailStrength, 0.75f);
	Course->PlayCue(FlightCue::Dodge, 1.f, 0.4f);
	Vibrate(bBarrelRoll ? 0.45f : 0.32f, bBarrelRoll ? 0.12f : 0.07f);
	return true;
}

void AFlightPawn::UpdateDodge(float Dt)
{
	DodgeCooldown = FMath::Max(0.f, DodgeCooldown - Dt);
	RollDenied = FMath::Max(0.f, RollDenied - Dt);
	RollStamina.Tick(Dt, IsRolling());
	if (DodgeTime <= 0) { return; }
	const double Before = 1.0 - DodgeTime / DodgeLength;
	DodgeTime = FMath::Max(0.f, DodgeTime - Dt);
	const double After = 1.0 - DodgeTime / DodgeLength;
	const double Delta = DodgeDirection * DodgeTravel * (FlightHandling::DodgeEase(After) - FlightHandling::DodgeEase(Before));
	Off.X = FMath::Clamp(Off.X + Delta, -double(AFlightCourse::LimitX), double(AFlightCourse::LimitX));
	if (DodgeTime == 0)
	{
		if (FMath::Abs(CommandX) < 0.18f) { Vel.X *= 0.5; }
		// Every dodge ends a full turn: drop the extra 360 so it never unwinds backwards.
		ShipRoll = FMath::UnwindDegrees(ShipRoll);
		bBarrelRoll = false;
	}
}

void AFlightPawn::ToggleGestureDodge()
{
	if (State != EFlightState::Paused) { return; }
	bGestureDodge = !bGestureDodge; Flick.Reset();
	if (GConfig)
	{
		GConfig->SetBool(TEXT("/Script/SpresenseMaze.FlightControls"),TEXT("GestureDodge"),bGestureDodge,GGameUserSettingsIni);
		GConfig->Flush(false,GGameUserSettingsIni);
	}
}
