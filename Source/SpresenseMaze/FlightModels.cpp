#include "FlightModels.h"

#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

// ---------------------------------------------------------------------------
// Mesh builder
// ---------------------------------------------------------------------------

int32 FFlightMesh::AddVert(const FVector& P, const FVector& N, const FVector2D& UV, const FLinearColor& C)
{
	Verts.Add(P);
	Normals.Add(N);
	UVs.Add(UV);
	Colors.Add(C);
	return Verts.Num() - 1;
}

void FFlightMesh::AddTri(int32 A, int32 B, int32 C, const FVector& OutwardHint)
{
	const FVector Cross = (Verts[B] - Verts[A]) ^ (Verts[C] - Verts[A]);
	// UE renders the face whose (b-a)x(c-a) points away from the viewer.
	if ((Cross | OutwardHint) > 0.f)
	{
		Swap(B, C);
	}
	Tris.Add(A);
	Tris.Add(B);
	Tris.Add(C);
}

void FFlightMesh::AddQuad(const FVector& A, const FVector& B, const FVector& C, const FVector& D,
	const FVector& Outward, const FLinearColor& Color)
{
	const FVector N = Outward.GetSafeNormal();
	const int32 I0 = AddVert(A, N, FVector2D(0, 0), Color);
	const int32 I1 = AddVert(B, N, FVector2D(1, 0), Color);
	const int32 I2 = AddVert(C, N, FVector2D(1, 1), Color);
	const int32 I3 = AddVert(D, N, FVector2D(0, 1), Color);
	AddTri(I0, I1, I2, N);
	AddTri(I0, I2, I3, N);
}

void FFlightMesh::RecomputeSmoothNormals(int32 FirstVert)
{
	for (int32 i = FirstVert; i < Normals.Num(); ++i)
	{
		Normals[i] = FVector::ZeroVector;
	}
	for (int32 t = 0; t + 2 < Tris.Num(); t += 3)
	{
		const int32 A = Tris[t], B = Tris[t + 1], C = Tris[t + 2];
		if (A < FirstVert && B < FirstVert && C < FirstVert)
		{
			continue;
		}
		const FVector FaceN = -((Verts[B] - Verts[A]) ^ (Verts[C] - Verts[A]));
		for (const int32 I : { A, B, C })
		{
			if (I >= FirstVert)
			{
				Normals[I] += FaceN;
			}
		}
	}
	for (int32 i = FirstVert; i < Normals.Num(); ++i)
	{
		Normals[i] = Normals[i].GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	}
}

void FFlightMesh::AddLoft(const TArray<TArray<FVector>>& Rings, bool bCapStart, bool bCapEnd, const FLinearColor& Color)
{
	if (Rings.Num() < 2)
	{
		return;
	}
	const int32 N = Rings[0].Num();
	TArray<FVector> Centroids;
	for (const TArray<FVector>& Ring : Rings)
	{
		FVector C = FVector::ZeroVector;
		for (const FVector& P : Ring)
		{
			C += P;
		}
		Centroids.Add(C / FMath::Max(Ring.Num(), 1));
	}

	const int32 Base = Verts.Num();
	for (int32 i = 0; i < Rings.Num(); ++i)
	{
		for (int32 j = 0; j < N; ++j)
		{
			AddVert(Rings[i][j], FVector::ZeroVector,
				FVector2D(j / static_cast<float>(N), i / static_cast<float>(Rings.Num() - 1)), Color);
		}
	}
	for (int32 i = 0; i + 1 < Rings.Num(); ++i)
	{
		const FVector Axis = (Centroids[i] + Centroids[i + 1]) * 0.5f;
		for (int32 j = 0; j < N; ++j)
		{
			const int32 J1 = (j + 1) % N;
			const int32 A = Base + i * N + j;
			const int32 B = Base + i * N + J1;
			const int32 C = Base + (i + 1) * N + J1;
			const int32 D = Base + (i + 1) * N + j;
			const FVector Mid = (Verts[A] + Verts[B] + Verts[C] + Verts[D]) * 0.25f;
			const FVector Hint = Mid - Axis;
			AddTri(A, B, C, Hint);
			AddTri(A, C, D, Hint);
		}
	}
	RecomputeSmoothNormals(Base);

	auto Cap = [&](int32 RingIndex, const FVector& Outward)
	{
		const FVector Nrm = Outward.GetSafeNormal();
		const int32 Center = AddVert(Centroids[RingIndex], Nrm, FVector2D(0.5f, 0.5f), Color);
		const int32 First = Verts.Num();
		for (int32 j = 0; j < N; ++j)
		{
			AddVert(Rings[RingIndex][j], Nrm, FVector2D(0, 0), Color);
		}
		for (int32 j = 0; j < N; ++j)
		{
			AddTri(Center, First + j, First + (j + 1) % N, Nrm);
		}
	};
	if (bCapStart)
	{
		Cap(0, Centroids[0] - Centroids[1]);
	}
	if (bCapEnd)
	{
		const int32 L = Rings.Num() - 1;
		Cap(L, Centroids[L] - Centroids[L - 1]);
	}
}

