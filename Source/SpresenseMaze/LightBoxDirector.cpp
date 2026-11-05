#include "LightBoxDirector.h"

#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Engine/GameViewportClient.h"
#include "ImageUtils.h"
#include "Misc/Base64.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#include "Android/AndroidJNI.h"
#endif

namespace
{
	const TCHAR* LightBoxSaveSlot = TEXT("LightBox_v1");
#if PLATFORM_ANDROID
	void VoiceCall(const char* Name)
	{
		if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
		{
			const jmethodID Method = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, Name, "()V", false);
			if (Method) { FJavaWrapper::CallVoidMethod(Env, FJavaWrapper::GameActivityThis, Method); }
		}
	}
#endif
	FString LoadCredential(const FString& Endpoint)
	{
#if PLATFORM_ANDROID
		if (JNIEnv* Env = FAndroidApplication::GetJavaEnv())
		{
			const jmethodID Method=FJavaWrapper::FindMethod(Env,FJavaWrapper::GameActivityClassID,"AndroidThunkJava_LightBoxLoadCredential","(Ljava/lang/String;)Ljava/lang/String;",false);
			jstring Url=Env->NewStringUTF(TCHAR_TO_UTF8(*Endpoint));
			jstring Result=static_cast<jstring>(FJavaWrapper::CallObjectMethod(Env,FJavaWrapper::GameActivityThis,Method,Url));
			Env->DeleteLocalRef(Url);
			if(Result){const char* Utf=Env->GetStringUTFChars(Result,nullptr);FString Value=UTF8_TO_TCHAR(Utf);Env->ReleaseStringUTFChars(Result,Utf);Env->DeleteLocalRef(Result);return Value;}
		}
#endif
		return FString();
	}
	bool SaveCredential(const FString& Endpoint,const FString& Token)
	{
#if PLATFORM_ANDROID
		if (JNIEnv* Env=FAndroidApplication::GetJavaEnv())
		{
			const jmethodID Method=FJavaWrapper::FindMethod(Env,FJavaWrapper::GameActivityClassID,"AndroidThunkJava_LightBoxSaveCredential","(Ljava/lang/String;Ljava/lang/String;)Z",false);
			jstring Url=Env->NewStringUTF(TCHAR_TO_UTF8(*Endpoint)),Key=Env->NewStringUTF(TCHAR_TO_UTF8(*Token));
			const bool Saved=FJavaWrapper::CallBooleanMethod(Env,FJavaWrapper::GameActivityThis,Method,Url,Key);
			Env->DeleteLocalRef(Url);Env->DeleteLocalRef(Key);return Saved;
		}
		return false;
#else
		return true; // Desktop keeps credentials in memory only.
#endif
	}
}

void ULightBoxDirector::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	#if !UE_BUILD_SHIPPING
	FString BatchDirectory;
	if(FParse::Param(FCommandLine::Get(),TEXT("PointCloudReview"))) {bJournalEnabled=false;Journal.bPersistent=false;return;}
	if(FParse::Value(FCommandLine::Get(),TEXT("LightBoxBatch="),BatchDirectory)) {bJournalEnabled=false;Journal.bPersistent=false;return;}
#endif
	bJournalEnabled=true; Journal.bPersistent=true; Journal.Load();
	Journal.RecoverInterrupted();
    if(!Journal.Records.IsEmpty()){const auto& R=Journal.Records.Last();UsageGroup=R.Group.IsEmpty()?R.Id:R.Group;}
	for(int32 I=FMath::Max(0,Journal.Records.Num()-20);I<Journal.Records.Num();++I)
		if(Journal.Records[I].Credits<0)MeterQueue.AddUnique(Journal.Records[I].Id);
	FLightBoxStyle Restored; FString JournalError;
	if(FLightBoxStyle::Parse(Journal.LastScene,Restored,JournalError)) Style=Restored;
	if (const ULightBoxSaveGame* Save = Cast<ULightBoxSaveGame>(UGameplayStatics::LoadGameFromSlot(LightBoxSaveSlot, 0)))
	{
		if (ValidateEndpoint(Save->Endpoint)) { Endpoint = Save->Endpoint; }
		FLightBoxStyle Saved; FString Error;
		if (FLightBoxStyle::Parse(Save->Scene, Saved, Error)) { SavedScene = Save->Scene; bHasSavedScene = true; }
	}
