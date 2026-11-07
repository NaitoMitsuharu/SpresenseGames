#include "LightBoxReadiness.h"

#include "HAL/PlatformProcess.h"

FString FLightBoxReadiness::SetupCommand()
{
	// One line the user pastes into Termux once. `bash -c "$(curl ...)"` keeps the
	// terminal as stdin so the script can ask questions (Copilot login).
	return TEXT("bash -c \"$(curl -fsSL https://raw.githubusercontent.com/NaitoMitsuharu/SpresenseGames/master/Tools/OpenClaw/setup-lightbox.sh)\"");
}

void FLightBoxReadiness::Begin(double Now, bool bHasKey)
{
	Need = ELightBoxSetupNeed::None;
	DetailText.Reset();
	bServiceUp = false;
	bProbeInFlight = false;
	LastProbe = -1000;
	if (!Bridge) { Step = bHasKey ? ELightBoxReadyStep::Ready : ELightBoxReadyStep::Idle; return; }
	while (!Bridge->Poll().IsEmpty()) {}   // results of an earlier run are stale
	Bridge->ConsumeResumed();
	Enter(ELightBoxReadyStep::Probe, Now);
}

void FLightBoxReadiness::Enter(ELightBoxReadyStep Next, double Now)
{
	Step = Next;
	StepStarted = Now;
	LastRecheck = Now;
}

void FLightBoxReadiness::NeedSetup(ELightBoxSetupNeed What, const FString& Why, double Now)
{
	Need = What;
	DetailText = Why;
	Enter(ELightBoxReadyStep::NeedsSetup, Now);
}

void FLightBoxReadiness::Fail(const FString& Why, double Now)
{
	Need = ELightBoxSetupNeed::None;
	DetailText = Why;
	Enter(ELightBoxReadyStep::Failed, Now);
}

bool FLightBoxReadiness::WantsProbe(double Now) const
{
	return (Step == ELightBoxReadyStep::Probe || Step == ELightBoxReadyStep::WaitService)
		&& !bProbeInFlight && Now - LastProbe >= ProbeInterval;
}

void FLightBoxReadiness::OnProbeResult(double Now, bool bUp, bool bHasKey)
{
	bProbeInFlight = false;
	if (Step == ELightBoxReadyStep::Probe)
	{
		bServiceUp = bUp;
		if (bUp && bHasKey) { Enter(ELightBoxReadyStep::Ready, Now); }
		else { Enter(ELightBoxReadyStep::CheckTermux, Now); }
	}
	else if (Step == ELightBoxReadyStep::WaitService && bUp)
	{
		AfterServiceUp(Now, bHasKey);
	}
}

void FLightBoxReadiness::AfterServiceUp(double Now, bool bHasKey)
{
	bServiceUp = true;
	if (bHasKey) { Enter(ELightBoxReadyStep::Ready, Now); return; }
	if (!Bridge->FetchToken()) { Fail(TEXT("Termuxを呼び出せませんでした"), Now); return; }
	Enter(ELightBoxReadyStep::FetchKey, Now);
}

void FLightBoxReadiness::HandleEvent(const FString& Event, double Now, bool bHasKey)
{
	const FString SetupWhy = TEXT("Termuxで最初のセットアップを1回だけ行います");
	if (Step == ELightBoxReadyStep::Doctor && Event.StartsWith(TEXT("doctor:")))
	{
		if (Event == TEXT("doctor:ok"))
		{
			if (bServiceUp) { AfterServiceUp(Now, bHasKey); return; }
			if (!Bridge->StartService()) { Fail(TEXT("Termuxを呼び出せませんでした"), Now); return; }
			Enter(ELightBoxReadyStep::WaitService, Now);
		}
		else if (Event == TEXT("doctor:error")) { Fail(TEXT("Termuxでの確認に失敗しました。もう一度お試しください"), Now); }
		else { NeedSetup(ELightBoxSetupNeed::RunSetup, SetupWhy, Now); }
	}
	else if (Step == ELightBoxReadyStep::FetchKey && Event.StartsWith(TEXT("token:")))
	{
		if (Event == TEXT("token:stored") || Event == TEXT("token:kept")) { bKeyStored = true; Enter(ELightBoxReadyStep::Ready, Now); }
		else if (Event == TEXT("token:missing")) { Fail(TEXT("認証キーを取得できませんでした。Termuxの画面を確認してください"), Now); }
		else { NeedSetup(ELightBoxSetupNeed::RunSetup, SetupWhy, Now); }
	}
}

