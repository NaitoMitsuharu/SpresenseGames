#include "FlightCourse.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "FlightModels.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace
{
	constexpr double CanyonStart = 250000.0;
	constexpr double BaseStart = 470000.0;
	constexpr double ArenaStart = 650000.0;
	constexpr double LoopRadius = 50000.0;

	// Continuous, deterministic erosion at several scales, shared at chunk seams.
	double Erosion(double S, double T)
	{
		return FMath::Sin(S * 0.00031 + T * 1.7) * 0.48
			+ FMath::Sin(S * 0.00083 - T * 3.1) * 0.27
			+ FMath::Sin(S * 0.00213 + T * 7.3) * 0.16
			+ FMath::Sin(S * 0.0057 - T * 11.7) * 0.09;
	}

	FLinearColor StoneColor(const FVector& P)
	{
		const double Layer = 0.5 + 0.5 * FMath::Sin(P.Z * 0.012 + FMath::Sin(P.X * 0.0009) * 0.8);
		const float Wet = FMath::SmoothStep(30.0, 280.0, P.Z);
		return FMath::Lerp(FLinearColor(0.12f, 0.14f, 0.13f),
			FMath::Lerp(FLinearColor(0.34f, 0.25f, 0.17f), FLinearColor(0.56f, 0.43f, 0.29f), Layer), Wet);
	}

	UMaterialInterface* RockMaterial(AActor* Owner)
	{
		if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Flight/M_FlightRock.M_FlightRock"))) { return M; }
		return FlightMaterials::Surface(Owner, FLinearColor::White, 0.f, 0.93f);
	}

	void UploadScenery(AActor* Owner, USceneComponent* Root, FFlightMesh& Mesh, UMaterialInterface* Mat, float CullDistance)
	{
		if (Mesh.Verts.IsEmpty()) { return; }
		UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(Owner);
		C->SetupAttachment(Root);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(true);
		C->SetCullDistance(CullDistance);
		C->RegisterComponent();
		Owner->AddInstanceComponent(C);
		Mesh.RecomputeSmoothNormals(0);
		Mesh.Upload(C, 0, Mat);
	}

	// An eroded island, sea stack or obstacle, with an irregular shoulder and submerged foot.
	void AddOutcrop(FFlightMesh& Mesh, const FVector& Center, double RX, double RY, double Height, double Seed)
	{
		constexpr int32 Sides = 40;
		constexpr int32 Levels = 18;
		const int32 First = Mesh.Verts.Num();
		for (int32 J = 0; J <= Levels; ++J)
		{
			const double T = J / double(Levels);
			const double Radius = FMath::Pow(1.0 - T, 0.46) * (1.0 - 0.18 * FMath::Sin(T * PI));
			for (int32 I = 0; I < Sides; ++I)
			{
				const double A = I * 2.0 * PI / Sides;
				const double Lobes = 1.0 + 0.19 * FMath::Sin(A * 3.0 + Seed) + 0.11 * FMath::Sin(A * 7.0 - Seed + T * 2.0);
				const double Terrace = 1.0 + 0.04 * FMath::Sin(T * 47.0 + A * 3.0);
				const FVector P = Center + FVector(RX * Radius * Lobes * Terrace * FMath::Cos(A) + T * FMath::Min(Height * 0.13, RX * 0.2),
					RY * Radius * Lobes * Terrace * FMath::Sin(A), Height * T - 240.0);
				Mesh.AddVert(P, FVector::UpVector, FVector2D(A, T), StoneColor(P));
			}
		}
		for (int32 J = 0; J < Levels; ++J)
		{
			for (int32 I = 0; I < Sides; ++I)
			{
				const int32 A = First + J * Sides + I, B = First + J * Sides + (I + 1) % Sides;
				const FVector Out = (Mesh.Verts[A] - Center) * FVector(1, 1, 0) + FVector(0, 0, Height * 0.3);
				Mesh.AddTri(A, B, B + Sides, Out);
				Mesh.AddTri(A, B + Sides, A + Sides, Out);
			}
		}
	}

	void AddShore(FFlightMesh& M, const FVector& Center, double RX, double RY, double Height, double Seed)
	{
		const int32 First = M.Verts.Num();
		const double T = FMath::Clamp(240.0 / Height, 0.0, 1.0);
		const double Radius = FMath::Pow(1 - T, 0.46) * (1 - 0.18 * FMath::Sin(T * PI));
		for (int32 I = 0; I <= 80; ++I)
		{
			const double A = I * 2 * PI / 80;
			const double Lobes = 1 + 0.19 * FMath::Sin(A * 3 + Seed) + 0.11 * FMath::Sin(A * 7 - Seed + T * 2);
			const double Terrace = 1 + 0.04 * FMath::Sin(T * 47 + A * 3);
			for (int32 J = 0; J < 2; ++J)
			{
				FVector P = Center + FVector((RX * Radius * Lobes * Terrace + 70 + J * 260) * FMath::Cos(A) + T * FMath::Min(Height * 0.13, RX * 0.2),
					(RY * Radius * Lobes * Terrace + 70 + J * 260) * FMath::Sin(A), 14);
				M.AddVert(P, FVector::UpVector, FVector2D(I / 80.0, J));
			}
		}
		for (int32 I = 0; I < 80; ++I)
		{
			const int32 A = First + I * 2;
			M.AddTri(A, A + 1, A + 3, FVector::UpVector);
			M.AddTri(A, A + 3, A + 2, FVector::UpVector);
		}
	}
}

