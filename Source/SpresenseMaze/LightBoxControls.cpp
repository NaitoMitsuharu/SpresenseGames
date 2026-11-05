#include "LightBoxControls.h"
#include "LightBoxGame.h"
#include "LightBoxPointCloud.h"
#include "LightBoxDirector.h"
#include "ExperienceUI.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElementTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#include "Android/AndroidJNI.h"
#endif

namespace {
const FLinearColor Paper(.94f,.98f,1.f), Mint(.42f,.94f,.80f);
const FSlateBrush* Glass()
{
    static const FSlateRoundedBoxBrush Brush(FLinearColor::White,24.f,Paper.CopyWithNewOpacity(.20f),1.f);
    return &Brush;
}

// Vector strokes avoid missing emoji/icon fonts on Android and stay sharp at every DPI.
class SLightBoxIcon final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SLightBoxIcon) : _Listening(false) {} SLATE_ARGUMENT(FString, Name) SLATE_ATTRIBUTE(bool, Listening) SLATE_END_ARGS()
    void Construct(const FArguments& Args) { Name=Args._Name;Listening=Args._Listening; }
    virtual void Tick(const FGeometry& G,double T,float Dt) override {SLeafWidget::Tick(G,T,Dt);if(Listening.Get())Invalidate(EInvalidateWidgetReason::Paint);}
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(26); }
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 L,const FWidgetStyle& Style,bool Enabled) const override
    {
        const FVector2D Size=G.GetLocalSize();const double Unit=FMath::Min(Size.X,Size.Y)/24.*(Name==TEXT("voice")?34./72.:1.);const FVector2D Offset=(Size-FVector2D(Unit*24))*.5;
        const FLinearColor Color=Style.GetColorAndOpacityTint()*Style.GetForegroundColor()*FLinearColor(1,1,1,Enabled?1.f:.28f);
        auto Line=[&](std::initializer_list<FVector2D> P){TArray<FVector2D> Points;for(auto V:P)Points.Add(Offset+V*Unit);FSlateDrawElement::MakeLines(Out,L,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,float(Unit*1.65));};
        auto Arc=[&](FVector2D C,double R,double Start,double End){TArray<FVector2D> P;for(int32 I=0;I<=28;++I){const double A=FMath::Lerp(Start,End,I/28.)*PI/180;P.Add(Offset+(C+FVector2D(FMath::Cos(A),FMath::Sin(A))*R)*Unit);}FSlateDrawElement::MakeLines(Out,L,G.ToPaintGeometry(),P,ESlateDrawEffect::None,Color,true,float(Unit*1.65));};
        if(Name==TEXT("menu")) {Line({{4,6},{20,6}});Line({{4,12},{20,12}});Line({{4,18},{20,18}});}
        else if(Name==TEXT("close")) {Line({{6,6},{18,18}});Line({{18,6},{6,18}});}
        else if(Name==TEXT("send")) {Line({{4,12},{19,12}});Line({{13,6},{19,12},{13,18}});}
        else if(Name==TEXT("stop")) Line({{6,6},{18,6},{18,18},{6,18},{6,6}});
        else if(Name==TEXT("check")) Line({{5,12},{10,17},{20,6}});
        else if(Name==TEXT("loading")) {
            const double Start=FPlatformTime::Seconds()*220.;
            Arc({12,12},8,Start,Start+285.);
        }
        else if(Name==TEXT("voice")) {
            // Match Spright's exhibition button: filled white mic, no plate, listening ring only.
            static const FSlateRoundedBoxBrush Capsule(FLinearColor::White,3.f);
            static const FSlateBrush White=*FCoreStyle::Get().GetBrush("WhiteBrush");
            auto Fill=[&](FVector2D P,FVector2D Extent,const FSlateBrush* Brush){FSlateDrawElement::MakeBox(Out,L,G.ToPaintGeometry(Extent*Unit,FSlateLayoutTransform(Offset+P*Unit)),Brush,ESlateDrawEffect::None,Color.CopyWithNewOpacity(Color.A*.92f));};
            Fill({9,2},{6,12},&Capsule);Arc({12,11},7,0,180);Line({{5,9},{5,11}});Line({{19,9},{19,11}});Fill({11,18},{2,3},&White);Fill({8,20},{8,2},&White);
            if(Listening.Get()){
                const double R=FMath::Min(Size.X,Size.Y)*29./72.,Start=FPlatformTime::Seconds()*160.;TArray<FVector2D> Points;
                for(int32 I=0;I<=64;++I){const double A=(Start+I/64.*285.)*PI/180;Points.Add(Size*.5+FVector2D(FMath::Cos(A),FMath::Sin(A))*R);}
                FSlateDrawElement::MakeLines(Out,L+1,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color.CopyWithNewOpacity(Color.A*.42f),true,float(FMath::Min(Size.X,Size.Y)*1.5/72.));
            }
        }
        else if(Name==TEXT("home")) {Line({{3,11},{12,3},{21,11}});Line({{6,10},{6,21},{10,21},{10,15},{14,15},{14,21},{18,21},{18,10}});}
        else if(Name==TEXT("save")) {Line({{5,3},{17,3},{21,7},{21,21},{3,21},{3,3},{5,3}});Line({{7,3},{7,9},{16,9},{16,3}});Line({{7,21},{7,14},{17,14},{17,21}});}
        else if(Name==TEXT("folder")) {Line({{3,19},{3,5},{10,5},{12,8},{21,8},{21,19},{3,19}});Line({{3,12},{21,12}});}
        else if(Name==TEXT("clock")) {Arc({12,12},9,0,360);Line({{12,6},{12,12},{16,14}});}
        else if(Name==TEXT("undo")||Name==TEXT("redo")) {const bool R=Name==TEXT("redo");if(R){Arc({11,13},7,-145,30);Line({{19,5},{19,11},{13,11}});}else{Arc({13,13},7,150,325);Line({{5,5},{5,11},{11,11}});}}
        else if(Name==TEXT("neutral")) {Arc({12,12},6,0,360);Line({{12,2},{12,7}});Line({{12,17},{12,22}});Line({{2,12},{7,12}});Line({{17,12},{22,12}});}
        else if(Name==TEXT("orbit")) {Arc({12,12},3,0,360);Arc({12,12},9,35,325);Line({{20,4},{21,10},{15,8}});}
        else if(Name==TEXT("view")) {Line({{3,8},{8,8},{10,5},{16,5},{18,8},{21,8},{21,20},{3,20},{3,8}});Arc({12,14},4,0,360);}
        else if(Name==TEXT("settings")) {Line({{4,6},{20,6}});Line({{4,12},{20,12}});Line({{4,18},{20,18}});Arc({9,6},2,0,360);Arc({16,12},2,0,360);Arc({7,18},2,0,360);}
        else if(Name==TEXT("grid")) {for(double X:{4.,14.})for(double Y:{4.,14.})Line({{X,Y},{X+6,Y},{X+6,Y+6},{X,Y+6},{X,Y}});}
        else if(Name==TEXT("star")) {TArray<FVector2D> P;for(int32 I=0;I<=10;++I){double A=(I*36-90)*PI/180,R=I%2?4.:9.;P.Add(Offset+(FVector2D(12)+FVector2D(FMath::Cos(A),FMath::Sin(A))*R)*Unit);}FSlateDrawElement::MakeLines(Out,L,G.ToPaintGeometry(),P,ESlateDrawEffect::None,Color,true,float(Unit*1.65));}
        else if(Name==TEXT("water")) {for(double Y:{7.,13.,19.})Line({{3,Y},{7,Y-2},{12,Y+1},{17,Y-2},{21,Y}});}
        else {Line({{12,3},{12,21}});Line({{3,12},{21,12}});Line({{6,6},{18,18}});Line({{6,18},{18,6}});}
        return L+2;
    }
