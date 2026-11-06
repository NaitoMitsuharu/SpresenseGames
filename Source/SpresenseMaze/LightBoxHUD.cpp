#include "LightBoxGame.h"
#include "LightBoxControls.h"

#include "AttitudeInputSubsystem.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "ExperienceUI.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "LightBoxDirector.h"
#include "Slate/SGameLayerManager.h"
#include "Styling/CoreStyle.h"
#include "TitleGameMode.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SViewport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#include "Android/AndroidJNI.h"
#endif

namespace
{
	const FLinearColor Ink(0.78f, 0.9f, 0.94f);
	const FLinearColor Mint(0.36f, 0.94f, 0.81f);
    void KeyboardGlass(bool Enabled) {
#if PLATFORM_ANDROID
        if(JNIEnv* Env=FAndroidApplication::GetJavaEnv()) {
            const jmethodID Method=FJavaWrapper::FindMethod(Env,FJavaWrapper::GameActivityClassID,"AndroidThunkJava_LightBoxGlassKeyboard","(Z)V",false);
            if(Method)FJavaWrapper::CallVoidMethod(Env,FJavaWrapper::GameActivityThis,Method,jboolean(Enabled));
        }
#endif
    }
}

void ALightBoxHUD::EnsureControls()
{
    if(Controls.IsValid()||!GetWorld()->GetGameViewport())return;
    KeyboardGlass(true);
    Controls=SNew(SLightBoxControls).HUD(TWeakObjectPtr<ALightBoxHUD>(this));
    GetWorld()->GetGameViewport()->AddViewportWidgetContent(Controls.ToSharedRef(),40);
    if(auto* PC=GetOwningPlayerController()){PC->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false).SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock));PC->bShowMouseCursor=true;}
}
bool ALightBoxHUD::HasDialog()const{return Dialog.IsValid()||(Controls.IsValid()&&Controls->IsInteracting());}
void ALightBoxHUD::OpenMenu(){EnsureControls();if(Controls.IsValid())Controls->OpenMenu();}
void ALightBoxHUD::AttachDialog(TSharedPtr<SWidget> Focus)
{
    Dialog=LightBoxUI::AnimateDialog(TWeakObjectPtr<ALightBoxHUD>(this),Dialog.ToSharedRef());
    GetWorld()->GetGameViewport()->AddViewportWidgetContent(Dialog.ToSharedRef(),100);
    FInputModeUIOnly Mode;Mode.SetWidgetToFocus(Focus.IsValid()?Focus:Dialog);GetOwningPlayerController()->SetInputMode(Mode);
    FSlateApplication::Get().SetKeyboardFocus(Focus.IsValid()?Focus:Dialog,EFocusCause::SetDirectly);
}

void ALightBoxHUD::DrawHUD()
{
	Super::DrawHUD(); if (!Canvas) { return; }
	EnsureControls();
	DrawSensorChips();
}

