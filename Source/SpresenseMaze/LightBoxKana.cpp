#include "LightBoxKana.h"
#include "Dom/JsonValue.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Rendering/SlateRenderer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace LightBoxKana
{
FString FlickGlyphs(int32 Key)
{
    static const TCHAR* Rows[] = {
        TEXT("あいうえお"), TEXT("かきくけこ"), TEXT("さしすせそ"),
        TEXT("たちつてと"), TEXT("なにぬねの"), TEXT("はひふへほ"),
        TEXT("まみむめも"), TEXT("や（ゆ）よ"), TEXT("らりるれろ"),
        TEXT(""), TEXT("わをんー〜"), TEXT("、。？！…") };
    return Key >= 0 && Key < 12 ? FString(Rows[Key]) : FString();
}

FString ToggleCycle(int32 Key)
{
    switch (Key)
    {
    case 0: return TEXT("あいうえおぁぃぅぇぉ");
    case 3: return TEXT("たちつてとっ");
    case 7: return TEXT("やゆよゃゅょ");
    case 10: return TEXT("わをんゎー");
    case 11: return TEXT("、。？！…・");
    default: return FlickGlyphs(Key);
    }
}

TCHAR NextVariant(TCHAR K)
{
    // Each group cycles in order and wraps.
    static const TCHAR* Groups[] = {
        TEXT("あぁ"), TEXT("いぃ"), TEXT("うぅゔ"), TEXT("えぇ"), TEXT("おぉ"),
        TEXT("かが"), TEXT("きぎ"), TEXT("くぐ"), TEXT("けげ"), TEXT("こご"),
        TEXT("さざ"), TEXT("しじ"), TEXT("すず"), TEXT("せぜ"), TEXT("そぞ"),
        TEXT("ただ"), TEXT("ちぢ"), TEXT("つっづ"), TEXT("てで"), TEXT("とど"),
        TEXT("はばぱ"), TEXT("ひびぴ"), TEXT("ふぶぷ"), TEXT("へべぺ"), TEXT("ほぼぽ"),
        TEXT("やゃ"), TEXT("ゆゅ"), TEXT("よょ"), TEXT("わゎ") };
    for (const TCHAR* G : Groups)
    {
        const int32 N = FCString::Strlen(G);
        for (int32 I = 0; I < N; ++I) { if (G[I] == K) { return G[(I + 1) % N]; } }
    }
    return K;
}

FString ToKatakana(const FString& In)
{
    FString Out = In;
    for (TCHAR& C : Out.GetCharArray()) { if (C >= 0x3041 && C <= 0x3096) { C += 0x60; } }
    return Out;
}

bool ParseConversion(const FString& Json, TArray<FLightBoxKanaSegment>& Out)
{
    TArray<TSharedPtr<FJsonValue>> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || Root.IsEmpty()) { return false; }
    TArray<FLightBoxKanaSegment> Parsed;
    for (const TSharedPtr<FJsonValue>& Item : Root)
    {
        const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
        if (!Item.IsValid() || !Item->TryGetArray(Pair) || Pair->Num() < 2) { return false; }
        FLightBoxKanaSegment Segment;
        if (!(*Pair)[0]->TryGetString(Segment.Reading)) { return false; }
        const TArray<TSharedPtr<FJsonValue>>* Words = nullptr;
        if (Segment.Reading.IsEmpty() || !(*Pair)[1]->TryGetArray(Words)) { return false; }
        for (const TSharedPtr<FJsonValue>& W : *Words)
        {
            FString Word; if (!W.IsValid() || !W->TryGetString(Word)) { return false; }
            // Half-width katakana reads poorly next to the rest of the UI.
            bool bHalf = false; for (TCHAR C : Word) { bHalf |= C >= 0xFF61 && C <= 0xFF9F; }
            if (!Word.IsEmpty() && !bHalf) { Segment.Candidates.AddUnique(Word); }
        }
        Segment.Candidates.AddUnique(Segment.Reading);
        Segment.Candidates.AddUnique(ToKatakana(Segment.Reading));
        Parsed.Add(MoveTemp(Segment));
    }
    Out = MoveTemp(Parsed);
    return true;
}

