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
    void RefineScene();
    FString ProgressLabel() const;
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
};