void ALightBoxHUD::DrawSensorChips()
{
	const ALightBoxPawn* Pawn = Cast<ALightBoxPawn>(GetOwningPawn());
	if (!Pawn || Dialog.IsValid()) { return; }
	const FLightBoxSensorReading& R = Pawn->GetSensorReading();
	if (R.bClap) { ClapShown = 0.8f; }
	ClapShown = FMath::Max(0.f, ClapShown - GetWorld()->GetDeltaSeconds());
	struct FChip { const TCHAR* Label; float Level; FLinearColor Color; };
	const FChip Chips[] = {
		{ TEXT("息"), R.Breath, FLinearColor(0.55f,0.9f,1.f) },
		{ TEXT("拍手"), ClapShown/0.8f, FLinearColor(1.f,1.f,1.f) },
		{ TEXT("ぬくもり"), R.Warmth, FLinearColor(1.f,0.62f,0.3f) },
		{ TEXT("暗がり"), R.Darkness, FLinearColor(0.62f,0.58f,1.f) },
		{ TEXT("まぶしい"), R.Brightness, FLinearColor(1.f,0.95f,0.6f) },
		{ TEXT("手"), R.Near, FLinearColor(0.55f,1.f,0.8f) },
		{ TEXT("磁石"), R.Magnet, FLinearColor(1.f,0.45f,0.6f) },
	};
	// Only what is reacting right now, centred just above the scene's bottom edge.
	const float Size = 17.f, Pad = 12*S, GapX = 10*S, ChipH = 32*S;
	float Total = 0; int32 Count = 0;
	for (const FChip& C : Chips) { if (C.Level > 0.06f) { Total += TextSize(C.Label, Size).X + Pad*2 + 14*S; ++Count; } }
	if (!Count) { return; }
	Total += GapX * (Count - 1);
	float X = (W - Total) * 0.5f;
	const float Y = H * 0.72f;
	for (const FChip& C : Chips)
	{
		if (C.Level <= 0.06f) { continue; }
		const float CW = TextSize(C.Label, Size).X + Pad*2 + 14*S, A = FMath::Clamp(C.Level, 0.f, 1.f);
		ExperienceUI::RoundRect(Canvas, X, Y, CW, ChipH, ChipH*0.5f, FLinearColor(0.03f,0.06f,0.09f,0.35f+0.3f*A));
		ExperienceUI::RoundRect(Canvas, X+ChipH*0.3f, Y+ChipH-3*S, (CW-ChipH*0.6f)*A, 3*S, 1.5f*S, C.Color.CopyWithNewOpacity(0.85f));
		Dot(X+Pad+4*S, Y+ChipH*0.5f, 4*S, C.Color.CopyWithNewOpacity(0.5f+0.5f*A));
		Text(C.Label, FLinearColor(0.95f,0.98f,1.f,0.7f+0.3f*A), X+Pad+14*S, Y+ChipH*0.5f-Size*0.62f*S, Size, false);
		X += CW + GapX;
	}
}

void ALightBoxHUD::NotifyHitBoxClick(FName Name)
{
	Super::NotifyHitBoxClick(Name); if (Dialog.IsValid()) { return; }
	ULightBoxDirector* D = GetGameInstance()->GetSubsystem<ULightBoxDirector>();
	if (Name == TEXT("Home")) { D->Cancel(); UGameplayStatics::OpenLevel(this, ATitleGameMode::TitleMapName); }
	else if (Name == TEXT("Preset0")) { D->SelectPreset(0); }
	else if (Name == TEXT("Preset1")) { D->SelectPreset(1); }
	else if (Name == TEXT("Preset2")) { D->SelectPreset(2); }
	else if (Name == TEXT("Save")) { D->SaveScene(); }
	else if (Name == TEXT("Restore")) { D->RestoreScene(); }
	else if (Name == TEXT("Neutral")) { GetGameInstance()->GetSubsystem<UAttitudeInputSubsystem>()->ResetNeutral(); }
	else if (Name == TEXT("Orbit")) { if(auto* P=Cast<ALightBoxPawn>(GetOwningPlayerController()->GetPawn()))P->ToggleCameraOrbit(); }
	else if (Name == TEXT("ViewReset")) { if(auto* P=Cast<ALightBoxPawn>(GetOwningPlayerController()->GetPawn()))P->ResetCameraView(); }
	else if (Name == TEXT("Settings")) { OpenSettings(); }
	else if (Name == TEXT("Words")) { OpenWords(); }
	else if (Name == TEXT("Examples")) { OpenExamples(); }
	else if (Name == TEXT("Journal")) { OpenJournal(); }
    else if (Name == TEXT("Refine")) { D->RefineScene(); }
	else if (Name == TEXT("Undo")) { D->Undo(); }
	else if (Name == TEXT("Redo")) { D->Redo(); }
	else if (Name == TEXT("Voice"))
	{
#if PLATFORM_ANDROID
		D->StartVoice();
#else
		if (D->bThinking) { D->Cancel(); D->Status = TEXT("お願いを取り消しました"); }
		else { OpenWords(); }
#endif
	}
}

