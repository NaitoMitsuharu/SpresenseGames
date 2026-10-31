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

// ---------------------------------------------------------------------------
// Player ship
// ---------------------------------------------------------------------------

namespace
{
	TArray<FVector> SuperEllipse(float X, float Width, float Height, float ZC, int32 N, float Exponent)
	{
		TArray<FVector> Ring;
		for (int32 k = 0; k < N; ++k)
		{
			const float A = 2.f * PI * k / N;
			const float C = FMath::Cos(A), S = FMath::Sin(A);
			const float Y = 0.5f * Width * FMath::Sign(C) * FMath::Pow(FMath::Abs(C), 2.f / Exponent);
			const float Z = ZC + 0.5f * Height * FMath::Sign(S) * FMath::Pow(FMath::Abs(S), 2.f / Exponent);
			Ring.Add(FVector(X, Y, Z));
		}
		return Ring;
	}

	/** Two-tone livery: graphite belly, pearl upper surfaces, decided per vertex. */
	void ApplyLivery(FFlightMesh& M, int32 FirstVert, float SplitZ)
	{
		const FLinearColor Pearl(0.82f, 0.85f, 0.88f);
		const FLinearColor Graphite(0.06f, 0.07f, 0.085f);
		for (int32 i = FirstVert; i < M.Verts.Num(); ++i)
		{
			const float T = FMath::SmoothStep(SplitZ - 6.f, SplitZ + 6.f, static_cast<float>(M.Verts[i].Z));
			M.Colors[i] = FMath::Lerp(Graphite, Pearl, T);
		}
	}

	UProceduralMeshComponent* NewPart(AActor* Owner, USceneComponent* Parent, const TCHAR* Name, const FVector& Pivot)
	{
		UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(Owner, Name);
		PMC->SetupAttachment(Parent);
		PMC->SetRelativeLocation(Pivot);
		PMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PMC->RegisterComponent();
		Owner->AddInstanceComponent(PMC);
		return PMC;
	}

	FVector Mirror(const FVector& V) { return FVector(V.X, -V.Y, V.Z); }
}

