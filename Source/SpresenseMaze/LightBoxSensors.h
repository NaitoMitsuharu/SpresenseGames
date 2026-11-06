#pragma once

#include "CoreMinimal.h"

class APlayerController;

/** World-space forces the phone's surroundings add to the Light Box motes. Zero = no effect. */
struct FLightBoxEnvironment
{
	/** Breath: push away from the camera (length 0..1). */
	FVector Wind = FVector::ZeroVector;
	/** Extra turbulence while blowing (0..1). */
	float Flutter = 0;
	/** Palm warmth: rising air (0..1). */
	float Warmth = 0;
	/** + gathers toward the centre (darkness, a hovering hand), - scatters (bright light). */
	float Gather = 0;
	/** A magnet near the phone: flow along this direction (length 0..1). */
	FVector Magnet = FVector::ZeroVector;
	bool IsZero() const { return Wind.IsZero() && Flutter == 0 && Warmth == 0 && Gather == 0 && Magnet.IsZero(); }
};

/**
 * Per-scene sensitivity of each phone sensor (scene JSON "sensors"). 0 ignores
 * that sensor, 1 is the default, 2 doubles it. Sound covers breath and claps.
 */
struct FLightBoxSensorGains
{
	float Sound = 1, Warmth = 1, Light = 1, Hand = 1, Magnet = 1;
	bool IsDefault() const { return Sound == 1 && Warmth == 1 && Light == 1 && Hand == 1 && Magnet == 1; }
};

/** Phone sensors (microphone, light, proximity, magnetometer) and the palm-hold warmth, normalised 0..1. */
struct FLightBoxSensorReading
{
	float Breath = 0, Warmth = 0, Darkness = 0, Brightness = 0, Near = 0, Magnet = 0;
	FVector2D MagnetScreen = FVector2D::ZeroVector;  // unit, screen right / up
	bool bClap = false;          // one frame per clap
	bool bNearReleased = false;  // one frame when a hovering hand pulls away
	bool bHasMic = false, bHasLight = false, bHasNear = false, bHasMagnet = false;
};

/**
 * Polls LightBoxSensors.java on Android. On PC the keys stand in for the
 * sensors: B breath, H warm, L dark, K bright, N hand near, M magnet, C clap.
 */
class FLightBoxSensorReader
{
public:
	void Start();
	void Stop();
	/** bHoldWarm: a finger or palm resting on the screen. */
	const FLightBoxSensorReading& Update(float Dt, APlayerController* PC, bool bHoldWarm);
	const FLightBoxSensorReading& Reading() const { return R; }
	static FLightBoxEnvironment ToEnvironment(const FLightBoxSensorReading& Reading, const FVector& CameraForward, const FVector& CameraRight, const FVector& CameraUp);

	/** The reading as this scene wants it felt: scaled per sensor, claps dropped when sound is off. */
	static FLightBoxSensorReading ApplyGains(const FLightBoxSensorReading& Reading, const FLightBoxSensorGains& Gains);

	/** Pure helpers shared with tests. */
	static float ApproachWarmth(float Warmth, bool bHolding, float Dt);
	static void LightLevels(float Lux, float BaseLux, float& OutDarkness, float& OutBrightness);
	static float MagnetStrength(float Magnitude, float BaseMagnitude);

private:
	FLightBoxSensorReading R;
	bool bStarted = false;
	int32 LastClaps = -1;
	float BaseLux = -1, NearHeld = 0;
	FVector BaseMagnet = FVector::ZeroVector;
	float BaseMagnetSeconds = 0;
	int32 BaseMagnetSamples = 0;
	float RawBreath = 0;
};
