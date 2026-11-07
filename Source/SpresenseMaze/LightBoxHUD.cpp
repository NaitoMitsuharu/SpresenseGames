#include "LightBoxGame.h"
#include "LightBoxAmbience.h"
#include "LightBoxControls.h"
#include "LightBoxPaint.h"

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
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Views/SListView.h"
#include "Brushes/SlateNoResource.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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
    // Sensor effects sit between the scene and the controls, edge to edge.
    Ambience=SNew(SLightBoxAmbience).Controller(GetOwningPlayerController());
    GetWorld()->GetGameViewport()->AddViewportWidgetContent(Ambience.ToSharedRef(),35);
    if(auto* PC=GetOwningPlayerController()){PC->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false).SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock));PC->bShowMouseCursor=true;}
}
bool ALightBoxHUD::HasDialog()const{return Dialog.IsValid()||(Controls.IsValid()&&Controls->IsInteracting());}
void ALightBoxHUD::OpenMenu(){EnsureControls();if(Controls.IsValid())Controls->OpenMenu();}
#if !UE_BUILD_SHIPPING
void ALightBoxHUD::ReviewSensors(){EnsureControls();if(Controls.IsValid())Controls->ReviewSensors();}
void ALightBoxHUD::ReviewKeyboard(){EnsureControls();if(Controls.IsValid())Controls->ReviewKeyboard();}
#endif
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
	UpdateSetupDialog();
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
	else if (Name == TEXT("Settings") || Name == TEXT("Auth")) { OpenSettings(0); }
	else if (Name == TEXT("Model")) { OpenSettings(1); }
	else if (Name == TEXT("Budget")) { OpenSettings(2); }
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
	bSetupOpen = false;
	if (APlayerController* PC = GetOwningPlayerController()) { PC->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false).SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock)); PC->bShowMouseCursor = true; FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
}

namespace
{
	int32 SetupStage(const FLightBoxReadiness& R)
	{
		if (R.GetStep() == ELightBoxReadyStep::Failed) { return 3; }
		switch (R.GetNeed())
		{
		case ELightBoxSetupNeed::InstallTermux: return 0;
		case ELightBoxSetupNeed::Permission: return 1;
		case ELightBoxSetupNeed::RunSetup: return 2;
		default: return 3;
		}
	}

	FString SetupGuide(const FLightBoxReadiness& R)
	{
		if (R.GetStep() == ELightBoxReadyStep::Failed) { return R.Detail(); }
		switch (R.GetNeed())
		{
		case ELightBoxSetupNeed::InstallTermux:
			return TEXT("言葉で景色を作るには、Termuxアプリが必要です。下のボタンから入手してインストールしてください。インストール後にこの画面へ戻ると、自動で続きを進めます。");
		case ELightBoxSetupNeed::Permission:
			return TEXT("表示された許可ダイアログで「許可」を選んでください。ダイアログが出ないときは、下のボタンを押してください。許可すると自動で続きを進めます。");
		case ELightBoxSetupNeed::RunSetup:
			return TEXT("最初の1回だけ、Termuxでセットアップを行います。\n1. 下のボタンを押すと、セットアップ用の1行がコピーされてTermuxが開きます。\n2. Termuxの画面を長押しして「Paste」を選び、Enterを押します。\n3. Copilotへのログインを案内されたら、画面の指示に従います。\n終わると自動でこのアプリに戻り、続きを進めます。");
		case ELightBoxSetupNeed::CheckTermux:
			return R.Detail();
		default:
			return FString();
		}
	}

	FString SetupAction(const FLightBoxReadiness& R)
	{
		if (R.GetStep() == ELightBoxReadyStep::Failed) { return TEXT("もう一度"); }
		switch (R.GetNeed())
		{
		case ELightBoxSetupNeed::InstallTermux: return TEXT("Termuxを入手");
		case ELightBoxSetupNeed::Permission: return TEXT("許可する");
		case ELightBoxSetupNeed::RunSetup: return TEXT("コピーしてTermuxを開く");
		case ELightBoxSetupNeed::CheckTermux: return TEXT("Termuxを開く");
		default: return TEXT("もう一度");
		}
	}
}