namespace FlightModels
{
	void BuildShip(AActor* Owner, USceneComponent* Parent, FFlightShipParts& Out)
	{
		FFlightMesh Paint, Metal, Glass, Glow, Plume;

		// Fuselage: superellipse sections, boxy enough to read as armoured.
		struct FStation { float X, W, H, ZC; };
		const FStation Stations[] = {
			{ -345.f, 60.f, 52.f, 6.f }, { -322.f, 80.f, 66.f, 6.f }, { -250.f, 104.f, 80.f, 4.f },
			{ -150.f, 118.f, 88.f, 2.f }, { -40.f, 116.f, 92.f, 4.f }, { 60.f, 104.f, 92.f, 8.f },
			{ 160.f, 82.f, 76.f, 6.f }, { 250.f, 56.f, 54.f, 2.f }, { 320.f, 32.f, 32.f, -2.f },
			{ 362.f, 12.f, 12.f, -4.f }, { 378.f, 2.f, 2.f, -5.f } };
		{
			TArray<TArray<FVector>> Rings;
			for (const FStation& St : Stations)
			{
				Rings.Add(SuperEllipse(St.X, St.W, St.H, St.ZC, 28, 2.5f));
			}
			const int32 First = Paint.Verts.Num();
			Paint.AddLoft(Rings, true, true);
			ApplyLivery(Paint, First, -4.f);
		}

		// Swept main wings with slight anhedral, two-tone like the body.
		const FVector Up = FVector::UpVector;
		{
			const int32 First = Paint.Verts.Num();
			const FVector RootLE(40.f, 50.f, -6.f), RootTE(-200.f, 50.f, -10.f);
			const FVector TipLE(-185.f, 410.f, -22.f), TipTE(-262.f, 410.f, -24.f);
			Paint.AddWing(RootLE, RootTE, TipLE, TipTE, 16.f, 5.f, Up);
			Paint.AddWing(Mirror(RootLE), Mirror(RootTE), Mirror(TipLE), Mirror(TipTE), 16.f, 5.f, Up);
			ApplyLivery(Paint, First, -14.f);
		}

		// Canted twin fins.
		{
			const float Cant = FMath::DegreesToRadians(20.f);
			const FVector Span(0.f, FMath::Sin(Cant), FMath::Cos(Cant));
			const FVector Normal = (FVector::ForwardVector ^ Span).GetSafeNormal();
			const FVector RootLE(-212.f, 32.f, 36.f), RootTE(-338.f, 32.f, 32.f);
			const FVector TipLE = FVector(-300.f, 0.f, 0.f) + FVector(0.f, 32.f, 34.f) + Span * 130.f;
			const FVector TipTE = FVector(-352.f, 0.f, 0.f) + FVector(0.f, 32.f, 32.f) + Span * 126.f;
			const int32 First = Paint.Verts.Num();
			Paint.AddWing(RootLE, RootTE, TipLE, TipTE, 11.f, 4.f, Normal);
			Paint.AddWing(Mirror(RootLE), Mirror(RootTE), Mirror(TipLE), Mirror(TipTE), 11.f, 4.f, Mirror(Normal));
			for (int32 i = First; i < Paint.Verts.Num(); ++i)
			{
				// Fin tips carry the amber accent of "things you act on".
				const float T = FMath::SmoothStep(120.f, 150.f, static_cast<float>(Paint.Verts[i].Z));
				Paint.Colors[i] = FMath::Lerp(FLinearColor(0.11f, 0.12f, 0.14f), FLinearColor(0.95f, 0.45f, 0.06f), T);
			}
		}

		// Twin engine nacelles, intakes and nozzles.
		const float EngineY = 62.f, EngineZ = -30.f;
		for (const float Side : { -1.f, 1.f })
		{
			const FVector Off(0.f, EngineY * Side, EngineZ);
			Metal.AddTube({ { -372.f, 25.f }, { -352.f, 31.f }, { -300.f, 36.f }, { -150.f, 38.f },
				{ -40.f, 36.f }, { 10.f, 31.f }, { 16.f, 27.f } }, Off, 20, true, true);
			// Nozzle petals ring
			Metal.AddTube({ { -386.f, 23.f }, { -372.f, 26.f } }, Off, 20, false, false);
			Glow.AddTube({ { -384.f, 21.f }, { -378.f, 21.f } }, Off, 20, true, true);
			Plume.AddTube({ { -386.f, 20.f }, { -470.f, 12.f }, { -580.f, 1.f } }, Off, 16, true, false);
			// Intake shadow disc
			Metal.AddTube({ { 16.f, 24.f }, { 18.f, 22.f } }, Off, 20, false, true, FLinearColor(0.15f, 0.15f, 0.15f));
			Out.Exhaust[Side < 0.f ? 0 : 1] = FVector(-390.f, EngineY * Side, EngineZ);
		}

		// Wing-root cannons.
		for (const float Side : { -1.f, 1.f })
		{
			const FVector Off(0.f, 108.f * Side, -10.f);
			Metal.AddTube({ { 20.f, 9.f }, { 60.f, 8.f }, { 124.f, 5.f }, { 128.f, 4.f } }, Off, 12, true, true);
			Glow.AddTube({ { 128.f, 3.f }, { 131.f, 3.f } }, Off, 10, false, true);
		}
		Out.GunL = FVector(134.f, -108.f, -10.f);
		Out.GunR = FVector(134.f, 108.f, -10.f);

		// Canopy and its frame.
		Glass.AddEllipsoid(FVector(118.f, 0.f, 34.f), FVector(108.f, 31.f, 32.f), 28, 10, true);
		Metal.AddBox(FTransform(FRotator(-14.f, 0.f, 0.f), FVector(54.f, 0.f, 58.f)), FVector(3.f, 26.f, 2.f));

		// Lights: wingtip pods and side stripes in the cyan "things that move you" colour.
		for (const float Side : { -1.f, 1.f })
		{
			Metal.AddEllipsoid(FVector(-222.f, 412.f * Side, -23.f), FVector(48.f, 7.f, 7.f), 12, 6);
			Glow.AddEllipsoid(FVector(-176.f, 412.f * Side, -23.f), FVector(8.f, 6.f, 6.f), 10, 5);
			Glow.AddBox(FTransform(FVector(-60.f, 59.5f * Side, 6.f)), FVector(95.f, 0.8f, 1.6f));
		}
		Out.WingTipL = FVector(-260.f, -412.f, -23.f);
		Out.WingTipR = FVector(-260.f, 412.f, -23.f);

		Out.Body = NewPart(Owner, Parent, TEXT("ShipBody"), FVector::ZeroVector);
		Out.Body->SetCastShadow(true);
		Paint.Upload(Out.Body, 0, FlightMaterials::Surface(Owner, FLinearColor::White, 0.35f, 0.3f, 0.6f));
		Metal.Upload(Out.Body, 1, FlightMaterials::Surface(Owner, FLinearColor(0.16f, 0.17f, 0.19f), 0.9f, 0.26f));
		Glass.Upload(Out.Body, 2, FlightMaterials::Surface(Owner, FLinearColor(0.01f, 0.05f, 0.07f), 0.2f, 0.03f, 1.f,
			FLinearColor(0.0f, 0.05f, 0.07f)));
		Out.TrimGlow = FlightMaterials::Glow(Owner, FLinearColor(0.25f, 0.85f, 1.f) * 6.f);
		Glow.Upload(Out.Body, 3, Out.TrimGlow);
		Out.PlumeGlow = FlightMaterials::Additive(Owner, FLinearColor(0.3f, 0.7f, 1.f) * 1.5f);
		Plume.Upload(Out.Body, 4, Out.PlumeGlow);
		Out.Body->SetCastShadow(true);

		// Movable surfaces, each pivoting on its hinge line.
		UMaterialInterface* SurfaceMat = Out.Body->GetMaterial(0);
		for (const float Side : { -1.f, 1.f })
		{
			FFlightMesh Aileron;
			Aileron.AddWing(FVector(0.f, -78.f, 0.f), FVector(-52.f, -78.f, -1.f),
				FVector(0.f, 78.f, -4.f), FVector(-44.f, 78.f, -5.f), 5.f, 3.f, Up);
			ApplyLivery(Aileron, 0, -60.f);
			const FVector Hinge(-246.f, 310.f * Side, -20.f);
			UProceduralMeshComponent* A = NewPart(Owner, Parent, Side < 0.f ? TEXT("AileronL") : TEXT("AileronR"), Hinge);
			A->SetRelativeRotation(FRotator(0.f, Side * -10.f, 0.f));
			Aileron.Upload(A, 0, SurfaceMat);
			(Side < 0.f ? Out.AileronL : Out.AileronR) = A;

			FFlightMesh Elevator;
			Elevator.AddWing(FVector(42.f, 0.f, 0.f) , FVector(-52.f, 0.f, 0.f),
				FVector(-6.f, 122.f * Side, -6.f), FVector(-58.f, 122.f * Side, -6.f), 9.f, 4.f, Up);
			ApplyLivery(Elevator, 0, -2.f);
			UProceduralMeshComponent* E = NewPart(Owner, Parent, Side < 0.f ? TEXT("ElevatorL") : TEXT("ElevatorR"),
				FVector(-300.f, 52.f * Side, 4.f));
			Elevator.Upload(E, 0, SurfaceMat);
			(Side < 0.f ? Out.ElevatorL : Out.ElevatorR) = E;
		}
		Out.EngineGlow = Out.TrimGlow;
		Out.Root = Parent;
	}

