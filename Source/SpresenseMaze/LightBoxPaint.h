#pragma once

#include "CoreMinimal.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/CompositeFont.h"
#include "Misc/Paths.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

/** Small vector painter for the Atelier's animated controls: gradients, glows and conic rings. */
namespace LightBoxPaint
{
    inline const FLinearColor Mint(.42f, .94f, .80f), Cyan(.30f, .78f, 1.f), Violet(.62f, .48f, 1.f), Pink(1.f, .45f, .72f), Paper(.94f, .98f, 1.f);

    /** The signature aurora: mint -> cyan -> violet -> pink -> mint over T in [0,1). */
    inline FLinearColor Aurora(float T)
    {
        static const FLinearColor Stops[] = { Mint, Cyan, Violet, Pink, Mint };
        T = (T - FMath::FloorToFloat(T)) * 4.f; const int32 I = FMath::Min(3, int32(T));
        return FMath::Lerp(Stops[I], Stops[I + 1], T - I);
    }

    /** Bundled Japanese Mincho: identical glyphs on Windows and Android, with Slate glyph caching. */
    inline FSlateFontInfo Font(float Size, const TCHAR* = TEXT("Regular"))
    {
        static const TSharedRef<const FCompositeFont> Family=MakeShared<FCompositeFont>(FName(TEXT("Regular")),
            FPaths::ProjectContentDir()/TEXT("LightBox/Fonts/ZenOldMincho-Regular.ttf"),EFontHinting::Default,EFontLoadingPolicy::LazyLoad);
        return FSlateFontInfo(Family,Size,TEXT("Regular"));
    }

