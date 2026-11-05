#include "LightBoxJournal.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	using FJson=TSharedPtr<FJsonObject>;
	void String(const FJson& O,const TCHAR* Key,FString& V) { O->TryGetStringField(Key,V); }
	void Number(const FJson& O,const TCHAR* Key,double& V) { double N; if(O->TryGetNumberField(Key,N)&&FMath::IsFinite(N)&&N>=0) V=N; }
	TSharedRef<FJsonObject> Encode(const FLightBoxRecord& R)
	{
		auto O=MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"),R.Id); O->SetStringField(TEXT("utc"),R.Utc); O->SetStringField(TEXT("words"),R.Words);
		O->SetStringField(TEXT("before"),R.Before); O->SetStringField(TEXT("after"),R.After); O->SetStringField(TEXT("raw"),R.Raw);
		O->SetStringField(TEXT("result"),R.Result); O->SetStringField(TEXT("difference"),R.Difference); O->SetStringField(TEXT("model"),R.Model);
		O->SetStringField(TEXT("pricingDate"),R.PricingDate); O->SetBoolField(TEXT("imported"),R.bImported); O->SetBoolField(TEXT("repair"),R.bRepair);
		O->SetNumberField(TEXT("input"),R.Input); O->SetNumberField(TEXT("output"),R.Output); O->SetNumberField(TEXT("cacheRead"),R.CacheRead);
		O->SetNumberField(TEXT("cacheWrite"),R.CacheWrite); O->SetNumberField(TEXT("total"),R.Total); O->SetNumberField(TEXT("credits"),R.Credits); O->SetNumberField(TEXT("seconds"),R.Seconds);
		return O;
	}
}

FString FLightBoxJournal::Directory() { return FPaths::ProjectSavedDir()/TEXT("LightBox/Journal"); }
FString FLightBoxJournal::ImagePath(const FString& Id,const TCHAR* Stage)
{
	FGuid Guid; if(!FGuid::Parse(Id,Guid)) return FString();
	return Directory()/(Guid.ToString(EGuidFormats::DigitsWithHyphens)+TEXT("_")+Stage+TEXT(".png"));
}
FLightBoxRecord* FLightBoxJournal::Find(const FString& Id) { return Records.FindByPredicate([&](const FLightBoxRecord& R){return R.Id.Equals(Id,ESearchCase::IgnoreCase);}); }