namespace
{
struct FRomajiEntry { const TCHAR* Key; const TCHAR* Kana; };
const FRomajiEntry RomajiTable[] = {
    {TEXT("a"),TEXT("あ")},{TEXT("i"),TEXT("い")},{TEXT("u"),TEXT("う")},{TEXT("e"),TEXT("え")},{TEXT("o"),TEXT("お")},
    {TEXT("ka"),TEXT("か")},{TEXT("ki"),TEXT("き")},{TEXT("ku"),TEXT("く")},{TEXT("ke"),TEXT("け")},{TEXT("ko"),TEXT("こ")},
    {TEXT("sa"),TEXT("さ")},{TEXT("si"),TEXT("し")},{TEXT("shi"),TEXT("し")},{TEXT("su"),TEXT("す")},{TEXT("se"),TEXT("せ")},{TEXT("so"),TEXT("そ")},
    {TEXT("ta"),TEXT("た")},{TEXT("ti"),TEXT("ち")},{TEXT("chi"),TEXT("ち")},{TEXT("tu"),TEXT("つ")},{TEXT("tsu"),TEXT("つ")},{TEXT("te"),TEXT("て")},{TEXT("to"),TEXT("と")},
    {TEXT("na"),TEXT("な")},{TEXT("ni"),TEXT("に")},{TEXT("nu"),TEXT("ぬ")},{TEXT("ne"),TEXT("ね")},{TEXT("no"),TEXT("の")},
    {TEXT("ha"),TEXT("は")},{TEXT("hi"),TEXT("ひ")},{TEXT("hu"),TEXT("ふ")},{TEXT("fu"),TEXT("ふ")},{TEXT("he"),TEXT("へ")},{TEXT("ho"),TEXT("ほ")},
    {TEXT("ma"),TEXT("ま")},{TEXT("mi"),TEXT("み")},{TEXT("mu"),TEXT("む")},{TEXT("me"),TEXT("め")},{TEXT("mo"),TEXT("も")},
    {TEXT("ya"),TEXT("や")},{TEXT("yu"),TEXT("ゆ")},{TEXT("yo"),TEXT("よ")},
    {TEXT("ra"),TEXT("ら")},{TEXT("ri"),TEXT("り")},{TEXT("ru"),TEXT("る")},{TEXT("re"),TEXT("れ")},{TEXT("ro"),TEXT("ろ")},
    {TEXT("wa"),TEXT("わ")},{TEXT("wo"),TEXT("を")},{TEXT("nn"),TEXT("ん")},{TEXT("n'"),TEXT("ん")},
    {TEXT("ga"),TEXT("が")},{TEXT("gi"),TEXT("ぎ")},{TEXT("gu"),TEXT("ぐ")},{TEXT("ge"),TEXT("げ")},{TEXT("go"),TEXT("ご")},
    {TEXT("za"),TEXT("ざ")},{TEXT("zi"),TEXT("じ")},{TEXT("ji"),TEXT("じ")},{TEXT("zu"),TEXT("ず")},{TEXT("ze"),TEXT("ぜ")},{TEXT("zo"),TEXT("ぞ")},
    {TEXT("da"),TEXT("だ")},{TEXT("di"),TEXT("ぢ")},{TEXT("du"),TEXT("づ")},{TEXT("de"),TEXT("で")},{TEXT("do"),TEXT("ど")},
    {TEXT("ba"),TEXT("ば")},{TEXT("bi"),TEXT("び")},{TEXT("bu"),TEXT("ぶ")},{TEXT("be"),TEXT("べ")},{TEXT("bo"),TEXT("ぼ")},
    {TEXT("pa"),TEXT("ぱ")},{TEXT("pi"),TEXT("ぴ")},{TEXT("pu"),TEXT("ぷ")},{TEXT("pe"),TEXT("ぺ")},{TEXT("po"),TEXT("ぽ")},
    {TEXT("kya"),TEXT("きゃ")},{TEXT("kyu"),TEXT("きゅ")},{TEXT("kyo"),TEXT("きょ")},
    {TEXT("sha"),TEXT("しゃ")},{TEXT("shu"),TEXT("しゅ")},{TEXT("she"),TEXT("しぇ")},{TEXT("sho"),TEXT("しょ")},
    {TEXT("sya"),TEXT("しゃ")},{TEXT("syu"),TEXT("しゅ")},{TEXT("syo"),TEXT("しょ")},
    {TEXT("cha"),TEXT("ちゃ")},{TEXT("chu"),TEXT("ちゅ")},{TEXT("che"),TEXT("ちぇ")},{TEXT("cho"),TEXT("ちょ")},
    {TEXT("tya"),TEXT("ちゃ")},{TEXT("tyu"),TEXT("ちゅ")},{TEXT("tyo"),TEXT("ちょ")},
    {TEXT("nya"),TEXT("にゃ")},{TEXT("nyu"),TEXT("にゅ")},{TEXT("nyo"),TEXT("にょ")},
    {TEXT("hya"),TEXT("ひゃ")},{TEXT("hyu"),TEXT("ひゅ")},{TEXT("hyo"),TEXT("ひょ")},
    {TEXT("mya"),TEXT("みゃ")},{TEXT("myu"),TEXT("みゅ")},{TEXT("myo"),TEXT("みょ")},
    {TEXT("rya"),TEXT("りゃ")},{TEXT("ryu"),TEXT("りゅ")},{TEXT("ryo"),TEXT("りょ")},
    {TEXT("gya"),TEXT("ぎゃ")},{TEXT("gyu"),TEXT("ぎゅ")},{TEXT("gyo"),TEXT("ぎょ")},
    {TEXT("ja"),TEXT("じゃ")},{TEXT("ju"),TEXT("じゅ")},{TEXT("je"),TEXT("じぇ")},{TEXT("jo"),TEXT("じょ")},
    {TEXT("zya"),TEXT("じゃ")},{TEXT("zyu"),TEXT("じゅ")},{TEXT("zyo"),TEXT("じょ")},
    {TEXT("bya"),TEXT("びゃ")},{TEXT("byu"),TEXT("びゅ")},{TEXT("byo"),TEXT("びょ")},
    {TEXT("pya"),TEXT("ぴゃ")},{TEXT("pyu"),TEXT("ぴゅ")},{TEXT("pyo"),TEXT("ぴょ")},
    {TEXT("fa"),TEXT("ふぁ")},{TEXT("fi"),TEXT("ふぃ")},{TEXT("fe"),TEXT("ふぇ")},{TEXT("fo"),TEXT("ふぉ")},
    {TEXT("thi"),TEXT("てぃ")},{TEXT("dhi"),TEXT("でぃ")},{TEXT("vu"),TEXT("ゔ")},
    {TEXT("xa"),TEXT("ぁ")},{TEXT("xi"),TEXT("ぃ")},{TEXT("xu"),TEXT("ぅ")},{TEXT("xe"),TEXT("ぇ")},{TEXT("xo"),TEXT("ぉ")},
    {TEXT("la"),TEXT("ぁ")},{TEXT("li"),TEXT("ぃ")},{TEXT("lu"),TEXT("ぅ")},{TEXT("le"),TEXT("ぇ")},{TEXT("lo"),TEXT("ぉ")},
    {TEXT("xya"),TEXT("ゃ")},{TEXT("xyu"),TEXT("ゅ")},{TEXT("xyo"),TEXT("ょ")},{TEXT("xtu"),TEXT("っ")},{TEXT("ltu"),TEXT("っ")},
    {TEXT("-"),TEXT("ー")},{TEXT(","),TEXT("、")},{TEXT("."),TEXT("。")},{TEXT("?"),TEXT("？")},{TEXT("!"),TEXT("！")},
};
bool IsPrefix(const FString& P)
{
    for (const FRomajiEntry& E : RomajiTable) { if (FString(E.Key).StartsWith(P, ESearchCase::CaseSensitive)) { return true; } }
    return false;
}
bool IsHiragana(TCHAR C) { return C >= 0x3041 && C <= 0x309F; }
bool IsSmallOrMark(TCHAR C)
{
    return FString(TEXT("ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮー、。，．！？!?）」』…・")).Contains(FString(1, &C));
}
bool IsBreakAfter(TCHAR C) { return FString(TEXT("、。，．！？!? 　）」』")).Contains(FString(1, &C)); }
}