void FFlightMesh::AddBox(const FTransform& Xf, const FVector& E, const FLinearColor& Color)
{
	auto P = [&](float X, float Y, float Z) { return Xf.TransformPosition(FVector(X * E.X, Y * E.Y, Z * E.Z)); };
	auto N = [&](float X, float Y, float Z) { return Xf.TransformVectorNoScale(FVector(X, Y, Z)); };
	AddQuad(P(-1, -1, 1), P(1, -1, 1), P(1, 1, 1), P(-1, 1, 1), N(0, 0, 1), Color);
	AddQuad(P(-1, -1, -1), P(-1, 1, -1), P(1, 1, -1), P(1, -1, -1), N(0, 0, -1), Color);
	AddQuad(P(1, -1, -1), P(1, 1, -1), P(1, 1, 1), P(1, -1, 1), N(1, 0, 0), Color);
	AddQuad(P(-1, -1, -1), P(-1, -1, 1), P(-1, 1, 1), P(-1, 1, -1), N(-1, 0, 0), Color);
	AddQuad(P(-1, 1, -1), P(-1, 1, 1), P(1, 1, 1), P(1, 1, -1), N(0, 1, 0), Color);
	AddQuad(P(-1, -1, -1), P(1, -1, -1), P(1, -1, 1), P(-1, -1, 1), N(0, -1, 0), Color);
}

void FFlightMesh::AddEllipsoid(const FVector& Center, const FVector& Radii, int32 Segments, int32 Stacks,
	bool bUpperHalfOnly, const FLinearColor& Color)
{
	TArray<TArray<FVector>> Rings;
	const float From = bUpperHalfOnly ? 0.f : -PI * 0.5f + 0.06f;
	const float To = PI * 0.5f - 0.06f;
	for (int32 k = 0; k <= Stacks; ++k)
	{
		const float Lat = FMath::Lerp(From, To, k / static_cast<float>(Stacks));
		TArray<FVector>& Ring = Rings.AddDefaulted_GetRef();
		for (int32 j = 0; j < Segments; ++j)
		{
			const float Lon = 2.f * PI * j / Segments;
			Ring.Add(Center + FVector(FMath::Cos(Lat) * FMath::Cos(Lon) * Radii.X,
				FMath::Cos(Lat) * FMath::Sin(Lon) * Radii.Y, FMath::Sin(Lat) * Radii.Z));
		}
	}
	AddLoft(Rings, !bUpperHalfOnly, true, Color);
}