	// -----------------------------------------------------------------------
	// Enemies (authored facing +X; the course turns them toward the player)
	// -----------------------------------------------------------------------

	const FFlightMesh& DroneHull()
	{
		static FFlightMesh M = []
		{
			FFlightMesh R;
			R.AddEllipsoid(FVector::ZeroVector, FVector(110.f, 82.f, 40.f), 24, 10);
			for (const float Side : { -1.f, 1.f })
			{
				R.AddWing(FVector(40.f, 66.f * Side, 0.f), FVector(-50.f, 66.f * Side, 0.f),
					FVector(92.f, 215.f * Side, 26.f), FVector(48.f, 224.f * Side, 30.f), 14.f, 4.f, FVector::UpVector);
			}
			R.AddTube({ { -96.f, 24.f }, { -150.f, 12.f }, { -190.f, 1.f } }, FVector::ZeroVector, 12, false, false);
			return R;
		}();
		return M;
	}

	const FFlightMesh& DroneGlow()
	{
		static FFlightMesh M = []
		{
			FFlightMesh R;
			R.AddEllipsoid(FVector(92.f, 0.f, 10.f), FVector(22.f, 32.f, 12.f), 14, 6);
			for (const float Side : { -1.f, 1.f })
			{
				R.AddEllipsoid(FVector(70.f, 220.f * Side, 28.f), FVector(10.f, 10.f, 10.f), 8, 4);
			}
			return R;
		}();
		return M;
	}