void ALightBoxHUD::CloseDialog()
{
	if (Dialog.IsValid() && GetWorld() && GetWorld()->GetGameViewport())
	{
		GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(Dialog.ToSharedRef());
	}
	Dialog.Reset();
	if (APlayerController* PC = GetOwningPlayerController()) { PC->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false).SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock)); PC->bShowMouseCursor = true; FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
}

void ALightBoxHUD::EndPlay(const EEndPlayReason::Type Reason)
{
	CloseDialog();
	if(Controls.IsValid()&&GetWorld()&&GetWorld()->GetGameViewport())GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(Controls.ToSharedRef());
	Controls.Reset();KeyboardGlass(false);Super::EndPlay(Reason);
}

void ALightBoxHUD::OpenWords()
{
    CloseDialog();EnsureControls();if(Controls.IsValid())Controls->FocusInput();
}

void ALightBoxHUD::OpenExamples()
{
	CloseDialog();
	const auto LayersHost=GetWorld()->GetGameViewport()->GetGameLayerManager();
	const float Ratio=LayersHost.IsValid()&&H>0?LayersHost->GetViewportWidgetHostGeometry().GetLocalSize().Y/H:1;
	const float J=S*Ratio;
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", FMath::RoundToInt(23 * J));
	TSharedRef<SUniformGridPanel> List=SNew(SUniformGridPanel).SlotPadding(FMargin(6*J));

	for (int32 I = 0; I < 13; ++I)
	{
		const int32 Index=I<6?I+7:I-6;
		const FLightBoxStyle Example = FLightBoxStyle::Example(Index);
		List->AddSlot(I%3,I/3)
		[
			SNew(SButton).ButtonStyle(LightBoxUI::ButtonStyle()).ContentPadding(18 * J).HAlign(HAlign_Left).VAlign(VAlign_Center)
			.OnClicked_Lambda([Weak = TWeakObjectPtr<ALightBoxHUD>(this), Index]()
			{
				if (Weak.IsValid()) { Weak->CloseDialog(); Weak->GetGameInstance()->GetSubsystem<ULightBoxDirector>()->SelectExample(Index); }
				return FReply::Handled();
			})
			[SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font).Text(FText::FromString(Example.Title))]
			]
		];
	}
	Dialog = SNew(SOverlay)
	+ SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0,0,0,.18f)).OnMouseButtonDown_Lambda([Weak=TWeakObjectPtr<ALightBoxHUD>(this)](const FGeometry&,const FPointerEvent&){if(Weak.IsValid())Weak->CloseDialog();return FReply::Handled();})]
	+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
	[SNew(SBox).WidthOverride(FMath::Min(W * Ratio * .88f, 1600 * J)).HeightOverride(H * Ratio * .84f)
	[SNew(SBorder).Padding(28 * J).BorderImage(ExperienceUI::ModalBrush()).BorderBackgroundColor(ExperienceUI::Ink.CopyWithNewOpacity(.78f))
	[SNew(SVerticalBox)
	+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10 * J)[SNew(STextBlock).Font(Font).ColorAndOpacity(Ink).Text(FText::FromString(TEXT("景色の見本")))]

	+ SVerticalBox::Slot().FillHeight(1)[List]
	+ SVerticalBox::Slot().AutoHeight().Padding(0, 14 * J, 0, 0)
	[SNew(SButton).ButtonStyle(LightBoxUI::ButtonStyle()).ToolTipText(FText::FromString(TEXT("閉じる"))).HAlign(HAlign_Center).ContentPadding(12 * J)
	.OnClicked_Lambda([Weak=TWeakObjectPtr<ALightBoxHUD>(this)]() { if (Weak.IsValid()) { Weak->CloseDialog(); } return FReply::Handled(); })
	[SNew(SBox).WidthOverride(24*J).HeightOverride(24*J)[LightBoxUI::Icon(TEXT("close"))]]]]]];
	AttachDialog();
}