void FFlightMesh::AddTorus(const FVector& Center, float R, float r, int32 Segments, int32 Sides)
{
	const int32 Base = Verts.Num();
	for (int32 u = 0; u < Segments; ++u)
	{
		const float A = 2.f * PI * u / Segments;
		const FVector Dir(0.f, FMath::Cos(A), FMath::Sin(A));
		for (int32 v = 0; v < Sides; ++v)
		{
			const float B = 2.f * PI * v / Sides;
			const FVector N = Dir * FMath::Cos(B) + FVector::ForwardVector * FMath::Sin(B);
			AddVert(Center + Dir * R + N * r, N, FVector2D(u / static_cast<float>(Segments), v / static_cast<float>(Sides)));
		}
	}
	for (int32 u = 0; u < Segments; ++u)
	{
		const int32 U1 = (u + 1) % Segments;
		for (int32 v = 0; v < Sides; ++v)
		{
			const int32 V1 = (v + 1) % Sides;
			const int32 A = Base + u * Sides + v, B = Base + U1 * Sides + v;
			const int32 C = Base + U1 * Sides + V1, D = Base + u * Sides + V1;
			const FVector Hint = Normals[A] + Normals[C];
			AddTri(A, B, C, Hint);
			AddTri(A, C, D, Hint);
		}
	}
}

void FFlightMesh::AddTube(const TArray<FVector2D>& Stations, const FVector& Offset, int32 Sides,
	bool bCapStart, bool bCapEnd, const FLinearColor& Color)
{
	TArray<TArray<FVector>> Rings;
	for (const FVector2D& St : Stations)
	{
		TArray<FVector>& Ring = Rings.AddDefaulted_GetRef();
		for (int32 j = 0; j < Sides; ++j)
		{
			const float A = 2.f * PI * j / Sides;
			Ring.Add(Offset + FVector(St.X, FMath::Cos(A) * St.Y, FMath::Sin(A) * St.Y));
		}
	}
	AddLoft(Rings, bCapStart, bCapEnd, Color);
}

void FFlightMesh::AddWing(const FVector& RootLE, const FVector& RootTE, const FVector& TipLE, const FVector& TipTE,
	float RootThickness, float TipThickness, const FVector& UpAxis)
{
	// NACA 00xx thickness, normalised so the maximum half-thickness is 0.5.
	auto HalfThickness = [](float C)
	{
		return 5.f * (0.2969f * FMath::Sqrt(C) - 0.126f * C - 0.3516f * C * C + 0.2843f * C * C * C - 0.1036f * C * C * C * C);
	};
	constexpr int32 Chord = 7;
	constexpr int32 Span = 4;
	TArray<TArray<FVector>> Rings;
	for (int32 s = 0; s <= Span; ++s)
	{
		const float T = s / static_cast<float>(Span);
		const FVector LE = FMath::Lerp(RootLE, TipLE, T);
		const FVector TE = FMath::Lerp(RootTE, TipTE, T);
		const float Thick = FMath::Lerp(RootThickness, TipThickness, T);
		TArray<FVector>& Ring = Rings.AddDefaulted_GetRef();
		auto ChordPos = [](int32 i) { return 0.5f * (1.f - FMath::Cos(PI * i / Chord)); };
		for (int32 i = 0; i <= Chord; ++i)
		{
			const float C = ChordPos(i);
			Ring.Add(FMath::Lerp(LE, TE, C) + UpAxis * HalfThickness(C) * Thick);
		}
		for (int32 i = Chord - 1; i >= 1; --i)
		{
			const float C = ChordPos(i);
			Ring.Add(FMath::Lerp(LE, TE, C) - UpAxis * HalfThickness(C) * Thick);
		}
	}
	AddLoft(Rings, false, true);
}

void FFlightMesh::Append(const FFlightMesh& Other, const FTransform& Xf)
{
	const int32 Base = Verts.Num();
	for (int32 i = 0; i < Other.Verts.Num(); ++i)
	{
		AddVert(Xf.TransformPosition(Other.Verts[i]), Xf.TransformVectorNoScale(Other.Normals[i]), Other.UVs[i], Other.Colors[i]);
	}
	const bool bMirror = Xf.GetDeterminant() < 0.f;
	for (int32 t = 0; t + 2 < Other.Tris.Num(); t += 3)
	{
		Tris.Add(Base + Other.Tris[t]);
		Tris.Add(Base + Other.Tris[bMirror ? t + 2 : t + 1]);
		Tris.Add(Base + Other.Tris[bMirror ? t + 1 : t + 2]);
	}
}

