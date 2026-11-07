#include "LightBoxSensors.h"

#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/ConfigCacheIni.h"

#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#include "Android/AndroidJNI.h"
#endif

namespace
{
#if PLATFORM_ANDROID
	void CallVoid(const char* Name)
	{
		if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
		{
			const jmethodID Method = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, Name, "()V", false);
			if (Method) { FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, Method); }
		}
	}

	FString PollLine()
	{
		JNIEnv* Env = FAndroidApplication::GetJavaEnv();
		if (!Env) { return FString(); }
		static jmethodID Poll = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_LightBoxSensorsPoll", "()Ljava/lang/String;", false);
		if (!Poll) { return FString(); }
		jstring Result = static_cast<jstring>(FJavaWrapper::CallObjectMethod(Env, FJavaWrapper::GameActivityThis, Poll));
		if (!Result) { return FString(); }
		const char* Utf = Env->GetStringUTFChars(Result, nullptr);
		const FString Line = UTF8_TO_TCHAR(Utf);
		Env->ReleaseStringUTFChars(Result, Utf);
		Env->DeleteLocalRef(Result);
		return Line;
	}
#endif

	float Approach(float Value, float Target, float Dt, float UpSeconds, float DownSeconds)
	{
		const float Seconds = Target > Value ? UpSeconds : DownSeconds;
		return FMath::FInterpConstantTo(Value, Target, Dt, 1.f / FMath::Max(Seconds, 0.01f));
	}
}

void FLightBoxSensorReader::Start()
{
	if (bStarted) { return; }
	bStarted = true;
	R = FLightBoxSensorReading();
	LastClaps = -1; BaseLux = -1; DarkHeld = BrightHeld = 0;
	BaseMagnet = FVector::ZeroVector; BaseMagnetSeconds = 0; BaseMagnetSamples = 0; RawBreath = 0;
#if PLATFORM_ANDROID
	CallVoid("AndroidThunkJava_LightBoxSensorsStart");
#endif
}

void FLightBoxSensorReader::Stop()
{
	if (!bStarted) { return; }
	bStarted = false;
#if PLATFORM_ANDROID
	CallVoid("AndroidThunkJava_LightBoxSensorsStop");
#endif
	R = FLightBoxSensorReading();
}

float FLightBoxSensorReader::ApproachWarmth(float Warmth, bool bHolding, float Dt)
{
	// About 3 s of a resting palm to warm fully, about 6 s to cool.
	return FMath::Clamp(Approach(Warmth, bHolding ? 1.f : 0.f, Dt, 3.f, 6.f), 0.f, 1.f);
}

void FLightBoxSensorReader::LightLevels(float Lux, float Base, float& OutDarkness, float& OutBrightness)
{
	OutDarkness = OutBrightness = 0;
	if (Lux < 0 || Base <= 0) { return; }
	// Relative to the room. A passing shadow or a lamp across the room (under ~3x) is ignored;
	// covering the sensor or a torch (about 11x) is full scale.
	const float Stops = FMath::Log2(FMath::Max(Lux, 0.5f) / FMath::Max(Base, 1.f));
	OutDarkness = FMath::Clamp((-Stops - FMath::Log2(3.f)) / 2.f, 0.f, 1.f);
	OutBrightness = FMath::Clamp((Stops - FMath::Log2(3.f)) / 2.f, 0.f, 1.f);
}

namespace { const TCHAR* SensorSection = TEXT("/Script/SpresenseMaze.LightBoxSensors"); }

namespace { const TCHAR* SensorKeys[] = { TEXT("Sound"), TEXT("Warmth"), TEXT("Light"), TEXT("Hand"), TEXT("Magnet"), TEXT("Tilt") }; }

bool FLightBoxSensorReader::IsEnabled(ELightBoxSensor Sensor)
{
	// The hand starts off: covering the phone already reads as darkness.
	bool bOn = Sensor != ELightBoxSensor::Hand;
	if (GConfig && Sensor < ELightBoxSensor::Count) { GConfig->GetBool(SensorSection, SensorKeys[int32(Sensor)], bOn, GGameUserSettingsIni); }
	return bOn;
}

