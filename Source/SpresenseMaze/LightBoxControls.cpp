#include "LightBoxControls.h"
#include "LightBoxGame.h"
#include "LightBoxPointCloud.h"
#include "LightBoxDirector.h"
#include "ExperienceUI.h"
#include "LightBoxInput.h"
#include "LightBoxPaint.h"
#include "LightBoxKana.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateNoResource.h"
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
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/IInputProcessor.h"
#if PLATFORM_ANDROID
#include "Android/AndroidApplication.h"
#include "Android/AndroidJNI.h"
#endif

namespace {
const FLinearColor Paper(.94f,.98f,1.f), Mint(.42f,.94f,.80f);
const FSlateBrush* Glass()
{
    static const FSlateRoundedBoxBrush Brush(FLinearColor::White,24.f,LightBoxPaint::Violet.CopyWithNewOpacity(.36f),1.f);
    return &Brush;
}

// The narration hairline under the status words. Idle it rests with a star at its centre;
// while waiting the star drifts, and local assembly lights the line up to the star.
class SLightBoxProgress final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SLightBoxProgress) {} SLATE_ATTRIBUTE(TOptional<float>,Percent) SLATE_ATTRIBUTE(bool,Busy) SLATE_END_ARGS()
    void Construct(const FArguments& A){Percent=A._Percent;Busy=A._Busy;}
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(240,18);}
    virtual void Tick(const FGeometry& G,double T,float Dt)override{SLeafWidget::Tick(G,T,Dt);const bool Now=Busy.Get();if(Now||Now!=bWasBusy)Invalidate(EInvalidateWidgetReason::Paint);bWasBusy=Now;}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 L,const FWidgetStyle& Style,bool)const override{
        const FVector2D Size=G.GetLocalSize();const auto Value=Percent.Get();const bool bBusy=Busy.Get();
        LightBoxPaint::FVectorPaintBatch Batch(Out,Style.GetColorAndOpacityTint().A);
        float At=.5f;
        if(bBusy&&Value.IsSet())At=FMath::Clamp(Value.GetValue(),0.f,1.f);
        else if(bBusy)At=float(.5-.42*FMath::Cos(FPlatformTime::Seconds()*1.6));
        const float Y=float(Size.Y)*.5f;
        LightBoxPaint::GlowLine(Out,L,G,FVector2D(0,Y),float(Size.X),bBusy?.9f:.8f,At,1.f,1.5f);
        if(bBusy&&Value.IsSet()&&At>.01f)
            LightBoxPaint::Fill(Out,L,G,FVector2D(Size.X*.06,Y-.9f),FVector2D(FMath::Max(0.0,Size.X*(At-.06)),1.8f),.9f,Mint.CopyWithNewOpacity(.0f),Mint.CopyWithNewOpacity(.85f),Orient_Horizontal);
        return L+1;
    }
private:TAttribute<TOptional<float>> Percent;TAttribute<bool> Busy;bool bWasBusy=true;
};

// Painted glass sheet behind the menu content (a brush cannot carry the gradient rim).
class SLightBoxPanel final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SLightBoxPanel) {} SLATE_END_ARGS()
    void Construct(const FArguments&){}
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(8);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 L,const FWidgetStyle& Style,bool)const override{
        LightBoxPaint::FVectorPaintBatch Batch(Out,Style.GetColorAndOpacityTint().A);
        LightBoxPaint::GlassPanel(Out,L,G,FVector2D(0),G.GetLocalSize(),24);return L+1;
    }
};

// Static menu glass: one mesh submission, with no blur or animation behind each row.
class SLightBoxMenuPanel final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SLightBoxMenuPanel) {} SLATE_END_ARGS()
    void Construct(const FArguments&) {}
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(8); }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out, int32 L, const FWidgetStyle& Style, bool) const override
    {
        using namespace LightBoxPaint;
        FVectorPaintBatch Batch(Out, Style.GetColorAndOpacityTint().A);
        Fill(Out,L,G,FVector2D(0),G.GetLocalSize(),28,FLinearColor(.022f,.043f,.088f,.97f),FLinearColor(.010f,.020f,.048f,.98f));
        TintStroke(Out,L,G,RoundedOutline(G,FVector2D(0),G.GetLocalSize(),28),[&](const FVector2D& P){return Hairline(float(P.X/G.GetLocalSize().X)).CopyWithNewOpacity(.45f);},1.2f);
        return L+1;
    }
};

const FScrollBoxStyle* AtelierMenuScrollStyle()
{
    static const FScrollBoxStyle Style=FScrollBoxStyle(FCoreStyle::Get().GetWidgetStyle<FScrollBoxStyle>("ScrollBox"))
        .SetTopShadowBrush(FSlateNoResource()).SetBottomShadowBrush(FSlateNoResource());
    return &Style;
}

const FButtonStyle* AtelierMenuStyle(bool Selected=false)
{
    static const FButtonStyle Quiet=FButtonStyle(*LightBoxUI::TileStyle())
        .SetNormal(FSlateRoundedBoxBrush(FLinearColor(.30f,.46f,.67f,.12f),16.f,Paper.CopyWithNewOpacity(.25f),1.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.12f,.48f,.50f,.22f),16.f,Mint.CopyWithNewOpacity(.65f),1.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.12f,.55f,.53f,.30f),16.f,Mint.CopyWithNewOpacity(.9f),1.f))
        .SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.30f,.46f,.67f,.04f),16.f,Paper.CopyWithNewOpacity(.12f),1.f));
    static const FButtonStyle Active=FButtonStyle(Quiet)
        .SetNormal(FSlateRoundedBoxBrush(FLinearColor(.08f,.40f,.42f,.22f),16.f,Mint.CopyWithNewOpacity(.68f),1.f));
    return Selected?&Active:&Quiet;
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
        else if(Name==TEXT("history")) {Arc({12,12},8,-200,110);Line({{2,5},{4,9},{8,7}});Line({{12,8},{12,12},{15,14}});}
        else if(Name==TEXT("sparkle")) {Line({{11,3},{13,10},{20,12},{13,14},{11,21},{9,14},{2,12},{9,10},{11,3}});Line({{19,3},{19,7}});Line({{17,5},{21,5}});}
        else if(Name==TEXT("chip")) {Line({{6,6},{18,6},{18,18},{6,18},{6,6}});for(double V:{10.,14.}){Line({{V,2.5},{V,6}});Line({{V,18},{V,21.5}});Line({{2.5,V},{6,V}});Line({{18,V},{21.5,V}});}Arc({12,12},1.6,0,360);}
        else if(Name==TEXT("key")) {Arc({7,12},4,0,360);Line({{11,12},{21,12}});Line({{17,12},{17,16}});Line({{20,12},{20,15}});}
        else if(Name==TEXT("dots")) {for(int32 I=0;I<3;++I)for(int32 K=0;K<3;++K){const double R=.7+.45*(I+K);Arc({5.+I*7,5.+K*7},R,0,360);}}
        else if(Name==TEXT("recenter")) {Line({{4,9},{4,4},{9,4}});Line({{15,4},{20,4},{20,9}});Line({{20,15},{20,20},{15,20}});Line({{9,20},{4,20},{4,15}});Arc({12,12},2.2,0,360);}
        else if(Name==TEXT("chevron")) Line({{6,9},{12,15},{18,9}});
        else if(Name==TEXT("next")) Line({{9,5},{16,12},{9,19}});
        else if(Name==TEXT("water")) {for(double Y:{7.,13.,19.})Line({{3,Y},{7,Y-2},{12,Y+1},{17,Y-2},{21,Y}});}
        else {Line({{12,3},{12,21}});Line({{3,12},{21,12}});Line({{6,6},{18,18}});Line({{6,18},{18,6}});}
        return L+2;
    }