	const FFlightMesh& TurretHull()
	{
		static FFlightMesh M = []
		{
			FFlightMesh R;
			FFlightMesh Base;
			Base.AddTube({ { 0.f, 170.f }, { 40.f, 160.f }, { 70.f, 120.f } }, FVector::ZeroVector, 6, true, true);
			R.Append(Base, FTransform(FRotator(90.f, 0.f, 0.f), FVector(0.f, 0.f, -60.f)));
			R.AddEllipsoid(FVector(0.f, 0.f, 10.f), FVector(115.f, 115.f, 80.f), 20, 8, true);
			for (const float Side : { -1.f, 1.f })
			{
				R.AddTube({ { 40.f, 16.f }, { 200.f, 12.f }, { 240.f, 14.f } }, FVector(0.f, 36.f * Side, 50.f), 10, true, true);
			}
			return R;
		}();
		return M;
	}

	const FFlightMesh& TurretGlow()
	{
		static FFlightMesh M = []
		{
			FFlightMesh R;
			R.AddEllipsoid(FVector(96.f, 0.f, 48.f), FVector(14.f, 24.f, 14.f), 12, 5);
			for (const float Side : { -1.f, 1.f })
			{
				R.AddTube({ { 240.f, 9.f }, { 244.f, 9.f } }, FVector(0.f, 36.f * Side, 50.f), 10, false, true);
			}
			return R;
		}();
		return M;
	}

	const FFlightMesh& BossHull()
	{
		static FFlightMesh M = []
		{
			FFlightMesh R;
			TArray<TArray<FVector>> Rings;
			struct FSt { float X, Y, Z, W; };
			const FSt St[] = { { -760.f, 260.f, 120.f, 20.f }, { -620.f, 900.f, 220.f, 10.f },
				{ -300.f, 1500.f, 300.f, 0.f }, { 100.f, 1300.f, 320.f, 10.f }, { 420.f, 760.f, 260.f, 20.f },
				{ 640.f, 300.f, 160.f, 10.f }, { 720.f, 40.f, 30.f, 0.f } };
			for (const FSt& S : St)
			{
				Rings.Add(SuperEllipse(S.X, S.Y, S.Z, S.W, 32, 2.2f));
			}
			R.AddLoft(Rings, true, true);
			for (const float Side : { -1.f, 1.f })
			{
				R.AddTube({ { -500.f, 60.f }, { -400.f, 110.f }, { 200.f, 110.f }, { 380.f, 70.f }, { 420.f, 40.f } },
					FVector(0.f, 820.f * Side, -40.f), 18, true, true);
			}
			return R;
		}();
		return M;
	}

	const FFlightMesh& BossArmor()
	{
		static FFlightMesh M = []
		{
			FFlightMesh R;
			for (int32 i = 0; i < 4; ++i)
			{
				const float X = -420.f + i * 230.f;
				R.AddBox(FTransform(FRotator(-6.f, 0.f, 0.f), FVector(X, 0.f, 170.f)), FVector(90.f, 260.f - i * 30.f, 22.f));
			}
			for (const float Side : { -1.f, 1.f })
			{
				R.AddWing(FVector(-80.f, 700.f * Side, -10.f), FVector(-520.f, 700.f * Side, -10.f),
					FVector(-420.f, 1250.f * Side, 60.f), FVector(-640.f, 1260.f * Side, 70.f), 50.f, 16.f, FVector::UpVector);
			}
			return R;
		}();
		return M;
	}

	const FFlightMesh& BossGlow()
	{
		static FFlightMesh M = []
		{
			FFlightMesh R;
			R.AddEllipsoid(FVector(560.f, 0.f, 40.f), FVector(110.f, 170.f, 80.f), 20, 8);
			for (const float Side : { -1.f, 1.f })
			{
				R.AddTube({ { 420.f, 34.f }, { 430.f, 34.f } }, FVector(0.f, 820.f * Side, -40.f), 14, false, true);
				R.AddBox(FTransform(FVector(-100.f, 560.f * Side, 150.f)), FVector(300.f, 6.f, 6.f));
			}
			return R;
		}();
		return M;
	}

	const FFlightMesh& RingMesh()
	{
		static FFlightMesh M = []
		{
			FFlightMesh R;
			R.AddTorus(FVector::ZeroVector, 440.f, 22.f, 56, 10);
			R.AddTorus(FVector::ZeroVector, 395.f, 5.f, 56, 6);
			return R;
		}();
		return M;
	}
}