void FLightBoxReadiness::Tick(double Now, bool bHasKey)
{
	if (!Bridge) { return; }
	for (FString Event = Bridge->Poll(); !Event.IsEmpty(); Event = Bridge->Poll())
	{
		HandleEvent(Event, Now, bHasKey);
	}
	const double Elapsed = Now - StepStarted;
	switch (Step)
	{
	case ELightBoxReadyStep::CheckTermux:
		switch (Bridge->Availability())
		{
		case ILightBoxTermuxBridge::NotInstalled:
			NeedSetup(ELightBoxSetupNeed::InstallTermux, TEXT("言葉で景色を作るには、Termuxアプリが必要です"), Now);
			break;
		case ILightBoxTermuxBridge::NoPermission:
			if (!bPermissionAsked) { bPermissionAsked = true; Bridge->RequestPermission(); }
			NeedSetup(ELightBoxSetupNeed::Permission, TEXT("「Termuxでのコマンド実行」を許可してください"), Now);
			break;
		default:
			if (!Bridge->Doctor()) { Fail(TEXT("Termuxを呼び出せませんでした"), Now); }
			else { Enter(ELightBoxReadyStep::Doctor, Now); }
		}
		break;
	case ELightBoxReadyStep::Doctor:
		// No answer at all usually means Termux refused the call (allow-external-apps).
		if (Elapsed > DoctorTimeout) { NeedSetup(ELightBoxSetupNeed::RunSetup, TEXT("Termuxで最初のセットアップを1回だけ行います"), Now); }
		break;
	case ELightBoxReadyStep::WaitService:
		if (Elapsed > ServiceTimeout)
		{
			NeedSetup(ELightBoxSetupNeed::CheckTermux, TEXT("演出サービスが起動しませんでした。Termuxで cd ~/lightbox && bash start-lightbox.sh を実行して表示を確認してください（記録は ~/lightbox/gateway.log、初回はCopilotへのログインが必要です）"), Now);
		}
		break;
	case ELightBoxReadyStep::FetchKey:
		if (Elapsed > KeyTimeout) { Fail(TEXT("認証キーを取得できませんでした。Termuxの画面を確認してください"), Now); }
		break;
	case ELightBoxReadyStep::NeedsSetup:
	{
		// Coming back from Termux, or a granted permission, continues by itself.
		const bool bResumed = Bridge->ConsumeResumed();
		const bool bRecheckDue = Now - LastRecheck >= 1.0;
		if (bRecheckDue) { LastRecheck = Now; }
		const bool bPermissionNow = Need == ELightBoxSetupNeed::Permission && bRecheckDue
			&& Bridge->Availability() == ILightBoxTermuxBridge::Ready;
		const bool bInstalledNow = Need == ELightBoxSetupNeed::InstallTermux && bRecheckDue
			&& Bridge->Availability() != ILightBoxTermuxBridge::NotInstalled;
		if (bResumed || bPermissionNow || bInstalledNow) { Begin(Now, bHasKey); }
		break;
	}
	default:
		break;
	}
}

bool FLightBoxReadiness::IsBusy() const
{
	switch (Step)
	{
	case ELightBoxReadyStep::Probe: case ELightBoxReadyStep::CheckTermux: case ELightBoxReadyStep::Doctor:
	case ELightBoxReadyStep::WaitService: case ELightBoxReadyStep::FetchKey:
		return true;
	default:
		return false;
	}
}

float FLightBoxReadiness::Fraction(double Now) const
{
	switch (Step)
	{
	case ELightBoxReadyStep::Probe: return .08f;
	case ELightBoxReadyStep::CheckTermux: case ELightBoxReadyStep::Doctor: return .25f;
	case ELightBoxReadyStep::WaitService: return .4f + .4f * FMath::Clamp(float((Now - StepStarted) / 20.0), 0.f, 1.f);
	case ELightBoxReadyStep::FetchKey: return .85f;
	case ELightBoxReadyStep::Ready: return 1.f;
	default: return 0.f;
	}
}