private:FString Name;TAttribute<bool> Listening;
};

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

void SLightBoxSheet::Construct(const FArguments& Args)
{
    ChildSlot[SNew(SOverlay)+SOverlay::Slot()[SNew(SLightBoxMenuPanel)]+SOverlay::Slot().Padding(Args._Padding)[Args._Content.Widget]];
}

namespace LightBoxUI {
const FButtonStyle* MenuStyle(bool Selected){return AtelierMenuStyle(Selected);}
const FScrollBoxStyle* ScrollStyle(){return AtelierMenuScrollStyle();}
const FSlateBrush* CardBrush(bool Selected)
{
    static const FSlateRoundedBoxBrush Normal(FLinearColor(.30f,.46f,.67f,.12f),16.f,Paper.CopyWithNewOpacity(.25f),1.f);
    static const FSlateRoundedBoxBrush Active(FLinearColor(.08f,.40f,.42f,.22f),16.f,Mint.CopyWithNewOpacity(.68f),1.f);
    return Selected?&Active:&Normal;
}
TSharedRef<SWidget> DialogHeader(const FString& Title,float J,TFunction<void()> OnClose)
{
    return SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
        [SNew(STextBlock).Font(LightBoxPaint::Spaced(FMath::RoundToInt(26*J),40)).ColorAndOpacity(Paper.CopyWithNewOpacity(.94f)).Text(FText::FromString(Title))]
        +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [SNew(SBox).WidthOverride(72*J).HeightOverride(72*J)
            [SNew(SButton).ButtonStyle(MenuStyle()).ContentPadding(22*J).ToolTipText(FText::FromString(TEXT("閉じる")))
                .OnClicked_Lambda([OnClose](){OnClose();return FReply::Handled();})[Icon(TEXT("close"))]]];
}
TSharedRef<SWidget> Icon(const TCHAR* Name){return SNew(SBox).WidthOverride(26).HeightOverride(26)[SNew(SLightBoxIcon).Name(Name)];}
const FButtonStyle* ButtonStyle(bool Primary)
{
    static const FButtonStyle GlassButton=FButtonStyle(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
        .SetNormal(FSlateRoundedBoxBrush(FLinearColor(1,1,1,.035f),16.f,Paper.CopyWithNewOpacity(.14f),1.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.06f,.30f,.28f,.22f),16.f,Mint.CopyWithNewOpacity(.5f),1.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.06f,.36f,.32f,.32f),16.f,Mint.CopyWithNewOpacity(.85f),1.f))
        .SetDisabled(FSlateRoundedBoxBrush(FLinearColor(1,1,1,.01f),16.f,Paper.CopyWithNewOpacity(.05f),1.f))
        .SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0))
        .SetNormalForeground(Paper).SetHoveredForeground(Paper).SetPressedForeground(Mint);
    static const FButtonStyle PrimaryButton=FButtonStyle(GlassButton)
        .SetNormal(FSlateRoundedBoxBrush(FLinearColor(.05f,.32f,.30f,.30f),16.f,Mint.CopyWithNewOpacity(.70f),1.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.06f,.40f,.36f,.40f),16.f,Mint,1.f)).SetNormalForeground(Paper).SetHoveredForeground(Mint);
    return Primary?&PrimaryButton:&GlassButton;
}
const FSlateBrush* PanelBrush(){return Glass();}
// Menu tiles: almost no plate, one hairline; the panel itself carries the colour.
const FButtonStyle* TileStyle()
{
    static const FButtonStyle Style=FButtonStyle(*ButtonStyle())
        .SetNormal(FSlateRoundedBoxBrush(FLinearColor(1,1,1,.028f),16.f,Paper.CopyWithNewOpacity(.10f),1.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.06f,.30f,.28f,.22f),16.f,Mint.CopyWithNewOpacity(.50f),1.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.06f,.36f,.32f,.32f),16.f,Mint.CopyWithNewOpacity(.85f),1.f))
        .SetDisabled(FSlateRoundedBoxBrush(FLinearColor(1,1,1,.008f),16.f,Paper.CopyWithNewOpacity(.04f),1.f));
    return &Style;
}
const FButtonStyle* MenuButtonStyle()
{
    static const FButtonStyle Style=FButtonStyle(*ButtonStyle())
        .SetNormal(FSlateRoundedBoxBrush(FLinearColor(.016f,.026f,.058f,.62f),39.f,Paper.CopyWithNewOpacity(.20f),1.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(.025f,.075f,.080f,.80f),39.f,Mint.CopyWithNewOpacity(.55f),1.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(.035f,.13f,.12f,.90f),39.f,Mint,1.f));
    return &Style;
}
const FEditableTextBoxStyle* FieldStyle()
{
    static const FEditableTextBoxStyle Field=FEditableTextBoxStyle(FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
        // Fields read as places to type: a visible plate and rim, lit in mint while focused.
        .SetBackgroundImageNormal(FSlateRoundedBoxBrush(FLinearColor(1,1,1,.06f),14.f,Paper.CopyWithNewOpacity(.20f),1.f))
        .SetBackgroundImageHovered(FSlateRoundedBoxBrush(FLinearColor(1,1,1,.08f),14.f,Paper.CopyWithNewOpacity(.30f),1.f))
        .SetBackgroundImageFocused(FSlateRoundedBoxBrush(FLinearColor(.06f,.30f,.28f,.16f),14.f,Mint.CopyWithNewOpacity(.75f),1.f))
        .SetForegroundColor(Paper).SetPadding(FMargin(16,13));
    return &Field;
}
TSharedRef<SWidget> AnimateDialog(TWeakObjectPtr<ALightBoxHUD> HUD,TSharedRef<SWidget> Content){return SNew(SLightBoxDialog).HUD(HUD)[Content];}
}