#if !UE_BUILD_SHIPPING
	// Local test endpoint only; never put authentication secrets in command lines.
	FString Override;
	if (FParse::Value(FCommandLine::Get(), TEXT("LightBoxEndpoint="), Override) && ValidateEndpoint(Override)) { Endpoint = Override; }
#endif
	BearerToken=LoadCredential(Endpoint);
}

void ULightBoxDirector::Deinitialize()
{
	Cancel(); BearerToken.Reset();
	for(auto R:{HistoryRequest,MeterRequest}) if(R.IsValid()) { R->OnProcessRequestComplete().Unbind(); R->CancelRequest(); }
	Super::Deinitialize();
}

bool ULightBoxDirector::ValidateEndpoint(const FString& Url)
{
	// This demo talks only to the on-device Gateway, never an arbitrary host.
	for (const FString& Host : { FString(TEXT("http://127.0.0.1:")), FString(TEXT("http://localhost:")) })
	{
		if (Url.StartsWith(Host, ESearchCase::CaseSensitive))
		{
			const FString Port = Url.Mid(Host.Len());
			if (Port.IsEmpty() || Port.Len() > 5) { return false; }
			for (TCHAR C : Port) { if (C < TEXT('0') || C > TEXT('9')) { return false; } }
			const int32 N = FCString::Atoi(*Port);
			return N > 0 && N <= 65535;
		}
	}
	return false;
}

bool ULightBoxDirector::Configure(const FString& Url, const FString& Token, FString& Error)
{
	FString Candidate = Url.TrimStartAndEnd(); Candidate.RemoveFromEnd(TEXT("/"));
	if (!ValidateEndpoint(Candidate)) { Error = TEXT("接続先は http://127.0.0.1:ポート番号 を指定してください"); return false; }
	const FString Key = Token.TrimStartAndEnd();
	if (Key.Contains(TEXT("\r")) || Key.Contains(TEXT("\n")) || Key.Len() > 2048)
	{
		Error = TEXT("認証キーを確認してください"); return false;
	}
	const FString EffectiveKey=!Key.IsEmpty()?Key:Candidate==Endpoint?BearerToken:LoadCredential(Candidate);
	if(EffectiveKey.IsEmpty()){Error=TEXT("認証キーを入力してください");return false;}
	if(!Key.IsEmpty()&&!SaveCredential(Candidate,EffectiveKey)){Error=TEXT("認証キーを保存できませんでした。もう一度お試しください");return false;}
	Cancel(); Endpoint = Candidate; BearerToken=EffectiveKey;
	Persist(); SyncJournal(); Status = TEXT("接続設定を更新しました"); Error.Reset(); return true;
}

void ULightBoxDirector::StopVoice()
{
#if PLATFORM_ANDROID
	VoiceCall("AndroidThunkJava_LightBoxStopVoice");
#endif
	bListening = false;
}

void ULightBoxDirector::Cancel()
{
    if(ReviewScreenshotHandle.IsValid()){UGameViewportClient::OnScreenshotCaptured().Remove(ReviewScreenshotHandle);ReviewScreenshotHandle.Reset();}
    bCapturingReview=false;ReviewImage.Reset();
	if(!ActiveRequestId.IsEmpty())
	{
		if(bJournalEnabled) Journal.Finish(ActiveRequestId,FString(),FString(),TEXT("取り消し（消費量は取得待ち）"),0);
		MeterQueue.AddUnique(ActiveRequestId); ActiveRequestId.Reset();
	}
	++Generation;
	CaptureId.Reset();
	if (Pending.IsValid()) { Pending->OnProcessRequestComplete().Unbind(); Pending->CancelRequest(); Pending.Reset(); }
	bThinking = false;
	if (bListening) { StopVoice(); }
}

void ULightBoxDirector::StartVoice()
{
	if (bThinking) { Cancel(); Status = TEXT("お願いを取り消しました"); return; }
	if (bListening) { StopVoice(); Status = TEXT("聞き取りを取り消しました"); return; }
#if PLATFORM_ANDROID
	VoiceDraft.Reset();
	bListening = true; VoiceStarted = FPlatformTime::Seconds(); Heard.Reset();
    OperationStarted=VoiceStarted;
	Status = TEXT("どう変えたいか、話しかけてください");
	VoiceCall("AndroidThunkJava_LightBoxStartVoice");
#else
	Status = TEXT("言葉の入力画面からお願いしてください");
#endif
}