private:FString Name;TAttribute<bool> Listening;
};

const FButtonStyle* VoiceStyle()
{
    static const FButtonStyle Style=FButtonStyle(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
        .SetNormal(FSlateNoResource()).SetDisabled(FSlateNoResource())
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(1,1,1,.06f),36.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(1,1,1,.12f),36.f))
        .SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0))
        .SetNormalForeground(FLinearColor::White).SetHoveredForeground(FLinearColor::White).SetPressedForeground(FLinearColor::White);
    return &Style;
}

class SLightBoxDialog final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SLightBoxDialog) {} SLATE_ARGUMENT(TWeakObjectPtr<ALightBoxHUD>, HUD) SLATE_DEFAULT_SLOT(FArguments,Content) SLATE_END_ARGS()
    void Construct(const FArguments& A){HUD=A._HUD;ChildSlot[A._Content.Widget];SetRenderOpacity(0);}
    virtual bool SupportsKeyboardFocus() const override{return true;}
    virtual FReply OnKeyDown(const FGeometry& G,const FKeyEvent& E)override{if(E.GetKey()==EKeys::Escape){if(HUD.IsValid())HUD->CloseDialog();return FReply::Handled();}return SCompoundWidget::OnKeyDown(G,E);}
    virtual void Tick(const FGeometry& G,double T,float Dt)override{SCompoundWidget::Tick(G,T,Dt);Age=FMath::Min(1.f,Age+Dt/.18f);SetRenderOpacity(1-FMath::Pow(1-Age,3.f));}
