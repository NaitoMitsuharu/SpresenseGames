#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"

/** One conversion unit: its kana reading and the spellings offered for it. */
struct FLightBoxKanaSegment
{
    FString Reading;
    TArray<FString> Candidates;
    int32 Choice = 0;
    FString Chosen() const { return Candidates.IsValidIndex(Choice) ? Candidates[Choice] : Reading; }
};

/** Pure helpers for the Atelier's own Japanese keyboard (shared with tests). */
namespace LightBoxKana
{
    /** 12-key flick layout: Key 0..11 -> five glyphs (centre, left, up, right, down). Empty for function keys. */
    FString FlickGlyphs(int32 Key);
    /** Tap-to-cycle order for a key (centre glyph first, then small forms where they exist). */
    FString ToggleCycle(int32 Key);
    /** ゛゜小 key: the next form of one kana (か→が→か, は→ば→ぱ→は, つ→っ→づ→つ). Same glyph if none. */
    TCHAR NextVariant(TCHAR Kana);
    FString ToKatakana(const FString& Hiragana);
    /** Google Input Tools style JSON: [["reading",["cand",...]],...]. */
    bool ParseConversion(const FString& Json, TArray<FLightBoxKanaSegment>& Out);
    /** Romaji typed on a hardware keyboard -> kana. Feed one character; finished kana is appended to Out. */
    struct FRomaji
    {
        FString Pending;
        void Feed(TCHAR Char, FString& Out);
        /** Settle a trailing "n" as ん; anything else unfinished stays as typed. */
        void Flush(FString& Out);
    };
    /** Line breaks a Japanese note at phrase edges, balanced so no line is left with a stray syllable. */
    FString WrapPhrases(const FString& Text, const struct FSlateFontInfo& Font, float MaxWidth);
}

/**
 * Committed text plus the kana being composed. Typing asks for a conversion after a short pause;
 * 変換 splits the reading into segments whose spellings can be chosen one by one.
 */
class FLightBoxComposer : public TSharedFromThis<FLightBoxComposer>
{
public:
    ~FLightBoxComposer();
    FString Committed;
    FString Reading;
    LightBoxKana::FRomaji Romaji;
    TArray<FLightBoxKanaSegment> Segments;
    bool bConverting = false;
    int32 Focus = 0;
    /** Bumped on every change, so widgets can animate new glyphs. */
    uint32 Revision = 0;
    double LastTypedAt = -10;

    void Type(const FString& Kana);
    /** One hardware-keyboard letter, read as romaji. */
    void TypeRomaji(TCHAR Char);
    /** Text that needs no conversion (digits, a desktop IME's result). */
    void TypeLiteral(const FString& Text);
    /** Replace the last composing kana (tap-to-cycle and ゛゜小). */
    bool ReplaceLast(TFunctionRef<TCHAR(TCHAR)> Change);
    void Backspace();
    /** 変換: start converting, or step the focused segment to its next spelling. Empty reading types a space. */
    void Convert();
    /** Choose a candidate from the bar. */
    void Pick(int32 Index);
    void MoveFocus(int32 Delta);
    void CommitComposition();
    void Clear();
    void SetText(const FString& Text) { Clear(); Committed = Text; ++Revision; }
    bool IsComposing() const { return !Reading.IsEmpty() || !Romaji.Pending.IsEmpty(); }
    bool IsEmpty() const { return Committed.IsEmpty() && !IsComposing(); }
    /** Everything the player means right now, composition included. */
    FString FullText() const;
    TArray<FString> Suggestions() const;
    void Tick(double Now);

private:
    friend class FLightBoxComposerAsyncTest;
    uint64 RequestGeneration = 0;
    void CancelRequest();
    TArray<FLightBoxKanaSegment> Live;
    FString LiveFor, AskedFor;
    bool bFallbackSegments = false;
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request;
    void Changed();
    void Ask(const FString& Reading);
    static TArray<FLightBoxKanaSegment> Fallback(const FString& Reading);
};