FString ULightBoxDirector::TakeVoiceDraft()
{
	FString Draft=MoveTemp(VoiceDraft);VoiceDraft.Reset();return Draft;
}

void ULightBoxDirector::PollVoice()
{
	PollJournal();
    if(bCapturingReview&&FPlatformTime::Seconds()-ReviewCaptureAt>8){Cancel();Status=TEXT("画面を取得できませんでした。もう一度お試しください");}
#if PLATFORM_ANDROID
	if (!bListening) { return; }
	if (FPlatformTime::Seconds() - VoiceStarted > 15.0)
	{
		StopVoice(); Status = TEXT("声が聞き取れませんでした。もう一度お試しください"); return;
	}
	JNIEnv* Env = FAndroidApplication::GetJavaEnv();
	if (!Env) { return; }
	static jmethodID Poll = FJavaWrapper::FindMethod(Env, FJavaWrapper::GameActivityClassID, "AndroidThunkJava_LightBoxPollVoice", "()Ljava/lang/String;", false);
	jstring Result = static_cast<jstring>(FJavaWrapper::CallObjectMethod(Env, FJavaWrapper::GameActivityThis, Poll));
	if (!Result) { return; }
	const char* Utf = Env->GetStringUTFChars(Result, nullptr);
	const FString Json = UTF8_TO_TCHAR(Utf);
	Env->ReleaseStringUTFChars(Result, Utf); Env->DeleteLocalRef(Result);
	if (Json.IsEmpty()) { return; }
	TSharedPtr<FJsonObject> O;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), O) || !O.IsValid()) { return; }
	FString Kind, Value;
	O->TryGetStringField(TEXT("kind"), Kind); O->TryGetStringField(TEXT("text"), Value);
	if (Kind == TEXT("partial")) { Heard = Value; }
	else if (Kind == TEXT("result")) {
		StopVoice();VoiceDraft=Value.TrimStartAndEnd();Heard=VoiceDraft;
		Status=TEXT("聞き取った内容を確認して、送信してください");
	}
	else if (Kind == TEXT("error")) { StopVoice(); Status = Value; }
#endif
}

void ULightBoxDirector::Submit(const FString& Words)
{
	const FString Prompt = Words.TrimStartAndEnd();
	if (Prompt == TEXT("元に戻して") || Prompt == TEXT("ひとつ前に戻して")) { Undo(); return; }
	if (Prompt.IsEmpty() || Prompt.Len() > 500) { Status = TEXT("お願いは1〜500文字で入力してください"); return; }
	Cancel(); Heard = Prompt;
	if (BearerToken.IsEmpty()) { Status = TEXT("接続設定で認証キーを入力してください"); return; }
	bThinking = true;OperationStarted=FPlatformTime::Seconds(); Status = TEXT("AIが景色を設計中");
	RequestScene(Prompt);
}