private:TWeakObjectPtr<ALightBoxHUD> HUD;float Age=0;
};
}

namespace LightBoxUI {
TSharedRef<SWidget> Icon(const TCHAR* Name){return SNew(SBox).WidthOverride(26).HeightOverride(26)[SNew(SLightBoxIcon).Name(Name)];}
const FButtonStyle* ButtonStyle(bool Primary)
{
    static const FButtonStyle GlassButton=FButtonStyle(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
        .SetNormal(FSlateRoundedBoxBrush(FLinearColor(.025f,.045f,.06f,.38f),18.f,Paper.CopyWithNewOpacity(.14f),1.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.16f,.28f,.30f,.65f),18.f,Mint.CopyWithNewOpacity(.4f),1.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.10f,.26f,.24f,.8f),18.f))
        .SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.018f,.034f,.047f,.16f),18.f,Paper.CopyWithNewOpacity(.07f),1.f))
        .SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0))
        .SetNormalForeground(Paper).SetHoveredForeground(Paper).SetPressedForeground(Mint);
    static const FButtonStyle PrimaryButton=FButtonStyle(GlassButton)
        .SetNormal(FSlateRoundedBoxBrush(Paper.CopyWithNewOpacity(.9f),18.f))
        .SetHovered(FSlateRoundedBoxBrush(Mint,18.f)).SetNormalForeground(ExperienceUI::Ink).SetHoveredForeground(ExperienceUI::Ink);
    return Primary?&PrimaryButton:&GlassButton;
}
const FEditableTextBoxStyle* FieldStyle()
{
    static const FEditableTextBoxStyle Field=FEditableTextBoxStyle(FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
        .SetBackgroundImageNormal(FSlateRoundedBoxBrush(FLinearColor(0,0,0,.06f),14.f))
        .SetBackgroundImageHovered(FSlateRoundedBoxBrush(FLinearColor(1,1,1,.04f),14.f))
        .SetBackgroundImageFocused(FSlateRoundedBoxBrush(FLinearColor(0,0,0,.10f),14.f,Mint.CopyWithNewOpacity(.35f),1.f))
        .SetForegroundColor(Paper).SetPadding(FMargin(16,13));
    return &Field;
}
TSharedRef<SWidget> AnimateDialog(TWeakObjectPtr<ALightBoxHUD> HUD,TSharedRef<SWidget> Content){return SNew(SLightBoxDialog).HUD(HUD)[Content];}
}

