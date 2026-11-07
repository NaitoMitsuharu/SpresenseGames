#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../LightBoxKana.h"
#include "Styling/CoreStyle.h"
#include "../LightBoxInput.h"
#include "../LightBoxAmbience.h"
#include "../LightBoxSensors.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SWindow.h"
#include "Input/HittestGrid.h"
#include "Types/PaintArgs.h"
#include "../LightBoxGame.h"
#include "../LightBoxPaint.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxKanaInputTest,"SpresenseMaze.LightBox.KanaInput",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxKanaInputTest::RunTest(const FString&)
{
    TestEqual(TEXT("Flick up on な is ぬ"),LightBoxKana::FlickGlyphs(4).Mid(2,1),FString(TEXT("ぬ")));
    TestEqual(TEXT("か gains a dakuten"),LightBoxKana::NextVariant(TEXT('か')),TEXT('が'));
    TestEqual(TEXT("は cycles back after ぱ"),LightBoxKana::NextVariant(LightBoxKana::NextVariant(LightBoxKana::NextVariant(TEXT('は')))),TEXT('は'));
    TestEqual(TEXT("つ shrinks first"),LightBoxKana::NextVariant(TEXT('つ')),TEXT('っ'));
    TestEqual(TEXT("Katakana"),LightBoxKana::ToKatakana(TEXT("ほしふる")),FString(TEXT("ホシフル")));

    auto Romaji=[](const TCHAR* Typed){LightBoxKana::FRomaji R;FString Out;for(const TCHAR* C=Typed;*C;++C)R.Feed(*C,Out);R.Flush(Out);return Out;};
    TestEqual(TEXT("Romaji words"),Romaji(TEXT("hoshifuruumi")),FString(TEXT("ほしふるうみ")));
    TestEqual(TEXT("Double consonant"),Romaji(TEXT("kitte")),FString(TEXT("きって")));
    TestEqual(TEXT("n before a consonant"),Romaji(TEXT("sanpo")),FString(TEXT("さんぽ")));
    TestEqual(TEXT("Trailing n"),Romaji(TEXT("hon")),FString(TEXT("ほん")));
    TestEqual(TEXT("Contracted sounds"),Romaji(TEXT("kyou")),FString(TEXT("きょう")));

    TArray<FLightBoxKanaSegment> Segments;
    TestTrue(TEXT("Conversion parses"),LightBoxKana::ParseConversion(TEXT("[[\"ほしふる\",[\"星降る\",\"ﾎｼﾌﾙ\"]],[\"うみ\",[\"海\",\"膿\"]]]"),Segments));
    if(Segments.Num()==2)
    {
        TestEqual(TEXT("Best spelling first"),Segments[0].Chosen(),FString(TEXT("星降る")));
        TestFalse(TEXT("Half-width katakana dropped"),Segments[0].Candidates.Contains(TEXT("ﾎｼﾌﾙ")));
        TestTrue(TEXT("Kana always offered"),Segments[1].Candidates.Contains(TEXT("うみ"))&&Segments[1].Candidates.Contains(TEXT("ウミ")));
    }
    TestFalse(TEXT("Garbage rejected"),LightBoxKana::ParseConversion(TEXT("<html>"),Segments));

    // Composition without the network: typing, cycling, converting to kana and committing.
    TSharedRef<FLightBoxComposer> C=MakeShared<FLightBoxComposer>();
    C->Type(TEXT("か"));TestTrue(TEXT("Dakuten replaces the last kana"),C->ReplaceLast([](TCHAR K){return LightBoxKana::NextVariant(K);}));
    TestEqual(TEXT("Reading"),C->Reading,FString(TEXT("が")));
    C->Convert();TestTrue(TEXT("Converting"),C->bConverting);
    C->Convert();TestEqual(TEXT("Second press steps to katakana"),C->FullText(),FString(TEXT("ガ")));
    C->CommitComposition();TestEqual(TEXT("Committed"),C->Committed,FString(TEXT("ガ")));
    C->Backspace();TestTrue(TEXT("Backspace clears"),C->IsEmpty());
    C->Convert();TestEqual(TEXT("変換 with nothing typed is a space"),C->Committed,FString(TEXT("　")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxNoteWrapTest,"SpresenseMaze.LightBox.NoteWrap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxNoteWrapTest::RunTest(const FString&)
{
    const FSlateFontInfo Font=FCoreStyle::GetDefaultFontStyle("Regular",17);
    const FString Note=TEXT("流体の物理計算ではなく、設定した経路と形によるアニメーションです");
    TArray<FString> Lines;LightBoxKana::WrapPhrases(Note,Font,340).ParseIntoArray(Lines,TEXT("\n"));
    TestTrue(TEXT("Wraps"),Lines.Num()>=2);
    TestEqual(TEXT("Nothing lost"),FString::Join(Lines,TEXT("")),Note);
    for(const FString& Line:Lines)
    {
        TestFalse(TEXT("No line starts with punctuation or a small kana"),FString(TEXT("、。ーっゃゅょ")).Contains(Line.Left(1)));
        TestTrue(TEXT("No stray syllable line"),Line.Len()>=4);
    }
    TestFalse(TEXT("Words stay whole"),Note.Contains(TEXT("アニメーショ\n")));
    AddInfo(FString::Join(Lines,TEXT(" | ")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxComposerAsyncTest,"SpresenseMaze.LightBox.ConversionRequestLifetime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxComposerAsyncTest::RunTest(const FString&)
{
    auto C=MakeShared<FLightBoxComposer>();
    C->Type(TEXT("か")); C->Convert();
    auto Old=C->Request;
    if (!TestTrue(TEXT("Conversion request started"),Old.IsValid())) { return false; }
    auto OldReply=Old->OnProcessRequestComplete();
    C->Clear(); C->Type(TEXT("か"));
    const uint32 Revision=C->Revision;
    OldReply.ExecuteIfBound(Old,nullptr,false);
    TestEqual(TEXT("A cancelled response for identical retyped kana cannot mutate state"),C->Revision,Revision);
    TestTrue(TEXT("Cancelled request cannot fill the candidate cache"),C->Live.IsEmpty());
    C->Convert(); auto Current=C->Request; auto Reply=Current->OnProcessRequestComplete();
    C->Convert(); TestEqual(TEXT("User picked katakana while waiting"),C->FullText(),FString(TEXT("カ")));
    Reply.ExecuteIfBound(Current,nullptr,false);
    TestEqual(TEXT("A late offline fallback preserves the selected spelling"),C->FullText(),FString(TEXT("カ")));
    C->CommitComposition(); TestFalse(TEXT("Commit releases the request"),C->Request.IsValid());
    C->Clear(); C->TypeRomaji(TEXT('k')); C->TypeRomaji(TEXT('a'));
    TestEqual(TEXT("Composer retains romaji across key events"),C->FullText(),FString(TEXT("か")));
    C->TypeRomaji(TEXT('n')); C->Type(TEXT("は"));
    TestEqual(TEXT("Switching from romaji to flick preserves the pending n"),C->FullText(),FString(TEXT("かんは")));
    C->Clear(); C->Type(TEXT("ほしうみ")); C->bConverting=true;
    C->Segments={{TEXT("ほし"),{TEXT("星"),TEXT("ほし")},0},{TEXT("うみ"),{TEXT("海"),TEXT("うみ")},0}};
    C->MoveFocus(1); C->Pick(1);
    TestEqual(TEXT("Picking the final segment commits all spellings"),C->FullText(),FString(TEXT("星うみ")));
    TestFalse(TEXT("Conversion is settled"),C->bConverting);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxTouchKeyboardTest,"SpresenseMaze.LightBox.TouchKeyboard",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxTouchKeyboardTest::RunTest(const FString&)
{
    auto C=MakeShared<FLightBoxComposer>();
    auto K=SNew(SLightBoxKeyboard).Composer(C);
    const FVector2D A(K->KeyRect(0).GetCenter()),Na(K->KeyRect(4).GetCenter()),DeleteKey(K->KeyRect(SLightBoxKeyboard::Delete).GetCenter());
    auto Window=SNew(SWindow).ClientSize(FVector2D(SLightBoxKeyboard::Width,SLightBoxKeyboard::Height)); Window->SetContent(K);
    const FGeometry G=FGeometry::MakeRoot(FVector2D(SLightBoxKeyboard::Width,SLightBoxKeyboard::Height),FSlateLayoutTransform());
    FArrangedChildren Arranged(EVisibility::All); Arranged.AddWidget(FArrangedWidget(Window,G)); Arranged.AddWidget(FArrangedWidget(K,G));
    const FWidgetPath Path(Window,Arranged);
    const auto User=FSlateApplication::Get().GetUser(0);
    auto Event=[](uint32 Finger,FVector2D P,bool Down){return FPointerEvent(0u,Finger,P,P,1.f,Down);};
    auto Down=[&](uint32 Finger,FVector2D P){K->OnTouchStarted(G,Event(Finger,P,true)); User->SetPointerCaptor(Finger,K,Path);};
    auto Up=[&](uint32 Finger,FVector2D P){K->OnTouchEnded(G,Event(Finger,P,false)); User->ReleaseCapture(Finger);};
    Down(2,A); Up(2,A);
    Down(2,A); Up(2,A);
    TestEqual(TEXT("Two taps cycle even though capture is released each time"),C->Reading,FString(TEXT("い")));
    C->Clear(); Down(2,Na);
    K->OnTouchStarted(G,Event(3,A,true)); K->OnTouchEnded(G,Event(3,A,false));
    TestTrue(TEXT("A second finger cannot release the first key"),C->IsEmpty());
    K->OnTouchMoved(G,Event(2,Na-FVector2D(0,60),true));
    Up(2,Na-FVector2D(0,60));
    TestEqual(TEXT("Captured touch movement flicks な up to ぬ"),C->Reading,FString(TEXT("ぬ")));
    C->SetText(TEXT("abc")); Down(2,DeleteKey);
    TestEqual(TEXT("Delete removes one glyph immediately"),C->FullText(),FString(TEXT("ab")));
    K->CancelInteraction(); K->Tick(G,0,1);
    TestEqual(TEXT("Closing a held Delete stops repeat"),C->FullText(),FString(TEXT("ab")));
    TestFalse(TEXT("Closing releases the touch capture"),User->DoesWidgetHaveAnyCapture(K));
    C->Clear(); Down(2,A); User->ReleaseCapture(2); K->OnTouchEnded(G,Event(2,A,false));
    TestTrue(TEXT("Losing capture cancels a key without inserting it"),C->IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxSensorSwitchTest,"SpresenseMaze.LightBox.SensorSwitchIsolation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxSensorSwitchTest::RunTest(const FString&)
{
    FLightBoxSensorReading Input; Input.Breath=Input.Warmth=Input.Darkness=Input.Brightness=Input.Near=Input.Magnet=1;
    Input.MagnetScreen=FVector2D(1,0); Input.bClap=Input.bNearReleased=true;
    Input.bHasMic=Input.bHasLight=Input.bHasNear=Input.bHasMagnet=true;
    bool On[int32(ELightBoxSensor::Count)]={false,false,false,false,false};
    auto R=Input; FLightBoxSensorReader::ApplySwitches(R,On);
    TestTrue(TEXT("Disabled channels and one-frame events are zero"),R.Breath==0&&R.Warmth==0&&R.Darkness==0&&R.Brightness==0&&R.Near==0&&R.Magnet==0&&!R.bClap&&!R.bNearReleased&&R.MagnetScreen.IsZero());
    TestTrue(TEXT("Off channels still report hardware availability to the dock"),R.bHasMic&&R.bHasLight&&R.bHasNear&&R.bHasMagnet);
    TestTrue(TEXT("No disabled sensor supplies a particle force"),FLightBoxSensorReader::ToEnvironment(R,FVector::ForwardVector,FVector::RightVector,FVector::UpVector).IsZero());
    FLightBoxSensorGains Gains; Gains.Sound=Gains.Warmth=Gains.Light=Gains.Hand=Gains.Magnet=2;
    R=FLightBoxSensorReader::ApplyGains(Input,Gains);
    TestTrue(TEXT("Sensitivity cannot invert darkness or extrapolate colours"),R.Breath==1&&R.Warmth==1&&R.Darkness==1&&R.Brightness==1&&R.Near==1&&R.Magnet==1);
    float D,B; FLightBoxSensorReader::LightLevels(100,300,D,B); TestEqual(TEXT("Three times darker is ignored"),D,0.f);
    FLightBoxSensorReader::LightLevels(900,300,D,B); TestEqual(TEXT("Three times brighter is ignored"),B,0.f);
    TestEqual(TEXT("Portrait-natural bottom microphone at ROTATION_90"),SLightBoxAmbience::MicSide(1),1);
    TestEqual(TEXT("Portrait-natural bottom microphone at ROTATION_270"),SLightBoxAmbience::MicSide(3),-1);
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxAmbienceLifecycleTest,"SpresenseMaze.LightBox.AmbiencePawnLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxAmbienceLifecycleTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    auto* PC=World->SpawnActor<APlayerController>();
    auto A=SNew(SLightBoxAmbience).Controller(PC);
    const FGeometry G=FGeometry::MakeRoot(FVector2D(1600,720),FSlateLayoutTransform());
    A->Tick(G,0,.1f); TestFalse(TEXT("No pawn starts quiet"),A->HasEffect());
    auto* First=World->SpawnActor<ALightBoxPawn>(); PC->SetPawn(First);
    First->Felt.Breath=First->Felt.Warmth=1; First->Felt.bClap=true; First->ClapStartedAt=FPlatformTime::Seconds()-.2;
    A->Tick(G,.1,.1f); First->Felt.bClap=false; A->Tick(G,.2,.1f);
    TestTrue(TEXT("Late possession supplies the pawn"),A->Pawn.Get()==First);
    TestTrue(TEXT("Sensors produce trails, embers and a wave"),!A->Streaks.IsEmpty()&&!A->Embers.IsEmpty()&&!A->Waves.IsEmpty());
    TestTrue(TEXT("Sound wave uses the exact particle burst clock even after a delayed UI tick"),A->Waves.Num()==1&&A->Waves[0]==First->ClapStartedAt);
    First->bSensorOn[int32(ELightBoxSensor::Sound)]=First->bSensorOn[int32(ELightBoxSensor::Warmth)]=false;
    FLightBoxSensorReader::ApplySwitches(First->Felt,First->bSensorOn);
    A->Tick(G,.2,.1f);
    TestTrue(TEXT("Off switches clear existing sensor trails immediately"),A->Streaks.IsEmpty()&&A->Embers.IsEmpty()&&A->Waves.IsEmpty());
    auto* Second=World->SpawnActor<ALightBoxPawn>(); PC->SetPawn(Second); A->Tick(G,.3,.1f);
    TestTrue(TEXT("Replacing the pawn follows the new possession"),A->Pawn.Get()==Second);
    Second->Felt.Breath=1; A->Tick(G,.4,.1f); PC->SetPawn(nullptr); Second->Destroy(); A->Tick(G,.5,.1f);
    TestFalse(TEXT("Unpossession/destruction clears all ambience"),A->HasEffect());
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxVectorPaintTest,"SpresenseMaze.LightBox.VectorPaintBatch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxVectorPaintTest::RunTest(const FString&)
{
    auto Window=SNew(SWindow).ClientSize(FVector2D(1600,720));
    const FGeometry G=FGeometry::MakeRoot(FVector2D(1600,720),FSlateLayoutTransform());
    FSlateWindowElementList Out(Window);
    {
        LightBoxPaint::FVectorPaintBatch Batch(Out,.5f);
        for(int32 I=0;I<196;++I)
        {
            const FVector2D At(100+I%28*20,100+I/28*20);
            LightBoxPaint::Disc(Out,1,G,At,9,FLinearColor::White,FLinearColor::Transparent,12);
            LightBoxPaint::Disc(Out,1,G,At,3,FLinearColor::White,FLinearColor::Transparent,10);
        }
    }
    const auto& Elements=Out.GetUncachedDrawElements().Get<(uint8)EElementType::ET_CustomVerts>();
    TestEqual(TEXT("392 adjacent magnetic dot meshes submit as one element"),Elements.Num(),1);
    if(Elements.Num()==1)
    {
        TestTrue(TEXT("Combined indices stay within the vertex array"),Elements[0].Indices.Num()>0);
        for(auto Index:Elements[0].Indices) { if(!Elements[0].Vertices.IsValidIndex(Index)) { AddError(TEXT("Mesh index overflow")); return false; } }
        TestTrue(TEXT("Inherited glass opacity reaches custom vertices"),Elements[0].Vertices[0].Color.A>=127&&Elements[0].Vertices[0].Color.A<=128);
        AddInfo(FString::Printf(TEXT("Magnetic dot geometry: 392 submissions / 5096 authored vertices -> %d submission / %d vertices"),Elements.Num(),Elements[0].Vertices.Num()));
    }
    FSlateWindowElementList Large(Window);
    {
        LightBoxPaint::FVectorPaintBatch Batch(Large);
        for(int32 I=0;I<23000;++I) { LightBoxPaint::Poly(Large,1,G,{FVector2D(1,1),FVector2D(2,1),FVector2D(1,2)},FLinearColor::White); }
    }
    const auto& Split=Large.GetUncachedDrawElements().Get<(uint8)EElementType::ET_CustomVerts>();
    TestEqual(TEXT("A mesh above 16-bit capacity splits into safe submissions"),Split.Num(),2);
    for(const auto& Element:Split) for(auto Index:Element.Indices) if(!Element.Vertices.IsValidIndex(Index)) { AddError(TEXT("Split mesh index overflow")); return false; }
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxConstellationPaintTest,"SpresenseMaze.LightBox.ConstellationPaintBudget",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxConstellationPaintTest::RunTest(const FString&)
{
    auto Window=SNew(SWindow).ClientSize(FVector2D(1600,720));
    FHittestGrid Grid; const FPaintArgs Args(nullptr,Grid,FVector2D::ZeroVector,0,0);
    const FSlateRect Clip(-1000,-1000,3000,2000);
    auto Check=[&](const TCHAR* Name,auto Widget,FVector2D Size,int32 MaxMeshes)
    {
        const FGeometry G=FGeometry::MakeRoot(Size,FSlateLayoutTransform());
        FSlateWindowElementList Out(Window); Widget->Tick(G,0,.1f);
        Widget->OnPaint(Args,G,Clip,Out,0,FWidgetStyle(),true);
        const auto& Meshes=Out.GetUncachedDrawElements().Get<(uint8)EElementType::ET_CustomVerts>();
        TestTrue(FString(Name)+TEXT(" stays within its mesh submission budget"),Meshes.Num()>0&&Meshes.Num()<=MaxMeshes);
        int32 Vertices=0;
        for(const auto& Mesh:Meshes)
        {
            Vertices+=Mesh.Vertices.Num();
            for(auto Index:Mesh.Indices) if(!Mesh.Vertices.IsValidIndex(Index)) { AddError(FString(Name)+TEXT(" has invalid mesh indices")); return; }
            for(const auto& Vertex:Mesh.Vertices) if(!FMath::IsFinite(Vertex.Position.X)||!FMath::IsFinite(Vertex.Position.Y)) { AddError(FString(Name)+TEXT(" has a non-finite stroke join")); return; }
        }
        TestTrue(FString(Name)+TEXT(" stays below 6000 UI vertices"),Vertices<6000);
        AddInfo(FString::Printf(TEXT("%s: %d custom meshes, %d vertices"),Name,Meshes.Num(),Vertices));
    };
    auto Corners=SNew(SLightBoxAmbience);
    for(const FVector2D Size:{FVector2D(1600,720),FVector2D(2400,1080),FVector2D(1600,1200)})
    { Check(TEXT("Static corner aurora"),Corners,Size,1); }
    FLightBoxSensorReading R; R.bHasMic=R.bHasLight=R.bHasNear=R.bHasMagnet=true;
    R.Breath=.8f; R.Warmth=.6f; R.Darkness=.7f; R.Near=.5f; R.Magnet=.8f; R.bClap=true;
    auto Dock=SNew(SLightBoxSensorDock).Reading([&](){return &R;}).IsOn([](int32){return true;});
    Check(TEXT("Active constellation dock"),Dock,Dock->ComputeDesiredSize(1),1);
    auto Mic=SNew(SLightBoxMicButton).Listening(true).IsActive(true);
    Check(TEXT("Listening instrument"),Mic,Mic->ComputeDesiredSize(1),1);
    auto Composer=MakeShared<FLightBoxComposer>();
    auto Keyboard=SNew(SLightBoxKeyboard).Composer(Composer);
    Check(TEXT("Idle glass keyboard"),Keyboard,FVector2D(SLightBoxKeyboard::Width,SLightBoxKeyboard::Height),8);
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxKeyboardModesTest,"SpresenseMaze.LightBox.KeyboardInputModes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxKeyboardModesTest::RunTest(const FString&)
{
    auto C=MakeShared<FLightBoxComposer>(); auto K=SNew(SLightBoxKeyboard).Composer(C);
    C->Type(TEXT("かな")); K->Release(SLightBoxKeyboard::LatinMode,0);
    TestEqual(TEXT("Switching preserves and commits kana"),C->Committed,FString(TEXT("かな")));
    K->Release(1,0); K->Release(1,0); K->Release(1,2);
    TestEqual(TEXT("ABC taps cycle; flicks directly append Latin, without Japanese conversion"),C->FullText(),FString(TEXT("かなbc")));
    TestTrue(TEXT("Latin remains literal"),C->Reading.IsEmpty()&&C->Romaji.Pending.IsEmpty());
    K->Release(SLightBoxKeyboard::Shift,0); K->Release(2,0);
    TestTrue(TEXT("Case switch produces uppercase D"),C->FullText().EndsWith(TEXT("D")));
    K->Release(SLightBoxKeyboard::NumberMode,0);K->Release(0,0);K->Release(0,0);K->Release(SLightBoxKeyboard::Convert,0);
    TestTrue(TEXT("Repeated number taps append twice and space is ASCII"),C->FullText().EndsWith(TEXT("D11 ")));
    const FString Before=C->FullText(); K->Release(SLightBoxKeyboard::Enter,0);
    TestEqual(TEXT("Confirm preserves already committed Latin"),C->FullText(),Before);
    K->Release(SLightBoxKeyboard::KanaMode,0);K->Release(4,2);
    TestTrue(TEXT("Returning to kana restores flick input"),C->FullText().EndsWith(TEXT("ぬ")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxBackdropMotionTest,"SpresenseMaze.LightBox.BackdropContinuousMotion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxBackdropMotionTest::RunTest(const FString&)
{
    LightBoxPaint::FCornerAuroraMesh M(FVector2D(1600,720));
    const auto Indices=M.Indices; M.Update(0); const auto First=M.Positions;
    M.Update(60); const auto Later=M.Positions;
    bool Changed=false;
    for(int32 I=0;I<First.Num();++I)
    {
        Changed|=First[I]!=Later[I];
        TestTrue(TEXT("Every animated vertex remains bounded and finite"),FMath::IsFinite(Later[I].X)&&FMath::IsFinite(Later[I].Y)&&FVector2D::Distance(Later[I],M.RestPositions[I])<=FVector2D(M.Size.GetMin()*.022,M.Size.GetMin()*.060).Size()+.01);
    }
    TestTrue(TEXT("The scenery evolves without restarting a short clip"),Changed);
    M.Update(60);TestTrue(TEXT("Repeated paints at the same time keep the cached mesh"),M.Positions==Later);
    M.Update(1000000);TestTrue(TEXT("Animation retains topology even after long uptime"),M.Indices==Indices&&M.Positions.Num()==First.Num());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxVoiceMeterTest,"SpresenseMaze.LightBox.VoiceMeter",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxVoiceMeterTest::RunTest(const FString&)
{
    bool Listening = false; float Level = 0;
    auto Mic = SNew(SLightBoxMicButton).Listening_Lambda([&](){return Listening;}).VoiceLevel_Lambda([&](){return Level;}).IsActive(true);
    const FGeometry G = FGeometry::MakeRoot(Mic->ComputeDesiredSize(1), FSlateLayoutTransform());
    Mic->Tick(G, 0, .1f);
    TestTrue(TEXT("Idle halo animates at a restrained cadence"), Mic->PaintInterval() >= 1. / 15.);
    const float Phase = Mic->RingPhase; Mic->Tick(G, .05, .1f);
    TestTrue(TEXT("Idle gradient progresses continuously"), Mic->RingPhase != Phase);
    Listening = true; Mic->Tick(G, .1, .1f);
    for (float V : Mic->Samples) { TestEqual(TEXT("Silence never fabricates a waveform"), V, 0.f); }
    Level = .8f; Mic->NextSampleAt = 0; Mic->Tick(G, .2, .1f);
    TestTrue(TEXT("Real voice level reaches the waveform"), Mic->Samples[6] > .5f);
    Level = 10; Mic->NextSampleAt = 0; Mic->Tick(G, .3, .1f);
    TestTrue(TEXT("Excess levels remain within the visual range"), Mic->Samples[6] <= 1.f);
    Listening = false; Mic->Tick(G, .4, 1.f);
    TestEqual(TEXT("Stop clears the envelope immediately"), Mic->Envelope, 0.f);
    for (float V : Mic->Samples) { TestEqual(TEXT("A new session cannot inherit old voice samples"), V, 0.f); }
    TestTrue(TEXT("Stopped waveform keeps the low idle cadence"), Mic->PaintInterval() >= 1. / 15.);
    Mic->Enabled = false;
    TestFalse(TEXT("Unavailable microphone does not animate"), Mic->NeedsAnimation());
    return true;
}

#endif
