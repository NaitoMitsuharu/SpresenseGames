#pragma once
#include "CoreMinimal.h"
#include "LightBoxStyle.h"

struct FLightBoxMesh
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FLinearColor> Colors;
	TArray<FVector2D> UVs;
	static bool ValidOutline(const TArray<FVector2D>& Outline);
	static FLightBoxMesh Build(const FLightBoxObject& Object, float Time=0);
};