void FRomaji::Feed(TCHAR Char, FString& Out)
{
    Char = FChar::ToLower(Char);
    Pending.AppendChar(Char);
    for (int32 Guard = 0; Guard < 4 && !Pending.IsEmpty(); ++Guard)
    {
        for (const FRomajiEntry& E : RomajiTable)
        {
            if (Pending.Equals(E.Key, ESearchCase::CaseSensitive)) { Out += E.Kana; Pending.Reset(); return; }
        }
        if (IsPrefix(Pending)) { return; }
        // "kk" -> っ + k, "nk" -> ん + k; otherwise the head is not romaji and passes through.
        const TCHAR First = Pending[0];
        if (Pending.Len() >= 2 && First == Pending[1] && First != TEXT('n') && FChar::IsAlpha(First)) { Out += TEXT("っ"); }
        else if (First == TEXT('n')) { Out += TEXT("ん"); }
        else { Out.AppendChar(First); }
        Pending.RightChopInline(1);
    }
}

void FRomaji::Flush(FString& Out)
{
    if (Pending == TEXT("n")) { Out += TEXT("ん"); }
    else { Out += Pending; }
    Pending.Reset();
}

FString WrapPhrases(const FString& Text, const FSlateFontInfo& Font, float MaxWidth)
{
    if (Text.IsEmpty() || !FSlateApplication::IsInitialized()) { return Text; }
    const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    TArray<FString> Out;
    TArray<FString> Paragraphs; Text.ParseIntoArray(Paragraphs, TEXT("\n"), false);
    for (const FString& Paragraph : Paragraphs)
    {
        if (Paragraph.IsEmpty()) { Out.Add(FString()); continue; }
        // Phrases end after punctuation, or where kana gives way to a new word (kanji, katakana, latin).
        TArray<FString> Units; FString Unit;
        for (int32 I = 0; I < Paragraph.Len(); ++I)
        {
            const TCHAR C = Paragraph[I];
            if (!Unit.IsEmpty())
            {
                const TCHAR Prev = Unit[Unit.Len() - 1];
                if (IsBreakAfter(Prev) || (IsHiragana(Prev) && !IsHiragana(C) && !IsSmallOrMark(C))) { Units.Add(Unit); Unit.Reset(); }
            }
            Unit.AppendChar(C);
        }
        if (!Unit.IsEmpty()) { Units.Add(Unit); }
        // A phrase wider than the box falls back to character breaks.
        TArray<FString> Fit;
        for (const FString& U : Units)
        {
            if (Measure->Measure(U, Font).X <= MaxWidth) { Fit.Add(U); continue; }
            FString Piece;
            for (TCHAR C : U)
            {
                if (!Piece.IsEmpty() && Measure->Measure(Piece + C, Font).X > MaxWidth && !IsSmallOrMark(C)) { Fit.Add(Piece); Piece.Reset(); }
                Piece.AppendChar(C);
            }
            if (!Piece.IsEmpty()) { Fit.Add(Piece); }
        }
        auto Lay = [&](float Width, TArray<FString>& Lines)
        {
            Lines.Reset(); FString Line;
            for (const FString& U : Fit)
            {
                if (!Line.IsEmpty() && Measure->Measure(Line + U, Font).X > Width) { Lines.Add(Line.TrimEnd()); Line.Reset(); }
                Line += U;
            }
            if (!Line.IsEmpty()) { Lines.Add(Line.TrimEnd()); }
        };
        TArray<FString> Lines; Lay(MaxWidth, Lines);
        // Balance: the narrowest width that keeps the same number of lines.
        if (Lines.Num() > 1)
        {
            float Lo = MaxWidth * 0.4f, Hi = MaxWidth; const int32 Count = Lines.Num();
            for (int32 Step = 0; Step < 10; ++Step)
            {
                const float Mid = (Lo + Hi) * 0.5f; TArray<FString> Try; Lay(Mid, Try);
                if (Try.Num() <= Count) { Hi = Mid; } else { Lo = Mid; }
            }
            Lay(Hi, Lines);
        }
        Out.Append(Lines);
    }
    return FString::Join(Out, TEXT("\n"));
}
}

