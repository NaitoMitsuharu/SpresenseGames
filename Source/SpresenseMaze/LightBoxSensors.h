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
	/** + gathers toward the centre, - scatters. Sensors no longer drive it; light and hand change the look instead. */
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

/** The physical sensors the player can switch on and off. */
enum class ELightBoxSensor : uint8 { Sound, Warmth, Light, Hand, Magnet, Tilt, Count };

/** Phone sensors (microphone, light, proximity, magnetometer) and the palm-hold warmth, normalised 0..1. */
struct FLightBoxSensorReading
{
	float Breath = 0, Warmth = 0, Darkness = 0, Brightness = 0, Near = 0, Magnet = 0;
	float Tilt = 0;                                  // how far the phone leans, 0..1 (filled by the pawn from the IMU)
	// Raw values for the long-press detail card (not used for effects).
	float Lux = -1, BaseLux = -1, MagnetMicroTesla = 0, RollDeg = 0, PitchDeg = 0;
	int32 Claps = 0;
	bool bProximityNear = false;
	FVector2D MagnetScreen = FVector2D::ZeroVector;  // unit, screen right / up
	bool bClap = false;          // one frame per clap
	float ClapStrength = 0;      // how loud the latest clap was, .2 .. 1
	bool bNearReleased = false;  // one frame when a hovering hand pulls away
	bool bHasMic = false, bHasLight = false, bHasNear = false, bHasMagnet = false, bHasTilt = false;
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
	/** How close a hand is, 0..1: the proximity switch gives only near/far, so the light it shades fills in the distance. */
	static float HandCloseness(bool bProximityNear, float Lux, float BaseLux);
	/** How long a light change must hold before it counts (s). */
	static constexpr float LightHoldSeconds = 0.35f;
	/** A clap's sound wave crosses to the scene centre in this long; the burst waits for it. */
	static constexpr float ClapTravelSeconds = 0.32f;
	/** Per-device sensor switches (saved). The proximity sensor starts off. */
	static bool IsEnabled(ELightBoxSensor Sensor);
	static void SetEnabled(ELightBoxSensor Sensor, bool bOn);
	/** Zero whatever the player switched off. */
	static void ApplySwitches(FLightBoxSensorReading& Reading, const bool (&On)[int32(ELightBoxSensor::Count)]);

private:
	FLightBoxSensorReading R;
	bool bStarted = false;
	int32 LastClaps = -1;
	float BaseLux = -1, DarkHeld = 0, BrightHeld = 0;
	FVector BaseMagnet = FVector::ZeroVector;
	float BaseMagnetSeconds = 0;
	int32 BaseMagnetSamples = 0;
	float RawBreath = 0;
};
