#include "LightBoxSensors.h"

#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

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
	LastClaps = -1; BaseLux = -1; NearHeld = 0;
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
	// Relative to the room: covering the sensor (about 8x darker) or a torch (8x brighter) is full scale.
	const float Stops = FMath::Log2(FMath::Max(Lux, 0.5f) / FMath::Max(Base, 1.f));
	OutDarkness = FMath::Clamp((-Stops - 0.5f) / 2.5f, 0.f, 1.f);
	OutBrightness = FMath::Clamp((Stops - 0.5f) / 2.5f, 0.f, 1.f);
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
		if (LastClaps >= 0 && Claps != LastClaps) { R.bClap = true; }
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
	R.Darkness = Approach(R.Darkness, Darkness, Dt, 0.25f, 0.6f);
	R.Brightness = Approach(R.Brightness, Brightness, Dt, 0.25f, 0.6f);

	const float WasNear = R.Near;
	R.Near = Approach(R.Near, NearTarget, Dt, 0.15f, 0.25f);
	NearHeld = NearTarget > 0 ? NearHeld + Dt : 0;
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
	Out.Breath *= G.Sound;
	Out.bClap = In.bClap && G.Sound > 0;
	Out.Warmth *= G.Warmth;
	Out.Darkness *= G.Light;
	Out.Brightness *= G.Light;
	Out.Near *= G.Hand;
	Out.bNearReleased = In.bNearReleased && G.Hand > 0;
	Out.Magnet *= G.Magnet;
	return Out;
}

FLightBoxEnvironment FLightBoxSensorReader::ToEnvironment(const FLightBoxSensorReading& S, const FVector& Forward, const FVector& Right, const FVector& Up)
{
	FLightBoxEnvironment E;
	const FVector Away = FVector(Forward.X, Forward.Y, 0).GetSafeNormal();
	E.Wind = Away * S.Breath;
	E.Flutter = S.Breath;
	E.Warmth = S.Warmth;
	E.Gather = FMath::Clamp(S.Darkness * 0.8f + S.Near - S.Brightness * 0.7f, -1.f, 1.f);
	E.Magnet = (Right * S.MagnetScreen.X + Up * S.MagnetScreen.Y).GetSafeNormal() * S.Magnet;
	return E;
}