void ALightBoxHUD::UpdateSetupDialog()
{
	ULightBoxDirector* D = GetGameInstance()->GetSubsystem<ULightBoxDirector>();
	const FLightBoxReadiness& R = D->GetReadiness();
	const bool bAsks = R.GetStep() == ELightBoxReadyStep::NeedsSetup || R.GetStep() == ELightBoxReadyStep::Failed;
	if (bSetupOpen && !bAsks)
	{
		CloseDialog();
		return;
	}
	const bool bDismissed = R.GetStep() == ELightBoxReadyStep::Failed ? bDismissedFailure : R.GetNeed() == DismissedNeed;
	if (bAsks && !bDismissed && !Dialog.IsValid()) { OpenSetup(); }
}

void ALightBoxHUD::OpenSetup()
{
	CloseDialog();
	const auto Host = GetWorld()->GetGameViewport()->GetGameLayerManager();
	const float Ratio = Host.IsValid() && H > 0 ? Host->GetViewportWidgetHostGeometry().GetLocalSize().Y / H : 1.f, J = S * Ratio;
	ULightBoxDirector* D = GetGameInstance()->GetSubsystem<ULightBoxDirector>();
	const FSlateFontInfo Font = LightBoxPaint::Font( FMath::RoundToInt(22 * J));
	const FSlateFontInfo Small = LightBoxPaint::Font( FMath::RoundToInt(19 * J));
	const float Width = FMath::Min(W * Ratio * 0.85f, 860 * J);
	const TCHAR* Steps[] = { TEXT("Termuxアプリ"), TEXT("コマンド実行の許可"), TEXT("Termuxの初回セットアップ"), TEXT("演出サービスと認証キー") };
	TSharedRef<SVerticalBox> Checklist = SNew(SVerticalBox);
	for (int32 I = 0; I < 4; ++I)
	{
		Checklist->AddSlot().AutoHeight().Padding(0, 3 * J)
		[
			SNew(STextBlock).Font(Small)
			.ColorAndOpacity_Lambda([D, I]() { return SetupStage(D->GetReadiness()) == I ? FSlateColor(Mint) : FSlateColor(Ink.CopyWithNewOpacity(.7f)); })
			.Text_Lambda([D, I, Label = FString(Steps[I])]()
			{
				const int32 Stage = SetupStage(D->GetReadiness());
				return FText::FromString((I < Stage ? TEXT("✓  ") : I == Stage ? TEXT("▶  ") : TEXT("・  ")) + Label);
			})
		];
	}
	Dialog = SNew(SOverlay)
	+ SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0, 0, 0, .18f))]
	+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		SNew(SBox).WidthOverride(Width)
		[
			SNew(SLightBoxSheet).Padding(28 * J)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12 * J)
				[SNew(STextBlock).Font(Font).ColorAndOpacity(Ink).Text(FText::FromString(TEXT("言葉で景色を作る準備")))]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12 * J)
				[Checklist]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16 * J)
				[SNew(STextBlock).Font(Small).ColorAndOpacity(Ink).AutoWrapText(true)
					.Text_Lambda([D]() { return FText::FromString(SetupGuide(D->GetReadiness())); })]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 12 * J, 0)
					[SNew(SButton).ButtonStyle(LightBoxUI::ButtonStyle()).HAlign(HAlign_Center).ContentPadding(12 * J)
					.OnClicked_Lambda([Weak = TWeakObjectPtr<ALightBoxHUD>(this), D]()
					{
						if (Weak.IsValid())
						{
							const FLightBoxReadiness& R = D->GetReadiness();
							if (R.GetStep() == ELightBoxReadyStep::Failed) { Weak->bDismissedFailure = true; }
							else { Weak->DismissedNeed = R.GetNeed(); }
							Weak->CloseDialog();
						}
						return FReply::Handled();
					})
					[SNew(STextBlock).Font(Font).ColorAndOpacity(Ink).Text(FText::FromString(TEXT("あとで")))]]
					+ SHorizontalBox::Slot().FillWidth(2)
					[SNew(SButton).ButtonStyle(LightBoxUI::ButtonStyle(true)).HAlign(HAlign_Center).ContentPadding(12 * J)
					.OnClicked_Lambda([D]()
					{
						FLightBoxReadiness& R = D->GetReadiness();
						if (R.GetStep() == ELightBoxReadyStep::Failed) { D->BeginReadiness(); }
						else { R.PerformSetupAction(); }
						return FReply::Handled();
					})
					[SNew(STextBlock).Font(Font).ColorAndOpacity(FLinearColor(0.02f, 0.05f, 0.07f))
						.Text_Lambda([D]() { return FText::FromString(SetupAction(D->GetReadiness())); })]]
				]
			]
		]
	];
	AttachDialog();
	bSetupOpen = true;
}