void SLightBoxControls::Construct(const FArguments& Args)
{
    HUD=Args._HUD;
    auto D=[this](){return HUD.IsValid()?HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>():nullptr;};
    if(D())LastStatus=D()->Status;
    TSharedRef<SUniformGridPanel> Grid=SNew(SUniformGridPanel).SlotPadding(5.f);
    const FName Ids[]={TEXT("Examples"),TEXT("Neutral"),TEXT("Journal"),TEXT("Undo"),TEXT("Save"),TEXT("Restore"),TEXT("Redo"),TEXT("Settings"),TEXT("Home")};
    const TCHAR* Icons[]={TEXT("grid"),TEXT("neutral"),TEXT("clock"),TEXT("undo"),TEXT("save"),TEXT("folder"),TEXT("redo"),TEXT("settings"),TEXT("home")};
    const TCHAR* Labels[]={TEXT("景色"),TEXT("姿勢"),TEXT("記録"),TEXT("戻す"),TEXT("保存"),TEXT("呼出"),TEXT("やり直す"),TEXT("設定"),TEXT("トップ")};
    for(int32 I=0;I<9;++I)Grid->AddSlot(I%3,I/3)[Action(Ids[I],Icons[I],Labels[I])];
    TSharedRef<SHorizontalBox> Presets=SNew(SHorizontalBox);
    const TCHAR* PresetIcons[]={TEXT("star"),TEXT("water"),TEXT("spark")};
    const TCHAR* PresetNames[]={TEXT("星"),TEXT("海"),TEXT("蛍")};
    for(int32 I=0;I<3;++I)Presets->AddSlot().FillWidth(1).Padding(5.f)[Action(FName(*FString::Printf(TEXT("Preset%d"),I)),PresetIcons[I],PresetNames[I])];
    TSharedRef<SHorizontalBox> View=SNew(SHorizontalBox);
    View->AddSlot().FillWidth(1).Padding(5.f)[Action(TEXT("Orbit"),TEXT("orbit"),TEXT("自動回転"))];
    View->AddSlot().FillWidth(1).Padding(5.f)[Action(TEXT("ViewReset"),TEXT("view"),TEXT("視点を戻す"))];
    Menu=SNew(SBorder).BorderImage(Glass()).BorderBackgroundColor(FLinearColor(.015f,.033f,.045f,.66f)).Padding(18.f)
    [SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(10,0,10,14)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",19)).ColorAndOpacity(Paper.CopyWithNewOpacity(.8f)).Text_Lambda([D](){return FText::FromString(D()?D()->GetStyle().Title:FString());})]
        +SVerticalBox::Slot().AutoHeight()[Presets]
        +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[Grid]
        +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[View]
        +SVerticalBox::Slot().AutoHeight().Padding(10,15,10,2)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",13)).ColorAndOpacity(Paper.CopyWithNewOpacity(.55f)).Text_Lambda([this,D](){
            if(!D())return FText::GetEmpty();FString Label=D()->GetJournal().Summary();if(auto* P=Cast<ALightBoxPawn>(HUD->GetOwningPlayerController()->GetPawn()))if(P->PointCloud&&P->PointCloud->PointTotal()>0)Label+=TEXT("\n")+FText::AsNumber(P->PointCloud->PointTotal()).ToString()+TEXT("粒");return FText::FromString(Label);
        })]
    ];
    Input=SNew(SEditableTextBox).Style(LightBoxUI::FieldStyle()).Font(FCoreStyle::GetDefaultFontStyle("Regular",23))
        .HintText(FText::FromString(TEXT("景色をつくる…"))).SelectAllTextWhenFocused(false)
        .OnTextCommitted_Lambda([this](const FText&,ETextCommit::Type Type){
#if !PLATFORM_ANDROID
            if(Type==ETextCommit::OnEnter)Submit();
#endif
        });
    ChildSlot
    [SNew(SSafeZone).IsTitleSafe(false)
    [SNew(SDPIScaler).DPIScale_Lambda([this](){return Scale;})
    [SNew(SOverlay)
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0,0,0,32)
        [SNew(SBox).WidthOverride_Lambda([this](){return FOptionalSize(FMath::Min(860.,FMath::Max(260.,HostSize.X/Scale-180)));})
        [SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight()
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(16,0,16,10)
                [SNew(SHorizontalBox)
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,10,0)
                    [SNew(SBox).WidthOverride(32).HeightOverride(32)
                        .Visibility_Lambda([D](){return D()&&D()->bThinking?EVisibility::HitTestInvisible:EVisibility::Collapsed;})
                    [SNew(SLightBoxIcon).Name(TEXT("loading")).Listening_Lambda([D](){return D()&&D()->bThinking;})]]
                    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",20)).ColorAndOpacity(Paper.CopyWithNewOpacity(.95f)).Justification(ETextJustify::Center)
                        .WrapTextAt(740).Visibility_Lambda([this,D](){return !bMenu&&D()&&(D()->bThinking||D()->bListening||StatusAge<5.f)?EVisibility::HitTestInvisible:EVisibility::Collapsed;})
                        .Text_Lambda([D](){
                            if(!D())return FText::GetEmpty();
                            if(D()->bThinking&&!D()->Heard.IsEmpty())return FText::FromString(FString::Printf(TEXT("「%s」\n%s"),*D()->Heard,*D()->Status));
                            return FText::FromString(D()->bListening&&!D()->Heard.IsEmpty()?D()->Heard:D()->Status);
                        })]]
                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,10)
                [SNew(SBox).WidthOverride(88).HeightOverride(88)
                [SNew(SButton).ButtonStyle(VoiceStyle()).ContentPadding(0)
                    .ToolTipText_Lambda([D](){return FText::FromString(D()&&D()->bListening?TEXT("聞き取りを取り消す"):TEXT("音声入力"));})
                    .IsEnabled_Lambda([this,D](){return !bMenu&&HUD.IsValid()&&!HUD->IsModalOpen()&&D()&&!D()->bThinking;})
                    .OnClicked_Lambda([this](){return Activate(TEXT("Voice"));})
                    [SNew(SLightBoxIcon).Name(TEXT("voice")).Listening_Lambda([D](){return D()&&D()->bListening;})]]]
                +SVerticalBox::Slot().AutoHeight()
                [SNew(SBorder).BorderImage(Glass()).BorderBackgroundColor(FLinearColor(.018f,.034f,.047f,.48f)).Padding(7.f)
            .IsEnabled_Lambda([this,D](){return !bMenu&&HUD.IsValid()&&!HUD->IsModalOpen()&&D()&&!D()->bListening;})
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Input.ToSharedRef()]
                +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5,0,2,0)
                [SNew(SBox).WidthOverride(58).HeightOverride(58)
                [SNew(SButton).ButtonStyle(LightBoxUI::ButtonStyle(true)).ContentPadding(15)
                    .ToolTipText_Lambda([D](){return FText::FromString(D()&&D()->bThinking?TEXT("取り消す"):TEXT("送信"));})
                    .IsEnabled_Lambda([this,D](){return D()&&(D()->bThinking||!Input->GetText().ToString().TrimStartAndEnd().IsEmpty());})
                    .OnClicked_Lambda([this,D](){if(D()&&D()->bThinking){D()->Cancel();D()->Status=TEXT("取り消しました");return FReply::Handled();}return Submit();})
                    [SNew(SOverlay)
                        +SOverlay::Slot()[SNew(SBox).Visibility_Lambda([D](){return D()&&D()->bThinking?EVisibility::Collapsed:EVisibility::HitTestInvisible;})[LightBoxUI::Icon(TEXT("send"))]]
                        +SOverlay::Slot()[SNew(SBox).Visibility_Lambda([D](){return D()&&D()->bThinking?EVisibility::HitTestInvisible:EVisibility::Collapsed;})[LightBoxUI::Icon(TEXT("stop"))]]
                    ]]]
            ]]]
        ]]
        +SOverlay::Slot()
        [SNew(SButton).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder")).ContentPadding(0)
            .Visibility_Lambda([this](){return bMenu?EVisibility::Visible:EVisibility::Collapsed;})
            .OnClicked_Lambda([this](){CloseMenu();return FReply::Handled();})]
        +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(TAttribute<FMargin>::CreateLambda([this](){return FMargin(0,126,42+RightInset/Scale,0);}))
        [SNew(SBox).WidthOverride(390).Visibility_Lambda([this](){return Progress>0?EVisibility::SelfHitTestInvisible:EVisibility::Collapsed;})[Menu.ToSharedRef()]]
        +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(TAttribute<FMargin>::CreateLambda([this](){return FMargin(0,42,42+RightInset/Scale,0);}))
        [SNew(SBox).WidthOverride(64).HeightOverride(64)
        [SNew(SButton).ButtonStyle(LightBoxUI::ButtonStyle()).ContentPadding(18).ToolTipText(FText::FromString(TEXT("メニュー")))
            .OnClicked_Lambda([this](){if(bMenu)CloseMenu();else OpenMenu();return FReply::Handled();})
            [SNew(SOverlay)
                +SOverlay::Slot()[SNew(SBox).Visibility_Lambda([this](){return bMenu?EVisibility::Collapsed:EVisibility::HitTestInvisible;})[LightBoxUI::Icon(TEXT("menu"))]]
                +SOverlay::Slot()[SNew(SBox).Visibility_Lambda([this](){return bMenu?EVisibility::HitTestInvisible:EVisibility::Collapsed;})[LightBoxUI::Icon(TEXT("close"))]]
            ]]]
    ]]];
    SetVisibility(EVisibility::SelfHitTestInvisible);
}