void ULightBoxDirector::RequestScene(const FString& Prompt, const FString& Previous, const FString& Diagnostic, int32 Attempt)
{
	bThinking = true;
	const FString RequestId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens).ToLower();
	const double Started=FPlatformTime::Seconds(); ActiveRequestId=RequestId;
    if(Attempt==0)UsageGroup=RequestId;
	if(bJournalEnabled) Journal.Begin(RequestId,Prompt,Style.ToJson(),Attempt>0,UsageGroup);
	if(bJournalEnabled && Attempt==0)
	{
		IFileManager::Get().MakeDirectory(*FLightBoxJournal::Directory(),true);
        if(!ReviewImage.IsEmpty()){
            // Already inside the screenshot callback: a nested request would be reset by the viewport.
            FString Encoded;ReviewImage.Split(TEXT(","),nullptr,&Encoded);TArray<uint8> Png;
            if(FBase64::Decode(Encoded,Png))FFileHelper::SaveArrayToFile(Png,*FLightBoxJournal::ImagePath(RequestId,TEXT("before")));
        }else FScreenshotRequest::RequestScreenshot(FLightBoxJournal::ImagePath(RequestId,TEXT("before")),true,false);
	}
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), TEXT("openclaw/default"));
	Body->SetBoolField(TEXT("stream"), false);
	Body->SetNumberField(TEXT("max_completion_tokens"), 8192);
	Body->SetStringField(TEXT("tool_choice"), TEXT("none"));
	TArray<TSharedPtr<FJsonValue>> Messages;
	for (int32 I = 0; I < 2; ++I)
	{
		const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("role"), I == 0 ? TEXT("system") : TEXT("user"));
        if(I==1&&!ReviewImage.IsEmpty()){
            auto Words=MakeShared<FJsonObject>();Words->SetStringField(TEXT("type"),TEXT("text"));Words->SetStringField(TEXT("text"),Prompt);
            auto Url=MakeShared<FJsonObject>();Url->SetStringField(TEXT("url"),ReviewImage);Url->SetStringField(TEXT("detail"),TEXT("high"));
            auto Picture=MakeShared<FJsonObject>();Picture->SetStringField(TEXT("type"),TEXT("image_url"));Picture->SetObjectField(TEXT("image_url"),Url);
            Message->SetArrayField(TEXT("content"),{MakeShared<FJsonValueObject>(Words),MakeShared<FJsonValueObject>(Picture)});
        }else Message->SetStringField(TEXT("content"), I == 0 ? FLightBoxStyle::Instruction(Style) : Prompt);
		Messages.Add(MakeShared<FJsonValueObject>(Message));
	}
	if (Attempt > 0)
	{
		auto Reply = MakeShared<FJsonObject>(); Reply->SetStringField(TEXT("role"), TEXT("assistant")); Reply->SetStringField(TEXT("content"), Previous);
		Messages.Add(MakeShared<FJsonValueObject>(Reply));
		auto Repair = MakeShared<FJsonObject>(); Repair->SetStringField(TEXT("role"), TEXT("user"));
		Repair->SetStringField(TEXT("content"), TEXT("The renderer rejected the previous scene: ") + Diagnostic
			+ TEXT(" Correct it using ONLY the listed fields, shapes and enum values. Preserve the user's requested composition. Output the complete compact JSON only, preferably <=16 object groups. No prose or tools."));
		Messages.Add(MakeShared<FJsonValueObject>(Repair));
	}
	Body->SetArrayField(TEXT("messages"), Messages);
	FString Json; FJsonSerializer::Serialize(Body, TJsonWriterFactory<>::Create(&Json));
	const uint64 RequestGeneration = Generation;
	Pending = FHttpModule::Get().CreateRequest();
	Pending->SetURL(Endpoint + TEXT("/v1/chat/completions"));
	Pending->SetVerb(TEXT("POST"));
	Pending->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Pending->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + BearerToken);
	Pending->SetHeader(TEXT("x-openclaw-session-key"),TEXT("agent:main:lightbox:")+RequestId);
	Pending->SetContentAsString(Json); Pending->SetTimeout(150.f); Pending->SetActivityTimeout(145.f);
	Pending->OnProcessRequestComplete().BindWeakLambda(this,
		[this, RequestGeneration, Prompt, Attempt, RequestId, Started](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess)
		{
			if (Generation != RequestGeneration) { return; }
			Pending.Reset(); bThinking = false;
			ActiveRequestId.Reset(); MeterQueue.AddUnique(RequestId);
			auto Log=[&](const FString& Result,const FString& After=FString())
			{
				if(bJournalEnabled) Journal.Finish(RequestId,After,Response.IsValid()&&Response->GetResponseCode()==200?Response->GetContentAsString():FString(),Result,FPlatformTime::Seconds()-Started);
			};
			if (!bSuccess || !Response.IsValid()) { Status = TEXT("演出サービスにつながりません。接続を確認してください"); Log(TEXT("通信失敗（消費量は未確定）")); return; }
			const int32 Code = Response->GetResponseCode();
			if (Code != 200)
			{
				if (Code == 401 || Code == 403) { Status = TEXT("認証キーや利用権限を確認してください"); }
				else if (Code == 429) { Status = TEXT("ただいま混み合っています。少し待ってお試しください"); }
				else if(!ReviewImage.IsEmpty()&&(Code==400||Code==415||Code==422)){Status=TEXT("現在の接続先では画像付きの造形レビューを利用できません");}
                else { Status = FString::Printf(TEXT("演出サービスから返答がありません（%d）"), Code); }
				Log(FString::Printf(TEXT("通信エラー %d"),Code));
				return;
			}
			FLightBoxStyle Next; FString Error, Why;
            Status=TEXT("景色の構成を確認中");
			if (!FLightBoxStyle::ParseCompletion(Response->GetContentAsString(), Next, Error, &Why) || !FLightBoxStyle::CompleteGenerated(Prompt,Style,Next,Why) || !FLightBoxStyle::ValidateIntent(Prompt,Next,Why))
			{
				if (Attempt == 0 && Response->GetContentAsString().Len() <= 131072)
				{
					FString Content; TSharedPtr<FJsonObject> Envelope;
					const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr; const TSharedPtr<FJsonObject>* Message = nullptr;
					if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Envelope)
						&& Envelope.IsValid() && Envelope->TryGetArrayField(TEXT("choices"), Choices) && Choices->Num() == 1
						&& (*Choices)[0]->Type == EJson::Object && (*Choices)[0]->AsObject()->TryGetObjectField(TEXT("message"), Message) && Message && (*Message).IsValid())
					{ (*Message)->TryGetStringField(TEXT("content"), Content); }
					Status = TEXT("景色を整えています…");
					Log(TEXT("検証失敗・組み直し"));
					RequestScene(Prompt, Content.Left(65536), Why, 1); return;
				}
				Status = Error.IsEmpty()?TEXT("お願いした動作を構成できませんでした。もう一度お試しください"):Error; Log(TEXT("検証失敗・変更なし")); return;
			}
			ApplyScene(Next);
			Log(TEXT("反映済み"),Next.ToJson()); CaptureId=RequestId; CaptureAt=FPlatformTime::Seconds()+1.2;
		});
	if (!Pending->ProcessRequest()) { Pending.Reset(); bThinking = false; ActiveRequestId.Reset(); if(bJournalEnabled) Journal.Finish(RequestId,FString(),FString(),TEXT("送信できませんでした"),0); Status = TEXT("接続を開始できませんでした"); }
}

