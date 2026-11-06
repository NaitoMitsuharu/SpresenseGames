#pragma once

#include "CoreMinimal.h"

/** What Hikari Atelier needs from Termux. Android uses JNI; tests use a fake. */
class ILightBoxTermuxBridge
{
public:
	enum EAvailability : int32 { Ready = 0, NotInstalled = 1, NoPermission = 2 };
	virtual ~ILightBoxTermuxBridge() = default;
	virtual int32 Availability() = 0;
	virtual void RequestPermission() = 0;
	virtual bool Doctor() = 0;
	virtual bool StartService() = 0;
	virtual bool FetchToken() = 0;
	/** Oldest event ("doctor:ok", "token:stored", ...) or empty. */
	virtual FString Poll() = 0;
	/** True once after the app returned to the foreground. */
	virtual bool ConsumeResumed() = 0;
	virtual void CopySetupAndOpenTermux(const FString& Command) = 0;
	virtual void OpenTermux() = 0;
};

/** JNI bridge on Android, nullptr elsewhere. */
SPRESENSEMAZE_API TSharedPtr<ILightBoxTermuxBridge> MakeLightBoxTermuxBridge();

enum class ELightBoxReadyStep : uint8
{
	Idle, Probe, CheckTermux, Doctor, WaitService, FetchKey, Ready, NeedsSetup, Failed
};

enum class ELightBoxSetupNeed : uint8
{
	None,
	InstallTermux,   // Termux is not installed
	Permission,      // RUN_COMMAND permission not granted yet
	RunSetup,        // one-line setup in Termux (scripts, tools, allow-external-apps)
	CheckTermux,     // Gateway did not come up: Termux shows why (e.g. Copilot login)
};

/**
 * Readiness check that runs when Hikari Atelier opens:
 *   1. is the on-device Gateway answering?  2. is Termux usable?
 *   3. start the Gateway if needed           4. fetch the key if none is saved
 * Missing one-time setup surfaces as a NeedsSetup state the HUD explains.
 * The owner performs HTTP probes (WantsProbe/OnProbeResult) and re-reads the
 * credential when a key was stored.
 */
class SPRESENSEMAZE_API FLightBoxReadiness
{
public:
	static constexpr double DoctorTimeout = 12.0;
	static constexpr double ServiceTimeout = 60.0;
	static constexpr double KeyTimeout = 75.0;
	static constexpr double ProbeInterval = 1.0;

	explicit FLightBoxReadiness(TSharedPtr<ILightBoxTermuxBridge> InBridge) : Bridge(MoveTemp(InBridge)) {}

	/** Starts (or restarts) the check. Without a bridge (desktop) there is nothing to automate. */
	void Begin(double Now, bool bHasKey);
	void Tick(double Now, bool bHasKey);
	void Retry(double Now, bool bHasKey) { Begin(Now, bHasKey); }

	/** The owner sends one probe when this returns true, then reports the result. */
	bool WantsProbe(double Now) const;
	void ProbeSent(double Now) { bProbeInFlight = true; LastProbe = Now; }
	void OnProbeResult(double Now, bool bUp, bool bHasKey);

	/** True once after a key was stored by the handover. */
	bool ConsumeKeyStored() { const bool V = bKeyStored; bKeyStored = false; return V; }

	ELightBoxReadyStep GetStep() const { return Step; }
	ELightBoxSetupNeed GetNeed() const { return Need; }
	bool IsBusy() const;
	float Fraction(double Now) const;
	FString Label() const;
	FString Detail() const { return DetailText; }

	/** Action for the setup panel's primary button. */
	void PerformSetupAction();
	void OpenTermux() { if (Bridge) Bridge->OpenTermux(); }
	static FString SetupCommand();

private:
	void Enter(ELightBoxReadyStep Next, double Now);
	void NeedSetup(ELightBoxSetupNeed What, const FString& Why, double Now);
	void Fail(const FString& Why, double Now);
	void AfterServiceUp(double Now, bool bHasKey);
	void HandleEvent(const FString& Event, double Now, bool bHasKey);

	TSharedPtr<ILightBoxTermuxBridge> Bridge;
	ELightBoxReadyStep Step = ELightBoxReadyStep::Idle;
	ELightBoxSetupNeed Need = ELightBoxSetupNeed::None;
	FString DetailText;
	double StepStarted = 0, LastProbe = -1000, LastRecheck = 0;
	bool bServiceUp = false, bProbeInFlight = false, bKeyStored = false;
	bool bPermissionAsked = false;
};
