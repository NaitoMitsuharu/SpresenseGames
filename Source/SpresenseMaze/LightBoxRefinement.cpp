#include "LightBoxDirector.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "ImageUtils.h"
#include "Misc/Base64.h"

FString ULightBoxDirector::ProgressLabel() const
{
    if(!bThinking&&!bListening)return FString();
    const double Start=bListening?VoiceStarted:OperationStarted;
    return FString::Printf(TEXT("%s · %d秒"),*Status,FMath::Max(0,FMath::FloorToInt(FPlatformTime::Seconds()-Start)));
}
FString ULightBoxDirector::CurrentCreditsLabel() const
{
    if(UsageGroup.IsEmpty())return TEXT("今回 — cr");
    double Sum=0;int32 Known=0,Unknown=0;
    for(const auto& R:Journal.Records)if((R.Group.IsEmpty()?R.Id:R.Group)==UsageGroup){if(R.Credits>=0){Sum+=R.Credits;++Known;}else ++Unknown;}
    if(!Known)return TEXT("今回 集計中");
    return FString::Printf(TEXT("今回 約%.4f cr%s"),Sum,Unknown?TEXT("＋集計中"):TEXT(""));
}
void ULightBoxDirector::RefineScene()
{
    if(bThinking||bListening)return;
    if(!EnsureCredential()){Status=TEXT("接続設定で認証キーを入力してください");return;}
    Cancel();OperationStarted=ReviewCaptureAt=FPlatformTime::Seconds();bThinking=bCapturingReview=true;
    Status=TEXT("造形を確認する画面を準備中");const uint64 RequestGeneration=Generation;
    ReviewScreenshotHandle=UGameViewportClient::OnScreenshotCaptured().AddWeakLambda(this,[this,RequestGeneration](int32 W,int32 H,const TArray<FColor>& Pixels){
        if(Generation!=RequestGeneration||!bCapturingReview)return;
        UGameViewportClient::OnScreenshotCaptured().Remove(ReviewScreenshotHandle);ReviewScreenshotHandle.Reset();bCapturingReview=false;
        if(W<=0||H<=0||Pixels.Num()!=W*H){bThinking=false;Status=TEXT("画面を取得できませんでした");return;}
        const double Scale=FMath::Min(1.,1280./FMath::Max(W,H));const int32 Width=FMath::Max(1,FMath::RoundToInt(W*Scale)),Height=FMath::Max(1,FMath::RoundToInt(H*Scale));
        TArray<FColor> Small;FImageUtils::ImageResize(W,H,Pixels,Width,Height,Small,true);
        TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Width,Height,Small,Png);
        if(Png.IsEmpty()){bThinking=false;Status=TEXT("確認用の画像を作れませんでした");return;}
        ReviewImage=TEXT("data:image/png;base64,")+FBase64::Encode(Png.GetData(),int32(Png.Num()));
        Heard=TEXT("この景色の造形を磨いて");Status=TEXT("AIが画面を見て造形を確認中");
        RequestScene(TEXT("添付画像は現在表示中の粒子の景色です。Current scene JSONと照合し、実際に見えるシルエット、頭身・比率、接続、顔、模様、構図を評価して改善してください。題材と主要な動作は維持し、特徴の不足・不自然な部位を優先して直してください。見えない部分を確認済みと主張せず、通常モデルや背景板は使わず、すべて粒子のままにしてください。施設は知識に基づく解釈であり実物再現を保証しません。変更理由をnoteに短く記載。完全なコンパクトJSONのみ返してください。"));
    });
    FScreenshotRequest::RequestScreenshot(TEXT(""),false,false);
}
