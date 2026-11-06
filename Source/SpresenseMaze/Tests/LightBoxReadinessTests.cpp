#include "Misc/AutomationTest.h"
#include "../LightBoxReadiness.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Scripted Termux: records calls and replays queued events. */
	class FFakeTermux final : public ILightBoxTermuxBridge
	{
	public:
		int32 Avail = Ready;
		bool bResumed = false;
		int32 Doctors = 0, Starts = 0, Fetches = 0, Permissions = 0, Copies = 0;
		FString Copied;
		TArray<FString> Events;
		virtual int32 Availability() override { return Avail; }
		virtual void RequestPermission() override { ++Permissions; }
		virtual bool Doctor() override { ++Doctors; return true; }
		virtual bool StartService() override { ++Starts; return true; }
		virtual bool FetchToken() override { ++Fetches; return true; }
		virtual FString Poll() override { return Events.IsEmpty() ? FString() : Events.Pop(EAllowShrinking::No); }
		virtual bool ConsumeResumed() override { const bool V = bResumed; bResumed = false; return V; }
		virtual void CopySetupAndOpenTermux(const FString& Command) override { ++Copies; Copied = Command; }
		virtual void OpenTermux() override {}
		void Push(const TCHAR* Event) { Events.Insert(Event, 0); }
	};

	void Probe(FLightBoxReadiness& R, double Now, bool bUp, bool bHasKey)
	{
		check(R.WantsProbe(Now));
		R.ProbeSent(Now);
		R.OnProbeResult(Now, bUp, bHasKey);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxReadinessHappyPathTest, "SpresenseMaze.LightBox.Readiness.StartsServiceAndFetchesKey", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightBoxReadinessHappyPathTest::RunTest(const FString&)
{
	auto Fake = MakeShared<FFakeTermux>();
	FLightBoxReadiness R(Fake);
	R.Begin(0, false);
	TestTrue(TEXT("Opening the atelier starts the check"), R.IsBusy());
	TestEqual(TEXT("First step probes the Gateway"), R.GetStep(), ELightBoxReadyStep::Probe);
	Probe(R, 0, false, false);
	R.Tick(0.1, false);
	TestEqual(TEXT("Termux is checked with one doctor call"), Fake->Doctors, 1);
	Fake->Push(TEXT("doctor:ok"));
	R.Tick(0.2, false);
	TestEqual(TEXT("Gateway down: it is started"), Fake->Starts, 1);
	TestEqual(TEXT("Then waits for it"), R.GetStep(), ELightBoxReadyStep::WaitService);
	TestFalse(TEXT("Probes are paced"), R.WantsProbe(0.5));
	Probe(R, 1.3, false, false);
	Probe(R, 2.4, true, false);
	TestEqual(TEXT("Up without a key: fetch it"), Fake->Fetches, 1);
	Fake->Push(TEXT("token:stored"));
	R.Tick(3, false);
	TestEqual(TEXT("Ready after the key arrived"), R.GetStep(), ELightBoxReadyStep::Ready);
	TestTrue(TEXT("Owner is told to re-read the credential"), R.ConsumeKeyStored());
	TestFalse(TEXT("Only once"), R.ConsumeKeyStored());
	TestFalse(TEXT("Progress row is free again"), R.IsBusy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxReadinessShortcutsTest, "SpresenseMaze.LightBox.Readiness.SkipsWhatIsAlreadyDone", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightBoxReadinessShortcutsTest::RunTest(const FString&)
{
	auto Fake = MakeShared<FFakeTermux>();
	FLightBoxReadiness R(Fake);
	R.Begin(0, true);
	Probe(R, 0, true, true);
	TestEqual(TEXT("Running Gateway and saved key: ready at once"), R.GetStep(), ELightBoxReadyStep::Ready);
	TestEqual(TEXT("Termux is not touched"), Fake->Doctors + Fake->Starts + Fake->Fetches, 0);

	R.Begin(10, false);
	Probe(R, 10, true, false);
	R.Tick(10.1, false);
	Fake->Push(TEXT("doctor:ok"));
	R.Tick(10.2, false);
	TestEqual(TEXT("Running Gateway is not started twice"), Fake->Starts, 0);
	TestEqual(TEXT("Only the key is fetched"), Fake->Fetches, 1);

	FLightBoxReadiness Desktop(nullptr);
	Desktop.Begin(0, false);
	TestFalse(TEXT("Desktop has nothing to automate"), Desktop.IsBusy());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxReadinessSetupTest, "SpresenseMaze.LightBox.Readiness.GuidesOneTimeSetup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightBoxReadinessSetupTest::RunTest(const FString&)
{
	auto Fake = MakeShared<FFakeTermux>();
	FLightBoxReadiness R(Fake);

	Fake->Avail = ILightBoxTermuxBridge::NotInstalled;
	R.Begin(0, false); Probe(R, 0, false, false); R.Tick(0.1, false);
	TestEqual(TEXT("Missing Termux asks for installation"), R.GetNeed(), ELightBoxSetupNeed::InstallTermux);
	Fake->Avail = ILightBoxTermuxBridge::NoPermission;
	R.Tick(1.2, false);
	TestEqual(TEXT("Installing it restarts the check"), R.GetStep(), ELightBoxReadyStep::Probe);
	Probe(R, 1.2, false, false); R.Tick(1.3, false);
	TestEqual(TEXT("Then the permission is needed"), R.GetNeed(), ELightBoxSetupNeed::Permission);
	TestEqual(TEXT("The system dialog is requested automatically, once"), Fake->Permissions, 1);
	Fake->Avail = ILightBoxTermuxBridge::Ready;
	R.Tick(2.4, false);
	TestEqual(TEXT("Granting it continues by itself"), R.GetStep(), ELightBoxReadyStep::Probe);

	Probe(R, 2.4, false, false); R.Tick(2.5, false);
	Fake->Push(TEXT("doctor:scripts"));
	R.Tick(2.6, false);
	TestEqual(TEXT("Missing scripts ask for the one-line setup"), R.GetNeed(), ELightBoxSetupNeed::RunSetup);
	R.PerformSetupAction();
	TestEqual(TEXT("The button copies the command and opens Termux"), Fake->Copies, 1);
	TestTrue(TEXT("The copied line runs the published setup script"), Fake->Copied.Contains(TEXT("setup-lightbox.sh")));
	R.Tick(3, false);
	TestEqual(TEXT("Waits while the user is in Termux"), R.GetStep(), ELightBoxReadyStep::NeedsSetup);
	Fake->bResumed = true;
	R.Tick(30, false);
	TestEqual(TEXT("Returning to the app re-runs the check"), R.GetStep(), ELightBoxReadyStep::Probe);

	Probe(R, 30, false, false); R.Tick(30.1, false);
	R.Tick(30.1 + FLightBoxReadiness::DoctorTimeout + 1, false);
	TestEqual(TEXT("A silent Termux (external apps not allowed) also leads to the setup"), R.GetNeed(), ELightBoxSetupNeed::RunSetup);

	R.Begin(100, false); Probe(R, 100, false, false); R.Tick(100.1, false);
	Fake->Push(TEXT("doctor:ok")); R.Tick(100.2, false);
	R.Tick(100.2 + FLightBoxReadiness::ServiceTimeout + 1, false);
	TestEqual(TEXT("A Gateway that never answers points to the Termux screen"), R.GetNeed(), ELightBoxSetupNeed::CheckTermux);
	return true;
}

#endif
