#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "LightBoxStyle.h"
#include "LightBoxJournal.h"
#include "LightBoxReadiness.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFramework/SaveGame.h"
#include "LightBoxDirector.generated.h"

UCLASS()
class SPRESENSEMAZE_API ULightBoxSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY() FString Scene;
	UPROPERTY() FString Endpoint = TEXT("http://127.0.0.1:18789");
	/** Copilot model sent as x-openclaw-model; empty = the OpenClaw profile default (gpt-6-luna). */
	UPROPERTY() FString Model;
	/** Particle budget measured on this phone, and the app build it was measured with. */
	UPROPERTY() int32 PointBudget = 0;
	UPROPERTY() FString BudgetBuild;
	UPROPERTY() FString BudgetDetail;
};

/** Voice + asynchronous local Gateway client; never part of the simulation tick. */
UCLASS()
class SPRESENSEMAZE_API ULightBoxDirector : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	void PollVoice();
	/** Runs the open-time check: Gateway up? Termux usable? start it, fetch the key. */
	void BeginReadiness();
	FLightBoxReadiness& GetReadiness() { return *Readiness; }
	const FLightBoxReadiness& GetReadiness() const { return *Readiness; }
	void StartVoice();
	FString TakeVoiceDraft();
	void Cancel();
	void Submit(const FString& Words);
    void RefineScene(bool bAutomatic=false);
    struct FModelChoice { const TCHAR* Id; const TCHAR* Label; };
    /** Models selectable in the connection settings; the first ("") is the profile default. */
    static TConstArrayView<FModelChoice> ModelChoices();
    const FString& GetModel() const { return Model; }
    FString ModelLabel() const;
    bool SetModel(const FString& Id);
    /** Default/maximum particles for this phone (startup calibration). */
    int32 PointBudget=FLightBoxStyle::DefaultPointCount;
    FString BudgetDetail;
    bool bCalibrationRequested=false;
    /** First launch, or the app was updated, or the user asked to measure again. */
    bool NeedsCalibration() const;
    void SetPointBudget(int32 Budget,const FString& Detail);
    static FString CurrentBuild();
    /** Image-review passes run automatically after each new request (0 turns it off). */
    /** Image-review passes after each new scene. Off: refining is the user's choice (メニュー > 仕上げる). */
    int32 AutoRefinePasses=0;
    bool IsAutoRefinePending() const { return PendingRefines>0; }
    FString ProgressLabel() const;
    /** Seconds since the current AI request started. */
    double OperationSeconds() const { return FPlatformTime::Seconds()-OperationStarted; }
    FString CurrentCreditsLabel() const;
	void SelectPreset(int32 Index);
	void SelectExample(int32 Index);
	void ApplyScene(const FLightBoxStyle& Next);
	bool Undo();
	bool Redo();
	bool CanUndo() const { return Past.Num() > 0; }
	bool CanRedo() const { return Future.Num() > 0; }
	int32 GetRevision() const { return Revision; }
	bool SaveScene();
	bool RestoreScene();
	bool Configure(const FString& Url, const FString& Token, FString& Error);
	static bool ValidateEndpoint(const FString& Url);
	void SyncJournal();
	const FLightBoxJournal& GetJournal() const { return Journal; }
	void RestoreRecord(const FString& Id);

	const FLightBoxStyle& GetStyle() const { return Style; }
	const FString& GetEndpoint() const { return Endpoint; }
	bool HasCredentials() const { return !BearerToken.IsEmpty(); }
	/** Picks up a key stored after launch (Termux handover via onNewIntent). */
	bool EnsureCredential();
private:
	void TickReadiness();
	TUniquePtr<FLightBoxReadiness> Readiness;
	FHttpRequestPtr ProbeRequest;
public:
	FString Status = TEXT("箱を傾けて、光を動かしてみよう");
	FString Heard;
	FString JournalStatus;
    /** Latest normalized speech recognizer level; zero when stopped or unsupported. */
    float VoiceLevel = 0;
	bool bListening = false;
	bool bThinking = false;
    bool bCapturingReview=false;
	bool bHasSavedScene = false;
	int32 Revision = 0;

private:
	friend class FLightBoxHttpRoundTripTest;
	friend class FLightBoxHttpRepairTest;
	friend class FLightBoxSlowInferenceTest;
    friend class FLightBoxUsageGroupTest;
    friend class FLightBoxScreenshotJournalTest;
	void StopVoice();
	void Persist();
	void RequestScene(const FString& Prompt, const FString& Previous = FString(), const FString& Diagnostic = FString(), int32 Attempt = 0);
	void PollJournal();
	FLightBoxJournal Journal;
	FString ActiveRequestId, CaptureId;
	TArray<FString> MeterQueue;
	TMap<FString,int32> MeterAttempts;
	FHttpRequestPtr HistoryRequest, MeterRequest;
	double MeterAt=0, CaptureAt=0;
	bool bJournalEnabled=false;
	FLightBoxStyle Style;
	FString Endpoint = TEXT("http://127.0.0.1:18789");
	FString BearerToken;
	FString VoiceDraft;
	FString SavedScene;
	TArray<FString> Past;
	TArray<FString> Future;
	FHttpRequestPtr Pending;
	uint64 Generation = 0;
	double VoiceStarted = 0;
    double OperationStarted=0,ReviewCaptureAt=0;
    FString UsageGroup,ReviewImage;
    FDelegateHandle ReviewScreenshotHandle;
    FString Model,BudgetBuild;
    bool bLastApplied=false;
    void FillSave(class ULightBoxSaveGame* Save) const;
    /** Token usage of every response in the current operation (initial + repair), for comparisons. */
    TArray<TSharedPtr<FJsonValue>> UsageLog;
#if !UE_BUILD_SHIPPING
    struct FBench { FString Dir,InstructionSuffix; TArray<FString> Prompts,Models; int32 Case=-1,Shot=0; double At=0,Started=0; bool bWaiting=false,bResetScene=false; };
    TUniquePtr<FBench> Bench;
    void TickBench();
#endif
    friend class FLightBoxAutoRefineTest;
    int32 PendingRefines=0,RefinePass=0;
    double RefineAt=0;
    FString RefineWords;
    bool bKeepUsageGroup=false;
};