void ALightBoxHUD::OpenJournal()
{
	CloseDialog(); auto* D=GetGameInstance()->GetSubsystem<ULightBoxDirector>();
	// The widget host is below the game-layer DPI scaler; the SViewport is above it.
	const auto LayerManager=GetWorld()->GetGameViewport()->GetGameLayerManager();
	const float HostHeight=LayerManager.IsValid()?LayerManager->GetViewportWidgetHostGeometry().GetLocalSize().Y:0.f;
	const float Ratio=H>0&&HostHeight>0?HostHeight/H:1.f;
	const float J=S*Ratio;
	const FSlateFontInfo Font=FCoreStyle::GetDefaultFontStyle("Regular",FMath::RoundToInt(20*J));
	const FSlateFontInfo Small=FCoreStyle::GetDefaultFontStyle("Regular",FMath::RoundToInt(16*J));
	const float PanelW=FMath::Min(W*.84f*Ratio,1120*J),Wrap=PanelW-64*J;
	TSharedRef<SVerticalBox> List=SNew(SVerticalBox);
	const auto& Records=D->GetJournal().Records;
	for(int32 I=Records.Num()-1;I>=FMath::Max(0,Records.Num()-40);--I)
	{
		const FLightBoxRecord& R=Records[I]; FString Stamp=R.Utc; FDateTime Utc;
		if(FDateTime::ParseIso8601(*R.Utc,Utc)) Stamp=(Utc+FTimespan::FromHours(9)).ToString(TEXT("%m/%d %H:%M:%S"));
		const FString Cost=R.Credits>=0?FString::Printf(TEXT("推定 %.4f cr"),R.Credits):TEXT("クレジット未取得");
		const FString Tokens=R.Total>=0?FString::Printf(TEXT("%.0f tokens（入力 %.0f / 出力 %.0f / キャッシュ読取 %.0f・書込 %.0f）"),R.Total,R.Input,R.Output,R.CacheRead,R.CacheWrite):TEXT("トークン内訳は未取得");
		TSharedRef<SVerticalBox> Row=SNew(SVerticalBox);
		Row->AddSlot().AutoHeight()[SNew(STextBlock).Font(Small).ColorAndOpacity(Mint).Text(FText::FromString(Stamp+TEXT("  /  ")+R.Result+(R.bRepair?TEXT("  /  自動修正"):TEXT(""))))];
		Row->AddSlot().AutoHeight().Padding(0,7*J,0,4*J)[SNew(STextBlock).Font(Font).WrapTextAt(Wrap-30*J).Text(FText::FromString(R.Words))];
		Row->AddSlot().AutoHeight()[SNew(STextBlock).Font(Small).WrapTextAt(Wrap-30*J).ColorAndOpacity(ExperienceUI::Muted).Text(FText::FromString(R.Difference.IsEmpty()?TEXT("変更前の景色は未記録"):R.Difference))];
		Row->AddSlot().AutoHeight().Padding(0,6*J,0,0)[SNew(STextBlock).Font(Small).WrapTextAt(Wrap-30*J).Text(FText::FromString(Cost+TEXT("  /  ")+Tokens))];
		if(!R.Model.IsEmpty()) Row->AddSlot().AutoHeight()[SNew(STextBlock).Font(Small).ColorAndOpacity(ExperienceUI::Muted).Text(FText::FromString(R.Model+TEXT("  単価確認 ")+R.PricingDate))];
		FLightBoxStyle Test; FString Error;
		if(FLightBoxStyle::Parse(R.After,Test,Error))
		{
			Row->AddSlot().AutoHeight().Padding(0,8*J,0,0)
			[SNew(SButton).ButtonStyle(LightBoxUI::ButtonStyle()).ContentPadding(8*J)
			.OnClicked_Lambda([Weak=TWeakObjectPtr<ALightBoxHUD>(this),Id=R.Id]() { if(Weak.IsValid()) { Weak->CloseDialog(); Weak->GetGameInstance()->GetSubsystem<ULightBoxDirector>()->RestoreRecord(Id); } return FReply::Handled(); })
			[SNew(STextBlock).Font(Small).Text(FText::FromString(TEXT("この景色を呼び出す（追加消費なし）")))]];
		}
		List->AddSlot().AutoHeight().Padding(0,0,0,12*J)[SNew(SBorder).BorderImage(ExperienceUI::ModalBrush()).BorderBackgroundColor(FLinearColor(.035f,.065f,.08f)).Padding(14*J)[Row]];
	}
	Dialog=SNew(SOverlay)
	+SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0,0,0,.18f)).OnMouseButtonDown_Lambda([Weak=TWeakObjectPtr<ALightBoxHUD>(this)](const FGeometry&,const FPointerEvent&){if(Weak.IsValid())Weak->CloseDialog();return FReply::Handled();})]
	+SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
	[SNew(SBox).WidthOverride(PanelW).HeightOverride(H*.84f*Ratio)
	[SNew(SBorder).Padding(28*J).BorderImage(ExperienceUI::ModalBrush()).BorderBackgroundColor(ExperienceUI::Ink.CopyWithNewOpacity(.78f))
	[SNew(SVerticalBox)
	+SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Font).Text(FText::FromString(TEXT("景色の記録・AI利用量"))).ColorAndOpacity(Ink)]
	+SVerticalBox::Slot().AutoHeight().Padding(0,10*J,0,5*J)[SNew(STextBlock).Font(Font).ColorAndOpacity(Mint).Text_Lambda([D](){return FText::FromString(D->GetJournal().Summary());})]
	+SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12*J)[SNew(STextBlock).Font(Small).WrapTextAt(Wrap).ColorAndOpacity(ExperienceUI::Muted)
	.Text(FText::FromString(TEXT("公表単価による概算で、確定請求額・アカウント残高ではありません。\n開発検証・自動修正・適用失敗を含む最大300回の記録分。未取得は0にしません。")))]
	+SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[List]]
	+SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Small).WrapTextAt(Wrap).ColorAndOpacity(ExperienceUI::Muted).Text_Lambda([D](){return FText::FromString(D->JournalStatus);})]
	+SVerticalBox::Slot().AutoHeight().Padding(0,12*J,0,0)
	[SNew(SHorizontalBox)
	+SHorizontalBox::Slot().FillWidth(1).Padding(0,0,12*J,0)[SNew(SButton).ButtonStyle(LightBoxUI::ButtonStyle()).HAlign(HAlign_Center).ContentPadding(10*J)
	.OnClicked_Lambda([Weak=TWeakObjectPtr<ALightBoxHUD>(this)](){ if(Weak.IsValid()) { Weak->GetGameInstance()->GetSubsystem<ULightBoxDirector>()->SyncJournal(); Weak->CloseDialog(); } return FReply::Handled(); })[SNew(STextBlock).Font(Font).Text(FText::FromString(TEXT("更新して閉じる")))]]
	+SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).ButtonStyle(LightBoxUI::ButtonStyle(true)).HAlign(HAlign_Center).ContentPadding(10*J)
	.OnClicked_Lambda([Weak=TWeakObjectPtr<ALightBoxHUD>(this)](){if(Weak.IsValid()) Weak->CloseDialog();return FReply::Handled();})[SNew(SBox).WidthOverride(24*J).HeightOverride(24*J)[LightBoxUI::Icon(TEXT("close"))]]]]]]];
	AttachDialog();
}