void FLightBoxSensorReader::SetEnabled(ELightBoxSensor Sensor, bool bOn)
{
	if (!GConfig || Sensor >= ELightBoxSensor::Count) { return; }
	GConfig->SetBool(SensorSection, SensorKeys[int32(Sensor)], bOn, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void FLightBoxSensorReader::ApplySwitches(FLightBoxSensorReading& R, const bool (&On)[int32(ELightBoxSensor::Count)])
{
	if (!On[int32(ELightBoxSensor::Sound)]) { R.Breath = 0; R.bClap = false; }
	if (!On[int32(ELightBoxSensor::Warmth)]) { R.Warmth = 0; }
	if (!On[int32(ELightBoxSensor::Light)]) { R.Darkness = R.Brightness = 0; }
	if (!On[int32(ELightBoxSensor::Hand)]) { R.Near = 0; R.bNearReleased = false; }
	if (!On[int32(ELightBoxSensor::Magnet)]) { R.Magnet = 0; R.MagnetScreen = FVector2D::ZeroVector; }
	if (!On[int32(ELightBoxSensor::Tilt)]) { R.Tilt = 0; }
}

float FLightBoxSensorReader::HandCloseness(bool bProximityNear, float Lux, float BaseLux)
{
	if (bProximityNear) { return 1.f; }
	// Too dim a room gives no usable shadow; otherwise the shade deepens as the hand comes closer.
	if (Lux < 0 || BaseLux < 8.f) { return 0.f; }
	return FMath::Clamp((1.f - Lux / BaseLux - .12f) / .70f, 0.f, 1.f) * .92f;
}

float FLightBoxSensorReader::MagnetStrength(float Magnitude, float BaseMagnitude)
{
	// The Earth's field is 25-65 uT whatever way the phone faces, so only a
	// clear rise in magnitude (rotation-invariant) counts as a magnet.
	return FMath::Clamp((Magnitude - BaseMagnitude - 60.f) / 300.f, 0.f, 1.f);
}

const FLightBoxSensorReading& FLightBoxSensorReader::Update(float Dt, APlayerController* PC, bool bHoldWarm)
{
	R.bClap = false;
	R.bNearReleased = false;
	if (!bStarted) { return R; }
	float Breath = 0, Lux = -1, Darkness = 0, Brightness = 0, NearTarget = 0;
	FVector Magnet = FVector::ZeroVector;
	bool bMagnetValid = false;

#if PLATFORM_ANDROID
	TArray<FString> Parts;
	PollLine().ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() >= 11)
	{
		Breath = FCString::Atof(*Parts[0]);
		const int32 Claps = FCString::Atoi(*Parts[1]);
		if (LastClaps >= 0 && Claps != LastClaps) { R.bClap = true; R.ClapStrength = Parts.Num() >= 12 ? FMath::Clamp(FCString::Atof(*Parts[11]), .2f, 1.f) : .6f; }
		LastClaps = Claps;
		Lux = FCString::Atof(*Parts[2]);
		NearTarget = FCString::Atoi(*Parts[3]) ? 1.f : 0.f;
		Magnet = FVector(FCString::Atof(*Parts[4]), FCString::Atof(*Parts[5]), FCString::Atof(*Parts[6]));
		R.bHasLight = FCString::Atoi(*Parts[7]) != 0;
		R.bHasNear = FCString::Atoi(*Parts[8]) != 0;
		R.bHasMagnet = FCString::Atoi(*Parts[9]) != 0;
		R.bHasMic = FCString::Atoi(*Parts[10]) != 0;
		bMagnetValid = R.bHasMagnet && !Magnet.IsNearlyZero();
	}
#else
	if (PC)
	{
		R.bHasMic = R.bHasLight = R.bHasNear = R.bHasMagnet = true;
		Breath = PC->IsInputKeyDown(EKeys::B) ? 1.f : 0.f;
		bHoldWarm |= PC->IsInputKeyDown(EKeys::H);
		Lux = PC->IsInputKeyDown(EKeys::L) ? 20.f : PC->IsInputKeyDown(EKeys::K) ? 2400.f : 300.f;
		NearTarget = PC->IsInputKeyDown(EKeys::N) ? 1.f : 0.f;
		Magnet = PC->IsInputKeyDown(EKeys::M) ? FVector(280.f, 200.f, 40.f) : FVector(20.f, 30.f, -35.f);
		bMagnetValid = true;
		R.bClap = PC->WasInputKeyJustPressed(EKeys::C);
		if (R.bClap) { R.ClapStrength = PC->IsInputKeyDown(EKeys::LeftShift) ? 1.f : PC->IsInputKeyDown(EKeys::LeftControl) ? .25f : .6f; }
	}
#endif

	// Breath already rises fast and falls slowly in Java; keep a light touch here.
	RawBreath = Approach(RawBreath, FMath::Clamp(Breath, 0.f, 1.f), Dt, 0.12f, 0.5f);
	R.Breath = RawBreath;
	R.Warmth = ApproachWarmth(R.Warmth, bHoldWarm, Dt);

	if (Lux >= 0)
	{
		if (BaseLux < 0) { BaseLux = FMath::Max(Lux, 1.f); }
		LightLevels(Lux, BaseLux, Darkness, Brightness);
		// Follow the room very slowly, and never while someone is covering or lighting the sensor.
		if (Darkness == 0 && Brightness == 0) { BaseLux += (FMath::Max(Lux, 1.f) - BaseLux) * (1.f - FMath::Exp(-Dt / 90.f)); }
	}
	// Only a change that holds for a moment counts: a hand passing over or a flicker does not.
	DarkHeld = Darkness > 0 ? DarkHeld + Dt : 0;
	BrightHeld = Brightness > 0 ? BrightHeld + Dt : 0;
	R.Darkness = Approach(R.Darkness, DarkHeld > LightHoldSeconds ? Darkness : 0.f, Dt, 0.4f, 0.6f);
	R.Brightness = Approach(R.Brightness, BrightHeld > LightHoldSeconds ? Brightness : 0.f, Dt, 0.4f, 0.6f);

	R.Lux = Lux; R.BaseLux = BaseLux; R.bProximityNear = NearTarget > .5f; R.Claps = FMath::Max(0, LastClaps);
	if (!Magnet.IsNearlyZero()) { R.MagnetMicroTesla = float(Magnet.Size()); }
	NearTarget = HandCloseness(NearTarget > .5f, Lux, BaseLux);
	const float WasNear = R.Near;
	R.Near = Approach(R.Near, NearTarget, Dt, 0.15f, 0.25f);
	if (WasNear > 0.6f && NearTarget == 0 && R.Near <= 0.6f) { R.bNearReleased = true; }

	if (bMagnetValid)
	{
		// The first second (sampled at rest) is the Earth's field here.
		if (BaseMagnetSeconds < 1.f)
		{
			BaseMagnetSeconds += Dt; ++BaseMagnetSamples;
			BaseMagnet += (Magnet - BaseMagnet) / BaseMagnetSamples;
		}
		else
		{
			const float Strength = MagnetStrength(Magnet.Size(), BaseMagnet.Size());
			R.Magnet = Approach(R.Magnet, Strength, Dt, 0.2f, 0.5f);
			const FVector2D Delta(Magnet.X - BaseMagnet.X, Magnet.Y - BaseMagnet.Y);
			if (Strength > 0 && !Delta.IsNearlyZero()) { R.MagnetScreen = Delta.GetSafeNormal(); }
		}
	}
	return R;
}

FLightBoxSensorReading FLightBoxSensorReader::ApplyGains(const FLightBoxSensorReading& In, const FLightBoxSensorGains& G)
{
	FLightBoxSensorReading Out = In;
	Out.Breath = FMath::Clamp(In.Breath * G.Sound, 0.f, 1.f);
	Out.bClap = In.bClap && G.Sound > 0;
	Out.Warmth = FMath::Clamp(In.Warmth * G.Warmth, 0.f, 1.f);
	Out.Darkness = FMath::Clamp(In.Darkness * G.Light, 0.f, 1.f);
	Out.Brightness = FMath::Clamp(In.Brightness * G.Light, 0.f, 1.f);
	Out.Near = FMath::Clamp(In.Near * G.Hand, 0.f, 1.f);
	Out.bNearReleased = In.bNearReleased && G.Hand > 0;
	Out.Magnet = FMath::Clamp(In.Magnet * G.Magnet, 0.f, 1.f);
	return Out;
}

FLightBoxEnvironment FLightBoxSensorReader::ToEnvironment(const FLightBoxSensorReading& S, const FVector& Forward, const FVector& Right, const FVector& Up)
{
	FLightBoxEnvironment E;
	const FVector Away = FVector(Forward.X, Forward.Y, 0).GetSafeNormal();
	E.Wind = Away * S.Breath;
	E.Flutter = S.Breath;
	E.Warmth = S.Warmth;
	// Light level and the hand change how the motes look (ALightBoxPawn::ApplySensorLook), not where they go.
	E.Magnet = (Right * S.MagnetScreen.X + Up * S.MagnetScreen.Y).GetSafeNormal() * S.Magnet;
	return E;
}