// External keyboards (USB/Bluetooth on the exhibit phone, a PC, adb) type straight into the Atelier's own input,
// wherever focus happens to be. Dialog text fields keep their keys.
#if PLATFORM_ANDROID
// UE hands Android hardware keys over with the Android key code in place of the character.
TCHAR AndroidCharacter(uint32 Code,bool bShift)
{
    if(Code>=29&&Code<=54){const TCHAR C=TCHAR('a'+(Code-29));return bShift?FChar::ToUpper(C):C;}   // KEYCODE_A..Z
    if(Code>=7&&Code<=16){static const TCHAR Shifted[]=TEXT(")!@#$%^&*(");return bShift?Shifted[Code-7]:TCHAR('0'+(Code-7));}
    switch(Code)
    {
    case 62: return TEXT(' ');
    case 55: return bShift?TEXT('<'):TEXT(',');
    case 56: return bShift?TEXT('>'):TEXT('.');
    case 69: return bShift?TEXT('_'):TEXT('-');
    case 76: return bShift?TEXT('?'):TEXT('/');
    case 75: return bShift?TEXT('"'):TEXT('\'');
    default: return 0;
    }
}
#endif
class FLightBoxKeyRouter final : public IInputProcessor
{
public:
    explicit FLightBoxKeyRouter(TWeakPtr<SLightBoxControls> In):Controls(In){}
    virtual void Tick(const float,FSlateApplication&,TSharedRef<ICursor>) override {}
    virtual bool HandleKeyDownEvent(FSlateApplication&,const FKeyEvent& E) override
    {
        const TSharedPtr<SLightBoxControls> C=Controls.Pin();
        if(!C.IsValid())return false;
        if(C->ExternalKey(E))return true;
        if(E.IsControlDown()||E.IsAltDown()||E.IsCommandDown())return false;
        // Key-down carries the character on every platform (Slate has no pre-processor hook for KeyChar).
        TCHAR Ch=TCHAR(E.GetCharacter());
#if PLATFORM_ANDROID
        Ch=AndroidCharacter(E.GetKeyCode(),E.IsShiftDown());
#else
        if(Ch>=TEXT('A')&&Ch<=TEXT('Z')&&!E.IsShiftDown())Ch=FChar::ToLower(Ch);
#endif
        return Ch>=32&&Ch!=127&&C->ExternalChar(Ch);
    }
    virtual const TCHAR* GetDebugName() const override { return TEXT("LightBoxKeyRouter"); }
private:
    TWeakPtr<SLightBoxControls> Controls;
};

SLightBoxControls::~SLightBoxControls()
{
    if(KeyRouter.IsValid()&&FSlateApplication::IsInitialized())FSlateApplication::Get().UnregisterInputPreProcessor(KeyRouter);
}
bool SLightBoxControls::AcceptsExternalText()const
{
    if(!HUD.IsValid()||HUD->IsModalOpen())return false;
    auto* D=HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>();return D&&!D->bListening;
}
bool SLightBoxControls::ExternalChar(TCHAR C)
{
    if(!AcceptsExternalText()||C<32)return false;
#if !PLATFORM_ANDROID
    // On a PC the letter keys also stand in for the phone sensors; only type while the keyboard is out.
    if(!bKeyboard)return false;
#endif
    if(!bKeyboard){if(C==TEXT(' '))return false;SetKeyboard(true);}
    if(Keyboard.IsValid()&&Keyboard->IsLiteralMode()){Composer->TypeLiteral(FString(1,&C));return true;}
    if(C==TEXT(' ')){Composer->Convert();return true;}
    // Romaji joins the composition; anything else (digits, symbols) goes straight in.
    if((C<128&&FChar::IsAlpha(C))||FString(TEXT("-,.?!'")).Contains(FString(1,&C)))Composer->TypeRomaji(C);
    else Composer->TypeLiteral(FString(1,&C));
    return true;
}
bool SLightBoxControls::ExternalKey(const FKeyEvent& E)
{
    if(!bKeyboard||!AcceptsExternalText())return false;
    const FKey Key=E.GetKey();
    if(Key==EKeys::BackSpace||Key==EKeys::Enter||Key==EKeys::Escape||Key==EKeys::Down||((Key==EKeys::Left||Key==EKeys::Right)&&Composer->bConverting))
        return OnKeyDown(FGeometry(),E).IsEventHandled();
    return false;
}