FLightBoxComposer::~FLightBoxComposer()
{
    CancelRequest();
}

void FLightBoxComposer::CancelRequest()
{
    ++RequestGeneration;
    if (Request.IsValid()) { Request->OnProcessRequestComplete().Unbind(); Request->CancelRequest(); Request.Reset(); }
    AskedFor.Reset();
}

TArray<FLightBoxKanaSegment> FLightBoxComposer::Fallback(const FString& R)
{
    FLightBoxKanaSegment S; S.Reading = R; S.Candidates = { R, LightBoxKana::ToKatakana(R) };
    return { S };
}

void FLightBoxComposer::Changed()
{
    if (Reading.IsEmpty() || Reading != AskedFor) { CancelRequest(); }
    ++Revision;
    LastTypedAt = FPlatformTime::Seconds();
}

void FLightBoxComposer::Type(const FString& Kana)
{
    if (bConverting) { CommitComposition(); }
    Romaji.Flush(Reading);
    Reading += Kana;
    Changed();
}

void FLightBoxComposer::TypeRomaji(TCHAR Char)
{
    if (bConverting) { CommitComposition(); }
    FString Kana; Romaji.Feed(Char, Kana);
    Reading += Kana;
    Changed();
}

void FLightBoxComposer::TypeLiteral(const FString& Text)
{
    CommitComposition();
    Committed += Text;
    Changed();
}