void ULightBoxDirector::SyncJournal()
{
	if(!bJournalEnabled||BearerToken.IsEmpty()||HistoryRequest.IsValid()) return;
	HistoryRequest=FHttpModule::Get().CreateRequest(); HistoryRequest->SetVerb(TEXT("GET"));
	HistoryRequest->SetURL(TEXT("http://127.0.0.1:18791/v1/history")); HistoryRequest->SetTimeout(5);
	HistoryRequest->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+BearerToken);
	HistoryRequest->OnProcessRequestComplete().BindWeakLambda(this,[this](FHttpRequestPtr,FHttpResponsePtr R,bool Ok)
	{
		HistoryRequest.Reset();
		if(Ok&&R.IsValid()&&R->GetResponseCode()==200&&Journal.Import(R->GetContentAsString(),true))
		{ Journal.Save(); JournalStatus=TEXT("過去ログと利用量を更新しました"); }
		else JournalStatus=TEXT("記録サービスに接続できません。利用量を0とは扱いません");
	});
	HistoryRequest->ProcessRequest();
}

void ULightBoxDirector::PollJournal()
{
	if(!bJournalEnabled) return;
	const double Now=FPlatformTime::Seconds();
	if(!CaptureId.IsEmpty()&&Now>=CaptureAt)
	{
		FScreenshotRequest::RequestScreenshot(FLightBoxJournal::ImagePath(CaptureId,TEXT("after")),true,false); CaptureId.Reset();
		// Keep screenshots for only the newest twenty attempts; all retained JSON records remain available.
		for(int32 I=0;I<Journal.Records.Num()-20;++I) for(const TCHAR* Stage:{TEXT("before"),TEXT("after")})
		{
			const FString File=FLightBoxJournal::ImagePath(Journal.Records[I].Id,Stage);
			if(!File.IsEmpty()) IFileManager::Get().Delete(*File,false,true);
		}
	}
	if(BearerToken.IsEmpty()||MeterRequest.IsValid()||MeterQueue.IsEmpty()||Now<MeterAt) return;
	const FString Id=MeterQueue[0]; MeterQueue.RemoveAt(0); ++MeterAttempts.FindOrAdd(Id);
	MeterRequest=FHttpModule::Get().CreateRequest(); MeterRequest->SetVerb(TEXT("GET"));
	MeterRequest->SetURL(TEXT("http://127.0.0.1:18791/v1/runs/")+Id); MeterRequest->SetTimeout(5);
	MeterRequest->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+BearerToken);
	MeterRequest->OnProcessRequestComplete().BindWeakLambda(this,[this,Id](FHttpRequestPtr,FHttpResponsePtr R,bool Ok)
	{
		MeterRequest.Reset();
		if(Ok&&R.IsValid()&&R->GetResponseCode()==200&&Journal.Import(R->GetContentAsString(),false))
		{ Journal.Save(); MeterAttempts.Remove(Id); JournalStatus.Reset(); }
		else if(MeterAttempts.FindRef(Id)<8)
		{ MeterQueue.AddUnique(Id); MeterAt=FPlatformTime::Seconds()+FMath::Min(30.,FMath::Pow(2.,MeterAttempts.FindRef(Id))); }
		else { MeterAttempts.Remove(Id); JournalStatus=TEXT("一部の利用量は未取得です。記録画面で更新できます"); }
	});
	MeterRequest->ProcessRequest();
}