void FLightBoxJournal::Load()
{
	FString Json;
	if(!FFileHelper::LoadFileToString(Json,*(Directory()/TEXT("journal.json")))) return;
	Import(Json,true);
	FJson O;
	if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),O)&&O.IsValid()) String(O,TEXT("lastScene"),LastScene);
}
void FLightBoxJournal::Save() const
{
	if(!bPersistent) return;
	IFileManager::Get().MakeDirectory(*Directory(),true);
	auto O=MakeShared<FJsonObject>(); O->SetStringField(TEXT("lastScene"),LastScene);
	TArray<TSharedPtr<FJsonValue>> A;
	for(const FLightBoxRecord& R:Records) A.Add(MakeShared<FJsonValueObject>(Encode(R)));
	O->SetArrayField(TEXT("records"),A); FString Json; FJsonSerializer::Serialize(O,TJsonWriterFactory<>::Create(&Json));
	const FString File=Directory()/TEXT("journal.json"), Temp=File+TEXT(".tmp");
	if(FFileHelper::SaveStringToFile(Json,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) IFileManager::Get().Move(*File,*Temp,true);
}
int32 FLightBoxJournal::RecoverInterrupted()
{
    int32 Changed=0;
    for(auto& R:Records) if(R.Result==TEXT("応答待ち")&&R.After.IsEmpty()){
        R.Result=TEXT("中断（アプリの再起動・未反映）");++Changed;
    }
    if(Changed)Save();
    return Changed;
}
void FLightBoxJournal::Begin(const FString& Id,const FString& Words,const FString& Before,bool Repair)
{
	FLightBoxRecord& R=Records.AddDefaulted_GetRef(); R.Id=Id; R.Utc=FDateTime::UtcNow().ToIso8601(); R.Words=Words;
	R.Before=Before; R.Result=TEXT("応答待ち"); R.bRepair=Repair;
	while(Records.Num()>300) Records.RemoveAt(0);
	Save();
}
void FLightBoxJournal::Finish(const FString& Id,const FString& After,const FString& Raw,const FString& Result,double Seconds)
{
	if(auto* R=Find(Id)) { R->After=After; R->Raw=Raw.Left(65536); R->Result=Result; R->Seconds=Seconds; R->Difference=Difference(R->Before,After); }
	Save();
}
bool FLightBoxJournal::Import(const FString& Json,bool History)
{
	if(Json.Len()>8*1024*1024) return false;
	FJson Root;
	if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root.IsValid()) return false;
	TArray<TSharedPtr<FJsonValue>> Items;
	if(History) { const TArray<TSharedPtr<FJsonValue>>* A=nullptr; if(!Root->TryGetArrayField(TEXT("records"),A)||A->Num()>300) return false; Items=*A; }
	else Items.Add(MakeShared<FJsonValueObject>(Root.ToSharedRef()));
	for(const auto& Item:Items)
	{
		if(Item->Type!=EJson::Object) continue;
		const FJson O=Item->AsObject(); FString Id; FGuid Guid;
		if(!O->TryGetStringField(TEXT("id"),Id)||!FGuid::Parse(Id,Guid)) continue;
		auto* Existing=Find(Id); const bool New=!Existing;
		FLightBoxRecord& R=Existing?*Existing:Records.AddDefaulted_GetRef();
		if(New)
		{
			R.Id=Id; String(O,TEXT("utc"),R.Utc); String(O,TEXT("words"),R.Words); R.Words=R.Words.Left(500);
			String(O,TEXT("before"),R.Before); String(O,TEXT("after"),R.After); String(O,TEXT("raw"),R.Raw); String(O,TEXT("result"),R.Result);
			String(O,TEXT("difference"),R.Difference); O->TryGetBoolField(TEXT("repair"),R.bRepair); R.bImported=true; O->TryGetBoolField(TEXT("imported"),R.bImported);
		}
		if(!New&&R.After.IsEmpty()) {
			FString Late;FLightBoxStyle Scene;FString Error;
			if(O->TryGetStringField(TEXT("after"),Late)&&FLightBoxStyle::Parse(Late,Scene,Error)) {
				R.After=Late;R.Difference=Difference(R.Before,Late);if(R.Raw.IsEmpty()) String(O,TEXT("raw"),R.Raw);
				R.Result+=TEXT("・返答あり（未反映）");
			}
		}
		String(O,TEXT("model"),R.Model); String(O,TEXT("pricingDate"),R.PricingDate);
		Number(O,TEXT("input"),R.Input); Number(O,TEXT("output"),R.Output); Number(O,TEXT("cacheRead"),R.CacheRead); Number(O,TEXT("cacheWrite"),R.CacheWrite);
		Number(O,TEXT("total"),R.Total); Number(O,TEXT("credits"),R.Credits); Number(O,TEXT("seconds"),R.Seconds);
	}
	Records.Sort([](const FLightBoxRecord& A,const FLightBoxRecord& B){return A.Utc<B.Utc;});
	while(Records.Num()>300) Records.RemoveAt(0);
	return true;
}
double FLightBoxJournal::Credits() const { double N=0; for(const auto& R:Records) if(R.Credits>=0) N+=R.Credits; return N; }
int32 FLightBoxJournal::Unmetered() const { int32 N=0; for(const auto& R:Records) if(R.Credits<0) ++N; return N; }
FString FLightBoxJournal::Summary() const
{
	if(Records.IsEmpty()) return TEXT("利用量の記録はまだありません");
	return FString::Printf(TEXT("記録分 約 %.3f cr%s"),Credits(),Unmetered()?*FString::Printf(TEXT(" / %d回は未取得"),Unmetered()):TEXT(""));
}
FString FLightBoxJournal::Difference(const FString& Before,const FString& After)
{
	FLightBoxStyle A,B; FString Error;
	if(!FLightBoxStyle::Parse(After,B,Error)) return FString();
	if(!FLightBoxStyle::Parse(Before,A,Error)) return TEXT("変更前は未記録");
	TArray<FString> Changes;
	if(A.Count!=B.Count) Changes.Add(FString::Printf(TEXT("光 %d→%d"),A.Count,B.Count));
	if(A.Behavior!=B.Behavior) Changes.Add(TEXT("動き"));
	if(!A.Color.Equals(B.Color)||!A.Accent.Equals(B.Accent)) Changes.Add(TEXT("色"));
	if(A.Objects.Num()!=B.Objects.Num()) Changes.Add(FString::Printf(TEXT("物体 %d→%d組"),A.Objects.Num(),B.Objects.Num()));
	else if(A.Objects.Num())
	{
		FLightBoxStyle ObjectsA,ObjectsB; ObjectsA.Objects=A.Objects; ObjectsB.Objects=B.Objects;
		if(ObjectsA.ToJson()!=ObjectsB.ToJson()) Changes.Add(TEXT("物体の形・配置"));
	}
	if(!A.Background.Equals(B.Background)||!A.Floor.Equals(B.Floor)||!FMath::IsNearlyEqual(A.Ambient,B.Ambient)) Changes.Add(TEXT("背景・明るさ"));
	if(A.CameraYaw!=B.CameraYaw||A.CameraPitch!=B.CameraPitch||A.CameraZoom!=B.CameraZoom) Changes.Add(TEXT("視点"));
	if(A.Shake!=B.Shake) Changes.Add(TEXT("振った反応"));
	if(Changes.IsEmpty()) Changes.Add(A.ToJson()==B.ToJson()?TEXT("設定の変更なし"):TEXT("演出設定"));
	return FString::Join(Changes,TEXT(" / "));
}