FString FLightBoxReadiness::Label() const
{
	switch (Step)
	{
	case ELightBoxReadyStep::Probe: return TEXT("接続を確認中（1/4）· 演出サービス");
	case ELightBoxReadyStep::CheckTermux: case ELightBoxReadyStep::Doctor: return TEXT("接続を確認中（2/4）· Termux");
	case ELightBoxReadyStep::WaitService: return TEXT("接続を準備中（3/4）· 演出サービスを起動しています");
	case ELightBoxReadyStep::FetchKey: return TEXT("接続を準備中（4/4）· 認証キーを設定しています");
	case ELightBoxReadyStep::Ready: return TEXT("接続の準備ができました");
	default: return DetailText;
	}
}

void FLightBoxReadiness::PerformSetupAction()
{
	if (!Bridge) { return; }
	switch (Need)
	{
	case ELightBoxSetupNeed::InstallTermux: FPlatformProcess::LaunchURL(TEXT("https://f-droid.org/packages/com.termux/"), nullptr, nullptr); break;
	case ELightBoxSetupNeed::Permission: Bridge->RequestPermission(); break;
	case ELightBoxSetupNeed::RunSetup: Bridge->CopySetupAndOpenTermux(SetupCommand()); break;
	case ELightBoxSetupNeed::CheckTermux: Bridge->OpenTermux(); break;
	default: break;
	}
}

#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#include "Android/AndroidJNI.h"

namespace
{
	class FAndroidTermuxBridge final : public ILightBoxTermuxBridge
	{
		static jmethodID Find(JNIEnv* Env, const char* Name, const char* Signature)
		{
			return FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, Name, Signature, false);
		}
		static bool CallBool(const char* Name)
		{
			JNIEnv* Env = FAndroidApplication::GetJavaEnv();
			return Env && FJavaWrapper::CallBooleanMethod(Env, FJavaWrapper::GameActivityThis, Find(Env, Name, "()Z"));
		}
		static void CallVoid(const char* Name)
		{
			if (JNIEnv* Env = FAndroidApplication::GetJavaEnv()) { FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, Find(Env, Name, "()V")); }
		}
	public:
		virtual int32 Availability() override
		{
			JNIEnv* Env = FAndroidApplication::GetJavaEnv();
			return Env ? FJavaWrapper::CallIntMethod(Env, FJavaWrapper::GameActivityThis, Find(Env, "AndroidThunkJava_LightBoxTermuxAvailability", "()I")) : int32(NotInstalled);
		}
		virtual void RequestPermission() override { CallVoid("AndroidThunkJava_LightBoxTermuxRequestPermission"); }
		virtual bool Doctor() override { return CallBool("AndroidThunkJava_LightBoxTermuxDoctor"); }
		virtual bool StartService() override { return CallBool("AndroidThunkJava_LightBoxTermuxStartService"); }
		virtual bool FetchToken() override { return CallBool("AndroidThunkJava_LightBoxTermuxFetchToken"); }
		virtual bool ConsumeResumed() override { return CallBool("AndroidThunkJava_LightBoxTermuxConsumeResumed"); }
		virtual void OpenTermux() override { CallVoid("AndroidThunkJava_LightBoxTermuxOpen"); }
		virtual FString Poll() override
		{
			JNIEnv* Env = FAndroidApplication::GetJavaEnv();
			if (!Env) { return FString(); }
			jstring Result = static_cast<jstring>(FJavaWrapper::CallObjectMethod(Env, FJavaWrapper::GameActivityThis, Find(Env, "AndroidThunkJava_LightBoxTermuxPoll", "()Ljava/lang/String;")));
			if (!Result) { return FString(); }
			const char* Utf = Env->GetStringUTFChars(Result, nullptr);
			FString Value = UTF8_TO_TCHAR(Utf);
			Env->ReleaseStringUTFChars(Result, Utf); Env->DeleteLocalRef(Result);
			return Value;
		}
		virtual void CopySetupAndOpenTermux(const FString& Command) override
		{
			if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
			{
				jstring Text = Env->NewStringUTF(TCHAR_TO_UTF8(*Command));
				FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, Find(Env, "AndroidThunkJava_LightBoxTermuxCopySetup", "(Ljava/lang/String;)V"), Text);
				Env->DeleteLocalRef(Text);
			}
		}
	};
}

TSharedPtr<ILightBoxTermuxBridge> MakeLightBoxTermuxBridge() { return MakeShared<FAndroidTermuxBridge>(); }
#else
TSharedPtr<ILightBoxTermuxBridge> MakeLightBoxTermuxBridge() { return nullptr; }
#endif
