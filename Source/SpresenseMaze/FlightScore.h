#pragma once

#include "CoreMinimal.h"

/** Deterministic run accounting. Only progress is rolled back; damage and retries
 * remain part of the attempt, so dying cannot manufacture a perfect result. */
struct FFlightScore
{
	int32 RingPoints = 0, CombatPoints = 0, FlyingPoints = 0, BonusPoints = 0;
	int32 Combo = 0, MaxCombo = 0, DamageCount = 0, Retries = 0, Grazes = 0;
	float ComboTime = 0.f, LowFlightTime = 0.f;
	int64 LastFlightCell = -1;
	bool bFinished = false;
	int32 Total() const { return RingPoints + CombatPoints + FlyingPoints + BonusPoints; }
	float Multiplier() const { return 1.f + 0.2f * FMath::Min(Combo / 5, 5); }
	void Tick(float Dt) { ComboTime = FMath::Max(0.f, ComboTime - Dt); if (ComboTime == 0.f) { Combo = 0; } }
	int32 Award(int32 Base, bool bRing)
	{
		if (bFinished) { return 0; }
		++Combo; MaxCombo = FMath::Max(MaxCombo, Combo); ComboTime = 6.f;
		const int32 Points = FMath::RoundToInt(Base * Multiplier());
		(bRing ? RingPoints : CombatPoints) += Points;
		return Points;
	}
	void Damage() { if (!bFinished) { ++DamageCount; Combo = 0; ComboTime = 0.f; LowFlightTime = 0.f; } }
	int32 CloseFlight(double S, float Dt, bool bClose)
	{
		// No endless arena farming; each 10 m of the main course can pay once.
		if (bFinished || S >= 650000 || !bClose) { LowFlightTime = 0.f; return 0; }
		LowFlightTime += Dt;
		const int64 Cell = FMath::FloorToInt64(S / 1000.0);
		if (LowFlightTime < 0.5f || Cell <= LastFlightCell) { return 0; }
		LowFlightTime = 0.f; LastFlightCell = Cell; FlyingPoints += 25;
		return 25;
	}
	int32 Graze() { if (bFinished) { return 0; } ++Grazes; FlyingPoints += 50; return 50; }
	void Restore(const FFlightScore& Checkpoint)
	{
		const int32 DamageSoFar = DamageCount, RetriesSoFar = Retries;
		*this = Checkpoint;
		DamageCount = DamageSoFar; Retries = RetriesSoFar + 1;
		Combo = 0; ComboTime = 0; LowFlightTime = 0;
	}
	void Finish()
	{
		if (bFinished) { return; }
		BonusPoints += 5000 + (DamageCount == 0 && Retries == 0 ? 3000 : 0);
		bFinished = true;
	}
	FString Grade() const
	{
		return Total() >= 30000 ? TEXT("S") : Total() >= 22000 ? TEXT("A") : Total() >= 14000 ? TEXT("B") : TEXT("C");
	}
};

namespace FlightHandling
{
	/** A quick tilt-and-return gesture. Sustained steering never fires a dodge. */
	struct FFlickGesture
	{
		float SinceNeutral = 10.f, ArmedTime = 0.f;
		int32 Side = 0;
		bool bNeutralSeen = false;
		void Reset() { *this = FFlickGesture(); }
		int32 Update(float Command, float Dt)
		{
			if (FMath::Abs(Command) < 0.18f)
			{
				const int32 Result = Side && ArmedTime <= 0.32f ? Side : 0;
				Side = 0; ArmedTime = 0; SinceNeutral = 0; bNeutralSeen = true;
				return Result;
			}
			SinceNeutral += Dt;
			if (!Side && bNeutralSeen && SinceNeutral <= 0.18f && FMath::Abs(Command) >= 0.78f)
			{
				Side = Command < 0 ? -1 : 1; ArmedTime = 0; bNeutralSeen = false;
			}
			if (Side)
			{
				ArmedTime += Dt;
				if (ArmedTime > 0.32f || Side * Command < 0) { Side = 0; bNeutralSeen = false; }
			}
			return 0;
		}
	};

	inline double DodgeEase(double T) { T = FMath::Clamp(T, 0.0, 1.0); return T*T*(3.0-2.0*T); }
	inline float MapTilt(float Angle, float DeadZone, float FullTilt)
	{
		const float T = FMath::Clamp((FMath::Abs(Angle) - DeadZone) / FMath::Max(FullTilt - DeadZone, 1.f), 0.f, 1.f);
		return FMath::Sign(Angle) * (0.65f * T + 0.35f * T * T);
	}
	/**
	 * Touch-slide aiming used while the Spresense steers. Dragging moves the
	 * line of fire (X = yaw right, Y = pitch up, degrees); releasing eases it
	 * back to the nose. A touch that starts on a HUD control never aims.
	 */
	struct FTouchAim
	{
		static constexpr float DegreesPerScreenHeight = 72.f;
		static constexpr float ReturnRate = 6.f;
		FVector2D Angles = FVector2D::ZeroVector;
		FVector2D Last = FVector2D::ZeroVector;
		bool bDragging = false, bIgnored = false;
		void Reset() { *this = FTouchAim(); }
		void Update(bool bDown, const FVector2D& Position, bool bStartsOnControl, float ViewHeight, float Dt, const FVector2D& Limits)
		{
			if (!bDown)
			{
				bDragging = bIgnored = false;
				Angles *= FMath::Exp(-ReturnRate * Dt);
				if (Angles.SizeSquared() < 1.e-4) { Angles = FVector2D::ZeroVector; }
				return;
			}
			if (!bDragging && !bIgnored)
			{
				if (bStartsOnControl) { bIgnored = true; return; }
				bDragging = true; Last = Position; return;
			}
			if (bIgnored) { return; }
			const FVector2D Delta = (Position - Last) / FMath::Max(ViewHeight, 1.f) * DegreesPerScreenHeight;
			Last = Position;
			Angles.X = FMath::Clamp(Angles.X + Delta.X, -Limits.X, Limits.X);
			Angles.Y = FMath::Clamp(Angles.Y - Delta.Y, -Limits.Y, Limits.Y);  // screen Y grows downward
		}
	};

	/**
	 * Barrel-roll stamina (Spresense mode). A roll costs more than half the
	 * gauge, so two rolls can never be chained; it refills after a short pause.
	 */
	struct FRollStamina
	{
		static constexpr float Max = 100.f, Cost = 55.f, Regen = 26.f, RegenDelay = 0.45f;
		float Value = Max, Delay = 0.f;
		bool CanSpend() const { return Value >= Cost; }
		bool TrySpend()
		{
			if (!CanSpend()) { return false; }
			Value -= Cost; Delay = RegenDelay; return true;
		}
		void Tick(float Dt, bool bRolling)
		{
			if (bRolling) { Delay = RegenDelay; return; }
			if (Delay > 0.f) { const float Used = FMath::Min(Delay, Dt); Delay -= Used; Dt -= Used; }
			Value = FMath::Min(Max, Value + Regen * Dt);
		}
		void Refill() { Value = Max; Delay = 0.f; }
	};

	/** Exact first-order response and its integral, independent of frame rate. */
	inline double Step(double& Velocity, double Target, double Dt, double Response)
	{
		const double Decay = FMath::Exp(-Response * Dt);
		const double Delta = Target * Dt + (Velocity - Target) * (1.0 - Decay) / Response;
		Velocity = Target + (Velocity - Target) * Decay;
		return Delta;
	}
}
