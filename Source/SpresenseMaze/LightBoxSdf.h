#pragma once
#include "CoreMinimal.h"

/** One primitive of an LLM-authored signed-distance sculpture. Units are local centimetres. */
struct FLightBoxSdfPart
{
    FString Type=TEXT("ellipsoid"); // ellipsoid | capsule | box | torus
    FVector A=FVector::ZeroVector;  // centre, or capsule start
    FVector B=FVector::ZeroVector;  // capsule end
    FVector R=FVector(10);          // ellipsoid radii / box half size / capsule (r1,r2) / torus (major,minor)
    FVector Rotation=FVector::ZeroVector; // degrees, ellipsoid/box/torus
    float Blend=8;   // smooth-union / smooth-cut radius in cm
    float Round=0;   // box corner rounding in cm
    bool bCut=false; // carve this part away (eye sockets, mouth, hollows)
    bool bSmooth=false; // no fur/leaf relief here (eyes, nose, claws, hard parts)
    bool bHasColor=false;
    FLinearColor Color=FLinearColor::White;
};

struct FLightBoxSdfMesh
{
    TArray<FVector> Vertices,Normals;
    TArray<FLinearColor> Colors; // alpha .25 marks "smooth" (no surface detail)
    TArray<int32> Triangles;
};

namespace LightBoxSdf
{
    float PartDistance(const FLightBoxSdfPart& Part,const FVector& Point);
    constexpr int32 MaxParts=40;
    constexpr int32 MaxVertices=24000;
    /** Signed distance at P (negative inside); optionally the blended colour and smooth flag at P. */
    float Distance(const TArray<FLightBoxSdfPart>& Parts,const FVector& P,const FLinearColor& Base,FLinearColor* Color=nullptr,bool* bSmooth=nullptr);
    /** Surface-nets mesh of the zero level set, cached by content (thread safe). */
    TSharedRef<const FLightBoxSdfMesh,ESPMode::ThreadSafe> Mesh(const TArray<FLightBoxSdfPart>& Parts,const FLinearColor& Base);
    FBox Bounds(const TArray<FLightBoxSdfPart>& Parts);
}
