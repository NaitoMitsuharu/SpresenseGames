#pragma once
#include "CoreMinimal.h"
struct FFlightMesh;

namespace MazeToyGeometry
{
	/** Smooth, genuinely rounded enamel shell, inside the supplied box bounds. */
	void RoundedBox(FFlightMesh& Mesh, const FVector& Center, const FVector& Half, float Radius);
}