void ALightBoxHUD::EndPlay(const EEndPlayReason::Type Reason)
{
	CloseDialog();
	if(Controls.IsValid()&&GetWorld()&&GetWorld()->GetGameViewport())GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(Controls.ToSharedRef());
	if(Ambience.IsValid()&&GetWorld()&&GetWorld()->GetGameViewport())GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(Ambience.ToSharedRef());
	Ambience.Reset();Controls.Reset();KeyboardGlass(false);Super::EndPlay(Reason);
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
	const FSlateFontInfo Font = LightBoxPaint::Font( FMath::RoundToInt(23 * J));
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
	[SNew(SLightBoxSheet).Padding(28 * J)
	[SNew(SVerticalBox)
	+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10 * J)[SNew(STextBlock).Font(Font).ColorAndOpacity(Ink).Text(FText::FromString(TEXT("景色の見本")))]

	+ SVerticalBox::Slot().FillHeight(1)[List]
	+ SVerticalBox::Slot().AutoHeight().Padding(0, 14 * J, 0, 0)
	[SNew(SButton).ButtonStyle(LightBoxUI::ButtonStyle()).ToolTipText(FText::FromString(TEXT("閉じる"))).HAlign(HAlign_Center).ContentPadding(12 * J)
	.OnClicked_Lambda([Weak=TWeakObjectPtr<ALightBoxHUD>(this)]() { if (Weak.IsValid()) { Weak->CloseDialog(); } return FReply::Handled(); })
	[SNew(SBox).WidthOverride(24*J).HeightOverride(24*J)[LightBoxUI::Icon(TEXT("close"))]]]]]];
	AttachDialog();
}

namespace
{
const FTableViewStyle* AtelierHistoryStyle()
{
    static const FTableViewStyle Style=FTableViewStyle().SetBackgroundBrush(FSlateNoResource());
    return &Style;
}
const FTableRowStyle* AtelierHistoryRowStyle()
{
    static const FTableRowStyle Style=FTableRowStyle(FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row"))
        .SetEvenRowBackgroundBrush(FSlateNoResource()).SetOddRowBackgroundBrush(FSlateNoResource())
        .SetEvenRowBackgroundHoveredBrush(FSlateNoResource()).SetOddRowBackgroundHoveredBrush(FSlateNoResource());
    return &Style;
}
struct FAtelierHistoryItem
{
    FString Id, Title, Asked, Stamp;
};

/** Logical dimensions match the menu; one DPI scale applies to text, icons, glass and hit areas. */
TSharedRef<SWidget> AtelierDialog(TWeakObjectPtr<ALightBoxHUD> Weak,const FString& Title,float Scale,float Width,float Height,TSharedRef<SWidget> Body,TSharedRef<SWidget> Footer)
{
    return SNew(SOverlay)
        +SOverlay::Slot()[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0,0,0,.30f))
            .OnMouseButtonDown_Lambda([Weak](const FGeometry&,const FPointerEvent&){if(Weak.IsValid())Weak->CloseDialog();return FReply::Handled();})]
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
        [SNew(SDPIScaler).DPIScale(Scale)
            [SNew(SBox).WidthOverride(Width).HeightOverride(Height)
                [SNew(SLightBoxSheet).Padding(24)
                    [SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight().Padding(2,0,0,16)[LightBoxUI::DialogHeader(Title,1,[Weak](){if(Weak.IsValid())Weak->CloseDialog();})]
                        +SVerticalBox::Slot().FillHeight(1)[Body]
                        +SVerticalBox::Slot().AutoHeight().Padding(0,16,0,0)[Footer]
                    ]]]];
}
TSharedRef<SWidget> DialogDone(TWeakObjectPtr<ALightBoxHUD> Weak)
{
    return SNew(SBox).HeightOverride(78)
        [SNew(SButton).ButtonStyle(LightBoxUI::MenuStyle()).ContentPadding(16).HAlign(HAlign_Center)
            .OnClicked_Lambda([Weak](){if(Weak.IsValid())Weak->CloseDialog();return FReply::Handled();})
            [SNew(STextBlock).Font(LightBoxPaint::Font(22)).Text(FText::FromString(TEXT("閉じる")))]];
}
}