void ALightBoxHUD::OpenSettings()
{
	CloseDialog();
	const auto Host=GetWorld()->GetGameViewport()->GetGameLayerManager();
	const float Ratio=Host.IsValid()&&H>0?Host->GetViewportWidgetHostGeometry().GetLocalSize().Y/H:1.f,J=S*Ratio;
	ULightBoxDirector* D = GetGameInstance()->GetSubsystem<ULightBoxDirector>();
	TSharedPtr<SEditableTextBox> Url, Token;
	TSharedPtr<STextBlock> Message;
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", FMath::RoundToInt(22 * J));
	Url = SNew(SEditableTextBox).Style(LightBoxUI::FieldStyle()).Font(Font).Text(FText::FromString(D->GetEndpoint()));
	Token = SNew(SEditableTextBox).Style(LightBoxUI::FieldStyle()).Font(Font).IsPassword(true).HintText(FText::FromString(D->HasCredentials()?TEXT("設定済み（変更するときだけ入力）"):TEXT("Termuxに表示された認証キー")));
	Message = SNew(STextBlock).Font(Font).WrapTextAt(FMath::Min(W * Ratio * 0.85f, 800 * J) - 56 * J).ColorAndOpacity(Mint)
		.Text(FText::FromString(TEXT("")));
	Dialog = SNew(SOverlay)
	+ SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0,0,0,.18f)).OnMouseButtonDown_Lambda([Weak=TWeakObjectPtr<ALightBoxHUD>(this)](const FGeometry&,const FPointerEvent&){if(Weak.IsValid())Weak->CloseDialog();return FReply::Handled();})]
	+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		SNew(SBox).WidthOverride(FMath::Min(W * Ratio * 0.85f, 800 * J))
		[
			SNew(SBorder).Padding(28 * J).BorderImage(ExperienceUI::ModalBrush()).BorderBackgroundColor(ExperienceUI::Ink.CopyWithNewOpacity(.78f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14 * J)
				[SNew(STextBlock).Font(Font).ColorAndOpacity(Ink).Text(FText::FromString(TEXT("接続設定")))]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6 * J)
				[SNew(STextBlock).Font(Font).ColorAndOpacity(Ink).Text(FText::FromString(TEXT("接続先")))]
				+ SVerticalBox::Slot().AutoHeight()
				[Url.ToSharedRef()]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 14 * J, 0, 6 * J)
				[SNew(STextBlock).Font(Font).ColorAndOpacity(Ink).Text(FText::FromString(TEXT("認証キー")))]
				+ SVerticalBox::Slot().AutoHeight()
				[Token.ToSharedRef()]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 12 * J)
				[Message.ToSharedRef()]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 12 * J, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 12 * J, 0)
					[SNew(SButton).ButtonStyle(LightBoxUI::ButtonStyle()).HAlign(HAlign_Center).ContentPadding(12 * J)
					.OnClicked_Lambda([Weak = TWeakObjectPtr<ALightBoxHUD>(this)]() { if (Weak.IsValid()) { Weak->CloseDialog(); } return FReply::Handled(); })
					[SNew(SBox).WidthOverride(24*J).HeightOverride(24*J)[LightBoxUI::Icon(TEXT("close"))]]]
					+ SHorizontalBox::Slot().FillWidth(1)
					[SNew(SButton).ButtonStyle(LightBoxUI::ButtonStyle(true)).HAlign(HAlign_Center).ContentPadding(12 * J)
					.OnClicked_Lambda([Weak = TWeakObjectPtr<ALightBoxHUD>(this), Url, Token, Message]()
					{
						if (Weak.IsValid())
						{
							FString Error;
							if (Weak->GetGameInstance()->GetSubsystem<ULightBoxDirector>()->Configure(Url->GetText().ToString(), Token->GetText().ToString(), Error)) { Token->SetText(FText::GetEmpty()); Weak->CloseDialog(); }
							else { Message->SetText(FText::FromString(Error)); }
						}
						return FReply::Handled();
					})[SNew(SBox).WidthOverride(24*J).HeightOverride(24*J)[LightBoxUI::Icon(TEXT("check"))]]]
				]
			]
		]
	];
	AttachDialog();
}