void AFlightCourse::BuildScenery()
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UMaterialInterface* Stone = RockMaterial(this);
	BaseHull->SetStaticMesh(Cylinder);
	BaseHull->SetMaterial(0, FlightMaterials::Structure(this, FLinearColor(0.12f, 0.16f, 0.18f), 0.65f, 0.48f));
	BaseDecks->SetStaticMesh(Cube);
	BaseDecks->SetMaterial(0, FlightMaterials::Structure(this, FLinearColor(0.31f, 0.34f, 0.33f), 0.35f, 0.65f));
	BaseLights->SetStaticMesh(Cube);
	BaseLights->SetMaterial(0, FlightMaterials::Glow(this, FLinearColor(0.28f, 0.63f, 0.72f) * 2.f));
	Clouds->SetStaticMesh(Plane);
	Clouds->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Flight/M_FlightCloud.M_FlightCloud")));
	Whitecaps->SetStaticMesh(Plane);
	Whitecaps->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Flight/M_FlightFoam.M_FlightFoam")));
	BaseHull->SetCullDistances(55000, 65000);
	BaseDecks->SetCullDistances(55000, 65000);
	BaseLights->SetCullDistances(35000, 45000);
	Clouds->SetCullDistances(90000, 100000);

	FRandomStream Rng(20261104);
	UMaterialInterface* ShoreMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Flight/M_FlightShoreFoam.M_FlightShoreFoam"));
	// Larger landforms make an archipelago, rather than a scatter of primitives.
	for (double S = -12000; S < CanyonStart - 10000; S += 10500)
	{
		FFlightMesh Islands;
		FFlightMesh Surf;
		const FPathFrame Fr = Path.Frame(S);
		for (double Side : { -1.0, 1.0 })
		{
			// Reserve authored sites so random islands do not bury buildings.
			if ((Side > 0 && FMath::Abs(S - 62000) < 18000) || (Side < 0 && FMath::Abs(S - 142000) < 18000)) { continue; }
			const double Offset = Side * Rng.FRandRange(10500, 23000);
			FVector P = Fr.P + Fr.R * Offset;
			P.Z = 0;
			const double RX = Rng.FRandRange(2800, 6500), RY = Rng.FRandRange(2400, 5000), Height = Rng.FRandRange(1800, 4200);
			AddOutcrop(Islands, P, RX, RY, Height, S * 0.01);
			AddShore(Surf, P, RX, RY, Height, S * 0.01);
			// Smaller rocks share the same geological family and cluster at the shore.
			P += Fr.F * 3600 + Fr.R * (Side * -1800);
			AddOutcrop(Islands, P, 1200, 1600, Rng.FRandRange(700, 1600), S * 0.03);
		}
		UploadScenery(this, Root, Islands, Stone, 100000.f);
		UploadScenery(this, Root, Surf, ShoreMaterial, 55000.f);
	}

	// One connected height field per bank. Cross-sections run from the submerged
	// channel through a steep weathered face to a broad, uneven plateau.
	const double BankX[] = { 2100, 2450, 2750, 2920, 3100, 3250, 3400, 3550, 3750, 4000, 4600, 5400, 6800, 8500, 11000, 15000, 22000, 32000 };
	const double BankZ[] = { -180, -60, 160, 580, 1200, 1950, 2750, 3450, 4050, 4350, 4650, 4850, 5200, 5800, 6200, 5600, 4100, -400 };
	constexpr int32 Width = UE_ARRAY_COUNT(BankX);
	constexpr double Begin = CanyonStart - 14000, End = BaseStart + 10000;
	constexpr int32 Rows = 40;
	constexpr double Chunk = 12000;
	for (double S0 = Begin; S0 < End; S0 += Chunk)
	{
		for (double Side : { -1.0, 1.0 })
		{
			FFlightMesh Bank;
			for (int32 J = 0; J <= Rows; ++J)
			{
				const double S = FMath::Min(S0 + J * Chunk / Rows, End);
				const FPathFrame Fr = Path.Frame(S);
				const double Fade = FMath::SmoothStep(Begin, Begin + 18000, S) * (1.0 - FMath::SmoothStep(End - 24000, End, S));
				for (int32 I = 0; I < Width; ++I)
				{
					const double E = Erosion(S, Side * 8.0 + I * 0.16);
					// Never intrude into the playable channel (including at bends).
					const double X = Side * (BankX[I] + 260 * E + 170 * FMath::Sin(S * 0.0012 + I * 0.65));
					FVector P = Fr.P + Fr.R * X;
					P.Z = (BankZ[I] + (I > 2 ? 750 * E : 0)) * Fade - 60;
					Bank.AddVert(P, FVector::UpVector, FVector2D(S / 1800, I / 4.0), StoneColor(P));
				}
			}
			for (int32 J = 0; J < Rows; ++J)
			{
				const FPathFrame Fr = Path.Frame(S0 + (J + 0.5) * Chunk / Rows);
				for (int32 I = 0; I < Width - 1; ++I)
				{
					const int32 A = J * Width + I;
					const FVector Hint = FVector::UpVector - Fr.R * Side;
					Bank.AddTri(A, A + Width, A + Width + 1, Hint);
					Bank.AddTri(A, A + Width + 1, A + 1, Hint);
				}
			}
			UploadScenery(this, Root, Bank, Stone, 95000.f);
		}
	}

	// Preserve the authored collision cylinders, with rounded eroded silhouettes.
	const double PillarS[] = { 300000, 316000, 334000, 352000, 395000, 411000, 446000 };
	const float PillarX[] = { 600, -650, 0, 700, -500, 450, -700 };
	for (int32 I = 0; I < UE_ARRAY_COUNT(PillarS); ++I)
	{
		Hazards.Add({ PillarS[I], PillarX[I], 270.f });
		const FPathFrame Fr = Path.Frame(PillarS[I]);
		FVector P = Fr.P + Fr.R * PillarX[I]; P.Z = 0;
		FFlightMesh Stack;
		AddOutcrop(Stack, P, 180, 175, 3200, I * 3.7);
		UploadScenery(this, Root, Stack, Stone, 65000.f);
	}

	auto Place = [](UInstancedStaticMeshComponent* ISM, const FPathFrame& Fr, const FVector& P, const FVector& Size, const FRotator& Rot = FRotator::ZeroRotator)
	{
		ISM->AddInstance(FTransform(Fr.Rotation() * Rot.Quaternion(), Fr.ToWorld(P.X, P.Y, P.Z), Size / 100.f), true);
	};
	auto Beam = [&](const FPathFrame& Fr, const FVector& A, const FVector& B, float Thickness)
	{
		Place(BaseDecks, Fr, (A + B) * 0.5, FVector((B - A).Size(), Thickness, Thickness), (B - A).Rotation());
	};
	auto Facility = [&](const FPathFrame& Fr, double Side, double Z, float Length)
	{
		const double X = Side * 8000;
		// Service deck, load-bearing spine, cross frames and engine pods.
		Place(BaseDecks, Fr, FVector(0, X, Z), FVector(Length, 3800, 180));
		Place(BaseDecks, Fr, FVector(0, X, Z - 500), FVector(Length * 0.95, 650, 800));
		for (int32 J = -2; J <= 2; ++J)
		{
			const double DX = J * Length / 5.5;
			Beam(Fr, FVector(DX, X - 1750, Z - 100), FVector(DX, X, Z - 1100), 110);
			Beam(Fr, FVector(DX, X + 1750, Z - 100), FVector(DX, X, Z - 1100), 110);
			for (double Edge : { -1.0, 1.0 })
			{
				Place(BaseHull, Fr, FVector(DX, X + Edge * 1300, Z - 620), FVector(650, 650, 850));
				Place(BaseLights, Fr, FVector(DX, X + Edge * 1300, Z - 1060), FVector(400, 400, 24));
			}
		}
		// Hangars with stepped roof, dark door recesses, vents and service blocks.
		for (int32 J = -1; J <= 1; ++J)
		{
			const double DX = J * Length * 0.28;
			Place(BaseDecks, Fr, FVector(DX, X + Side * 580, Z + 650), FVector(Length * 0.21, 1800, 1200));
			Place(BaseDecks, Fr, FVector(DX, X + Side * 580, Z + 1310), FVector(Length * 0.23, 1950, 140));
			Place(BaseHull, Fr, FVector(DX, X - Side * 340, Z + 580), FVector(700, 45, 900), FRotator(90, 0, 0));
			for (int32 K = -2; K <= 2; ++K)
			{
				Place(BaseDecks, Fr, FVector(DX + K * 180, X + Side * 580, Z + 1450), FVector(70, 1000, 120));
			}
			Place(BaseLights, Fr, FVector(DX, X - Side * 450, Z + 1140), FVector(800, 30, 35));
		}
		// Safety rails, stanchions and inset landing guidance.
		for (double Edge : { -1.0, 1.0 })
		{
			Place(BaseDecks, Fr, FVector(0, X + Edge * 1860, Z + 220), FVector(Length, 35, 35));
			for (int32 J = -5; J <= 5; ++J)
			{
				Place(BaseDecks, Fr, FVector(J * Length / 11.0, X + Edge * 1860, Z + 150), FVector(30, 30, 300));
				Place(BaseLights, Fr, FVector(J * Length / 11.0, X - Side * 1250, Z + 98), FVector(180, 35, 12));
			}
		}
		// Control tower: tapered silhouette, observation band and antenna.
		Place(BaseDecks, Fr, FVector(Length * 0.34, X + Side * 1100, Z + 2100), FVector(500, 550, 1700));
		Place(BaseDecks, Fr, FVector(Length * 0.34, X + Side * 1100, Z + 3000), FVector(950, 950, 240));
		Place(BaseLights, Fr, FVector(Length * 0.34, X + Side * 1100, Z + 3150), FVector(880, 880, 60));
		Place(BaseDecks, Fr, FVector(Length * 0.34, X + Side * 1100, Z + 3300), FVector(1050, 1050, 170));
		Place(BaseHull, Fr, FVector(Length * 0.34, X + Side * 1100, Z + 3950), FVector(40, 40, 1200));
	};
	for (double S = BaseStart + 9000; S < ArenaStart; S += 15500)
	{
		const FPathFrame Fr = Path.Frame(S);
		if (FMath::Abs(S - 608000) > 14000) { Facility(Fr, -1, -2300, 8500); }
		if (FMath::Abs(S - 562000) > 14000) { Facility(Fr, 1, -2600, 10500); }
		// Overhead service gantry; all beams stay clear of the flight box.
		for (double Side : { -1.0, 1.0 })
		{
			Beam(Fr, FVector(0, Side * 3300, -2400), FVector(0, Side * 3300, 2450), 180);
			Beam(Fr, FVector(0, Side * 3300, 2450), FVector(0, Side * 2200, 2450), 180);
			Beam(Fr, FVector(0, Side * 3300, 1450), FVector(0, Side * 2300, 2450), 100);
			Place(BaseLights, Fr, FVector(-100, Side * 3280, 0), FVector(35, 60, 1900));
		}
		Beam(Fr, FVector(0, -3300, 2450), FVector(0, 3300, 2450), 180);
	}
	for (int32 I = 0; I < 14; ++I)
	{
		const FPathFrame Fr = Path.Frame(ArenaStart + I * 2.0 * PI * LoopRadius / 14);
		Facility(Fr, -1, -3100, 11000);
	}

	// Soft translucent cloud patches; no opaque sphere surfaces or circular rims.
	for (double S = BaseStart - 5000; S < ArenaStart + 2.0 * PI * LoopRadius; S += 9500)
	{
		const FPathFrame Fr = Path.Frame(S);
		for (int32 K = 0; K < 3; ++K)
		{
			FVector P = Fr.P + Fr.R * ((K - 1) * 15000 + Rng.FRandRange(-4000, 4000));
			P.Z = Rng.FRandRange(1800, 2800);
			Clouds->AddInstance(FTransform(FRotator(0, Rng.FRandRange(0, 360), 0), P,
				FVector(Rng.FRandRange(260, 400), Rng.FRandRange(220, 330), 1)), true);
		}
	}
	for (int32 I = 0; I < 28 * 28; ++I)
	{
		Whitecaps->AddInstance(FTransform(FVector(0, 0, -1.e6)), true);
	}
	BuildLandmarks();
}
