#pragma once
#include "CoreMinimal.h"

/** A real 3D model pre-converted to particles (Tools/PointCloud/ModelToPoints.py). Never rendered as a mesh. */
struct FLightBoxModelCloud
{
    FVector3f Min=FVector3f::ZeroVector,Max=FVector3f::ZeroVector; // UE cm, Z up
    TArray<FVector3f> Positions,Normals;
    TArray<FColor> Colors; // sRGB
    FVector3f Extent() const { return (Max-Min).ComponentMax(FVector3f(.001f)); }
};

namespace LightBoxModels
{
    struct FEntry { const TCHAR* Id; const TCHAR* Description; };
    /** Bundled models the AI may request with shape:"model", model:"<id>". */
    TConstArrayView<FEntry> Catalog();
    bool IsKnown(const FString& Id);
    /** Parses an .lbpt buffer; returns null on any malformed input. */
    TSharedPtr<const FLightBoxModelCloud,ESPMode::ThreadSafe> Parse(const TArray<uint8>& Bytes);
    /** Loads Content/LightBox/Models/<Id>.lbpt once and caches it (thread safe). Null if missing. */
    TSharedPtr<const FLightBoxModelCloud,ESPMode::ThreadSafe> Load(const FString& Id);
    /** Uniform scale that fits the model inside Size (cm) while keeping its real proportions. */
    float FitScale(const FLightBoxModelCloud& Cloud,const FVector& Size);
    /** Model point in the object's local space: centred on the box, uniformly scaled. */
    inline FVector Local(const FLightBoxModelCloud& Cloud,int32 Index,float Scale) { return FVector((Cloud.Positions[Index]-(Cloud.Min+Cloud.Max)*.5f)*Scale); }
}
