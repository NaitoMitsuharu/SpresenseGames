#pragma once

#include "CoreMinimal.h"
#include "LightBoxStyle.h"
#include "LightBoxSensors.h"

struct FLightMote
{
	FVector Position = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FVector FlockForce = FVector::ZeroVector;
	TArray<FVector> Trail;
	float Phase = 0;
	float Age = 0;
};

/** Local simulation: bounded particles; spatial hashing and capped 30Hz neighbour steering. */
class FLightBoxSimulation
{
public:
	TArray<FLightMote> Motes;
	float Burst = 0;
	float Stillness = 0;
	void Reset(int32 Count = 144);
	void Reset(const FLightBoxStyle& Style);
    static FString Signature(const FLightBoxStyle& Style);
	static FVector Target(int32 Index, int32 Count, const FLightBoxStyle& Style, float Time);
	void Impulse(const FLightBoxStyle& Style);
	void Step(float Seconds, const FVector2D& Tilt, float Twist, const FLightBoxStyle& Style, const FLightBoxEnvironment& Env = FLightBoxEnvironment());
private:
	FString Configuration;
	void UpdateFlock(const FLightBoxStyle& Style);
	float Time = 0, TrailClock = 0, FlockClock = 1;
};