bool FLightBoxComposer::ReplaceLast(TFunctionRef<TCHAR(TCHAR)> Change)
{
    if (bConverting || Reading.IsEmpty()) { return false; }
    const TCHAR Next = Change(Reading[Reading.Len() - 1]);
    if (Next == Reading[Reading.Len() - 1]) { return false; }
    Reading[Reading.Len() - 1] = Next;
    Changed();
    return true;
}

void FLightBoxComposer::Backspace()
{
    if (bConverting) { bConverting = false; Segments.Reset(); }
    else if (!Romaji.Pending.IsEmpty()) { Romaji.Pending.LeftChopInline(1); }
    else if (!Reading.IsEmpty()) { Reading.LeftChopInline(1); }
    else if (!Committed.IsEmpty()) { Committed.LeftChopInline(1); }
    Changed();
}

void FLightBoxComposer::Convert()
{
    Romaji.Flush(Reading);
    if (Reading.IsEmpty()) { Committed += TEXT("　"); Changed(); return; }
    if (!bConverting)
    {
        bConverting = true; Focus = 0;
        bFallbackSegments = LiveFor != Reading || Live.IsEmpty();
        Segments = bFallbackSegments ? Fallback(Reading) : Live;
        // The first press already shows the best guess; the next press steps through spellings.
        if (bFallbackSegments) { Ask(Reading); }
    }
    else if (Segments.IsValidIndex(Focus) && Segments[Focus].Candidates.Num() > 0)
    {
        bFallbackSegments = false; // A late reply must not replace a spelling the player chose.
        FLightBoxKanaSegment& S = Segments[Focus];
        S.Choice = (S.Choice + 1) % S.Candidates.Num();
    }
    Changed();
}

void FLightBoxComposer::Pick(int32 Index)
{
    if (bConverting && Segments.IsValidIndex(Focus))
    {
        if (!Segments[Focus].Candidates.IsValidIndex(Index)) { return; }
        bFallbackSegments = false;
        Segments[Focus].Choice = Index;
        if (++Focus >= Segments.Num()) { CommitComposition(); return; }
        Changed();
        return;
    }
    const TArray<FString> List = Suggestions();
    if (!List.IsValidIndex(Index)) { return; }
    Committed += List[Index];
    Reading.Reset(); Romaji.Pending.Reset();
    Changed();
}

void FLightBoxComposer::MoveFocus(int32 Delta)
{
    if (!bConverting || Segments.IsEmpty()) { return; }
    bFallbackSegments = false;
    Focus = FMath::Clamp(Focus + Delta, 0, Segments.Num() - 1);
    ++Revision;
}

