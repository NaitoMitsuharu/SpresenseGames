#pragma once
#include "CoreMinimal.h"
#include "LightBoxStyle.h"

struct FLightBoxRecord
{
	FString Id, Group, Utc, Words, Before, After, Raw, Result, Difference, Model, PricingDate;
	double Input=-1, Output=-1, CacheRead=-1, CacheWrite=-1, Total=-1, Credits=-1, Seconds=0;
	bool bImported=false, bRepair=false;
};

/** Persistent scene changes and provider metering. Never receives authentication headers. */
class FLightBoxJournal
{
public:
	TArray<FLightBoxRecord> Records;
	FString LastScene;
	bool bPersistent=false;
	void Load();
	int32 RecoverInterrupted();
	void Save() const;
	void Begin(const FString& Id,const FString& Words,const FString& Before,bool Repair,const FString& Group=FString());
	void Finish(const FString& Id,const FString& After,const FString& Raw,const FString& Result,double Seconds);
	bool Import(const FString& Json,bool History);
	FLightBoxRecord* Find(const FString& Id);
	double Credits() const;
	int32 Unmetered() const;
	FString Summary() const;
	static FString Difference(const FString& Before,const FString& After);
	static FString Directory();
	static FString ImagePath(const FString& Id,const TCHAR* Stage);
};