TSharedRef<SWidget> SLightBoxControls::Action(FName Id,const TCHAR* Name,const TCHAR* Label)
{
    return SNew(SButton).ButtonStyle(LightBoxUI::ButtonStyle()).ContentPadding(FMargin(10,12)).HAlign(HAlign_Center).ToolTipText(FText::FromString(Label))
        .IsEnabled_Lambda([this,Id](){if(!HUD.IsValid()||!bMenu)return false;auto* D=HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>();return Id==TEXT("Undo")?D->CanUndo():Id==TEXT("Redo")?D->CanRedo():Id!=TEXT("Restore")||D->bHasSavedScene;})
        .OnClicked_Lambda([this,Id](){return Activate(Id);})
        [SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[SNew(SBox).WidthOverride(26).HeightOverride(26)[LightBoxUI::Icon(Name)]]
            +SVerticalBox::Slot().AutoHeight().Padding(0,7,0,0).HAlign(HAlign_Center)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",14)).Text_Lambda([this,Id,Caption=FString(Label)](){
                if(Id==TEXT("Orbit")&&HUD.IsValid())if(auto* P=Cast<ALightBoxPawn>(HUD->GetOwningPlayerController()->GetPawn()))if(P->IsCameraOrbiting())return FText::FromString(TEXT("回転を止める"));
                return FText::FromString(Caption);
            })]
        ];
}
void SLightBoxControls::Tick(const FGeometry& G,double Time,float Dt)
{
    SCompoundWidget::Tick(G,Time,Dt);HostSize=G.GetLocalSize();Scale=FMath::Clamp(float(HostSize.Y/1080),.45f,1.6f);
#if PLATFORM_ANDROID
    InsetAge+=Dt;if(InsetAge>.5f){InsetAge=0;if(JNIEnv* Env=FAndroidApplication::GetJavaEnv()){
        const jmethodID Method=FJavaWrapper::FindMethod(Env,FJavaWrapper::GameActivityClassID,"AndroidThunkJava_LightBoxRightInset","()I",false);
        if(Method)RightInset=FJavaWrapper::CallIntMethod(Env,FJavaWrapper::GameActivityThis,Method);
    }}
#endif
    Progress=FMath::Clamp(Progress+(bMenu?1.f:-1.f)*Dt/.22f,0.f,1.f);const float Ease=1-FMath::Pow(1-Progress,3.f);
    Menu->SetRenderOpacity(Ease);Menu->SetRenderTransform(FSlateRenderTransform(FVector2D(0,(1-Ease)*-20)));
    Menu->SetVisibility(bMenu?EVisibility::SelfHitTestInvisible:EVisibility::HitTestInvisible);
    if(HUD.IsValid()){
        auto* D=HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>();
        FString Voice=D->TakeVoiceDraft();if(!Voice.IsEmpty()){Input->SetText(FText::FromString(Voice));Submit();}
        StatusAge+=Dt;if(LastStatus!=D->Status){LastStatus=D->Status;StatusAge=0;}
        if(D->bListening)StatusAge=0;
    }
}
bool SLightBoxControls::IsInteracting()const{return bMenu||(Input.IsValid()&&(Input->HasKeyboardFocus()||Input->HasFocusedDescendants()));}
void SLightBoxControls::OpenMenu(){bMenu=true;FSlateApplication::Get().SetKeyboardFocus(SharedThis(this),EFocusCause::SetDirectly);}
void SLightBoxControls::CloseMenu(){bMenu=false;FSlateApplication::Get().SetAllUserFocusToGameViewport();}
void SLightBoxControls::FocusInput(){bMenu=false;FSlateApplication::Get().SetKeyboardFocus(Input,EFocusCause::SetDirectly);}
FReply SLightBoxControls::OnKeyDown(const FGeometry& G,const FKeyEvent& E){if(E.GetKey()==EKeys::Escape){CloseMenu();return FReply::Handled();}return SCompoundWidget::OnKeyDown(G,E);}
FReply SLightBoxControls::Submit()
{
    if(!HUD.IsValid())return FReply::Handled();auto* D=HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>();const FString Words=Input->GetText().ToString().TrimStartAndEnd();
    if(!D->bThinking&&!Words.IsEmpty()){const int32 Revision=D->GetRevision();D->Submit(Words);if(D->bThinking||D->GetRevision()!=Revision){Input->SetText(FText::GetEmpty());FSlateApplication::Get().SetAllUserFocusToGameViewport();}}
    return FReply::Handled();
}
FReply SLightBoxControls::Activate(FName Id){if(HUD.IsValid()){CloseMenu();HUD->NotifyHitBoxClick(Id);}return FReply::Handled();}