    inline FVector2D Measure(const FString& Text, const FSlateFontInfo& F)
    {
        return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, F);
    }

    /** Collect adjacent meshes on the same layer. Lifetime is one paint, never a renderer-global handle.
     * Flush at clip boundaries and before text/lines to preserve Slate ordering and clipping. */
    class FVectorPaintBatch
    {
    public:
        inline static thread_local FVectorPaintBatch* Active = nullptr;
        explicit FVectorPaintBatch(FSlateWindowElementList& InOut, float InOpacity = 1.f)
            : Opacity(InOpacity), Out(InOut), Previous(Active)
        {
            static const FSlateBrush White = *FCoreStyle::Get().GetBrush("WhiteBrush");
            Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(White);
            Active = this;
        }
        ~FVectorPaintBatch() { Flush(); Active = Previous; }
        FVectorPaintBatch(const FVectorPaintBatch&) = delete;
        FVectorPaintBatch& operator=(const FVectorPaintBatch&) = delete;
        void Flush()
        {
            if (!Indices.IsEmpty()) { FSlateDrawElement::MakeCustomVerts(Out, Layer, Handle, Vertices, Indices, nullptr, 0, 0); }
            Vertices.Reset(); Indices.Reset();
        }
        void Add(int32 L, const FGeometry& G, const TArray<FVector2D>& P, const TArray<FLinearColor>& C, const TArray<SlateIndex>& I)
        {
            // SlateIndex is 16 bit on some targets. Never wrap an index in a combined mesh.
            if (L != Layer || Vertices.Num() + P.Num() > 65535) { Flush(); }
            Layer = L;
            const int32 Base = Vertices.Num();
            const auto& T = G.GetAccumulatedRenderTransform();
            for (int32 K = 0; K < P.Num(); ++K)
            {
                FLinearColor Colour = C[K]; Colour.A *= Opacity;
                Vertices.Add(FSlateVertex::Make(T, FVector2f(P[K]), FVector2f(.5f, .5f), Colour.ToFColor(true)));
            }
            for (SlateIndex Index : I) { Indices.Add(SlateIndex(Base + Index)); }
        }
        float Opacity;
    private:
        FSlateWindowElementList& Out;
        FVectorPaintBatch* Previous;
        FSlateResourceHandle Handle;
        int32 Layer = INDEX_NONE;
        TArray<FSlateVertex> Vertices;
        TArray<SlateIndex> Indices;
    };

    inline void Tris(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const TArray<FVector2D>& P, const TArray<FLinearColor>& C, const TArray<SlateIndex>& I)
    {
        if (I.IsEmpty()) { return; }
        if (FVectorPaintBatch::Active) { FVectorPaintBatch::Active->Add(L, G, P, C, I); }
        else { FVectorPaintBatch Batch(Out); Batch.Add(L, G, P, C, I); }
    }

    inline FLinearColor PaintColour(FLinearColor C)
    {
        if (FVectorPaintBatch::Active) { FVectorPaintBatch::Active->Flush(); C.A *= FVectorPaintBatch::Active->Opacity; }
        return C;
    }

    // Reduce small curves towards a quarter physical pixel of chord error, capped by authored detail.
    inline int32 CurveSteps(const FGeometry& G, float Radius, float Sweep, int32 Maximum)
    {
        const float Pixels = FMath::Max(1.f, Radius * G.GetAccumulatedLayoutTransform().GetScale());
        const float Step = 2.f * FMath::Acos(FMath::Clamp(1.f - .25f / Pixels, -1.f, 1.f));
        return FMath::Clamp(FMath::CeilToInt(FMath::Abs(Sweep) * PI / 180.f / Step), 8, FMath::Max(8, Maximum));
    }

    /** Rounded rectangle with a linear gradient (A at the top, or the left when Along is horizontal). */
    inline void Fill(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2D Pos, FVector2D Size, float Radius,
        const FLinearColor& A, const FLinearColor& B, EOrientation Along = Orient_Vertical)
    {
        if (Size.X <= 0 || Size.Y <= 0 || (A.A <= .001f && B.A <= .001f)) { return; }
        Radius = FMath::Min3(Radius, float(Size.X) * .5f, float(Size.Y) * .5f);
        auto ColorAt = [&](const FVector2D& P) { return FMath::Lerp(A, B, float(Along == Orient_Horizontal ? (P.X - Pos.X) / Size.X : (P.Y - Pos.Y) / Size.Y)); };
        TArray<FVector2D> P{ Pos + Size * .5 }; TArray<FLinearColor> C{ ColorAt(P[0]) }; TArray<SlateIndex> I;
        const FVector2D Corners[] = { {Pos.X + Size.X - Radius, Pos.Y + Radius}, {Pos.X + Size.X - Radius, Pos.Y + Size.Y - Radius}, {Pos.X + Radius, Pos.Y + Size.Y - Radius}, {Pos.X + Radius, Pos.Y + Radius} };
        const int32 Steps = FMath::Clamp(CurveSteps(G,Radius,360,32)/4,2,8);
        for (int32 K = 0; K < 4; ++K)
        {
            for (int32 S = 0; S <= Steps; ++S)
            {
                const float Ang = FMath::DegreesToRadians(-90.f + 90.f * K + 90.f * S / Steps);
                const FVector2D V = Corners[K] + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * Radius;
                P.Add(V); C.Add(ColorAt(V));
            }
        }
        const int32 N = P.Num() - 1;
        for (int32 K = 1; K <= N; ++K) { I.Append({ 0, SlateIndex(K), SlateIndex(K % N + 1) }); }
        Tris(Out, L, G, P, C, I);
    }
    inline void Fill(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2D Pos, FVector2D Size, float Radius, const FLinearColor& C)
    {
        Fill(Out, L, G, Pos, Size, Radius, C, C);
    }

    /** Radial glow: Inner colour at the centre fading to Outer at radius R. */
    inline void Disc(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2D C, float R, const FLinearColor& Inner, const FLinearColor& Outer, int32 Sides = 48)
    {
        if (R <= 0 || (Inner.A <= .001f && Outer.A <= .001f)) { return; }
        Sides = CurveSteps(G, R, 360, Sides);
        TArray<FVector2D> P{ C }; TArray<FLinearColor> Col{ Inner }; TArray<SlateIndex> I;
        for (int32 K = 0; K <= Sides; ++K)
        {
            const float A = 2 * PI * K / Sides; P.Add(C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R); Col.Add(Outer);
            if (K > 0) { I.Append({ 0, SlateIndex(K), SlateIndex(K + 1) }); }
        }
        Tris(Out, L, G, P, Col, I);
    }

    /** Ring arc whose colour comes from Tint(t), t = 0..1 along the sweep (degrees, clockwise from +X). */
    template <typename FTint>
    void Ring(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2D C, float R, float Width, float StartDeg, float SweepDeg, FTint&& Tint, int32 Steps = 72)
    {
        if (R <= 0 || Width <= 0 || FMath::IsNearlyZero(SweepDeg)) { return; }
        Steps = CurveSteps(G, R + Width * .5f, SweepDeg, Steps);
        TArray<FVector2D> P; TArray<FLinearColor> Col; TArray<SlateIndex> I;
        for (int32 K = 0; K <= Steps; ++K)
        {
            const float T = float(K) / Steps, A = FMath::DegreesToRadians(StartDeg + SweepDeg * T);
            const FVector2D D(FMath::Cos(A), FMath::Sin(A)); const FLinearColor Color = Tint(T);
            P.Add(C + D * (R - Width * .5f)); P.Add(C + D * (R + Width * .5f)); Col.Add(Color); Col.Add(Color);
            if (K > 0) { const SlateIndex B = SlateIndex(K * 2); I.Append({ SlateIndex(B - 2), SlateIndex(B - 1), B, SlateIndex(B - 1), SlateIndex(B + 1), B }); }
        }
        Tris(Out, L, G, P, Col, I);
    }

    /** Soft light in one mesh: a coloured core with a wide, transparent feather; no blur target. */
    template <typename FTint>
    void GlowRing(FSlateWindowElementList& Out,int32 L,const FGeometry& G,FVector2D Centre,float Radius,float Width,float Feather,float Start,float Sweep,FTint&& Tint,int32 Maximum=48)
    {
        if(Radius<=0 || Width<=0 || FMath::IsNearlyZero(Sweep)) { return; }
        const int32 Steps=CurveSteps(G,Radius+Width*.5f+Feather,Sweep,Maximum);
        const float Half=Width*.5f;
        const float Offset[]={-Half-Feather,-Half-Feather*.32f,-Half,Half,Half+Feather*.32f,Half+Feather};
        const float Opacity[]={0,.12f,1,1,.12f,0};
        TArray<FVector2D> P; TArray<FLinearColor> C; TArray<SlateIndex> I;
        P.Reserve((Steps+1)*6); C.Reserve((Steps+1)*6); I.Reserve(Steps*30);
        for(int32 K=0;K<=Steps;++K)
        {
            const float T=float(K)/Steps,A=FMath::DegreesToRadians(Start+Sweep*T);
            const FVector2D D(FMath::Cos(A),FMath::Sin(A));
            for(int32 J=0;J<6;++J)
            {
                FLinearColor Col=Tint(T); Col.A*=Opacity[J]; P.Add(Centre+D*(Radius+Offset[J])); C.Add(Col);
                if(K>0 && J<5)
                {
                    const SlateIndex B=SlateIndex(K*6+J),A0=SlateIndex(B-6);
                    I.Append({A0,SlateIndex(A0+1),B,SlateIndex(A0+1),SlateIndex(B+1),B});
                }
            }
        }
        Tris(Out,L,G,P,C,I);
    }

    /** Size-dependent corner mesh with reusable topology and bounded continuous motion, leaving the centre clear. */
    struct FCornerAuroraMesh
    {
        FVector2D Size;
        TArray<FVector2D> Positions;
        TArray<FLinearColor> Colours;
        TArray<SlateIndex> Indices;
        struct FMotion { int32 Kind; float Along; uint32 Seed; bool Opposite; };
        TArray<FMotion> Motion;
        TArray<FVector2D> RestPositions;
        TArray<FLinearColor> RestColours;
        double LastTime=-1;
        // Smooth random knots never replay a short animation clip; all motion is bounded.
        static float Noise(double T,uint32 Seed)
        {
            const uint32 I=uint32(FMath::FloorToDouble(T)); const double F=T-FMath::FloorToDouble(T);
            auto H=[Seed](uint32 X) { X^=Seed*747796405u; X^=X>>16; X*=2246822519u; X^=X>>13; X*=3266489917u; X^=X>>16; return float(X&0xffffffu)/8388607.5f-1.f; };
            return FMath::Lerp(H(I),H(I+1),float(F*F*F*(F*(F*6-15)+10)));
        }
        void Update(double Seconds)
        {
            if(Seconds==LastTime) { return; } LastTime=Seconds;
            const float Short=float(FMath::Min(Size.X,Size.Y));
            for(int32 K=0;K<Positions.Num();++K)
            {
                const auto& M=Motion[K]; FVector2D D=FVector2D::ZeroVector; float A=1;
                if(M.Kind==1)
                {
                    // Free at the side edge where the curtain enters, still at the end of its tail.
                    const float Envelope=FMath::Sin(PI*(.25f+.75f*M.Along));
                    // Noise never repeats, so there is no loop seam; a few seconds per sway reads as a living curtain.
                    D=FVector2D(Short*.022f*Noise(Seconds*.055+M.Along*.8,M.Seed+71),Short*.060f*Noise(Seconds*.090+M.Along*1.7,M.Seed))*Envelope;
                    if(M.Opposite) { D=-D; }
                    // Brighter patches drift along each band, like light running through a curtain.
                    A=.72f+.28f*Noise(Seconds*.16+M.Along*3.2,M.Seed+193);
                }
                else if(M.Kind==2)
                {
                    D=FVector2D(2.2f*Noise(Seconds*.022,M.Seed),1.6f*Noise(Seconds*.017,M.Seed+71));
                    A=.85f+.15f*Noise(Seconds*.09,M.Seed+193);
                }
                Positions[K]=RestPositions[K]+D; Colours[K]=RestColours[K]; Colours[K].A*=A;
            }
        }
        explicit FCornerAuroraMesh(FVector2D InSize) : Size(InSize)
        {
            int32 Kind=0; float Along=0; uint32 Seed=0;
            auto Add=[&](FVector2D P,FLinearColor C,bool Opposite)
            { Positions.Add(Opposite?Size-P:P); Colours.Add(C); Motion.Add({Kind,Along,Seed,Opposite}); };
            for(bool Opposite:{false,true})
            {
                // Each corner's area ends on an exponential curve: it starts down the side edge (Reach.Y of the height)
                // and sweeps out along the top/bottom edge (Reach.X of the width), hugging that edge in a long tail.
                // Top left: 30% down the left edge, 70% along the top. Bottom right: from just below the middle of
                // the right edge, 65% along the bottom.
                const FVector2D Reach=Opposite?FVector2D(.65,.45):FVector2D(.70,.30);
                constexpr float Steep=3.2f;
                auto Edge=[&](float T)
                {
                    const float E=(FMath::Exp(-Steep*T)-FMath::Exp(-Steep))/(1-FMath::Exp(-Steep));
                    return FVector2D(Reach.X*T*Size.X,Reach.Y*E*Size.Y);
                };
                constexpr int32 Steps=40;
                Kind=0;
                // Dark blue toward the corner, completely transparent on the curve.
                const float Depth[]={0,.45f,.78f,1},Shade[]={.62f,.42f,.16f,0};
                int32 Base=Positions.Num();
                for(int32 K=0;K<=Steps;++K)
                {
                    const FVector2D C=Edge(float(K)/Steps);
                    for(int32 J=0;J<4;++J)
                    {
                        Add(C*Depth[J],FLinearColor(.010f,.018f,.060f,Shade[J]),Opposite);
                        if(K>0 && J<3) { const SlateIndex B=SlateIndex(Base+K*4+J),A=SlateIndex(B-4); Indices.Append({A,SlateIndex(A+1),B,SlateIndex(A+1),SlateIndex(B+1),B}); }
                    }
                }
                constexpr int32 Lanes=7;
                const float Across[]={-1,-.67f,-.34f,0,.34f,.67f,1},Falloff[]={0,.10f,.42f,1,.42f,.10f,0};
                const float Inset[]={.70f,.80f,.60f,.90f};
                for(int32 Band=0;Band<4;++Band)
                {
                    Kind=1; Seed=211+Band*37;
                    Base=Positions.Num();
                    for(int32 K=0;K<=Steps;++K)
                    {
                        // Starts a little beyond the side edge so the band's end is cut off-screen, not on a visible slant.
                        Along=float(K)/Steps; const float T=Along*1.15f-.15f;
                        // The band follows the curve inside the area, swaying across it (along the curve's normal).
                        const FVector2D Here=Edge(T)*Inset[Band],Next=Edge(FMath::Min(1.f,T+.01f))*Inset[Band],Prev=Edge(T-.01f)*Inset[Band];
                        FVector2D Normal=FVector2D(-(Next-Prev).Y,(Next-Prev).X).GetSafeNormal();
                        const float Wave=(.040f*FMath::Sin(T*5.9f+Band*.9f)+.010f*FMath::Sin(T*17+Band))*Size.Y;
                        const float X=float(Here.X+Normal.X*Wave),Y=float(Here.Y+Normal.Y*Wave);
                        const float Width=(Band==0?.090f:Band==1?.062f:Band==2?.046f:.022f)*Size.Y*(1-.45f*T);
                        // Full where the curtain leaves the side edge, thinning out along the tail.
                        const float Fade=FMath::SmoothStep(0.f,1.f,(1-T)/.55f)*(.78f+.22f*FMath::Sin(T*9+Band));
                        const FLinearColor Hue=FMath::Lerp(Band==0?FLinearColor(.28f,.035f,.70f):Band==1?FLinearColor(.012f,.48f,.27f):FLinearColor(.012f,.22f,.48f),FLinearColor(.18f,.035f,.40f),T*.25f);
                        for(int32 J=0;J<Lanes;++J)
                        {
                            FLinearColor C=Hue; C.A=Falloff[J]*Fade*(Band==0?.60f:Band==1?.46f:Band==2?.26f:.16f);
                            Add(FVector2D(X,Y)+Normal*(Across[J]*Width),C,Opposite);
                            if(K>0 && J<Lanes-1)
                            {
                                const SlateIndex B=SlateIndex(Base+K*Lanes+J),A=SlateIndex(B-Lanes);
                                Indices.Append({A,SlateIndex(A+1),B,SlateIndex(A+1),SlateIndex(B+1),B});
                            }
                        }
                    }
                }
            }
            Kind=2;
            FRandomStream Stars(19860719);
            const int32 Count=FMath::Clamp(FMath::RoundToInt(Size.X*Size.Y/4000),240,480);
            for(int32 K=0;K<Count;++K)
            {
                Seed=4001+K*97;
                const FVector2D At(Stars.FRand()*Size.X,Stars.FRand()*Size.Y);
                const FVector2D Q=(At-Size*.5)/Size;
                if(FMath::Square(Q.X/.28)+FMath::Square(Q.Y/.34)<1) { continue; } // Keep decorative stars off the sculpture.
                const float R=Stars.FRandRange(.28f,.85f),A=Stars.FRandRange(.22f,.78f);
                const FLinearColor Hue=FMath::Lerp(FLinearColor(.25f,.63f,.85f),FLinearColor(.88f,.94f,1.f),Stars.FRand());
                const SlateIndex Base=SlateIndex(Positions.Num());
                for(const FVector2D D:{FVector2D(-R,-R),FVector2D(R,-R),FVector2D(R,R),FVector2D(-R,R)})
                { Add(At+D,Hue.CopyWithNewOpacity(A),false); }
                Indices.Append({Base,SlateIndex(Base+1),SlateIndex(Base+2),Base,SlateIndex(Base+2),SlateIndex(Base+3)});
                if(K%23==0)
                {
                    const int32 Start=Positions.Num(); Add(At,Hue.CopyWithNewOpacity(.35f),false);
                    for(int32 J=0;J<=8;++J)
                    {
                        const float Angle=2*PI*J/8;
                        Add(At+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*3.2f,Hue.CopyWithNewOpacity(0),false);
                        if(J>0) { Indices.Append({SlateIndex(Start),SlateIndex(Start+J),SlateIndex(Start+J+1)}); }
                    }
                }
            }
            RestPositions=Positions; RestColours=Colours;
        }
    };

    /** Filled convex polygon in one colour. */
    inline void Poly(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const TArray<FVector2D>& P, const FLinearColor& C)
    {
        TArray<FLinearColor> Col; Col.Init(C, P.Num()); TArray<SlateIndex> I;
        for (int32 K = 1; K + 1 < P.Num(); ++K) { I.Append({ 0, SlateIndex(K), SlateIndex(K + 1) }); }
        Tris(Out, L, G, P, Col, I);
    }

    inline void Line(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const TArray<FVector2D>& P, const FLinearColor& C, float Width)
    {
        FSlateDrawElement::MakeLines(Out, L, G.ToPaintGeometry(), P, ESlateDrawEffect::None, PaintColour(C), true, Width);
    }

    /** Text centred on a point. */
    inline void Text(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2D Centre, const FString& S, const FSlateFontInfo& F, const FLinearColor& C)
    {
        const FVector2D Size = Measure(S, F);
        FSlateDrawElement::MakeText(Out, L, G.ToPaintGeometry(Size, FSlateLayoutTransform(Centre - Size * .5)), S, F, ESlateDrawEffect::None, PaintColour(C));
    }
    inline void TextAt(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2D TopLeft, const FString& S, const FSlateFontInfo& F, const FLinearColor& C)
    {
        const FVector2D Size = Measure(S, F);
        FSlateDrawElement::MakeText(Out, L, G.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)), S, F, ESlateDrawEffect::None, PaintColour(C));
    }


    /** Feathered vector stroke, batched with other custom geometry (no per-line Slate element). */
    inline void Stroke(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const TArray<FVector2D>& Points, FLinearColor Colour, float Width)
    {
        if (Points.Num()<2 || Width<=0 || Colour.A<=.001f) { return; }
        const float Feather=1.f/FMath::Max(.1f,G.GetAccumulatedLayoutTransform().GetScale());
        const float Half=Width*.5f;
        TArray<FVector2D> P; TArray<FLinearColor> C; TArray<SlateIndex> I;
        P.Reserve(Points.Num()*4); C.Reserve(Points.Num()*4); I.Reserve((Points.Num()-1)*18);
        FLinearColor Clear=Colour; Clear.A=0;
        for(int32 K=0;K<Points.Num();++K)
        {
            const FVector2D Before=(Points[K]-Points[FMath::Max(0,K-1)]).GetSafeNormal();
            const FVector2D After=(Points[FMath::Min(Points.Num()-1,K+1)]-Points[K]).GetSafeNormal();
            FVector2D D=(Before+After).GetSafeNormal(); if(D.IsNearlyZero()) { D=After.IsNearlyZero()?Before:After; }
            FVector2D N(-D.Y,D.X);
            const FVector2D Segment=After.IsNearlyZero()?Before:After;
            N/=FMath::Max(.6,FMath::Abs(FVector2D::DotProduct(N,FVector2D(-Segment.Y,Segment.X))));
            P.Append({Points[K]-N*(Half+Feather),Points[K]-N*Half,Points[K]+N*Half,Points[K]+N*(Half+Feather)});
            C.Append({Clear,Colour,Colour,Clear});
            if(K>0) for(int32 J=0;J<3;++J)
            {
                const SlateIndex B=SlateIndex(K*4+J), A=SlateIndex(B-4);
                I.Append({A,SlateIndex(A+1),B,SlateIndex(A+1),SlateIndex(B+1),B});
            }
        }
        Tris(Out,L,G,P,C,I);
    }

    inline void ArcStroke(FSlateWindowElementList& Out,int32 L,const FGeometry& G,FVector2D Centre,float Radius,float Start,float Sweep,FLinearColor Colour,float Width)
    {
        TArray<FVector2D> P; const int32 N=CurveSteps(G,Radius,Sweep,32);
        for(int32 K=0;K<=N;++K) { const float A=FMath::DegreesToRadians(Start+Sweep*K/N); P.Add(Centre+FVector2D(FMath::Cos(A),FMath::Sin(A))*Radius); }
        Stroke(Out,L,G,P,Colour,Width);
    }

    /** Thin optical rim and dark glass: shared by keys, candidates and controls; no blur pass. */
    inline void GlassTile(FSlateWindowElementList& Out,int32 L,const FGeometry& G,FVector2D Pos,FVector2D Size,float Radius,FLinearColor Accent,float Emphasis=0)
    {
        const FLinearColor Rim=FMath::Lerp(Violet.CopyWithNewOpacity(.34f),Accent.CopyWithNewOpacity(.78f),Emphasis);
        Fill(Out,L,G,Pos,Size,Radius,
            FMath::Lerp(FLinearColor(.025f,.035f,.070f,.88f),FLinearColor(.035f,.09f,.10f,.94f),Emphasis*.6f),FLinearColor(.008f,.012f,.033f,.94f));
        // Feather the silhouette as well as the glyphs, including at fractional phone DPI scales.
        TArray<FVector2D> Edge;
        const FVector2D Centres[]={Pos+FVector2D(Size.X-Radius,Radius),Pos+Size-FVector2D(Radius),Pos+FVector2D(Radius,Size.Y-Radius),Pos+FVector2D(Radius)};
        const int32 Steps=FMath::Clamp(CurveSteps(G,Radius,360,32)/4,3,8);
        for(int32 K=0;K<4;++K) for(int32 J=0;J<=Steps;++J)
        {
            const float A=FMath::DegreesToRadians(-90.f+90*K+90.f*J/Steps);
            Edge.Add(Centres[K]+FVector2D(FMath::Cos(A),FMath::Sin(A))*Radius);
        }
        const FVector2D First=Edge[0]; Edge.Add(First);
        Stroke(Out,L,G,Edge,Rim.CopyWithNewOpacity(.065f+.07f*Emphasis),4.f);
        Stroke(Out,L,G,Edge,Rim,1.1f);
        const float Reach=FMath::Max(3.f,float(Size.X)*.5f-Radius-3);
        Stroke(Out,L,G,{Pos+FVector2D(Size.X*.5f-Reach,1.8f),Pos+FVector2D(Size.X*.5f+Reach,1.8f)},Paper.CopyWithNewOpacity(.06f+.12f*Emphasis),.8f);
    }

    /** Mincho with generous tracking, for titles, captions and the brand mark. Spacing is in 1/1000 em. */
    inline FSlateFontInfo Spaced(float Size, int32 Spacing = 160)
    {
        FSlateFontInfo F = Font(Size); F.LetterSpacing = Spacing; return F;
    }

    /** Signature hairline: cyan on the left through violet to pink on the right. */
    inline FLinearColor Hairline(float T)
    {
        static const FLinearColor Stops[] = { FLinearColor(.06f, .86f, .82f), FLinearColor(.20f, .60f, 1.f), FLinearColor(.62f, .36f, 1.f), FLinearColor(1.f, .36f, .80f) };
        T = FMath::Clamp(T, 0.f, 1.f) * 3.f; const int32 I = FMath::Min(2, int32(T));
        return FMath::Lerp(Stops[I], Stops[I + 1], T - I);
    }

    /** Horizontal hairline that fades at both ends, with an optional bright star at DotAt (0..1). */
    inline void GlowLine(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2D From, float Length, float Opacity = 1.f, float DotAt = .5f, float DotGlow = 1.f, float Thick = 1.4f)
    {
        if (Length <= 1 || Opacity <= .001f) { return; }
        TArray<FVector2D> P; TArray<FLinearColor> C; TArray<SlateIndex> I; constexpr int32 Steps = 24;
        const float Feather = 1.2f / FMath::Max(.1f, G.GetAccumulatedLayoutTransform().GetScale());
        const float Off[] = { -Thick * .5f - Feather, -Thick * .5f, Thick * .5f, Thick * .5f + Feather }, Op[] = { 0, 1, 1, 0 };
        for (int32 K = 0; K <= Steps; ++K)
        {
            const float T = float(K) / Steps, Edge = FMath::SmoothStep(0.f, .22f, FMath::Min(T, 1 - T));
            const float Near = DotAt >= 0 ? FMath::Exp(-FMath::Square((T - DotAt) * 5.5f)) : 0;
            FLinearColor Col = FMath::Lerp(Hairline(T), Paper, .25f * Near); Col.A = Opacity * Edge * (.80f + .20f * Near);
            for (int32 J = 0; J < 4; ++J)
            {
                P.Add(From + FVector2D(Length * T, Off[J])); C.Add(Col.CopyWithNewOpacity(Col.A * Op[J]));
                if (K > 0 && J < 3) { const SlateIndex B = SlateIndex(K * 4 + J), A = SlateIndex(B - 4); I.Append({ A, SlateIndex(A + 1), B, SlateIndex(A + 1), SlateIndex(B + 1), B }); }
            }
        }
        Tris(Out, L, G, P, C, I);
        if (DotAt >= 0 && DotGlow > .001f)
        {
            const FVector2D Dot = From + FVector2D(Length * DotAt, 0);
            Disc(Out, L, G, Dot, 18.f, Hairline(DotAt).CopyWithNewOpacity(.36f * Opacity * DotGlow), FLinearColor::Transparent, 16);
            Disc(Out, L, G, Dot, 3.f, Paper.CopyWithNewOpacity(Opacity * DotGlow), Hairline(DotAt).CopyWithNewOpacity(.5f * Opacity * DotGlow), 10);
        }
    }

    inline TArray<FVector2D> RoundedOutline(const FGeometry& G, FVector2D Pos, FVector2D Size, float Radius)
    {
        Radius = FMath::Min3(Radius, float(Size.X) * .5f, float(Size.Y) * .5f);
        TArray<FVector2D> Edge;
        const FVector2D Centres[] = { Pos + FVector2D(Size.X - Radius, Radius), Pos + Size - FVector2D(Radius), Pos + FVector2D(Radius, Size.Y - Radius), Pos + FVector2D(Radius) };
        const int32 Steps = FMath::Clamp(CurveSteps(G, Radius, 360, 32) / 4, 3, 8);
        for (int32 K = 0; K < 4; ++K) for (int32 J = 0; J <= Steps; ++J)
        {
            const float A = FMath::DegreesToRadians(-90.f + 90 * K + 90.f * J / Steps);
            Edge.Add(Centres[K] + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
        }
        const FVector2D First = Edge[0]; Edge.Add(First);
        return Edge;
    }

    /** Feathered stroke whose colour follows the point position (for gradient rims). */
    template <typename FTint>
    void TintStroke(FSlateWindowElementList& Out, int32 L, const FGeometry& G, const TArray<FVector2D>& Points, FTint&& Tint, float Width)
    {
        if (Points.Num() < 2 || Width <= 0) { return; }
        const float Feather = 1.f / FMath::Max(.1f, G.GetAccumulatedLayoutTransform().GetScale()), Half = Width * .5f;
        TArray<FVector2D> P; TArray<FLinearColor> C; TArray<SlateIndex> I;
        for (int32 K = 0; K < Points.Num(); ++K)
        {
            const FVector2D Before = (Points[K] - Points[FMath::Max(0, K - 1)]).GetSafeNormal(), After = (Points[FMath::Min(Points.Num() - 1, K + 1)] - Points[K]).GetSafeNormal();
            FVector2D D = (Before + After).GetSafeNormal(); if (D.IsNearlyZero()) { D = After.IsNearlyZero() ? Before : After; }
            const FVector2D N(-D.Y, D.X); const FLinearColor Col = Tint(Points[K]);
            P.Append({ Points[K] - N * (Half + Feather), Points[K] - N * Half, Points[K] + N * Half, Points[K] + N * (Half + Feather) });
            C.Append({ Col.CopyWithNewOpacity(0), Col, Col, Col.CopyWithNewOpacity(0) });
            if (K > 0) for (int32 J = 0; J < 3; ++J) { const SlateIndex B = SlateIndex(K * 4 + J), A = SlateIndex(B - 4); I.Append({ A, SlateIndex(A + 1), B, SlateIndex(A + 1), SlateIndex(B + 1), B }); }
        }
        Tris(Out, L, G, P, C, I);
    }

    /** Large glass sheet for the menu and keyboard: deep navy, a cyan-to-pink rim and a lit top hairline. */
    inline void GlassPanel(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2D Pos, FVector2D Size, float Radius)
    {
        Fill(Out, L, G, Pos, Size, Radius, FLinearColor(.016f, .024f, .060f, .90f), FLinearColor(.008f, .012f, .034f, .93f));
        const TArray<FVector2D> Edge = RoundedOutline(G, Pos, Size, Radius);
        TintStroke(Out, L, G, Edge, [&](const FVector2D& P) {
            const float T = float((P.X - Pos.X) / FMath::Max(1.0, Size.X)), Top = 1 - float((P.Y - Pos.Y) / FMath::Max(1.0, Size.Y));
            return Hairline(T).CopyWithNewOpacity(.20f + .26f * Top); }, 1.1f);
        GlowLine(Out, L, G, Pos + FVector2D(Radius, .8f), float(Size.X) - 2 * Radius, .55f, .5f, .5f, 1.f);
    }

    /** Quiet key or chip: barely-there glass with one hairline rim; Emphasis lights it in Accent. */
    inline void KeyTile(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2D Pos, FVector2D Size, float Radius, FLinearColor Accent, float Emphasis = 0)
    {
        Fill(Out, L, G, Pos, Size, Radius,
            FMath::Lerp(FLinearColor(1, 1, 1, .035f), Accent.CopyWithNewOpacity(.16f), Emphasis), FMath::Lerp(FLinearColor(1, 1, 1, .012f), Accent.CopyWithNewOpacity(.06f), Emphasis));
        Stroke(Out, L, G, RoundedOutline(G, Pos, Size, Radius), FMath::Lerp(Paper.CopyWithNewOpacity(.11f), Accent.CopyWithNewOpacity(.75f), Emphasis), 1.f);
    }

    /** Paper plane pointing along +X, centred at C, length S, rotated by Deg. */
    inline void Plane(FSlateWindowElementList& Out, int32 L, const FGeometry& G, FVector2D C, float S, float Deg, const FLinearColor& Body, const FLinearColor& Fold)
    {
        const float A = FMath::DegreesToRadians(Deg); const FVector2D X(FMath::Cos(A), FMath::Sin(A)), Y(-X.Y, X.X);
        auto At = [&](float U, float V) { return C + (X * U + Y * V) * S; };
        // Open wings and a visible centre fold read as a paper plane, rather than a filled arrowhead.
        const FLinearColor Ink=Fold;
        const float W=FMath::Max(1.3f,S*.065f);
        Stroke(Out,L,G,{At(.5f,0),At(-.48f,-.38f),At(-.22f,0),At(-.48f,.38f),At(.5f,0)},Ink,W);
        Stroke(Out,L,G,{At(-.22f,0),At(.5f,0)},Body.CopyWithNewOpacity(Ink.A*.8f),W*.7f);
    }
}