void ALightBoxHUD::OpenJournal()
{
    CloseDialog();auto* D=GetGameInstance()->GetSubsystem<ULightBoxDirector>();
    const auto Host=GetWorld()->GetGameViewport()->GetGameLayerManager();
    const float Ratio=Host.IsValid()&&H>0?Host->GetViewportWidgetHostGeometry().GetLocalSize().Y/H:1.f;
    const float J=FMath::Max(.3f,S*Ratio),PanelW=FMath::Min(W*Ratio*.9f/J,920.f),PanelH=FMath::Min(H*Ratio*.90f/J,960.f);
    const TWeakObjectPtr<ALightBoxHUD> Weak(this);
    auto Items=MakeShared<TArray<TSharedPtr<FAtelierHistoryItem>>>();
    const auto& Records=D->GetJournal().Records;
    for(int32 I=Records.Num()-1;I>=0&&Items->Num()<40;--I)
    {
        const auto& R=Records[I];if(R.After.IsEmpty())continue;
        // Opening a history list must not rebuild every saved sculpture. Restoration validates it fully.
        TSharedPtr<FJsonObject> Scene;FString Title;
        if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(R.After),Scene)||!Scene.IsValid()||!Scene->TryGetStringField(TEXT("title"),Title))continue;
        auto Item=MakeShared<FAtelierHistoryItem>();Item->Id=R.Id;Item->Title=Title.IsEmpty()?TEXT("無題の景色"):Title;
        FDateTime Utc;Item->Stamp=FDateTime::ParseIso8601(*R.Utc,Utc)?(Utc+FTimespan::FromHours(9)).ToString(TEXT("%m/%d %H:%M")):R.Utc;
        Item->Asked=R.Words.TrimStartAndEnd();
        if(Item->Asked.StartsWith(TEXT("元のお願い"))){int32 A=Item->Asked.Find(TEXT("「")),B=Item->Asked.Find(TEXT("」"));Item->Asked=TEXT("クオリティ調整")+(A>=0&&B>A?TEXT("「")+Item->Asked.Mid(A+1,B-A-1)+TEXT("」"):FString());}
        if(Item->Asked.Len()>44)Item->Asked=Item->Asked.Left(43)+TEXT("…");
        Items->Add(Item);
    }
    TSharedRef<SWidget> Body=SNew(STextBlock).Font(LightBoxPaint::Font(21)).ColorAndOpacity(Ink).Text(FText::FromString(TEXT("まだ景色の履歴はありません")));
    if(!Items->IsEmpty())
    {
        Body=SNew(SListView<TSharedPtr<FAtelierHistoryItem>>).ListViewStyle(AtelierHistoryStyle()).ListItemsSource(&Items.Get()).SelectionMode(ESelectionMode::None).ItemHeight(148).AllowOverscroll(EAllowOverscroll::No)
            .OnGenerateRow_Lambda([Weak,Items,PanelW](TSharedPtr<FAtelierHistoryItem> Item,const TSharedRef<STableViewBase>& ListOwner)
            {
                return SNew(STableRow<TSharedPtr<FAtelierHistoryItem>>,ListOwner).Style(AtelierHistoryRowStyle()).Padding(FMargin(0,0,0,10))
                    [SNew(SBox).MinDesiredHeight(148)
                        [SNew(SButton).ButtonStyle(LightBoxUI::MenuStyle()).TouchMethod(EButtonTouchMethod::PreciseTap).ContentPadding(FMargin(20,16)).HAlign(HAlign_Fill)
                            .OnClicked_Lambda([Weak,Id=Item->Id](){if(Weak.IsValid()){Weak->CloseDialog();Weak->GetGameInstance()->GetSubsystem<ULightBoxDirector>()->RestoreRecord(Id);}return FReply::Handled();})
                            [SNew(SHorizontalBox)
                                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                                [SNew(SVerticalBox)
                                    +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(LightBoxPaint::Spaced(23,30)).ColorAndOpacity(Ink).OverflowPolicy(ETextOverflowPolicy::Ellipsis).Text(FText::FromString(Item->Title))]
                                    +SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)[SNew(STextBlock).Font(LightBoxPaint::Font(19)).WrapTextAt(PanelW-240).ColorAndOpacity(Ink.CopyWithNewOpacity(.8f)).Text(FText::FromString(Item->Asked))]
                                    +SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)[SNew(STextBlock).Font(LightBoxPaint::Font(17)).ColorAndOpacity(Ink.CopyWithNewOpacity(.65f)).Text(FText::FromString(Item->Stamp))]
                                ]
                                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(20,0,0,0)
                                [SNew(STextBlock).Font(LightBoxPaint::Font(20)).ColorAndOpacity(Mint).Text(FText::FromString(TEXT("呼び出す ›")))]
                            ]]];
            });
    }
    auto Footer=SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(4,0,4,12)[SNew(STextBlock).Font(LightBoxPaint::Font(18)).ColorAndOpacity(Ink.CopyWithNewOpacity(.7f)).AutoWrapText(true).Text_Lambda([D](){return FText::FromString(TEXT("AI利用量（概算）　")+D->GetJournal().Summary());})]
        +SVerticalBox::Slot().AutoHeight()[DialogDone(Weak)];
    Dialog=AtelierDialog(Weak,TEXT("履歴"),J,PanelW,PanelH,Body,Footer);AttachDialog();
}

