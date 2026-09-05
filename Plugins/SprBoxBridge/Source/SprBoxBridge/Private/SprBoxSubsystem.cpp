#include "SprBoxSubsystem.h"

#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#include "Android/AndroidJNI.h"

namespace SprBoxJNI
{
	static jmethodID Method_Init = nullptr;
	static jmethodID Method_Shutdown = nullptr;
	static jmethodID Method_GetImu = nullptr;
	static jmethodID Method_IsConnected = nullptr;
	static jmethodID Method_GetStatus = nullptr;
	static jmethodID Method_Recalibrate = nullptr;
	static jmethodID Method_CalibStart = nullptr;
	static jmethodID Method_CalibFinish = nullptr;

	static void CacheMethods(JNIEnv* Env)
	{
		if (Method_Init != nullptr)
		{
			return;
		}
		jclass GameActivityClass = FAndroidApplication::FindJavaClass("com/epicgames/unreal/GameActivity");
		Method_Init        = FJavaWrapper::FindMethod(Env, GameActivityClass, "AndroidThunkJava_SprBoxInit", "(IIID)Z", false);
		Method_Shutdown    = FJavaWrapper::FindMethod(Env, GameActivityClass, "AndroidThunkJava_SprBoxShutdown", "()V", false);
		Method_GetImu      = FJavaWrapper::FindMethod(Env, GameActivityClass, "AndroidThunkJava_SprBoxGetImu", "()[D", false);
		Method_IsConnected = FJavaWrapper::FindMethod(Env, GameActivityClass, "AndroidThunkJava_SprBoxIsConnected", "()Z", false);
		Method_GetStatus   = FJavaWrapper::FindMethod(Env, GameActivityClass, "AndroidThunkJava_SprBoxGetStatus", "()I", false);
		Method_Recalibrate = FJavaWrapper::FindMethod(Env, GameActivityClass, "AndroidThunkJava_SprBoxRecalibrate", "()V", false);
		Method_CalibStart  = FJavaWrapper::FindMethod(Env, GameActivityClass, "AndroidThunkJava_SprBoxCalibStart", "(Z)Z", false);
		Method_CalibFinish = FJavaWrapper::FindMethod(Env, GameActivityClass, "AndroidThunkJava_SprBoxCalibFinish", "()Z", false);
	}
}
#endif

bool USprBoxSubsystem::InitSprBox(int32 Rate, int32 AccelRange, int32 GyroRange, float Beta)
{
#if PLATFORM_ANDROID
	if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
	{
		SprBoxJNI::CacheMethods(Env);
		const bool bOk = FJavaWrapper::CallBooleanMethod(
			Env, FJavaWrapper::GameActivityThis, SprBoxJNI::Method_Init,
			(jint)Rate, (jint)AccelRange, (jint)GyroRange, (jdouble)Beta);
		bInitialized = bOk;
		return bOk;
	}
	return false;
#else
	// Editor / desktop: no device, gameplay should use keyboard fallback
	bInitialized = false;
	return false;
#endif
}

void USprBoxSubsystem::ShutdownSprBox()
{
#if PLATFORM_ANDROID
	if (!bInitialized)
	{
		return;
	}
	if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
	{
		SprBoxJNI::CacheMethods(Env);
		FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, SprBoxJNI::Method_Shutdown);
	}
#endif
	bInitialized = false;
}

void USprBoxSubsystem::Deinitialize()
{
	ShutdownSprBox();
	Super::Deinitialize();
}

FSprBoxImuData USprBoxSubsystem::GetImuData() const
{
	FSprBoxImuData Data;
#if PLATFORM_ANDROID
	if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
	{
		SprBoxJNI::CacheMethods(Env);
		jdoubleArray JArray = (jdoubleArray)FJavaWrapper::CallObjectMethod(
			Env, FJavaWrapper::GameActivityThis, SprBoxJNI::Method_GetImu);
		if (JArray != nullptr)
		{
			jdouble Values[10] = { 0 };
			Env->GetDoubleArrayRegion(JArray, 0, 10, Values);
			Env->DeleteLocalRef(JArray);

			Data.Ax = (float)Values[0];
			Data.Ay = (float)Values[1];
			Data.Az = (float)Values[2];
			Data.Gx = (float)Values[3];
			Data.Gy = (float)Values[4];
			Data.Gz = (float)Values[5];
			Data.Roll = (float)Values[6];
			Data.Pitch = (float)Values[7];
			Data.Yaw = (float)Values[8];
			Data.Sequence = (int64)Values[9];
			Data.bValid = Data.Sequence > 0;
		}
	}
#endif
	return Data;
}

void USprBoxSubsystem::GetConnectionStatus(bool& bServiceOnline, bool& bBoardOnline) const
{
	bServiceOnline = false;
	bBoardOnline = false;
#if PLATFORM_ANDROID
	if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
	{
		SprBoxJNI::CacheMethods(Env);
		const int32 Status = (int32)FJavaWrapper::CallIntMethod(
			Env, FJavaWrapper::GameActivityThis, SprBoxJNI::Method_GetStatus);
		bServiceOnline = (Status & 1) != 0;
		bBoardOnline = (Status & 2) != 0;
	}
#endif
}

void USprBoxSubsystem::RecalibrateImu()
{
#if PLATFORM_ANDROID
	if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
	{
		SprBoxJNI::CacheMethods(Env);
		FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, SprBoxJNI::Method_Recalibrate);
	}
#endif
}

bool USprBoxSubsystem::StartImuCalibration(bool bReuseRunningStream)
{
#if PLATFORM_ANDROID
	if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
	{
		SprBoxJNI::CacheMethods(Env);
		return FJavaWrapper::CallBooleanMethod(
			Env, FJavaWrapper::GameActivityThis, SprBoxJNI::Method_CalibStart, (jboolean)bReuseRunningStream);
	}
#endif
	return false;
}

bool USprBoxSubsystem::FinishImuCalibration()
{
#if PLATFORM_ANDROID
	if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
	{
		SprBoxJNI::CacheMethods(Env);
		return FJavaWrapper::CallBooleanMethod(
			Env, FJavaWrapper::GameActivityThis, SprBoxJNI::Method_CalibFinish);
	}
#endif
	return false;
}

bool USprBoxSubsystem::IsSpresenseConnected() const
{
#if PLATFORM_ANDROID
	if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
	{
		SprBoxJNI::CacheMethods(Env);
		return FJavaWrapper::CallBooleanMethod(
			Env, FJavaWrapper::GameActivityThis, SprBoxJNI::Method_IsConnected);
	}
#endif
	return false;
}
