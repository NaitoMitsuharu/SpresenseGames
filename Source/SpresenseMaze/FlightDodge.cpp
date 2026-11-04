#include "FlightPawn.h"
#include "FlightCourse.h"
#include "Misc/ConfigCacheIni.h"

bool AFlightPawn::CanDodge(int32 Direction) const
{
	return State == EFlightState::Playing && Course && !Course->Score.bFinished && DodgeCooldown <= 0 &&
		(Direction == -1 || Direction == 1) && AFlightCourse::LimitX - Direction * Off.X > 300;
}

bool AFlightPawn::RequestDodge(int32 Direction)
{
	if (!CanDodge(Direction)) { return false; }
	DodgeDirection = Direction;
	DodgeTravel = FMath::Min(640.f, float(AFlightCourse::LimitX - Direction * Off.X) - 60.f);
	DodgeTime = DodgeDuration; DodgeCooldown = DodgeRecharge;
	TrailTime = FMath::Max(TrailTime, 0.45f); TrailStrength = FMath::Max(TrailStrength, 0.75f);
	Course->PlayCue(FlightCue::Dodge, 1.f, 0.4f);
	Vibrate(0.32f, 0.07f);
	return true;
}

void AFlightPawn::UpdateDodge(float Dt)
{
	DodgeCooldown = FMath::Max(0.f, DodgeCooldown - Dt);
	if (DodgeTime <= 0) { return; }
	const double Before = 1.0 - DodgeTime / DodgeDuration;
	DodgeTime = FMath::Max(0.f, DodgeTime - Dt);
	const double After = 1.0 - DodgeTime / DodgeDuration;
	const double Delta = DodgeDirection * DodgeTravel * (FlightHandling::DodgeEase(After) - FlightHandling::DodgeEase(Before));
	Off.X = FMath::Clamp(Off.X + Delta, -double(AFlightCourse::LimitX), double(AFlightCourse::LimitX));
	if (DodgeTime == 0 && FMath::Abs(CommandX) < 0.18f) { Vel.X *= 0.5; }
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
