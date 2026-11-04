#pragma once

#include "CoreMinimal.h"

class AActor;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USceneComponent;

/**
 * CPU-side triangle list used to author the flight game's models in code.
 * Triangles are always added with an outward hint so winding is correct by
 * construction (UE front faces have (b-a)x(c-a) pointing inward).
 */
struct FFlightMesh
{
	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;

	int32 AddVert(const FVector& P, const FVector& N = FVector::ZeroVector,
		const FVector2D& UV = FVector2D::ZeroVector, const FLinearColor& C = FLinearColor::White);
	void AddTri(int32 A, int32 B, int32 C, const FVector& OutwardHint);
	/** Flat-shaded quad (own vertices), corners in loop order. */
	void AddQuad(const FVector& A, const FVector& B, const FVector& C, const FVector& D,
		const FVector& Outward, const FLinearColor& Color = FLinearColor::White);

	/**
	 * Smooth loft through closed rings that all share the same point count.
	 * The outward direction of every face is taken from its ring centroid.
	 */
	void AddLoft(const TArray<TArray<FVector>>& Rings, bool bCapStart, bool bCapEnd,
		const FLinearColor& Color = FLinearColor::White);

	void AddBox(const FTransform& Xf, const FVector& HalfExtents, const FLinearColor& Color = FLinearColor::White);
	void AddEllipsoid(const FVector& Center, const FVector& Radii, int32 Segments, int32 Stacks,
		bool bUpperHalfOnly = false, const FLinearColor& Color = FLinearColor::White);
	/** Torus around local X (the ring faces +X), major radius R, tube radius r. */
	void AddTorus(const FVector& Center, float R, float r, int32 Segments, int32 Sides);
	/** Lofted tube around +X with a radius per station: {X, Radius}. */
	void AddTube(const TArray<FVector2D>& Stations, const FVector& Offset, int32 Sides,
		bool bCapStart, bool bCapEnd, const FLinearColor& Color = FLinearColor::White);
	/**
	 * Thin lifting surface with a symmetric airfoil section, lofted from root to
	 * tip. Leading/trailing edges given in component space.
	 */
	void AddWing(const FVector& RootLE, const FVector& RootTE, const FVector& TipLE, const FVector& TipTE,
		float RootThickness, float TipThickness, const FVector& UpAxis);

	void Append(const FFlightMesh& Other, const FTransform& Xf);
	void RecomputeSmoothNormals(int32 FirstVert);

	/** Upload as section Index of a procedural mesh. */
	void Upload(UProceduralMeshComponent* PMC, int32 Index, UMaterialInterface* Material) const;
};

/** Runtime material helpers (assets authored by Tools/CreateFlightAssets.py). */
namespace FlightMaterials
{
	UMaterialInstanceDynamic* Structure(UObject* Outer, const FLinearColor& BaseColor, float Metallic = 0.5f, float Roughness = 0.55f);
	/** Lit opaque surface. Metallic/Roughness distinguish paint, metal and glass. */
	UMaterialInstanceDynamic* Surface(UObject* Outer, const FLinearColor& BaseColor, float Metallic,
		float Roughness, float Specular = 0.5f, const FLinearColor& Emissive = FLinearColor::Black);
	/** Unlit additive glow; set "Color" to fade (black is invisible). */
	UMaterialInstanceDynamic* Additive(UObject* Outer, const FLinearColor& Color);
	/** Opaque emissive (engine nozzles, lamps) that stays readable in bright sky. */
	UMaterialInstanceDynamic* Glow(UObject* Outer, const FLinearColor& Color);
}

/** Components of the player ship that the pawn animates. */
struct FFlightShipParts
{
	TObjectPtr<USceneComponent> Root = nullptr;
	TObjectPtr<UProceduralMeshComponent> Body = nullptr;
	TObjectPtr<UProceduralMeshComponent> AileronL = nullptr;
	TObjectPtr<UProceduralMeshComponent> AileronR = nullptr;
	TObjectPtr<UProceduralMeshComponent> ElevatorL = nullptr;
	TObjectPtr<UProceduralMeshComponent> ElevatorR = nullptr;
	TObjectPtr<UMaterialInstanceDynamic> EngineGlow = nullptr;
	TObjectPtr<UMaterialInstanceDynamic> PlumeGlow = nullptr;
	TObjectPtr<UMaterialInstanceDynamic> TrimGlow = nullptr;
	/** Gun muzzles and wingtips in ship space, for bolts and ring trails. */
	FVector GunL = FVector::ZeroVector;
	FVector GunR = FVector::ZeroVector;
	FVector WingTipL = FVector::ZeroVector;
	FVector WingTipR = FVector::ZeroVector;
	FVector Exhaust[2];
};

namespace FlightModels
{
	/** Builds the player ship under Parent (nose +X, ~7m long). */
	void BuildShip(AActor* Owner, USceneComponent* Parent, FFlightShipParts& Out);

	/** Shared enemy geometry, section 0 = hull, section 1 = glow. */
	const FFlightMesh& DroneHull();
	const FFlightMesh& DroneGlow();
	const FFlightMesh& TurretHull();
	const FFlightMesh& TurretGlow();
	const FFlightMesh& BossHull();
	const FFlightMesh& BossArmor();
	const FFlightMesh& BossGlow();
	const FFlightMesh& RingMesh();
}