void SLightBoxControls::Construct(const FArguments& Args)
{
    HUD=Args._HUD;
    auto D=[this](){return HUD.IsValid()?HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>():nullptr;};
    if(D())LastStatus=D()->Status;
    auto Row=[this](FName A,const TCHAR* IconA,const TCHAR* LabelA,FName B,const TCHAR* IconB,const TCHAR* LabelB)
    {
        return SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[Action(A,IconA,LabelA)]
            +SHorizontalBox::Slot().FillWidth(1).Padding(6,0,0,0)[Action(B,IconB,LabelB)];
    };
    auto Section=[](const TCHAR* Name)
    {
        static const FSlateRoundedBoxBrush Accent(FLinearColor::White,2.f);
        return SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [SNew(SBox).WidthOverride(4).HeightOverride(24)[SNew(SBorder).BorderImage(&Accent).BorderBackgroundColor(Mint.CopyWithNewOpacity(.8f))]]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14,0,0,0)
            [SNew(STextBlock).Font(LightBoxPaint::Spaced(22,40)).ColorAndOpacity(Paper).Text(FText::FromString(Name))];
    };
    Menu=SNew(SOverlay)
    +SOverlay::Slot()[SNew(SLightBoxMenuPanel)]
    +SOverlay::Slot().Padding(FMargin(24,22,24,22))
    [SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(4,0,0,12)
        [SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,0,12,0)
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(LightBoxPaint::Spaced(16,30)).ColorAndOpacity(Mint).Text(FText::FromString(TEXT("景色の説明")))]
                +SVerticalBox::Slot().AutoHeight().Padding(0,2,0,0)
                [SNew(SBox).MaxDesiredHeight(108)
                    [SNew(SScrollBox).Style(AtelierMenuScrollStyle()).AllowOverscroll(EAllowOverscroll::No).ScrollBarThickness(FVector2D(5,5))
                        +SScrollBox::Slot()[SNew(STextBlock).Font(LightBoxPaint::Font(18)).WrapTextAt(480).AutoWrapText(true).LineHeightPercentage(1.15f).ColorAndOpacity(Paper.CopyWithNewOpacity(.88f))
                            .Text_Lambda([this](){const FString Note=SceneDescription();return FText::FromString(Note.IsEmpty()?TEXT("この景色の説明はまだありません"):Note);})]]]
            ]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
            [SNew(SBox).WidthOverride(72).HeightOverride(72)
                [SNew(SButton).ButtonStyle(AtelierMenuStyle()).ContentPadding(22).ToolTipText(FText::FromString(TEXT("メニューを閉じる")))
                    .OnClicked_Lambda([this](){CloseMenu();return FReply::Handled();})[LightBoxUI::Icon(TEXT("close"))]]]
        ]
        // Header, spending and exit stay reachable; only the body scrolls on constrained screens.
        +SVerticalBox::Slot().FillHeight(1)
        [SNew(SScrollBox).Style(AtelierMenuScrollStyle()).AllowOverscroll(EAllowOverscroll::No).ScrollBarThickness(FVector2D(5,5)).ScrollBarPadding(FMargin(8,2,0,2))
            +SScrollBox::Slot()
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(4,0,0,6)[Section(TEXT("編集"))]
                +SVerticalBox::Slot().AutoHeight()[Row(TEXT("Undo"),TEXT("undo"),TEXT("戻す"),TEXT("Redo"),TEXT("redo"),TEXT("進む"))]
                +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[Row(TEXT("Journal"),TEXT("history"),TEXT("履歴"),TEXT("Refine"),TEXT("sparkle"),TEXT("仕上げる"))]
                +SVerticalBox::Slot().AutoHeight().Padding(4,10,0,6)[Section(TEXT("視点"))]
                +SVerticalBox::Slot().AutoHeight()[Row(TEXT("ViewReset"),TEXT("recenter"),TEXT("正面に戻す"),TEXT("Orbit"),TEXT("orbit"),TEXT("自動回転"))]
                +SVerticalBox::Slot().AutoHeight().Padding(4,10,0,6)[Section(TEXT("設定"))]
                +SVerticalBox::Slot().AutoHeight()[SettingRow(TEXT("Model"),TEXT("chip"),TEXT("AIモデル"))]
                +SVerticalBox::Slot().AutoHeight().Padding(0,7,0,0)[SettingRow(TEXT("Auth"),TEXT("key"),TEXT("接続設定"))]
                +SVerticalBox::Slot().AutoHeight().Padding(0,7,0,0)[SettingRow(TEXT("Budget"),TEXT("dots"),TEXT("粒子上限"))]
            ]
        ]
        +SVerticalBox::Slot().AutoHeight().Padding(6,10,6,0)
        [SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
            [SNew(STextBlock).Font(LightBoxPaint::Font(21)).ColorAndOpacity(Paper.CopyWithNewOpacity(.84f)).Text_Lambda([this](){
                int32 Count=0;if(HUD.IsValid())if(auto* PC=HUD->GetOwningPlayerController())if(auto* P=Cast<ALightBoxPawn>(PC->GetPawn()))if(P->PointCloud)Count=P->PointCloud->PointTotal();
                return FText::FromString(FText::AsNumber(Count).ToString()+TEXT(" dots"));})]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12,0,0,0)
            [SNew(STextBlock).Font(LightBoxPaint::Font(18)).ColorAndOpacity(Paper.CopyWithNewOpacity(.8f)).Text_Lambda([D](){return FText::FromString(D()?D()->GetJournal().Summary():FString());})]
        ]
        +SVerticalBox::Slot().AutoHeight().Padding(6,5,6,10)
        [SNew(STextBlock).Font(LightBoxPaint::Font(17)).ColorAndOpacity(Paper.CopyWithNewOpacity(.65f)).Text_Lambda([D](){return FText::FromString(D()?D()->CurrentCreditsLabel():FString());})]
        +SVerticalBox::Slot().AutoHeight()[Action(TEXT("Home"),TEXT("home"),TEXT("トップへ戻る"))]
    ];
    Composer=MakeShared<FLightBoxComposer>();
    auto CanType=[this,D](){return !bMenu&&HUD.IsValid()&&!HUD->IsModalOpen()&&D()&&!D()->bListening;};
    ChildSlot
    [SNew(SSafeZone).IsTitleSafe(false).PadLeft(false).PadRight(false)
    [SNew(SDPIScaler).DPIScale_Lambda([this](){return Scale;})
    [SNew(SOverlay)
        // Narration, bottom centre: one quiet line of words over the signature hairline.
        +SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(TAttribute<FMargin>::CreateLambda([this](){const float Side=FMath::Max(LeftReserve(),RightReserve());return FMargin(Side,0,Side,56);}))
        [SNew(SBox).HAlign(HAlign_Center).Visibility_Lambda([this](){return bMenu||(HUD.IsValid()&&HUD->IsModalOpen())?EVisibility::Collapsed:EVisibility::HitTestInvisible;})
        [SNew(SBox).WidthOverride_Lambda([this](){return FOptionalSize(FMath::Min(640.f,FMath::Max(260.f,StatusWidth())));})
        [SNew(SVerticalBox)
            // While a scene is being made: three quiet steps, the current one lit.
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,10)
            [StageStrip()]
            // Fill, not centre: auto-wrap inside a shrink-to-fit slot collapses to a few characters per line.
            +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Fill).Padding(16,0,16,12)
            [SNew(STextBlock).Font(LightBoxPaint::Spaced(23,180)).ColorAndOpacity(Paper.CopyWithNewOpacity(.92f)).Justification(ETextJustify::Center).LineHeightPercentage(1.2f)
                .ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor(0,0,0,.6f))
                .AutoWrapText(true)
                .Text_Lambda([this](){return FText::FromString(LightBoxKana::WrapPhrases(NarrationText(),LightBoxPaint::Spaced(23,180),FMath::Min(540.f,FMath::Max(200.f,StatusWidth()-120))));})]
            +SVerticalBox::Slot().AutoHeight().Padding(10,0,10,10)
            [SNew(STextBlock).Font(LightBoxPaint::Font(18)).ColorAndOpacity(Paper.CopyWithNewOpacity(.78f)).Justification(ETextJustify::Center).AutoWrapText(true).LineHeightPercentage(1.12f)
                .Visibility_Lambda([this](){return !bKeyboard&&KeyboardOpen<=0&&HUD.IsValid()&&!HUD->IsModalOpen()&&!IsSceneBusy()&&!SceneDescription().IsEmpty()&&StatusAge>=5.f?EVisibility::HitTestInvisible:EVisibility::Collapsed;})
                .Text_Lambda([this](){const FString Note=SceneDescription();return FText::FromString(Note.Len()>48?Note.Left(48)+TEXT("…"):Note);})]
            +SVerticalBox::Slot().AutoHeight()
            [SNew(SBox).HeightOverride(18)[SNew(SLightBoxProgress).Percent_Lambda([this](){return SceneProgress();}).Busy_Lambda([this](){return IsSceneBusy();})]]
        ]]]
        // Tapping outside the open menu closes it.
        +SOverlay::Slot()
        [SNew(SButton).ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder")).ContentPadding(0)
            .Visibility_Lambda([this](){return bMenu?EVisibility::Visible:EVisibility::Collapsed;})
            .OnClicked_Lambda([this](){CloseMenu();return FReply::Handled();})]
        // Words, top centre: what has been typed, with the send button right after it.
        +SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0,26,0,0)
        [SNew(SHorizontalBox).IsEnabled_Lambda(CanType).Visibility_Lambda([this](){return bMenu||(HUD.IsValid()&&HUD->IsModalOpen())?EVisibility::Collapsed:EVisibility::SelfHitTestInvisible;})
            // Same width as the send button on the right, so the words sit on the screen's centre line.
            +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(86)]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [SNew(SLightBoxPrompt).Composer(Composer).Open_Lambda([this](){return bKeyboard;})
                .MaxWidth_Lambda([this](){return FMath::Clamp(Units()-2*(64+42+40)-110,300.f,980.f);})
                .OnClicked_Lambda([this](){SetKeyboard(!bKeyboard);})]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10,0,0,0)
            [SAssignNew(Send,SLightBoxSendButton)
                .Ready_Lambda([this,D](){return D()&&!D()->bThinking&&!D()->bListening&&!Composer->FullText().TrimStartAndEnd().IsEmpty();})
                .Busy_Lambda([D](){return D()&&D()->bThinking;})
                .OnClicked_Lambda([this,D](){if(D()&&D()->bThinking){D()->Cancel();D()->Status=TEXT("取り消しました");return;}Submit();})]
        ]
        // Voice, right centre.
        +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(TAttribute<FMargin>::CreateLambda([this](){return FMargin(0,30,40+RightInset/Scale,0);}))
        [SNew(SLightBoxMicButton).Visibility_Lambda([this](){return bMenu||(HUD.IsValid()&&HUD->IsModalOpen())?EVisibility::Collapsed:EVisibility::Visible;})
            .Listening_Lambda([D](){return D()&&D()->bListening;})
            .VoiceLevel_Lambda([D](){return D()?D()->VoiceLevel:0.f;})
            .IsActive_Lambda([this,D](){return !bMenu&&HUD.IsValid()&&!HUD->IsModalOpen()&&D()&&!D()->bThinking;})
            .OnClicked_Lambda([this](){Activate(TEXT("Voice"));})]
        // Sensor column, left centre (mirroring the mic on the right); it steps aside while the keyboard is out.
        +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(TAttribute<FMargin>::CreateLambda([this](){return FMargin(32+LeftInset/Scale,30,0,0);}))
        [SAssignNew(Dock,SLightBoxSensorDock)
            .Reading([this]()->const FLightBoxSensorReading*{if(!HUD.IsValid())return nullptr;auto* P=Cast<ALightBoxPawn>(HUD->GetOwningPlayerController()->GetPawn());return P?&P->GetSensorReading():nullptr;})
            .IsOn([this](int32 I){auto* P=HUD.IsValid()?Cast<ALightBoxPawn>(HUD->GetOwningPlayerController()->GetPawn()):nullptr;return !P||P->IsSensorOn(ELightBoxSensor(I));})
            .OnToggle([this](int32 I){
                auto* P=HUD.IsValid()?Cast<ALightBoxPawn>(HUD->GetOwningPlayerController()->GetPawn()):nullptr;if(!P)return;
                const ELightBoxSensor Sensor=ELightBoxSensor(I);P->SetSensorOn(Sensor,!P->IsSensorOn(Sensor));
                static const TCHAR* Names[]={TEXT("マイク（息・拍手）"),TEXT("ぬくもり"),TEXT("照度センサ（明るさ）"),TEXT("近接センサ（手）"),TEXT("磁気センサ（磁石）"),TEXT("IMU（傾き）")};
                FString Line=FString(Names[I])+(P->IsSensorOn(Sensor)?TEXT(" オン"):TEXT(" オフ"));
                if(Sensor==ELightBoxSensor::Hand&&P->IsSensorOn(Sensor))Line+=TEXT("：手を近づけるほどオーロラが強く浮かび上がります");
                if(Sensor==ELightBoxSensor::Tilt&&P->IsSensorOn(Sensor))Line+=TEXT("：端末を傾けると粒が流れます");
                if(auto* D=HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>())D->Status=Line;})]
        // The Atelier's own keyboard slides in on the left.
        +SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(TAttribute<FMargin>::CreateLambda([this](){return FMargin(28+LeftInset/Scale,0,0,26);}))
        [SNew(SBox).Visibility_Lambda([this](){return KeyboardOpen>0?EVisibility::SelfHitTestInvisible:EVisibility::Collapsed;})
        // 1.2x: keys about 7 x 6 mm on a 450 ppi phone, comfortable for flicks; text and hit areas scale together.
        [SNew(SDPIScaler).DPIScale(KeyboardScale)
        [SAssignNew(Keyboard,SLightBoxKeyboard).Composer(Composer)
            .OnClose_Lambda([this](){SetKeyboard(false);})]]]
        +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(TAttribute<FMargin>::CreateLambda([this](){return FMargin(0,30,42+RightInset/Scale,30);}))
        [SNew(SBox).WidthOverride(620).HeightOverride_Lambda([this](){return FOptionalSize(FMath::Min(1020.f,FMath::Max(480.f,float(HostSize.Y/Scale)-60.f)));}).Visibility_Lambda([this](){return Progress>0?EVisibility::SelfHitTestInvisible:EVisibility::Collapsed;})[Menu.ToSharedRef()]]
        +SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(TAttribute<FMargin>::CreateLambda([this](){return FMargin(0,40,52+RightInset/Scale,0);}))
        [SNew(SBox).WidthOverride(76).HeightOverride(76).Visibility_Lambda([this](){return bMenu?EVisibility::Collapsed:EVisibility::SelfHitTestInvisible;})
        [SNew(SButton).ButtonStyle(LightBoxUI::MenuButtonStyle()).ContentPadding(20).ToolTipText(FText::FromString(TEXT("メニュー")))
            .OnClicked_Lambda([this](){if(bMenu)CloseMenu();else OpenMenu();return FReply::Handled();})
            [SNew(SOverlay)
                +SOverlay::Slot()[SNew(SBox).Visibility_Lambda([this](){return bMenu?EVisibility::Collapsed:EVisibility::HitTestInvisible;})[LightBoxUI::Icon(TEXT("menu"))]]
                +SOverlay::Slot()[SNew(SBox).Visibility_Lambda([this](){return bMenu?EVisibility::HitTestInvisible:EVisibility::Collapsed;})[LightBoxUI::Icon(TEXT("close"))]]
            ]]]
    ]]];
    SetVisibility(EVisibility::SelfHitTestInvisible);
    KeyRouter=MakeShared<FLightBoxKeyRouter>(TWeakPtr<SLightBoxControls>(SharedThis(this)));
    FSlateApplication::Get().RegisterInputPreProcessor(KeyRouter);
}