void ULightBoxDirector::RestoreRecord(const FString& Id)
{
	const auto* R=Journal.Find(Id); if(!R) return;
	FLightBoxStyle Next; FString Error;
	if(!FLightBoxStyle::Parse(R->After,Next,Error)) { Status=TEXT("この返答は景色として呼び出せません"); return; }
	Cancel(); ApplyScene(Next); Heard.Reset(); Status=TEXT("記録した景色を呼び出しました（追加のAI通信なし）");
}

void ULightBoxDirector::SelectPreset(int32 Index)
{
	Cancel(); ApplyScene(FLightBoxStyle::Preset(Index));
	Heard.Reset(); Status = TEXT("箱を傾けると光が流れ、振ると反応します");
}

void ULightBoxDirector::ApplyScene(const FLightBoxStyle& Next)
{
	Past.Add(Style.ToJson()); if (Past.Num() > 16) { Past.RemoveAt(0); }
	Future.Reset(); Style = Next; ++Revision;
	if(bJournalEnabled) { Journal.LastScene=Style.ToJson(); Journal.Save(); }
	Status = Style.Note.IsEmpty() ? TEXT("景色が変わりました。箱を動かしてみよう") : Style.Note;
}

void ULightBoxDirector::SelectExample(int32 Index)
{
	Cancel(); ApplyScene(FLightBoxStyle::Example(Index)); Heard.Reset();
}

bool ULightBoxDirector::Undo()
{
	if (!Past.Num()) { Status = TEXT("戻せる景色はまだありません"); return false; }
	FLightBoxStyle Previous; FString Error;
	if (!FLightBoxStyle::Parse(Past.Last(), Previous, Error)) { return false; }
	Cancel(); Future.Add(Style.ToJson()); Past.Pop(); Style = Previous; ++Revision; Heard.Reset();
	if(bJournalEnabled) { Journal.LastScene=Style.ToJson(); Journal.Save(); }
	Status = TEXT("ひとつ前の景色に戻しました"); return true;
}

bool ULightBoxDirector::Redo()
{
	if (!Future.Num()) { Status = TEXT("やり直せる景色はまだありません"); return false; }
	FLightBoxStyle Next; FString Error;
	if (!FLightBoxStyle::Parse(Future.Last(), Next, Error)) { return false; }
	Cancel(); Past.Add(Style.ToJson()); Future.Pop(); Style = Next; ++Revision; Heard.Reset();
	if(bJournalEnabled) { Journal.LastScene=Style.ToJson(); Journal.Save(); }
	Status = TEXT("次の景色に進めました"); return true;
}

void ULightBoxDirector::Persist()
{
	ULightBoxSaveGame* Save = Cast<ULightBoxSaveGame>(UGameplayStatics::CreateSaveGameObject(ULightBoxSaveGame::StaticClass()));
	Save->Scene = SavedScene; Save->Endpoint = Endpoint;
	UGameplayStatics::SaveGameToSlot(Save, LightBoxSaveSlot, 0);
}

bool ULightBoxDirector::SaveScene()
{
	ULightBoxSaveGame* Save = Cast<ULightBoxSaveGame>(UGameplayStatics::CreateSaveGameObject(ULightBoxSaveGame::StaticClass()));
	Save->Scene = Style.ToJson(); Save->Endpoint = Endpoint;
	if (!UGameplayStatics::SaveGameToSlot(Save, LightBoxSaveSlot, 0)) { Status = TEXT("保存できませんでした"); return false; }
	SavedScene = Save->Scene; bHasSavedScene = true; Status = TEXT("この景色を保存しました"); return true;
}

bool ULightBoxDirector::RestoreScene()
{
	FLightBoxStyle Next; FString Error;
	if (!bHasSavedScene || !FLightBoxStyle::Parse(SavedScene, Next, Error)) { Status = TEXT("保存した景色はまだありません"); return false; }
	Cancel(); ApplyScene(Next); Heard.Reset(); Status = TEXT("保存した景色に戻しました"); return true;
}