void FLightBoxComposer::CommitComposition()
{
    Romaji.Flush(Reading);
    if (bConverting) { for (const FLightBoxKanaSegment& S : Segments) { Committed += S.Chosen(); } }
    else { Committed += Reading; }
    Reading.Reset(); Segments.Reset(); bConverting = false; Focus = 0;
    Changed();
}

void FLightBoxComposer::Clear()
{
    CancelRequest(); Live.Reset(); LiveFor.Reset(); bFallbackSegments = false;
    Committed.Reset(); Reading.Reset(); Romaji.Pending.Reset(); Segments.Reset(); bConverting = false; Focus = 0;
    Changed();
}

FString FLightBoxComposer::FullText() const
{
    FString Out = Committed;
    if (bConverting) { for (const FLightBoxKanaSegment& S : Segments) { Out += S.Chosen(); } }
    else { Out += Reading + Romaji.Pending; }
    return Out;
}

TArray<FString> FLightBoxComposer::Suggestions() const
{
    if (bConverting) { return Segments.IsValidIndex(Focus) ? Segments[Focus].Candidates : TArray<FString>(); }
    if (Reading.IsEmpty()) { return {}; }
    TArray<FString> Out;
    if (LiveFor == Reading && !Live.IsEmpty())
    {
        FString Best; for (const FLightBoxKanaSegment& S : Live) { Best += S.Chosen(); }
        Out.AddUnique(Best);
        if (Live.Num() == 1) { for (const FString& C : Live[0].Candidates) { Out.AddUnique(C); } }
    }
    Out.AddUnique(Reading);
    Out.AddUnique(LightBoxKana::ToKatakana(Reading));
    return Out;
}

void FLightBoxComposer::Tick(double Now)
{
    // Ask once typing pauses briefly; a reply for an older reading is ignored.
    if (!Reading.IsEmpty() && !bConverting && Reading != LiveFor && Reading != AskedFor && Now - LastTypedAt > 0.22) { Ask(Reading); }
}

void FLightBoxComposer::Ask(const FString& For)
{
    // For may alias Reading. Capture its value and identify this particular request, including A -> B -> A edits.
    CancelRequest();
    AskedFor = For;
    const uint64 Generation = RequestGeneration;
    Request = FHttpModule::Get().CreateRequest();
    Request->SetVerb(TEXT("GET"));
    Request->SetURL(TEXT("https://www.google.com/transliterate?langpair=ja-Hira%7Cja&text=") + FGenericPlatformHttp::UrlEncode(For));
    Request->SetTimeout(4.f);
    TWeakPtr<FLightBoxComposer> Weak = AsShared();
    Request->OnProcessRequestComplete().BindLambda([Weak, For, Generation](FHttpRequestPtr Completed, FHttpResponsePtr Response, bool bOk)
    {
        const TSharedPtr<FLightBoxComposer> Self = Weak.Pin();
        if (!Self.IsValid() || Self->RequestGeneration != Generation || Self->Request != Completed || Self->Reading != For) { return; }
        Self->Request.Reset();
        TArray<FLightBoxKanaSegment> Parsed;
        if (!bOk || !Response.IsValid() || Response->GetResponseCode() != 200 || !LightBoxKana::ParseConversion(Response->GetContentAsString(), Parsed))
        {
            Parsed = Fallback(For);  // offline: kana and katakana still work
        }
        FString Joined; for (const auto& Segment : Parsed) { Joined += Segment.Reading; }
        if (Joined != For) { Parsed = Fallback(For); }
        Self->Live = Parsed; Self->LiveFor = For;
        if (Self->bConverting && Self->bFallbackSegments && Self->Reading == For)
        {
            Self->Segments = Parsed; Self->Focus = 0; Self->bFallbackSegments = false;
        }
        ++Self->Revision;
    });
    const FHttpRequestPtr Started = Request;
    if (!Started->ProcessRequest() && Request == Started)
    {
        Started->OnProcessRequestComplete().ExecuteIfBound(Started, nullptr, false);
    }
}