bool SLightBoxControls::CanUse(FName Id)const
{
    if(!HUD.IsValid()||!bMenu)return false;auto* D=HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>();
    if(Id==TEXT("Refine"))return !IsSceneBusy();
    return Id==TEXT("Undo")?D->CanUndo():Id==TEXT("Redo")?D->CanRedo():Id!=TEXT("Restore")||D->bHasSavedScene;
}
bool SLightBoxControls::IsOrbiting() const
{
    if(HUD.IsValid())if(auto* PC=HUD->GetOwningPlayerController())if(auto* P=Cast<ALightBoxPawn>(PC->GetPawn()))return P->IsCameraOrbiting();
    return false;
}

TSharedRef<SWidget> SLightBoxControls::Action(FName Id,const TCHAR* Name,const TCHAR* Label)
{
    TSharedPtr<SButton> Button;
    auto Widget=SNew(SBox).HeightOverride(82)
    [SAssignNew(Button,SButton).ButtonStyle(AtelierMenuStyle()).TouchMethod(EButtonTouchMethod::PreciseTap).ContentPadding(FMargin(16,12)).HAlign(HAlign_Center).VAlign(VAlign_Center)
        .ToolTipText(FText::FromString(Label)).IsEnabled_Lambda([this,Id](){return CanUse(Id);})
        .OnClicked_Lambda([this,Id](){return Activate(Id);})
        [SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(30).HeightOverride(30)[SNew(SLightBoxIcon).Name(Name)]]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16,0,0,0)
            [SNew(STextBlock).Font(LightBoxPaint::Spaced(22,20)).ColorAndOpacity_Lambda([this,Id](){return Id==TEXT("Orbit")&&IsOrbiting()?Mint:Paper;})
                .Text_Lambda([this,Id,Caption=FString(Label)](){return FText::FromString(Id==TEXT("Orbit")&&IsOrbiting()?TEXT("回転を止める"):Caption);})]
        ]];
    if(Id==TEXT("Orbit"))OrbitButton=Button;
    return Widget;
}