void FFlightMesh::Upload(UProceduralMeshComponent* PMC, int32 Index, UMaterialInterface* Material) const
{
	if (!PMC)
	{
		return;
	}
	PMC->CreateMeshSection_LinearColor(Index, Verts, Tris, Normals, UVs, Colors, TArray<FProcMeshTangent>(), false);
	if (Material)
	{
		PMC->SetMaterial(Index, Material);
	}
}

// ---------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------

namespace FlightMaterials
{
	UMaterialInstanceDynamic* Structure(UObject* Outer, const FLinearColor& BaseColor, float Metallic, float Roughness)
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Flight/M_FlightStructure.M_FlightStructure"));
		if (!Parent) { return Surface(Outer, BaseColor, Metallic, Roughness); }
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, Outer);
		MID->SetVectorParameterValue(TEXT("BaseColor"), BaseColor);
		MID->SetScalarParameterValue(TEXT("Metallic"), Metallic);
		MID->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		return MID;
	}
	UMaterialInstanceDynamic* Surface(UObject* Outer, const FLinearColor& BaseColor, float Metallic,
		float Roughness, float Specular, const FLinearColor& Emissive)
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Flight/M_FlightSurface.M_FlightSurface"));
		if (!Parent)
		{
			Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_MazeTile.M_MazeTile"));
		}
		if (!Parent)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, Outer);
		MID->SetVectorParameterValue(TEXT("BaseColor"), BaseColor);
		MID->SetVectorParameterValue(TEXT("Color"), BaseColor);
		MID->SetScalarParameterValue(TEXT("Metallic"), Metallic);
		MID->SetScalarParameterValue(TEXT("Roughness"), Roughness);
		MID->SetScalarParameterValue(TEXT("Specular"), Specular);
		MID->SetVectorParameterValue(TEXT("Emissive"), Emissive);
		return MID;
	}

	UMaterialInstanceDynamic* Additive(UObject* Outer, const FLinearColor& Color)
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Flight/M_FlightAdditive.M_FlightAdditive"));
		if (!Parent)
		{
			Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_MazeGlow.M_MazeGlow"));
		}
		if (!Parent)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, Outer);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		return MID;
	}

	UMaterialInstanceDynamic* Glow(UObject* Outer, const FLinearColor& Color)
	{
		UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_MazeGlow.M_MazeGlow"));
		if (!Parent)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, Outer);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		return MID;
	}
}

namespace FlightModels
{
	const FFlightMesh& RingMesh()
	{
		static FFlightMesh M = []
		{
			FFlightMesh R;
			// Four slim arcs leave breathing room around the actual pass aperture.
			constexpr int32 Segments = 16, Sides = 6;
			for (int32 Q = 0; Q < 4; ++Q)
			{
				const int32 First = R.Verts.Num();
				for (int32 I = 0; I <= Segments; ++I)
				{
					const double A = FMath::DegreesToRadians(Q * 90.0 + 13.0 + I * 64.0 / Segments);
					for (int32 J = 0; J < Sides; ++J)
					{
						const double B = J * 2 * PI / Sides, Radius = 440 + 8.5 * FMath::Sin(B);
						const FVector N(FMath::Cos(B),FMath::Sin(B)*FMath::Cos(A),FMath::Sin(B)*FMath::Sin(A));
						R.AddVert(FVector(8.5*FMath::Cos(B),Radius*FMath::Cos(A),Radius*FMath::Sin(A)),N,FVector2D(I/double(Segments),J/double(Sides)));
					}
				}
				for (int32 I = 0; I < Segments; ++I)
				{
					for (int32 J = 0; J < Sides; ++J)
					{
						const int32 A=First+I*Sides+J,B=First+I*Sides+(J+1)%Sides;
						const FVector Hint=R.Normals[A]+R.Normals[B];
						R.AddTri(A,B,B+Sides,Hint); R.AddTri(A,B+Sides,A+Sides,Hint);
					}
				}
				const double Angle = Q*PI/2;
				R.AddBox(FTransform(FRotator(0,0,Q*90),FVector(0,472*FMath::Cos(Angle),472*FMath::Sin(Angle))),FVector(3,5,16));
			}
			return R;
		}();
		return M;
	}
}