void ALightBoxHUD::OpenSettings(int32 Section)
{
    CloseDialog();Section=FMath::Clamp(Section,0,2);
    const auto Host=GetWorld()->GetGameViewport()->GetGameLayerManager();
    const float Ratio=Host.IsValid()&&H>0?Host->GetViewportWidgetHostGeometry().GetLocalSize().Y/H:1.f,J=FMath::Max(.3f,S*Ratio);
    const float Width=FMath::Min(W*Ratio*.9f/J,Section==0?800.f:680.f),Height=FMath::Min(H*Ratio*.90f/J,Section==1?760.f:700.f);
    auto* D=GetGameInstance()->GetSubsystem<ULightBoxDirector>();const TWeakObjectPtr<ALightBoxHUD> Weak(this);
    const auto Font=LightBoxPaint::Font(22),Small=LightBoxPaint::Font(19);
    auto Body=SNew(SVerticalBox);TSharedRef<SWidget> Footer=DialogDone(Weak);TSharedPtr<SEditableTextBox> Url;
    static const TCHAR* Titles[]={TEXT("接続設定"),TEXT("AIモデル"),TEXT("粒子上限")};
    if(Section==1)
    {
        Body->AddSlot().AutoHeight().Padding(4,0,4,16)[SNew(STextBlock).Font(Small).ColorAndOpacity(Ink.CopyWithNewOpacity(.8f)).AutoWrapText(true).Text(FText::FromString(TEXT("景色づくりに使うAIを選びます")))];
        for(const auto& Choice:ULightBoxDirector::ModelChoices())
        {
            const FString Id=Choice.Id,Name=Choice.Label;auto IsOn=[Weak,Id](){return Weak.IsValid()&&Weak->GetGameInstance()->GetSubsystem<ULightBoxDirector>()->GetModel()==Id;};
            Body->AddSlot().AutoHeight().Padding(0,0,0,10)
                [SNew(SBorder).BorderImage_Lambda([IsOn](){return LightBoxUI::CardBrush(IsOn());}).Padding(0)
                    [SNew(SBox).HeightOverride(78)
                        [SNew(SButton).ButtonStyle(LightBoxUI::MenuStyle()).TouchMethod(EButtonTouchMethod::PreciseTap).ContentPadding(FMargin(20,12)).HAlign(HAlign_Fill)
                            .OnClicked_Lambda([Weak,Id](){if(Weak.IsValid())Weak->GetGameInstance()->GetSubsystem<ULightBoxDirector>()->SetModel(Id);return FReply::Handled();})
                            [SNew(SHorizontalBox)
                                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Font(Font).ColorAndOpacity_Lambda([IsOn](){return IsOn()?Mint:Ink;}).Text(FText::FromString(Name))]
                                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(28).HeightOverride(28).Visibility_Lambda([IsOn](){return IsOn()?EVisibility::HitTestInvisible:EVisibility::Hidden;})[LightBoxUI::Icon(TEXT("check"))]]
                            ]]]];
        }
    }
    else if(Section==2)
    {
        Body->AddSlot().AutoHeight().Padding(0,0,0,18)
            [SNew(SBorder).BorderImage(LightBoxUI::CardBrush(true)).Padding(24)
                [SNew(SVerticalBox)
                    +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(Small).ColorAndOpacity(Ink).Text(FText::FromString(TEXT("この端末の粒子上限")))]
                    +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,0)[SNew(STextBlock).Font(LightBoxPaint::Spaced(36,30)).ColorAndOpacity(Mint).Text_Lambda([D](){return FText::FromString(FText::AsNumber(D->PointBudget).ToString()+TEXT(" 粒"));})]
                ]];
        Body->AddSlot().AutoHeight().Padding(4,0,4,14)[SNew(STextBlock).Font(Small).ColorAndOpacity(Ink).AutoWrapText(true).LineHeightPercentage(1.15f).Text(FText::FromString(TEXT("この端末で自動測定しています。\n目標の滑らかさ：30〜45 fps")))];
        Body->AddSlot().AutoHeight().Padding(4,0,4,8)[SNew(STextBlock).Font(Small).ColorAndOpacity(Ink.CopyWithNewOpacity(.72f)).AutoWrapText(true).LineHeightPercentage(1.2f).Text_Lambda([D](){return FText::FromString(D->BudgetDetail);})];
        Footer=SNew(SBox).HeightOverride(82)
            [SNew(SButton).ButtonStyle(LightBoxUI::MenuStyle(true)).ContentPadding(16).HAlign(HAlign_Center)
                .OnClicked_Lambda([Weak](){if(Weak.IsValid()){Weak->GetGameInstance()->GetSubsystem<ULightBoxDirector>()->bCalibrationRequested=true;Weak->CloseDialog();}return FReply::Handled();})
                [SNew(STextBlock).Font(Font).Text(FText::FromString(TEXT("性能を測り直す")))]];
    }
    else
    {
        auto Message=SNew(STextBlock).Font(Small).AutoWrapText(true).ColorAndOpacity(Mint);
        Url=SNew(SEditableTextBox).Style(LightBoxUI::FieldStyle()).Font(Font).Text(FText::FromString(D->GetEndpoint()));
        auto Token=SNew(SEditableTextBox).Style(LightBoxUI::FieldStyle()).Font(Font).IsPassword(true).HintText(FText::FromString(D->EnsureCredential()?TEXT("設定済み（変更時のみ入力）"):TEXT("Termuxに表示された認証キー")));
        Body->AddSlot().AutoHeight().Padding(4,0,4,18)[SNew(STextBlock).Font(Small).ColorAndOpacity(Ink.CopyWithNewOpacity(.8f)).AutoWrapText(true).Text(FText::FromString(TEXT("景色づくりのサービスに接続します。認証キーは伏せて表示します。")))];
        Body->AddSlot().AutoHeight().Padding(4,0,0,8)[SNew(STextBlock).Font(Small).ColorAndOpacity(Ink).Text(FText::FromString(TEXT("接続先")))];
        Body->AddSlot().AutoHeight()[SNew(SBox).MinDesiredHeight(72)[Url.ToSharedRef()]];
        Body->AddSlot().AutoHeight().Padding(4,20,0,8)[SNew(STextBlock).Font(Small).ColorAndOpacity(Ink).Text(FText::FromString(TEXT("認証キー")))];
        Body->AddSlot().AutoHeight()[SNew(SBox).MinDesiredHeight(72)[Token]];
        Body->AddSlot().AutoHeight().Padding(4,12,4,0)[Message];
        Footer=SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)
            [SNew(SBox).HeightOverride(82)[SNew(SButton).ButtonStyle(LightBoxUI::MenuStyle()).HAlign(HAlign_Center).ContentPadding(14)
                .OnClicked_Lambda([Weak](){if(Weak.IsValid()){auto* HUD=Weak.Get();HUD->DismissedNeed=ELightBoxSetupNeed::None;HUD->bDismissedFailure=false;HUD->CloseDialog();HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>()->BeginReadiness();}return FReply::Handled();})
                [SNew(STextBlock).Font(Font).Text(FText::FromString(TEXT("自動で準備する")))]]]
            +SHorizontalBox::Slot().FillWidth(1).Padding(6,0,0,0)
            [SNew(SBox).HeightOverride(82)[SNew(SButton).ButtonStyle(LightBoxUI::MenuStyle(true)).HAlign(HAlign_Center).ContentPadding(14)
                .OnClicked_Lambda([Weak,Url,Token,Message](){if(Weak.IsValid()){FString Error;if(Weak->GetGameInstance()->GetSubsystem<ULightBoxDirector>()->Configure(Url->GetText().ToString(),Token->GetText().ToString(),Error)){Token->SetText(FText::GetEmpty());Weak->CloseDialog();}else Message->SetText(FText::FromString(Error));}return FReply::Handled();})
                [SNew(STextBlock).Font(Font).Text(FText::FromString(TEXT("保存する")))]]];
    }
    auto Scroll=SNew(SScrollBox).Style(LightBoxUI::ScrollStyle()).AllowOverscroll(EAllowOverscroll::No).ScrollBarThickness(FVector2D(5,5))+SScrollBox::Slot()[Body];
    Dialog=AtelierDialog(Weak,Titles[Section],J,Width,Height,Scroll,Footer);AttachDialog();
}
