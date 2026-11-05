#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class ALightBoxHUD;
class SEditableTextBox;
struct FButtonStyle;
struct FEditableTextBoxStyle;

/** Persistent glass controls. Only interactive regions consume scene gestures. */
class SLightBoxControls : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SLightBoxControls) {}
        SLATE_ARGUMENT(TWeakObjectPtr<ALightBoxHUD>, HUD)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual void Tick(const FGeometry&, double, float) override;
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent&) override;
    void OpenMenu();
    void FocusInput();
    bool IsInteracting() const;
private:
    TWeakObjectPtr<ALightBoxHUD> HUD;
    TSharedPtr<SEditableTextBox> Input;
    TSharedPtr<SWidget> Menu;
    float Scale=1, Progress=0, StatusAge=10;
    FVector2D HostSize=FVector2D(1600,900);
    bool bMenu=false;
    FString LastStatus;
    void CloseMenu();
    FReply Submit();
    FReply Activate(FName Action);
    TSharedRef<SWidget> Action(FName Id,const TCHAR* Icon,const TCHAR* Label);
};

namespace LightBoxUI
{
    TSharedRef<SWidget> Icon(const TCHAR* Name);
    const FButtonStyle* ButtonStyle(bool Primary=false);
    const FEditableTextBoxStyle* FieldStyle();
    TSharedRef<SWidget> AnimateDialog(TWeakObjectPtr<ALightBoxHUD> HUD,TSharedRef<SWidget> Content);
}