FString SLightBoxControls::SettingValue(FName Id) const
{
    auto* D=HUD.IsValid()?HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>():nullptr;
    if(!D)return FString(TEXT("—"));
    if(Id==TEXT("Model"))return D->ModelLabel();
    if(Id==TEXT("Budget"))return FText::AsNumber(D->PointBudget).ToString();
    const auto& Ready=D->GetReadiness();
    if(Ready.GetStep()==ELightBoxReadyStep::Ready)return TEXT("接続済み");
    if(Ready.IsBusy())return TEXT("確認中");
    return D->HasCredentials()?TEXT("未確認"):TEXT("未設定");
}

TSharedRef<SWidget> SLightBoxControls::SettingRow(FName Id,const TCHAR* Name,const TCHAR* Label)
{
    return SNew(SBox).HeightOverride(72)
    [SNew(SButton).ButtonStyle(AtelierMenuStyle()).TouchMethod(EButtonTouchMethod::PreciseTap).ContentPadding(FMargin(18,12)).HAlign(HAlign_Fill).VAlign(VAlign_Center)
        .IsEnabled_Lambda([this,Id](){return CanUse(Id);}).ToolTipText_Lambda([this,Id,Caption=FString(Label)](){return FText::FromString(Caption+TEXT("：")+SettingValue(Id));})
        .OnClicked_Lambda([this,Id](){return Activate(Id);})
        [SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(28).HeightOverride(28)[SNew(SLightBoxIcon).Name(Name)]]
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(16,0,10,0)
            [SNew(STextBlock).Font(LightBoxPaint::Spaced(22,20)).ColorAndOpacity(Paper).Text(FText::FromString(Label))]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [SNew(SBox).MaxDesiredWidth(206)
                [SNew(STextBlock).Font(LightBoxPaint::Font(21)).OverflowPolicy(ETextOverflowPolicy::Ellipsis).ColorAndOpacity_Lambda([this,Id](){
                    auto* D=HUD.IsValid()?HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>():nullptr;
                    return Id==TEXT("Auth")&&(!D||D->GetReadiness().GetStep()!=ELightBoxReadyStep::Ready)?Paper.CopyWithNewOpacity(.65f):Mint.CopyWithNewOpacity(.92f);})
                    .Text_Lambda([this,Id](){return FText::FromString(SettingValue(Id));})]]
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14,0,0,0)
            [SNew(SBox).WidthOverride(18).HeightOverride(24)[SNew(SLightBoxIcon).Name(TEXT("next"))]]
        ]];
}

void SLightBoxControls::Tick(const FGeometry& G,double Time,float Dt)
{
    const bool Orbit=IsOrbiting();
    if(Orbit!=bMenuOrbit&&OrbitButton.IsValid()){bMenuOrbit=Orbit;OrbitButton->SetButtonStyle(AtelierMenuStyle(Orbit));}
    SCompoundWidget::Tick(G,Time,Dt);HostSize=G.GetLocalSize();Scale=FMath::Clamp(FMath::Min(float(HostSize.Y/1080),float((HostSize.X-LeftInset-RightInset)/1600)),.30f,1.6f);
#if PLATFORM_ANDROID
    InsetAge+=Dt;if(InsetAge>.5f){InsetAge=0;if(JNIEnv* Env=FAndroidApplication::GetJavaEnv()){
        const jmethodID Method=FJavaWrapper::FindMethod(Env,FJavaWrapper::GameActivityClassID,"AndroidThunkJava_LightBoxRightInset","()I",false);
        if(Method)RightInset=FJavaWrapper::CallIntMethod(Env,FJavaWrapper::GameActivityThis,Method);
        const jmethodID Left=FJavaWrapper::FindMethod(Env,FJavaWrapper::GameActivityClassID,"AndroidThunkJava_LightBoxLeftInset","()I",false);
        if(Left)LeftInset=FJavaWrapper::CallIntMethod(Env,FJavaWrapper::GameActivityThis,Left);
    }}
    // Android brings the navigation bar back after other apps, dialogs or the setup flow; keep the stage full-screen.
    ImmersiveAge+=Dt;if(ImmersiveAge>2.f){ImmersiveAge=0;if(JNIEnv* Env=FAndroidApplication::GetJavaEnv()){
        static const jmethodID Hide=FJavaWrapper::FindMethod(Env,FJavaWrapper::GameActivityClassID,"AndroidThunkJava_LightBoxKeepImmersive","()V",false);
        if(Hide)FJavaWrapper::CallVoidMethod(Env,FJavaWrapper::GameActivityThis,Hide);
    }}
#endif
    Progress=FMath::Clamp(Progress+(bMenu?1.f:-1.f)*Dt/.22f,0.f,1.f);const float Ease=1-FMath::Pow(1-Progress,3.f);
    Menu->SetRenderOpacity(Ease);Menu->SetRenderTransform(FSlateRenderTransform(FVector2D(0,(1-Ease)*-20)));
    Menu->SetVisibility(bMenu?EVisibility::SelfHitTestInvisible:EVisibility::HitTestInvisible);
    // The keyboard glides in from the left edge with a slight overshoot.
    KeyboardOpen=FMath::Clamp(KeyboardOpen+(bKeyboard?1.f:-1.f)*Dt/.28f,0.f,1.f);
    const float K=bKeyboard?1+2.2f*FMath::Pow(KeyboardOpen-1,3.f)+1.2f*FMath::Pow(KeyboardOpen-1,2.f):KeyboardOpen*KeyboardOpen;
    Keyboard->SetVisibility(bKeyboard?EVisibility::Visible:EVisibility::HitTestInvisible);
    Keyboard->SetRenderOpacity(FMath::Clamp(KeyboardOpen*1.6f,0.f,1.f));
    Keyboard->SetRenderTransform(FSlateRenderTransform(FVector2D((K-1)*(SLightBoxKeyboard::Width+40),0)));
    Dock->SetRenderOpacity(1-FMath::Clamp(KeyboardOpen*2.f,0.f,1.f));Dock->SetVisibility(bMenu||bKeyboard||KeyboardOpen>0?EVisibility::HitTestInvisible:EVisibility::Visible);
    Composer->Tick(FPlatformTime::Seconds());
    if(HUD.IsValid()){
        auto* D=HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>();
        FString Voice=D->TakeVoiceDraft();if(!Voice.IsEmpty()){Composer->SetText(Voice);Submit();}
        StatusAge+=Dt;if(LastStatus!=D->Status){LastStatus=D->Status;StatusAge=0;}
        GenerationAge=D->bThinking?0.f:GenerationAge+Dt;
        if(auto* P=Cast<ALightBoxPawn>(HUD->GetOwningPlayerController()->GetPawn()))if(P->PointCloud)P->PointCloud->SetThinking(D->bThinking);
        if(D->bListening)StatusAge=0;
        if(bKeyboard&&(HUD->IsModalOpen()||D->bListening))SetKeyboard(false);
    }
}
float SLightBoxControls::NoteWidth()const{return 300.f;}
int32 SLightBoxControls::GenerationStage()const
{
    if(!HUD.IsValid())return INDEX_NONE;auto* D=HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>();
    if(D->bListening||D->GetReadiness().IsBusy())return INDEX_NONE;
    if(D->bThinking)return D->OperationSeconds()<2.5?0:1;
    if(GenerationAge<6.f&&IsSceneBusy())return 2;
    return INDEX_NONE;
}
TSharedRef<SWidget> SLightBoxControls::StageStrip()
{
    static const TCHAR* Steps[]={TEXT("送信"),TEXT("考え中"),TEXT("組み立て")};
    TSharedRef<SHorizontalBox> Row=SNew(SHorizontalBox).Visibility_Lambda([this](){return GenerationStage()!=INDEX_NONE?EVisibility::HitTestInvisible:EVisibility::Collapsed;});
    for(int32 I=0;I<3;++I)
    {
        if(I>0)Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(14,0)[SNew(STextBlock).Font(LightBoxPaint::Font(16)).ColorAndOpacity(Paper.CopyWithNewOpacity(.3f)).Text(FText::FromString(TEXT("—")))];
        Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
        [SNew(STextBlock).Font(LightBoxPaint::Spaced(16,160)).Text(FText::FromString(Steps[I]))
            .ColorAndOpacity_Lambda([this,I](){const int32 S=GenerationStage();return FSlateColor(I==S?FLinearColor(.55f,1.f,.90f):Paper.CopyWithNewOpacity(I<S?.62f:.34f));})];
    }
    return Row;
}
FString SLightBoxControls::SceneDescription() const
{
    const auto* D=HUD.IsValid()?HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>():nullptr;
    return D?D->GetStyle().Note.TrimStartAndEnd():FString();
}
FString SLightBoxControls::NarrationText()const
{
    if(!HUD.IsValid())return FString();auto* D=HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>();
    if(D->bThinking&&D->Heard==TEXT("この景色の造形を磨いて"))return FString::Printf(TEXT("仕上げています · %d秒"),FMath::Max(0,FMath::FloorToInt(D->OperationSeconds())));
    if(D->bThinking)return D->Heard.IsEmpty()?FString::Printf(TEXT("%d秒"),FMath::Max(0,FMath::FloorToInt(D->OperationSeconds()))):TEXT("「")+D->Heard.Left(40)+FString::Printf(TEXT("」 · %d秒"),FMath::Max(0,FMath::FloorToInt(D->OperationSeconds())));
    if(IsSceneBusy())return SceneProgressText();
    // Fresh news first (a sensor toggled, an error); otherwise the scene's own name.
    if(StatusAge<5.f&&!D->Status.IsEmpty()&&D->Status!=D->GetStyle().Note)return D->Status;
    const FString Title=D->GetStyle().Title.TrimStartAndEnd();
    return Title.IsEmpty()?TEXT("ひかりが、動きはじめる"):Title;
}
void SLightBoxControls::SetKeyboard(bool bOpen)
{
    if(!bOpen&&Keyboard.IsValid())Keyboard->CancelInteraction();
    bKeyboard=bOpen;
    if(bOpen){bMenu=false;FSlateApplication::Get().SetKeyboardFocus(SharedThis(this),EFocusCause::SetDirectly);}
    else{Composer->CommitComposition();FSlateApplication::Get().SetAllUserFocusToGameViewport();}
}
bool SLightBoxControls::IsInteracting()const{return bMenu||bKeyboard;}
FReply SLightBoxControls::OnKeyChar(const FGeometry& G,const FCharacterEvent& E)
{
    // Typing arrives through the key router (FLightBoxKeyRouter) so it also works without focus; never type twice.
    if(bKeyboard)return FReply::Handled();
    return SCompoundWidget::OnKeyChar(G,E);
}
#if !UE_BUILD_SHIPPING
void SLightBoxControls::ReviewSensors(){if(Dock.IsValid())Dock->ShowDemo();}
void SLightBoxControls::ReviewKeyboard()
{
    SetKeyboard(true);Composer->SetText(TEXT("夜の"));Composer->Type(TEXT("ほしふるうみ"));
    if(Keyboard.IsValid()){if(FParse::Param(FCommandLine::Get(),TEXT("LightBoxLatinReview")))Keyboard->ShowLatin();else Keyboard->ShowFlick(4,2);}
}
#endif
bool SLightBoxControls::IsSceneBusy()const
{
    if(!HUD.IsValid())return false;auto* D=HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>();if(D->bThinking||D->bListening||D->GetReadiness().IsBusy())return true;
    // A storyboard keeps the particles flying between poses forever; that is the show, not a busy scene.
    if(auto* P=Cast<ALightBoxPawn>(HUD->GetOwningPlayerController()->GetPawn()))if(P->PointCloud&&!P->IsPlayingStory())return P->PointCloud->IsPreparing()||P->PointCloud->Progress()<1;
    return false;
}
TOptional<float> SLightBoxControls::SceneProgress()const
{
    if(!HUD.IsValid())return {};auto* D=HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>();if(D->bThinking||D->bListening)return {};
    if(D->GetReadiness().IsBusy())return D->GetReadiness().Fraction(FPlatformTime::Seconds());
    if(auto* P=Cast<ALightBoxPawn>(HUD->GetOwningPlayerController()->GetPawn()))if(P->PointCloud&&!P->PointCloud->IsPreparing()&&!P->IsPlayingStory())return P->PointCloud->Progress();
    return {};
}
FString SLightBoxControls::SceneProgressText()const
{
    if(!HUD.IsValid())return FString();auto* D=HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>();
    if(D->bThinking||D->bListening)return D->Heard.IsEmpty()?D->ProgressLabel():TEXT("「")+D->Heard.Left(70)+TEXT("」\n")+D->ProgressLabel();
    if(D->GetReadiness().IsBusy())return D->GetReadiness().Label();const auto Value=SceneProgress();
    return Value.IsSet()?FString::Printf(TEXT("粒子を配置中 · %d%%"),FMath::Clamp(FMath::FloorToInt(Value.GetValue()*100),0,100)):TEXT("粒子の行き先を準備中");
}
void SLightBoxControls::OpenMenu(){if(bKeyboard)SetKeyboard(false);bMenu=true;FSlateApplication::Get().SetKeyboardFocus(SharedThis(this),EFocusCause::SetDirectly);}
void SLightBoxControls::CloseMenu(){bMenu=false;FSlateApplication::Get().SetAllUserFocusToGameViewport();}
void SLightBoxControls::FocusInput(){SetKeyboard(true);}
FReply SLightBoxControls::OnKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    const FKey Key=E.GetKey();
    if(Key==EKeys::Escape){if(bKeyboard)SetKeyboard(false);else CloseMenu();return FReply::Handled();}
    if(!bKeyboard)return SCompoundWidget::OnKeyDown(G,E);
    const bool bComposing=Composer->IsComposing()||Composer->bConverting;
    if(Key==EKeys::BackSpace){Composer->Backspace();return FReply::Handled();}
    if(Key==EKeys::SpaceBar && Keyboard.IsValid()&&Keyboard->IsLiteralMode()){Composer->TypeLiteral(TEXT(" "));return FReply::Handled();}
    if(Key==EKeys::SpaceBar||(Key==EKeys::Down&&Composer->bConverting)){Composer->Convert();return FReply::Handled();}
    // Enter confirms a composition; with nothing left to confirm (or with Ctrl) it sends, as on any keyboard.
    if(Key==EKeys::Enter){if(E.IsControlDown()||!bComposing)Submit();else Composer->CommitComposition();return FReply::Handled();}
    if(Composer->bConverting&&(Key==EKeys::Left||Key==EKeys::Right)){
        Composer->MoveFocus(Key==EKeys::Right?1:-1);return FReply::Handled();
    }
    return FReply::Handled();
}
FReply SLightBoxControls::Submit()
{
    if(!HUD.IsValid())return FReply::Handled();auto* D=HUD->GetGameInstance()->GetSubsystem<ULightBoxDirector>();
    Composer->CommitComposition();const FString Words=Composer->FullText().TrimStartAndEnd();
    if(!D->bThinking&&!Words.IsEmpty()){
        const int32 Revision=D->GetRevision();D->Submit(Words);
        if(D->bThinking||D->GetRevision()!=Revision){Composer->Clear();if(Send.IsValid())Send->Launch();SetKeyboard(false);}
    }
    return FReply::Handled();
}
FReply SLightBoxControls::Activate(FName Id){if(HUD.IsValid()){CloseMenu();HUD->NotifyHitBoxClick(Id);}return FReply::Handled();}
