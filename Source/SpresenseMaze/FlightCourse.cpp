#include "FlightCourse.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "FlightModels.h"
#include "FlightPawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Section boundaries along the course (cm). The arena loops after BossS.
	constexpr double CanyonS = 250000.0;
	constexpr double SkyBaseS = 470000.0;
	constexpr double BossS = 650000.0;
	constexpr double ArenaRadius = 50000.0;

	struct FSegment
	{
		double Length;
		double TurnDeg;
		double EndZ;
	};

	// Sea: wide S-curves just above the water. Canyon: tighter turns between
	// cliffs. Sky base: climb above the cloud deck. Totals match the constants.
	const FSegment Segments[] = {
		{ 30000, 0, 1000 }, { 40000, 35, 1000 }, { 40000, -50, 1200 }, { 35000, 25, 1000 },
		{ 45000, -20, 1150 }, { 30000, 30, 1000 }, { 30000, 0, 1000 },
		{ 20000, 0, 1000 }, { 30000, 60, 1000 }, { 30000, -70, 1050 }, { 25000, 45, 1150 },
		{ 30000, -55, 1000 }, { 30000, 40, 1050 }, { 25000, -30, 1200 }, { 30000, 0, 2000 },
		{ 40000, 0, 6000 }, { 35000, -40, 9000 }, { 35000, 50, 9000 }, { 35000, -30, 9500 }, { 35000, 20, 9000 },
	};

	enum ECue : uint8
	{
		CueLaser = FlightCue::Laser, CueSmallBoom = FlightCue::SmallBoom, CueBigBoom = FlightCue::BigBoom,
		CueRing = FlightCue::Ring, CueHeal = FlightCue::Heal, CueDamage = FlightCue::Damage,
		CueWarning = FlightCue::Warning, CueEnemyShot = FlightCue::EnemyShot
	};

	UStaticMesh* EngineMesh(const TCHAR* Path)
	{
		return LoadObject<UStaticMesh>(nullptr, Path);
	}

	UInstancedStaticMeshComponent* MakeISM(AActor* Owner, USceneComponent* Root, const TCHAR* Name, bool bShadow)
	{
		UInstancedStaticMeshComponent* ISM = Owner->CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		ISM->SetupAttachment(Root);
		ISM->SetMobility(EComponentMobility::Movable);
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->SetCastShadow(bShadow);
		ISM->SetUsingAbsoluteLocation(true);
		ISM->SetUsingAbsoluteRotation(true);
		ISM->SetUsingAbsoluteScale(true);
		return ISM;
	}

	FTransform Hidden()
	{
		return FTransform(FQuat::Identity, FVector(0.f, 0.f, -1.e6f), FVector(0.001f));
	}
}

// ---------------------------------------------------------------------------
// Path
// ---------------------------------------------------------------------------

void FFlightPath::Build()
{
	Points.Reset();
	FVector P(0.0, 0.0, Segments[0].EndZ);
	double Heading = 0.0;
	Points.Add(P);
	for (const FSegment& Seg : Segments)
	{
		const int32 N = FMath::Max(1, FMath::RoundToInt(Seg.Length / Step));
		const double H0 = Heading;
		const double Z0 = P.Z;
		for (int32 k = 1; k <= N; ++k)
		{
			const double T = k / static_cast<double>(N);
			const double E = FMath::SmoothStep(0.0, 1.0, T);
			const double PrevE = FMath::SmoothStep(0.0, 1.0, (k - 0.5) / N);
			const double MidHeading = H0 + FMath::DegreesToRadians(Seg.TurnDeg) * PrevE;
			P.X += FMath::Cos(MidHeading) * Step;
			P.Y += FMath::Sin(MidHeading) * Step;
			P.Z = FMath::Lerp(Z0, Seg.EndZ, E);
			Points.Add(P);
		}
		Heading = H0 + FMath::DegreesToRadians(Seg.TurnDeg);
	}
	LoopStart = (Points.Num() - 1) * Step;
	ArenaHeading = Heading;
	ArenaZ = P.Z;
	ArenaRadius = ::ArenaRadius;
	// Turning right: the centre sits to the right of the final heading.
	ArenaCenter = P + FVector(-FMath::Sin(Heading), FMath::Cos(Heading), 0.0) * ArenaRadius;
	ArenaCenter.Z = ArenaZ;
}

FPathFrame FFlightPath::Frame(double S) const
{
	FPathFrame Out;
	if (Points.Num() < 3)
	{
		return Out;
	}
	if (S >= LoopStart)
	{
		const double Phi = FMath::Fmod(S - LoopStart, 2.0 * PI * ArenaRadius) / ArenaRadius;
		const double Theta = ArenaHeading + Phi;
		const FVector Right(-FMath::Sin(Theta), FMath::Cos(Theta), 0.0);
		Out.P = ArenaCenter - Right * ArenaRadius;
		Out.F = FVector(FMath::Cos(Theta), FMath::Sin(Theta), 0.0);
		Out.R = Right;
		Out.U = FVector::UpVector;
		return Out;
	}

	auto TangentAt = [this](int32 i)
	{
		const int32 A = FMath::Clamp(i - 1, 0, Points.Num() - 1);
		const int32 B = FMath::Clamp(i + 1, 0, Points.Num() - 1);
		return (Points[B] - Points[A]).GetSafeNormal();
	};
	if (S <= 0.0)
	{
		Out.F = TangentAt(0);
		Out.P = Points[0] + Out.F * S;
	}
	else
	{
		const double U = S / Step;
		const int32 I = FMath::Min(FMath::FloorToInt(U), Points.Num() - 2);
		const double T = U - I;
		Out.P = FMath::Lerp(Points[I], Points[I + 1], T);
		Out.F = FMath::Lerp(TangentAt(I), TangentAt(I + 1), T).GetSafeNormal();
	}
	Out.R = (FVector::UpVector ^ Out.F).GetSafeNormal();
	Out.U = Out.F ^ Out.R;
	return Out;
}

// ---------------------------------------------------------------------------
// Actor
// ---------------------------------------------------------------------------

AFlightCourse::AFlightCourse()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Sea = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Sea"));
	Sea->SetupAttachment(Root);
	Sea->SetUsingAbsoluteLocation(true);
	Sea->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sea->SetCastShadow(false);
	Sea->SetMobility(EComponentMobility::Movable);

	Rocks = MakeISM(this, Root, TEXT("Rocks"), true);
	RockSpires = MakeISM(this, Root, TEXT("RockSpires"), true);
	Clouds = MakeISM(this, Root, TEXT("Clouds"), false);
	BaseHull = MakeISM(this, Root, TEXT("BaseHull"), true);
	BaseDecks = MakeISM(this, Root, TEXT("BaseDecks"), true);
	BaseLights = MakeISM(this, Root, TEXT("BaseLights"), false);
	Whitecaps = MakeISM(this, Root, TEXT("Whitecaps"), false);
	Bolts = MakeISM(this, Root, TEXT("Bolts"), false);
	Bullets = MakeISM(this, Root, TEXT("Bullets"), false);
	Debris = MakeISM(this, Root, TEXT("Debris"), false);
	Blasts = MakeISM(this, Root, TEXT("Blasts"), false);

	BlastLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("BlastLight"));
	BlastLight->SetupAttachment(Root);
	BlastLight->SetUsingAbsoluteLocation(true);
	BlastLight->SetIntensity(0.f);
	BlastLight->SetAttenuationRadius(3500.f);
	BlastLight->SetCastShadows(false);
	BlastLight->SetLightColor(FLinearColor(1.f, 0.55f, 0.2f));
}

EFlightSection AFlightCourse::SectionAt(double S) const
{
	if (S >= BossS) { return EFlightSection::Boss; }
	if (S >= SkyBaseS) { return EFlightSection::SkyBase; }
	if (S >= CanyonS) { return EFlightSection::Canyon; }
	return EFlightSection::Sea;
}

FString AFlightCourse::SectionName(EFlightSection Section)
{
	switch (Section)
	{
	case EFlightSection::Sea: return TEXT("海上");
	case EFlightSection::Canyon: return TEXT("峡谷");
	case EFlightSection::SkyBase: return TEXT("空中基地");
	default: return TEXT("大型機");
	}
}

void AFlightCourse::BeginPlay()
{
	Super::BeginPlay();
#if PLATFORM_ANDROID || PLATFORM_IOS
	MaxEnemies = 12;
	MaxShots = 32;
	MaxBullets = 44;
	MaxDebris = 96;
#endif
	Path.Build();
	Checkpoints = { 0.0, CanyonS, SkyBaseS, BossS - 8000.0 };
	BuildEnvironment();
	BuildScenery();
	BuildSchedule();
	BuildRings();
	BuildPools();
}

void FlightEnvironment::Build(AActor* Owner, USceneComponent* Root, bool bWithPlanet)
{
	// Alien sky: teal-violet Rayleigh, warm low sun, a hazy horizon.
	UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>(Owner, TEXT("Sun"));
	Sun->SetupAttachment(Root);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetWorldRotation(FRotator(-24.f, 35.f, 0.f));
	Sun->SetIntensity(7.5f);
	Sun->SetLightColor(FLinearColor(1.f, 0.86f, 0.74f));
	Sun->SetAtmosphereSunLight(true);
	Sun->SetCastShadows(true);
	Sun->SetDynamicShadowDistanceMovableLight(9000.f);
	Sun->SetDynamicShadowCascades(2);
	Sun->RegisterComponent();
	Owner->AddInstanceComponent(Sun);

	USkyAtmosphereComponent* Sky = NewObject<USkyAtmosphereComponent>(Owner, TEXT("Atmosphere"));
	Sky->SetupAttachment(Root);
	Sky->SetRayleighScattering(FLinearColor(0.32f, 0.5f, 0.92f));
	Sky->SetMieScatteringScale(0.012f);
	Sky->SetMieAnisotropy(0.8f);
	Sky->SetSkyLuminanceFactor(FLinearColor(1.15f, 1.f, 1.2f));
	Sky->SetAerialPespectiveViewDistanceScale(0.7f);
	Sky->RegisterComponent();
	Owner->AddInstanceComponent(Sky);

	USkyLightComponent* SkyLight = NewObject<USkyLightComponent>(Owner, TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SetRealTimeCapture(false);
	SkyLight->SetIntensity(1.2f);
	SkyLight->RegisterComponent();
	Owner->AddInstanceComponent(SkyLight);
	SkyLight->RecaptureSky();

	UExponentialHeightFogComponent* Fog = NewObject<UExponentialHeightFogComponent>(Owner, TEXT("Fog"));
	Fog->SetupAttachment(Root);
	Fog->SetWorldLocation(FVector(0.f, 0.f, 0.f));
	Fog->SetFogDensity(0.008f);
	Fog->SetFogHeightFalloff(0.05f);
	Fog->SetFogInscatteringColor(FLinearColor(0.16f, 0.32f, 0.42f));
	Fog->SetDirectionalInscatteringExponent(6.f);
	Fog->SetDirectionalInscatteringColor(FLinearColor(0.55f, 0.36f, 0.22f));
	Fog->SetStartDistance(4000.f);
	Fog->RegisterComponent();
	Owner->AddInstanceComponent(Fog);

	UPostProcessComponent* PP = NewObject<UPostProcessComponent>(Owner, TEXT("Post"));
	PP->SetupAttachment(Root);
	PP->bUnbound = true;
	FPostProcessSettings& PS = PP->Settings;
	PS.bOverride_AutoExposureMethod = true;
	PS.AutoExposureMethod = AEM_Manual;
	PS.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PS.AutoExposureApplyPhysicalCameraExposure = false;
	PS.bOverride_AutoExposureBias = true;
	PS.AutoExposureBias = 0.f;
	PS.bOverride_BloomIntensity = true;
	PS.BloomIntensity = 0.9f;
	PS.bOverride_VignetteIntensity = true;
	PS.VignetteIntensity = 0.35f;
	PS.bOverride_ColorSaturation = true;
	PS.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.08f);
	PP->RegisterComponent();
	Owner->AddInstanceComponent(PP);

	// Far gas giant with a ring, high in the sky so the height fog stays thin.
	UStaticMesh* Sphere = EngineMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cylinder = EngineMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	const FVector PlanetPos = FVector(1.6e6f, -0.9e6f, 0.75e6f);
	if (Sphere && bWithPlanet)
	{
		UStaticMeshComponent* Planet = NewObject<UStaticMeshComponent>(Owner, TEXT("Planet"));
		Planet->SetupAttachment(Root);
		Planet->SetStaticMesh(Sphere);
		Planet->SetWorldLocation(PlanetPos);
		Planet->SetWorldScale3D(FVector(3200.f));
		Planet->SetCastShadow(false);
		Planet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Planet->SetMaterial(0, FlightMaterials::Glow(Owner, FLinearColor(0.42f, 0.3f, 0.5f) * 0.14f));
		Planet->RegisterComponent();
		Owner->AddInstanceComponent(Planet);
	}
	if (Cylinder && bWithPlanet)
	{
		UStaticMeshComponent* PlanetRing = NewObject<UStaticMeshComponent>(Owner, TEXT("PlanetRing"));
		PlanetRing->SetupAttachment(Root);
		PlanetRing->SetStaticMesh(Cylinder);
		PlanetRing->SetWorldLocation(PlanetPos);
		PlanetRing->SetWorldRotation(FRotator(18.f, 30.f, 12.f));
		PlanetRing->SetWorldScale3D(FVector(7400.f, 7400.f, 6.f));
		PlanetRing->SetCastShadow(false);
		PlanetRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PlanetRing->SetMaterial(0, FlightMaterials::Additive(Owner, FLinearColor(0.5f, 0.42f, 0.36f) * 0.35f));
		PlanetRing->RegisterComponent();
		Owner->AddInstanceComponent(PlanetRing);
	}
}

void AFlightCourse::BuildEnvironment()
{
	FlightEnvironment::Build(this, Root, true);

	if (UStaticMesh* Plane = EngineMesh(TEXT("/Engine/BasicShapes/Plane.Plane")))
	{
		Sea->SetStaticMesh(Plane);
		Sea->SetWorldScale3D(FVector(6000.f, 6000.f, 1.f));
		Sea->SetMaterial(0, FlightMaterials::Surface(this, FLinearColor(0.012f, 0.05f, 0.07f), 0.f, 0.06f, 0.9f));
	}
}

void AFlightCourse::BuildScenery()
{
	UStaticMesh* Cube = EngineMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cone = EngineMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
	UStaticMesh* Sphere = EngineMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cylinder = EngineMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	Rocks->SetStaticMesh(Cube);
	Rocks->SetMaterial(0, FlightMaterials::Surface(this, FLinearColor(0.2f, 0.12f, 0.1f), 0.f, 0.85f, 0.35f));
	RockSpires->SetStaticMesh(Cone);
	RockSpires->SetMaterial(0, FlightMaterials::Surface(this, FLinearColor(0.15f, 0.1f, 0.11f), 0.f, 0.8f, 0.35f));
	Clouds->SetStaticMesh(Sphere);
	// Soft additive banks: no hard white rims, they just brighten the haze.
	Clouds->SetMaterial(0, FlightMaterials::Additive(this, FLinearColor(0.11f, 0.12f, 0.15f)));
	BaseHull->SetStaticMesh(Cylinder);
	BaseHull->SetMaterial(0, FlightMaterials::Surface(this, FLinearColor(0.08f, 0.09f, 0.11f), 0.85f, 0.32f));
	BaseDecks->SetStaticMesh(Cube);
	BaseDecks->SetMaterial(0, FlightMaterials::Surface(this, FLinearColor(0.42f, 0.45f, 0.5f), 0.7f, 0.28f));
	BaseLights->SetStaticMesh(Cube);
	BaseLights->SetMaterial(0, FlightMaterials::Glow(this, FLinearColor(0.25f, 0.85f, 1.f) * 5.f));
	Whitecaps->SetStaticMesh(Sphere);
	Whitecaps->SetMaterial(0, FlightMaterials::Additive(this, FLinearColor(0.32f, 0.42f, 0.46f)));

	FRandomStream Rng(20261101);
	auto Place = [this](UInstancedStaticMeshComponent* ISM, const FPathFrame& Fr, double X, double Z,
		const FRotator& LocalRot, const FVector& Scale)
	{
		const FQuat Q = Fr.Rotation() * LocalRot.Quaternion();
		ISM->AddInstance(FTransform(Q, Fr.P + Fr.R * X + Fr.U * Z, Scale), true);
	};

	// --- Sea: rock spires and islands well outside the flight box ---
	for (double S = 6000.0; S < CanyonS - 4000.0; S += 2600.0)
	{
		const FPathFrame Fr = Path.Frame(S);
		for (const double Side : { -1.0, 1.0 })
		{
			if (Rng.FRand() < 0.45f)
			{
				continue;
			}
			const double X = Side * Rng.FRandRange(4200.f, 22000.f);
			const float Hgt = Rng.FRandRange(18.f, 60.f);
			const float Rad = Rng.FRandRange(8.f, 22.f);
			Place(RockSpires, Fr, X, -Fr.P.Z - 200.0, FRotator(Rng.FRandRange(-6.f, 6.f), Rng.FRandRange(0.f, 360.f), Rng.FRandRange(-6.f, 6.f)),
				FVector(Rad, Rad * Rng.FRandRange(0.7f, 1.2f), Hgt));
			if (Rng.FRand() < 0.5f)
			{
				Place(Rocks, Fr, X + Rng.FRandRange(-900.f, 900.f), -Fr.P.Z,
					FRotator(Rng.FRandRange(-20.f, 20.f), Rng.FRandRange(0.f, 90.f), Rng.FRandRange(-20.f, 20.f)),
					FVector(Rng.FRandRange(10.f, 26.f), Rng.FRandRange(10.f, 26.f), Rng.FRandRange(4.f, 12.f)));
			}
		}
	}

	// --- Canyon: two layers of faceted cliffs, pillars and arches ---
	for (double S = CanyonS - 6000.0; S < SkyBaseS - 12000.0; S += 1100.0)
	{
		const FPathFrame Fr = Path.Frame(S);
		for (const double Side : { -1.0, 1.0 })
		{
			const float Thick = Rng.FRandRange(900.f, 1700.f);
			const double X = Side * (LimitX + 800.0 + Thick * 0.5f + Rng.FRandRange(0.f, 500.f));
			const float Tall = Rng.FRandRange(2600.f, 5200.f);
			Place(Rocks, Fr, X, -Fr.P.Z + Tall * 0.5f - 300.0,
				FRotator(Rng.FRandRange(-8.f, 8.f), Rng.FRandRange(-25.f, 25.f), Rng.FRandRange(-10.f, 10.f)),
				FVector(Rng.FRandRange(14.f, 26.f), Thick / 100.f, Tall / 100.f));
			const float Tall2 = Tall + Rng.FRandRange(1500.f, 4000.f);
			Place(Rocks, Fr, X + Side * Rng.FRandRange(1600.f, 2600.f), -Fr.P.Z + Tall2 * 0.5f - 300.0,
				FRotator(Rng.FRandRange(-6.f, 6.f), Rng.FRandRange(0.f, 90.f), Rng.FRandRange(-6.f, 6.f)),
				FVector(Rng.FRandRange(18.f, 30.f), Rng.FRandRange(14.f, 24.f), Tall2 / 100.f));
		}
	}
	const double PillarS[] = { 300000.0, 316000.0, 334000.0, 352000.0, 395000.0, 411000.0, 446000.0 };
	const float PillarX[] = { 600.f, -650.f, 0.f, 700.f, -500.f, 450.f, -700.f };
	for (int32 i = 0; i < UE_ARRAY_COUNT(PillarS); ++i)
	{
		const FPathFrame Fr = Path.Frame(PillarS[i]);
		Hazards.Add({ PillarS[i], PillarX[i], 270.f });
		Place(Rocks, Fr, PillarX[i], -Fr.P.Z + 2800.0, FRotator(0.f, 45.f, 0.f), FVector(4.6f, 4.6f, 62.f));
		Place(Rocks, Fr, PillarX[i], -Fr.P.Z + 300.0, FRotator(8.f, 20.f, 5.f), FVector(8.f, 7.f, 6.f));
	}
	for (const double ArchS : { 268000.0, 342000.0, 378000.0, 428000.0 })
	{
		const FPathFrame Fr = Path.Frame(ArchS);
		Place(Rocks, Fr, 0.0, 1700.0, FRotator(0.f, 0.f, Rng.FRandRange(-4.f, 4.f)), FVector(12.f, 70.f, 6.f));
	}

	// --- Sky base: floating platforms, gates, struts and beacons ---
	auto Platform = [&](const FPathFrame& Fr, double X, double Z, float Radius)
	{
		Place(BaseHull, Fr, X, Z - 260.0, FRotator::ZeroRotator, FVector(Radius / 50.f, Radius / 50.f, 4.f));
		Place(BaseDecks, Fr, X, Z, FRotator(0.f, 45.f, 0.f), FVector(Radius / 72.f, Radius / 72.f, 0.6f));
		const int32 Towers = Rng.RandRange(1, 3);
		for (int32 t = 0; t < Towers; ++t)
		{
			const double TX = X + Rng.FRandRange(-Radius * 0.5f, Radius * 0.5f);
			const float TH = Rng.FRandRange(900.f, 2600.f);
			Place(BaseDecks, Fr, TX, Z + TH * 0.5f, FRotator(0.f, Rng.FRandRange(0.f, 90.f), 0.f),
				FVector(Rng.FRandRange(3.f, 6.f), Rng.FRandRange(3.f, 6.f), TH / 100.f));
			Place(BaseLights, Fr, TX, Z + TH + 40.0, FRotator::ZeroRotator, FVector(1.6f, 1.6f, 0.4f));
		}
		Place(BaseLights, Fr, X, Z - 560.0, FRotator::ZeroRotator, FVector(Radius / 60.f, 0.25f, 0.25f));
	};
	for (double S = SkyBaseS + 4000.0; S < BossS; S += 5200.0)
	{
		const FPathFrame Fr = Path.Frame(S);
		for (const double Side : { -1.0, 1.0 })
		{
			if (Rng.FRand() < 0.35f)
			{
				continue;
			}
			Platform(Fr, Side * Rng.FRandRange(3600.f, 9000.f), Rng.FRandRange(-2600.f, 900.f), Rng.FRandRange(1600.f, 3000.f));
		}
	}
	for (double S = SkyBaseS + 9000.0; S < BossS; S += 15000.0)
	{
		// Gate frame outside the flight box: passing through them reads as speed.
		const FPathFrame Fr = Path.Frame(S);
		const float GX = LimitX + 900.f, GZ = LimitZ + 700.f;
		Place(BaseDecks, Fr, -GX, 0.0, FRotator::ZeroRotator, FVector(2.2f, 2.2f, GZ * 2.f / 100.f + 2.f));
		Place(BaseDecks, Fr, GX, 0.0, FRotator::ZeroRotator, FVector(2.2f, 2.2f, GZ * 2.f / 100.f + 2.f));
		Place(BaseDecks, Fr, 0.0, GZ, FRotator::ZeroRotator, FVector(2.2f, GX * 2.f / 100.f + 2.f, 2.2f));
		Place(BaseDecks, Fr, 0.0, -GZ, FRotator::ZeroRotator, FVector(2.2f, GX * 2.f / 100.f + 2.f, 2.2f));
		Place(BaseLights, Fr, -GX + 120.0, 0.0, FRotator::ZeroRotator, FVector(0.3f, 0.3f, GZ * 2.f / 100.f));
		Place(BaseLights, Fr, GX - 120.0, 0.0, FRotator::ZeroRotator, FVector(0.3f, 0.3f, GZ * 2.f / 100.f));
		Place(BaseLights, Fr, 0.0, GZ - 120.0, FRotator::ZeroRotator, FVector(0.3f, GX * 2.f / 100.f, 0.3f));
	}
	// Arena: a ring of platforms around the loop so the circle reads as a place.
	for (int32 i = 0; i < 18; ++i)
	{
		const FPathFrame Fr = Path.Frame(BossS + i * (2.0 * PI * ArenaRadius / 18.0));
		Platform(Fr, -Rng.FRandRange(6000.f, 11000.f), Rng.FRandRange(-2800.f, 600.f), Rng.FRandRange(2200.f, 3600.f));
		Platform(Fr, Rng.FRandRange(7000.f, 14000.f), Rng.FRandRange(-3500.f, 0.f), Rng.FRandRange(2000.f, 3000.f));
	}

	// Cloud deck under the sky base and arena.
	for (double S = SkyBaseS + 8000.0; S < BossS + 2.0 * PI * ArenaRadius; S += 3200.0)
	{
		const FPathFrame Fr = Path.Frame(S);
		for (int32 k = 0; k < 3; ++k)
		{
			const double X = Rng.FRandRange(-26000.f, 26000.f);
			const FVector Pos = Fr.P + Fr.R * X;
			const float R = Rng.FRandRange(60.f, 140.f);
			Clouds->AddInstance(FTransform(FRotator(0.f, Rng.FRandRange(0.f, 180.f), 0.f),
				FVector(Pos.X, Pos.Y, Rng.FRandRange(1500.f, 4500.f)), FVector(R * 1.8f, R, R * 0.12f)), true);
		}
	}

	// Sea glints: placed on a world grid each time the camera crosses a cell.
	for (int32 i = 0; i < 28 * 28; ++i)
	{
		Whitecaps->AddInstance(Hidden(), true);
	}
}

void AFlightCourse::BuildSchedule()
{
	auto Add = [this](double S, uint8 Kind, uint8 Pattern, int32 Count, FVector2D Off = FVector2D::ZeroVector)
	{
		FFlightSpawn Sp;
		Sp.TriggerS = S;
		Sp.Kind = Kind;
		Sp.Pattern = Pattern;
		Sp.Count = Count;
		Sp.Off = Off;
		Schedule.Add(Sp);
	};
	// Sea
	Add(20000, 0, 0, 3);
	Add(42000, 0, 2, 4, FVector2D(-1, 150));
	Add(66000, 0, 1, 2);
	Add(92000, 0, 0, 5);
	Add(118000, 0, 2, 4, FVector2D(1, -150));
	Add(142000, 0, 1, 3);
	Add(172000, 0, 0, 4);
	Add(196000, 0, 2, 5, FVector2D(-1, 0));
	Add(222000, 0, 1, 3);
	// Canyon: turrets on low ledges plus drones
	for (int32 i = 0; i < 12; ++i)
	{
		const double TS = 268000.0 + i * 15000.0;
		const float X = (i % 2 == 0 ? 1.f : -1.f) * 1180.f;
		Add(TS - 9000.0, 1, 0, 1, FVector2D(X, -LimitZ + 120.f));
		const FPathFrame Fr = Path.Frame(TS);
		Rocks->AddInstance(FTransform(Fr.Rotation(), Fr.ToWorld(0, X, -LimitZ - 180.f), FVector(6.f, 6.f, 3.f)), true);
	}
	Add(280000, 0, 0, 3);
	Add(322000, 0, 1, 2);
	Add(362000, 0, 2, 3, FVector2D(1, 200));
	Add(402000, 0, 0, 4);
	Add(440000, 0, 1, 3);
	// Sky base: turrets on small decks
	for (int32 i = 0; i < 13; ++i)
	{
		const double TS = 482000.0 + i * 12000.0;
		const float X = (i % 2 == 0 ? -1.f : 1.f) * 1250.f;
		Add(TS - 9000.0, 1, 0, 1, FVector2D(X, -LimitZ + 100.f));
		const FPathFrame Fr = Path.Frame(TS);
		BaseDecks->AddInstance(FTransform(Fr.Rotation(), Fr.ToWorld(0, X, -LimitZ - 40.f), FVector(5.f, 5.f, 0.5f)), true);
		BaseLights->AddInstance(FTransform(Fr.Rotation(), Fr.ToWorld(0, X, -LimitZ - 80.f), FVector(5.2f, 0.2f, 0.2f)), true);
	}
	Add(490000, 0, 1, 3);
	Add(522000, 0, 2, 5, FVector2D(-1, 100));
	Add(552000, 0, 0, 5);
	Add(586000, 0, 1, 4);
	Add(616000, 0, 2, 4, FVector2D(1, -100));
	// Boss
	Add(BossS, 2, 0, 1);
}

void AFlightCourse::BuildRings()
{
	struct FGroup { double S; int32 Shape; };
	const FGroup Groups[] = {
		{ 12000, 0 }, { 52000, 1 }, { 100000, 2 }, { 150000, 3 }, { 205000, 4 }, { 238000, 1 },
		{ 262000, 0 }, { 308000, 1 }, { 372000, 2 }, { 424000, 3 },
		{ 478000, 0 }, { 530000, 1 }, { 598000, 3 },
	};
	UMaterialInterface* Parent = nullptr;
	for (const FGroup& G : Groups)
	{
		for (int32 i = 0; i < 5; ++i)
		{
			FFlightRing Ring;
			Ring.S = G.S + i * 2300.0;
			const float T = i / 4.f;
			switch (G.Shape)
			{
			case 0: Ring.Off = FVector2D(0.f, 0.f); break;                                       // straight line
			case 1: Ring.Off = FVector2D((i % 2 == 0 ? -1.f : 1.f) * 700.f, 0.f); break;          // slalom
			case 2: Ring.Off = FVector2D(0.f, FMath::Lerp(-450.f, 500.f, T)); break;             // climb
			case 3: Ring.Off = FVector2D(FMath::Cos(T * 2.f * PI) * 650.f, FMath::Sin(T * 2.f * PI) * 400.f); break; // spiral
			default: Ring.Off = FVector2D(FMath::Lerp(-800.f, 800.f, T), FMath::Lerp(450.f, -450.f, T)); break; // dive
			}
			// Keep rings clear of canyon pillars.
			for (const FFlightHazard& Hz : Hazards)
			{
				if (FMath::Abs(Hz.S - Ring.S) < 2600.0 && FMath::Abs(Hz.X - Ring.Off.X) < 950.f)
				{
					Ring.Off.X = Hz.X > 0.f ? Hz.X - 1050.f : Hz.X + 1050.f;
				}
			}
			const FString Name = FString::Printf(TEXT("Ring_%d"), Rings.Num());
			UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(this, *Name);
			PMC->SetupAttachment(Root);
			PMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			PMC->SetCastShadow(false);
			PMC->RegisterComponent();
			AddInstanceComponent(PMC);
			Ring.MID = FlightMaterials::Additive(this, FLinearColor(0.25f, 0.85f, 1.f) * 2.f);
			FlightModels::RingMesh().Upload(PMC, 0, Ring.MID);
			const FPathFrame Fr = Path.Frame(Ring.S);
			PMC->SetWorldLocationAndRotation(Fr.ToWorld(0, Ring.Off.X, Ring.Off.Y), Fr.Rotation());
			PMC->SetVisibility(false);
			Ring.Mesh = PMC;
			Rings.Add(Ring);
		}
	}
	RingsTotal = Rings.Num();
}

void AFlightCourse::BuildPools()
{
	UStaticMesh* Sphere = EngineMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cube = EngineMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Bolts->SetStaticMesh(Sphere);
	Bolts->SetMaterial(0, FlightMaterials::Additive(this, FLinearColor(0.35f, 0.95f, 1.f) * 4.f));
	Bullets->SetStaticMesh(Sphere);
	Bullets->SetMaterial(0, FlightMaterials::Additive(this, FLinearColor(1.f, 0.25f, 0.55f) * 4.f));
	Debris->SetStaticMesh(Cube);
	Debris->SetMaterial(0, FlightMaterials::Glow(this, FLinearColor(1.f, 0.42f, 0.08f) * 5.f));
	Blasts->SetStaticMesh(Sphere);
	BlastMID = FlightMaterials::Additive(this, FLinearColor(1.f, 0.5f, 0.16f) * 2.2f);
	Blasts->SetMaterial(0, BlastMID);

	Shots.SetNum(MaxShots);
	EnemyBullets.SetNum(MaxBullets);
	Particles.SetNum(MaxDebris);
	BlastPool.SetNum(10);
	for (int32 i = 0; i < MaxShots; ++i) { Bolts->AddInstance(Hidden(), true); }
	for (int32 i = 0; i < MaxBullets; ++i) { Bullets->AddInstance(Hidden(), true); }
	for (int32 i = 0; i < MaxDebris; ++i) { Debris->AddInstance(Hidden(), true); }
	for (int32 i = 0; i < BlastPool.Num(); ++i) { Blasts->AddInstance(Hidden(), true); }

	UMaterialInstanceDynamic* HullMID = FlightMaterials::Surface(this, FLinearColor(0.12f, 0.06f, 0.16f), 0.85f, 0.3f);
	UMaterialInstanceDynamic* TurretMID = FlightMaterials::Surface(this, FLinearColor(0.2f, 0.18f, 0.16f), 0.8f, 0.38f);
	KeepAlive.Add(HullMID);
	KeepAlive.Add(TurretMID);

	// Enemy slots: drones first, a fixed number of turrets after them.
	constexpr int32 TurretSlots = 5;
	Enemies.SetNum(MaxEnemies);
	for (int32 i = 0; i < MaxEnemies; ++i)
	{
		FFlightEnemy& E = Enemies[i];
		E.Kind = i >= MaxEnemies - TurretSlots ? 1 : 0;
		UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(this, *FString::Printf(TEXT("Enemy_%d"), i));
		PMC->SetupAttachment(Root);
		PMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PMC->RegisterComponent();
		AddInstanceComponent(PMC);
		E.GlowMID = FlightMaterials::Glow(this, FLinearColor(1.f, 0.2f, 0.35f) * 6.f);
		if (E.Kind == 0)
		{
			FlightModels::DroneHull().Upload(PMC, 0, HullMID);
			FlightModels::DroneGlow().Upload(PMC, 1, E.GlowMID);
		}
		else
		{
			FlightModels::TurretHull().Upload(PMC, 0, TurretMID);
			FlightModels::TurretGlow().Upload(PMC, 1, E.GlowMID);
		}
		PMC->SetVisibility(false);
		E.Mesh = PMC;
	}

	BossMesh = NewObject<UProceduralMeshComponent>(this, TEXT("Boss"));
	BossMesh->SetupAttachment(Root);
	BossMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BossMesh->RegisterComponent();
	AddInstanceComponent(BossMesh);
	FlightModels::BossHull().Upload(BossMesh, 0, FlightMaterials::Surface(this, FLinearColor(0.07f, 0.06f, 0.09f), 0.9f, 0.25f));
	FlightModels::BossArmor().Upload(BossMesh, 1, FlightMaterials::Surface(this, FLinearColor(0.5f, 0.18f, 0.08f), 0.6f, 0.35f));
	BossGlowMID = FlightMaterials::Glow(this, FLinearColor(1.f, 0.3f, 0.12f) * 8.f);
	FlightModels::BossGlow().Upload(BossMesh, 2, BossGlowMID);
	BossMesh->SetVisibility(false);
	Boss.Kind = 2;
	Boss.Mesh = BossMesh;
	Boss.GlowMID = BossGlowMID;
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

FFlightEnemy* AFlightCourse::AllocEnemy(uint8 Kind)
{
	for (FFlightEnemy& E : Enemies)
	{
		if (!E.bActive && E.Kind == Kind)
		{
			return &E;
		}
	}
	return nullptr;
}

void AFlightCourse::SpawnEnemy(const FFlightSpawn& Sp, double ShipS, int32 Index)
{
	if (Sp.Kind == 2)
	{
		Boss.bActive = true;
		Boss.S = ShipS + 16000.0;
		Boss.Off = FVector2D(0.f, 200.f);
		Boss.HP = Boss.MaxHP = 150.f;
		Boss.Age = 0.f;
		Boss.FireTimer = 3.5f;
		Boss.Pattern = 0;
		Boss.HitFlash = 0.f;
		BossMesh->SetVisibility(true);
		bBossActive = true;
		BossHealth01 = 1.f;
		PlayCue(CueWarning);
		return;
	}
	FFlightEnemy* E = AllocEnemy(Sp.Kind);
	if (!E)
	{
		return;
	}
	E->bActive = true;
	E->Pattern = Sp.Pattern;
	E->Age = 0.f;
	E->HitFlash = 0.f;
	E->Vel = FVector2D::ZeroVector;
	if (Sp.Kind == 1)
	{
		E->S = Sp.TriggerS + 9000.0;
		E->Off = Sp.Off;
		E->SpeedS = 0.0;
		E->HP = E->MaxHP = 3.f;
		E->FireTimer = 0.6f;
	}
	else
	{
		E->HP = E->MaxHP = 2.f;
		const float Spread = (Index - (Sp.Count - 1) * 0.5f);
		switch (Sp.Pattern)
		{
		case 0: // approach in a V formation, weaving
			E->S = ShipS + 9500.0 + FMath::Abs(Spread) * 700.0;
			E->Anchor = FVector2D(Spread * 650.f, FMath::Abs(Spread) * 120.f);
			E->SpeedS = ShipSpeed * 0.55;
			E->FireTimer = 2.2f + Index * 0.25f;
			break;
		case 1: // escort ahead, strafing and firing
			E->S = ShipS + 11000.0 + Index * 600.0;
			E->Anchor = FVector2D(Spread * 900.f, 150.f);
			E->SpeedS = ShipSpeed;
			E->FireTimer = 2.4f + Index * 0.5f;
			break;
		default: // cross the course from one side
		{
			const float Side = Sp.Off.X < 0.f ? -1.f : 1.f;
			E->S = ShipS + 7500.0 + Index * 650.0;
			E->Anchor = FVector2D(Side * -(LimitX + 900.f) - Side * Index * 350.f, Sp.Off.Y);
			E->Vel = FVector2D(Side * 1500.f, 0.f);
			E->SpeedS = ShipSpeed * 0.85;
			E->FireTimer = 1.4f + Index * 0.2f;
			break;
		}
		}
		E->Off = E->Anchor;
	}
	E->Mesh->SetVisibility(true);
}

void AFlightCourse::FireBullet(const FFlightEnemy& E, const FVector2D& Target, double ShipS, float LeadTime,
	const FVector2D& Spread)
{
	for (FFlightShot& B : EnemyBullets)
	{
		if (B.bActive)
		{
			continue;
		}
		constexpr double Closing = 4300.0;
		const double Gap = FMath::Max(E.S - ShipS, 800.0);
		const float T = static_cast<float>(Gap / Closing);
		B.bActive = true;
		B.S = E.S;
		B.Off = E.Off;
		B.SpeedS = ShipSpeed - Closing;
		B.Vel = (Target + Spread - E.Off) / FMath::Max(T, 0.3f);
		B.Life = T + 1.5f;
		PlayCue(CueEnemyShot, 1.f, 0.5f);
		return;
	}
}

void AFlightCourse::FireBolt(double S, const FVector2D& Off, const FVector& Dir, double InShipSpeed)
{
	for (FFlightShot& B : Shots)
	{
		if (B.bActive)
		{
			continue;
		}
		constexpr double BoltSpeed = 19000.0;
		B.bActive = true;
		B.S = S;
		B.Off = Off;
		B.SpeedS = InShipSpeed + Dir.X * BoltSpeed;
		B.Vel = FVector2D(Dir.Y, Dir.Z) * BoltSpeed;
		B.Life = 0.55f;
		LaserCueTimer -= 1.f;
		if (LaserCueTimer <= 0.f)
		{
			// Every other bolt: a constant stream of zaps tires the ear.
			LaserCueTimer = 2.f;
			PlayCue(CueLaser, 1.f, 0.35f);
		}
		return;
	}
}

void AFlightCourse::KillEnemy(FFlightEnemy& E, bool bByPlayer)
{
	const FPathFrame Fr = Path.Frame(E.S);
	const FVector World = Fr.ToWorld(0, E.Off.X, E.Off.Y);
	if (bByPlayer)
	{
		++Kills;
		SpawnExplosion(World, E.Kind == 1 ? 1.3f : 1.f);
		EmitDebris(World, E.Kind == 1 ? 16 : 12, 1800.f, 0.22f);
		PlayCue(CueSmallBoom, FMath::FRandRange(0.9f, 1.1f));
	}
	E.bActive = false;
	E.Mesh->SetVisibility(false);
}

void AFlightCourse::Simulate(float Dt, AFlightPawn* Ship)
{
	if (!Ship)
	{
		return;
	}
	WorldTime += Dt;
	const double ShipS = Ship->S;
	const FVector2D ShipOff = Ship->Off;

	// 1) Spawns
	for (FFlightSpawn& Sp : Schedule)
	{
		if (!Sp.bDone && ShipS >= Sp.TriggerS)
		{
			Sp.bDone = true;
			for (int32 i = 0; i < Sp.Count; ++i)
			{
				SpawnEnemy(Sp, ShipS, i);
			}
		}
	}

	// 2) Enemies
	for (FFlightEnemy& E : Enemies)
	{
		if (!E.bActive)
		{
			continue;
		}
		E.Age += Dt;
		E.HitFlash = FMath::Max(E.HitFlash - Dt * 6.f, 0.f);
		const double Gap = E.S - ShipS;
		if (E.Kind == 1)
		{
			E.FireTimer -= Dt;
			if (Gap > 2000.0 && Gap < 8500.0 && E.FireTimer <= 0.f)
			{
				E.FireTimer = 1.5f;
				FireBullet(E, ShipOff + Ship->Vel * 0.4f, ShipS, 0.f);
			}
		}
		else
		{
			switch (E.Pattern)
			{
			case 0:
				E.Off = E.Anchor + FVector2D(FMath::Sin(E.Age * 2.1f) * 320.f, FMath::Sin(E.Age * 1.4f) * 120.f);
				break;
			case 1:
			{
				const double Hold = 3800.0 + FMath::Abs(E.Anchor.X) * 0.8 + (E.Age > 9.f ? (E.Age - 9.f) * 3500.0 : 0.0);
				E.SpeedS = ShipSpeed + FMath::Clamp((Hold - Gap) * 1.6, -3500.0, 3500.0);
				E.Off = E.Anchor + FVector2D(FMath::Sin(E.Age * 1.3f + E.Anchor.X) * 650.f, FMath::Cos(E.Age * 0.9f) * 320.f);
				break;
			}
			default:
				E.Anchor += E.Vel * Dt;
				E.Off = E.Anchor + FVector2D(0.f, FMath::Sin(E.Age * 3.f) * 90.f);
				break;
			}
			E.Off.Y = FMath::Clamp(E.Off.Y, -LimitZ, LimitZ);
			E.S += E.SpeedS * Dt;
			E.FireTimer -= Dt;
			if (E.FireTimer <= 0.f && Gap > 2500.0)
			{
				E.FireTimer = E.Pattern == 1 ? 1.9f : 99.f;
				FireBullet(E, ShipOff + Ship->Vel * 0.5f, ShipS, 0.f);
			}
		}
		const bool bGone = Gap < -2000.0 || Gap > 16000.0 ||
			(E.Pattern == 2 && E.Kind == 0 && FMath::Abs(E.Off.X) > LimitX + 1400.f && E.Age > 2.f);
		if (bGone)
		{
			KillEnemy(E, false);
			continue;
		}
		// Ram: the ship flies through the enemy's plane close enough to touch.
		const double PrevGap = E.S - E.SpeedS * Dt - LastShipS;
		if (PrevGap > 0.0 && Gap <= 0.0 && FVector2D::Distance(E.Off, ShipOff) < 300.f)
		{
			KillEnemy(E, true);
			Ship->ApplyDamage(20.f, Path.Frame(E.S).ToWorld(0, E.Off.X, E.Off.Y));
		}
	}

	// 3) Boss
	if (Boss.bActive)
	{
		Boss.Age += Dt;
		Boss.HitFlash = FMath::Max(Boss.HitFlash - Dt * 6.f, 0.f);
		const double Gap = Boss.S - ShipS;
		const double Hold = 5600.0;
		Boss.SpeedS = ShipSpeed + FMath::Clamp((Hold - Gap) * 1.2, -6000.0, 2500.0);
		Boss.S += Boss.SpeedS * Dt;
		const bool bPhase2 = Boss.HP < Boss.MaxHP * 0.5f;
		Boss.Off = FVector2D(FMath::Sin(Boss.Age * (bPhase2 ? 0.62f : 0.45f)) * 900.f,
			FMath::Sin(Boss.Age * 0.7f) * 330.f + 120.f);
		Boss.FireTimer -= Dt;
		if (Boss.FireTimer <= 0.f && Gap < 9000.0)
		{
			Boss.FireTimer = bPhase2 ? 1.45f : 2.1f;
			const int32 Mode = Boss.Pattern++ % 3;
			if (Mode == 0)
			{
				for (int32 k = -3; k <= 3; ++k)
				{
					FireBullet(Boss, ShipOff, ShipS, 0.f, FVector2D(k * 420.f, 0.f));
				}
			}
			else if (Mode == 1)
			{
				for (int32 k = 0; k < (bPhase2 ? 5 : 3); ++k)
				{
					FireBullet(Boss, ShipOff + Ship->Vel * 0.6f, ShipS, 0.f, FVector2D(k * 60.f - 120.f, k * 40.f - 80.f));
				}
			}
			else
			{
				for (int32 k = 0; k < 10; ++k)
				{
					const float A = 2.f * PI * k / 10.f + Boss.Age;
					FireBullet(Boss, ShipOff, ShipS, 0.f, FVector2D(FMath::Cos(A) * 950.f, FMath::Sin(A) * 520.f));
				}
			}
		}
		BossHealth01 = Boss.HP / Boss.MaxHP;
	}
	else if (bBossDefeated && BossDefeatTime > 0.0)
	{
		// Chain of explosions after the boss breaks apart.
		const float Since = WorldTime - static_cast<float>(BossDefeatTime);
		if (Since < 1.8f && FMath::Fmod(Since, 0.22f) < Dt)
		{
			const FPathFrame Fr = Path.Frame(Boss.S);
			SpawnExplosion(Fr.ToWorld(0, Boss.Off.X + FMath::FRandRange(-900.f, 900.f), Boss.Off.Y + FMath::FRandRange(-300.f, 300.f)), 2.2f);
			PlayCue(CueSmallBoom, 0.7f);
		}
	}

	// 4) Player bolts
	for (FFlightShot& B : Shots)
	{
		if (!B.bActive)
		{
			continue;
		}
		const double PrevS = B.S;
		const FVector2D PrevOff = B.Off;
		B.S += B.SpeedS * Dt;
		B.Off += B.Vel * Dt;
		B.Life -= Dt;
		if (B.Life <= 0.f)
		{
			B.bActive = false;
			continue;
		}
		// Swept test in path space, three sub-steps are plenty at 60 fps.
		auto HitTest = [&](const FFlightEnemy& E, double RS, float RX, float RZ) -> bool
		{
			for (int32 k = 1; k <= 3; ++k)
			{
				const float T = k / 3.f;
				const double PS = FMath::Lerp(PrevS, B.S, static_cast<double>(T));
				const FVector2D PO = FMath::Lerp(PrevOff, B.Off, T);
				const double DS = (PS - E.S) / RS;
				const double DX = (PO.X - E.Off.X) / RX;
				const double DZ = (PO.Y - E.Off.Y) / RZ;
				if (DS * DS + DX * DX + DZ * DZ < 1.0)
				{
					return true;
				}
			}
			return false;
		};
		for (FFlightEnemy& E : Enemies)
		{
			if (E.bActive && HitTest(E, 320.0, 260.f, E.Kind == 1 ? 200.f : 170.f))
			{
				B.bActive = false;
				E.HP -= 1.f;
				E.HitFlash = 1.f;
				const FVector W = Path.Frame(B.S).ToWorld(0, B.Off.X, B.Off.Y);
				EmitDebris(W, 3, 900.f, 0.1f);
				if (E.HP <= 0.f)
				{
					KillEnemy(E, true);
				}
				break;
			}
		}
		if (B.bActive && Boss.bActive && HitTest(Boss, 800.0, 1250.f, 380.f))
		{
			B.bActive = false;
			Boss.HP -= 1.f;
			Boss.HitFlash = 1.f;
			const FVector W = Path.Frame(B.S).ToWorld(-200.0, B.Off.X, B.Off.Y);
			EmitDebris(W, 2, 1200.f, 0.14f);
			if (Boss.HP <= 0.f)
			{
				Boss.bActive = false;
				bBossActive = false;
				bBossDefeated = true;
				BossDefeatTime = WorldTime;
				++Kills;
				const FVector Center = Path.Frame(Boss.S).ToWorld(0, Boss.Off.X, Boss.Off.Y);
				SpawnExplosion(Center, 4.f);
				EmitDebris(Center, 48, 3200.f, 0.45f);
				PlayCue(CueBigBoom);
				BossMesh->SetVisibility(false);
				Ship->OnBossDefeated();
			}
		}
	}

	// 5) Enemy bullets crossing the ship's plane
	for (FFlightShot& B : EnemyBullets)
	{
		if (!B.bActive)
		{
			continue;
		}
		const double PrevGap = B.S - LastShipS;
		B.S += B.SpeedS * Dt;
		B.Off += B.Vel * Dt;
		B.Life -= Dt;
		const double Gap = B.S - ShipS;
		if (PrevGap > 0.0 && Gap <= 0.0 && FVector2D::Distance(B.Off, ShipOff) < 150.f)
		{
			B.bActive = false;
			Ship->ApplyDamage(12.f, Path.Frame(B.S).ToWorld(0, B.Off.X, B.Off.Y));
			continue;
		}
		if (B.Life <= 0.f || Gap < -2500.0)
		{
			B.bActive = false;
		}
	}

	// 6) Rings: resolved exactly once when the ship crosses their plane.
	for (FFlightRing& Ring : Rings)
	{
		if (Ring.bResolved || Ring.S > ShipS || Ring.S <= LastShipS)
		{
			continue;
		}
		Ring.bResolved = true;
		if (FVector2D::Distance(Ring.Off, ShipOff) < 365.f)
		{
			Ring.bPassed = true;
			Ring.Flash = 1.f;
			++RingsPassed;
			++RingStreak;
			RingPulse = 1.f;
			const bool bBonus = RingStreak % 5 == 0;
			PlayCue(CueRing, 1.f + FMath::Min(RingStreak, 10) * 0.06f);
			if (bBonus)
			{
				PlayCue(CueHeal);
			}
			Ship->OnRingPassed(RingStreak, bBonus);
		}
		else
		{
			RingStreak = 0;
			Ship->OnRingMissed();
		}
	}

	// 7) Canyon pillars
	for (const FFlightHazard& Hz : Hazards)
	{
		if (Hz.S <= ShipS && Hz.S > LastShipS && FMath::Abs(ShipOff.X - Hz.X) < Hz.Radius + 160.f)
		{
			Ship->ApplyDamage(30.f, Path.Frame(Hz.S).ToWorld(0, Hz.X, ShipOff.Y));
		}
	}

	LastShipS = ShipS;
	UpdateEnemyTransforms();
	UpdateShotInstances(Bolts, Shots, FVector(2.4f, 0.14f, 0.14f), true);
	UpdateShotInstances(Bullets, EnemyBullets, FVector(0.6f), false);
}

void AFlightCourse::UpdateEnemyTransforms()
{
	const double ShipS = LastShipS;
	auto Pose = [this, ShipS](FFlightEnemy& E, float Scale)
	{
		const FPathFrame Fr = Path.Frame(E.S);
		// Face the player; bank into lateral motion.
		const FRotator Local(0.f, 180.f, FMath::Clamp(E.Vel.X * -0.01f, -30.f, 30.f) + (E.Kind == 0 ? FMath::Sin(E.Age * 2.1f) * 12.f : 0.f));
		FQuat Q = Fr.Rotation() * Local.Quaternion();
		if (E.Kind == 1)
		{
			// Turrets track the ship around their vertical axis.
			Q = Fr.Rotation() * FRotator(0.f, 180.f, 0.f).Quaternion();
		}
		E.Mesh->SetWorldTransform(FTransform(Q, Fr.ToWorld(0, E.Off.X, E.Off.Y), FVector(Scale)));
		const FLinearColor Base = E.Kind == 2 ? FLinearColor(1.f, 0.3f, 0.12f) * 8.f : FLinearColor(1.f, 0.2f, 0.35f) * 6.f;
		E.GlowMID->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(Base, FLinearColor(10.f, 10.f, 10.f), E.HitFlash));
		E.Mesh->SetVisibility(E.S - ShipS < 14000.0);
	};
	for (FFlightEnemy& E : Enemies)
	{
		if (E.bActive)
		{
			Pose(E, 1.f);
		}
	}
	if (Boss.bActive)
	{
		Pose(Boss, 1.f);
	}
}

void AFlightCourse::UpdateShotInstances(UInstancedStaticMeshComponent* ISM, const TArray<FFlightShot>& List,
	const FVector& Scale, bool bAlign)
{
	TArray<FTransform> Xf;
	Xf.SetNum(List.Num());
	for (int32 i = 0; i < List.Num(); ++i)
	{
		const FFlightShot& B = List[i];
		if (!B.bActive)
		{
			Xf[i] = Hidden();
			continue;
		}
		const FPathFrame Fr = Path.Frame(B.S);
		FQuat Q = Fr.Rotation();
		if (bAlign)
		{
			const FVector Dir = (Fr.F * (B.SpeedS - ShipSpeed) + Fr.R * B.Vel.X + Fr.U * B.Vel.Y).GetSafeNormal();
			Q = FRotationMatrix::MakeFromXZ(Dir, Fr.U).ToQuat();
		}
		Xf[i] = FTransform(Q, Fr.ToWorld(0, B.Off.X, B.Off.Y), Scale);
	}
	ISM->BatchUpdateInstancesTransforms(0, Xf, true, true, true);
}

void AFlightCourse::SpawnExplosion(const FVector& World, float Scale)
{
	for (FFlightBlast& B : BlastPool)
	{
		if (!B.bActive)
		{
			B = FFlightBlast();
			B.bActive = true;
			B.Pos = World;
			B.Size = Scale;
			B.Life = 0.45f + 0.15f * Scale;
			break;
		}
	}
	BlastLight->SetWorldLocation(World);
	BlastLight->SetIntensity(60000.f * Scale);
	BlastLightTime = 0.25f + 0.1f * Scale;
}

void AFlightCourse::EmitDebris(const FVector& World, int32 Count, float Speed, float Size)
{
	for (int32 i = 0; i < Count && Particles.Num() > 0; ++i)
	{
		FFlightParticle& P = Particles[NextParticle];
		NextParticle = (NextParticle + 1) % Particles.Num();
		P.bActive = true;
		P.Pos = World;
		P.Vel = FMath::VRand() * Speed * FMath::FRandRange(0.4f, 1.f);
		P.Rot = FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f);
		P.Spin = FRotator(FMath::FRandRange(-600.f, 600.f), FMath::FRandRange(-600.f, 600.f), 0.f);
		P.Age = 0.f;
		P.Life = FMath::FRandRange(0.5f, 1.1f);
		P.Size = Size * FMath::FRandRange(0.6f, 1.4f);
	}
}

void AFlightCourse::TickEffects(float Dt)
{
	// Debris: glowing shards with drag, shrinking to nothing.
	{
		TArray<FTransform> Xf;
		Xf.SetNum(Particles.Num());
		for (int32 i = 0; i < Particles.Num(); ++i)
		{
			FFlightParticle& P = Particles[i];
			if (P.bActive)
			{
				P.Age += Dt;
				if (P.Age >= P.Life)
				{
					P.bActive = false;
				}
			}
			if (!P.bActive)
			{
				Xf[i] = Hidden();
				continue;
			}
			P.Vel *= FMath::Exp(-2.2f * Dt);
			P.Vel.Z -= 600.f * Dt;
			P.Pos += P.Vel * Dt;
			P.Rot += P.Spin * Dt;
			const float K = 1.f - P.Age / P.Life;
			Xf[i] = FTransform(P.Rot, P.Pos, FVector(P.Size * K, P.Size * K * 0.6f, P.Size * K * 0.3f));
		}
		Debris->BatchUpdateInstancesTransforms(0, Xf, true, true, true);
	}

	// Blasts: a quick flash that expands, then collapses.
	{
		TArray<FTransform> Xf;
		Xf.SetNum(BlastPool.Num());
		for (int32 i = 0; i < BlastPool.Num(); ++i)
		{
			FFlightBlast& B = BlastPool[i];
			if (B.bActive)
			{
				B.Age += Dt;
				if (B.Age >= B.Life)
				{
					B.bActive = false;
				}
			}
			if (!B.bActive)
			{
				Xf[i] = Hidden();
				continue;
			}
			const float T = B.Age / B.Life;
			const float Grow = T < 0.25f ? FMath::Sqrt(T / 0.25f) : 1.f - FMath::Square((T - 0.25f) / 0.75f);
			Xf[i] = FTransform(FRotator(0.f, T * 90.f, 0.f), B.Pos, FVector(B.Size * 4.2f * FMath::Max(Grow, 0.02f)));
		}
		Blasts->BatchUpdateInstancesTransforms(0, Xf, true, true, true);
	}
	if (BlastLightTime > 0.f)
	{
		BlastLightTime -= Dt;
		BlastLight->SetIntensity(FMath::Max(BlastLight->Intensity * FMath::Exp(-9.f * Dt), 0.f));
		if (BlastLightTime <= 0.f)
		{
			BlastLight->SetIntensity(0.f);
		}
	}

	// Rings: visible only near the ship, flash on pass, dim on miss.
	RingPulse = FMath::Max(RingPulse - Dt * 2.5f, 0.f);
	for (FFlightRing& Ring : Rings)
	{
		const double Gap = Ring.S - LastShipS;
		const bool bVisible = Gap > -1500.0 && Gap < 42000.0;
		Ring.Mesh->SetVisibility(bVisible);
		if (!bVisible)
		{
			continue;
		}
		Ring.Flash = FMath::Max(Ring.Flash - Dt * 2.2f, 0.f);
		FLinearColor C = FLinearColor(0.25f, 0.85f, 1.f) * 2.f;
		if (Ring.bResolved && !Ring.bPassed)
		{
			C *= 0.18f;
		}
		else if (Ring.bPassed)
		{
			C = FMath::Lerp(C * 0.35f, FLinearColor(1.f, 0.95f, 0.75f) * 9.f, Ring.Flash);
		}
		else
		{
			C *= 0.85f + 0.15f * FMath::Sin(WorldTime * 4.f + static_cast<float>(Ring.S) * 0.001f);
		}
		Ring.MID->SetVectorParameterValue(TEXT("Color"), C);
		Ring.Mesh->SetWorldScale3D(FVector(1.f + 0.3f * (1.f - Ring.Flash) * (Ring.bPassed ? 1.f : 0.f)));
	}

	if (AFlightPawn* Ship = Cast<AFlightPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		UpdateSeaDetail(Ship->GetCameraLocation());
	}
}

void AFlightCourse::UpdateSeaDetail(const FVector& Cam)
{
	Sea->SetWorldLocation(FVector(Cam.X, Cam.Y, 0.f));
	constexpr float Cell = 1500.f;
	constexpr int32 Grid = 28;
	const FVector Snap(FMath::FloorToFloat(Cam.X / Cell), FMath::FloorToFloat(Cam.Y / Cell), 0.f);
	const bool bLow = Cam.Z < 4500.f;
	if (Snap == LastCameraPos && bLow == Whitecaps->IsVisible())
	{
		return;
	}
	LastCameraPos = Snap;
	Whitecaps->SetVisibility(bLow);
	if (!bLow)
	{
		return;
	}
	TArray<FTransform> Xf;
	Xf.SetNum(Grid * Grid);
	for (int32 j = 0; j < Grid; ++j)
	{
		for (int32 i = 0; i < Grid; ++i)
		{
			const int32 CX = static_cast<int32>(Snap.X) + i - Grid / 2;
			const int32 CY = static_cast<int32>(Snap.Y) + j - Grid / 2;
			FRandomStream R(CX * 73856093 ^ CY * 19349663);
			const FVector Pos((CX + R.FRand()) * Cell, (CY + R.FRand()) * Cell, 3.f);
			Xf[j * Grid + i] = FTransform(FRotator(0.f, R.FRandRange(-20.f, 20.f), 0.f), Pos,
				FVector(R.FRandRange(1.5f, 4.5f), R.FRandRange(0.2f, 0.5f), 0.02f));
		}
	}
	Whitecaps->BatchUpdateInstancesTransforms(0, Xf, true, true, true);
}

void AFlightCourse::ResetFrom(double S)
{
	for (FFlightEnemy& E : Enemies)
	{
		E.bActive = false;
		E.Mesh->SetVisibility(false);
	}
	Boss.bActive = false;
	BossMesh->SetVisibility(false);
	bBossActive = false;
	for (FFlightShot& B : Shots) { B.bActive = false; }
	for (FFlightShot& B : EnemyBullets) { B.bActive = false; }
	for (FFlightSpawn& Sp : Schedule)
	{
		// Waves before the checkpoint stay spent; everything after replays.
		Sp.bDone = Sp.TriggerS < S - 1.0;
	}
	for (FFlightRing& Ring : Rings)
	{
		if (Ring.S >= S)
		{
			if (Ring.bPassed)
			{
				--RingsPassed;
			}
			Ring.bPassed = Ring.bResolved = false;
			Ring.Flash = 0.f;
		}
	}
	RingStreak = 0;
	LastShipS = S;
	if (S < BossS)
	{
		bBossDefeated = false;
	}
	UpdateShotInstances(Bolts, Shots, FVector(1.f), true);
	UpdateShotInstances(Bullets, EnemyBullets, FVector(1.f), false);
}

// ---------------------------------------------------------------------------
// Procedural audio (no sound assets, same approach as the maze)
// ---------------------------------------------------------------------------

void AFlightCourse::PlayCue(uint8 Cue, float Pitch, float Volume)
{
	constexpr int32 SampleRate = 32000;
	float Duration = 0.2f;
	switch (Cue)
	{
	case CueLaser: Duration = 0.07f; break;
	case CueSmallBoom: Duration = 0.45f; break;
	case CueBigBoom: Duration = 1.4f; break;
	case CueRing: Duration = 0.32f; break;
	case CueHeal: Duration = 0.55f; break;
	case CueDamage: Duration = 0.25f; break;
	case CueWarning: Duration = 1.6f; break;
	default: Duration = 0.12f; break;
	}
	const int32 Count = FMath::CeilToInt(Duration * SampleRate);
	TArray<int16> PCM;
	PCM.SetNumUninitialized(Count);
	FRandomStream Rng(Cue * 977 + FMath::Rand());
	double PhaseA = 0.0, PhaseB = 0.0;
	float Noise = 0.f;
	for (int32 i = 0; i < Count; ++i)
	{
		const float T = i / static_cast<float>(SampleRate);
		const float P = T / Duration;
		Noise = FMath::Lerp(Noise, Rng.FRandRange(-1.f, 1.f), 0.35f);
		float Sample = 0.f;
		switch (Cue)
		{
		case CueLaser:
			PhaseA += 2.0 * PI * (2400.0 - 1700.0 * P) * Pitch / SampleRate;
			Sample = 0.4f * FMath::Sin(PhaseA) * (1.f - P);
			break;
		case CueSmallBoom:
		case CueBigBoom:
		{
			const float Decay = Cue == CueBigBoom ? 2.6f : 7.f;
			PhaseA += 2.0 * PI * (70.0 - 30.0 * P) * Pitch / SampleRate;
			Sample = (0.7f * Noise + 0.5f * FMath::Sin(PhaseA)) * FMath::Exp(-T * Decay) * FMath::Min(T / 0.004f, 1.f);
			break;
		}
		case CueRing:
		{
			// Two-note chime, pitch rises with the streak.
			const double F = (P < 0.35f ? 880.0 : 1318.5) * Pitch;
			PhaseA += 2.0 * PI * F / SampleRate;
			PhaseB += 2.0 * PI * F * 2.01 / SampleRate;
			const float Local = P < 0.35f ? P / 0.35f : (P - 0.35f) / 0.65f;
			Sample = (0.45f * FMath::Sin(PhaseA) + 0.15f * FMath::Sin(PhaseB)) * FMath::Exp(-Local * 3.5f);
			break;
		}
		case CueHeal:
			PhaseA += 2.0 * PI * (440.0 + 660.0 * P) / SampleRate;
			PhaseB += 2.0 * PI * (660.0 + 990.0 * P) / SampleRate;
			Sample = (0.35f * FMath::Sin(PhaseA) + 0.25f * FMath::Sin(PhaseB)) * FMath::Sin(P * PI);
			break;
		case CueDamage:
			PhaseA += 2.0 * PI * (180.0 - 90.0 * P) / SampleRate;
			Sample = (0.6f * FMath::Sin(PhaseA) + 0.4f * Noise) * FMath::Exp(-T * 12.f);
			break;
		case CueWarning:
		{
			const bool bHigh = FMath::Fmod(T, 0.4f) < 0.2f;
			PhaseA += 2.0 * PI * (bHigh ? 740.0 : 520.0) / SampleRate;
			Sample = 0.35f * (FMath::Sin(PhaseA) > 0.0 ? 1.f : -1.f) * 0.5f * FMath::Min((Duration - T) / 0.2f, 1.f);
			break;
		}
		default:
			PhaseA += 2.0 * PI * (520.0 - 300.0 * P) / SampleRate;
			Sample = 0.3f * FMath::Sin(PhaseA) * (1.f - P);
			break;
		}
		Sample = Sample / (1.f + FMath::Abs(Sample)) * Volume;
		PCM[i] = static_cast<int16>(FMath::Clamp(Sample, -1.f, 1.f) * 32000.f);
	}
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	Wave->SetSampleRate(SampleRate);
	Wave->SetNumFrames(Count);
	Wave->NumChannels = 1;
	Wave->Duration = Duration;
	Wave->bLooping = false;
	Wave->QueueAudio(reinterpret_cast<const uint8*>(PCM.GetData()), PCM.Num() * sizeof(int16));
	UGameplayStatics::PlaySound2D(this, Wave);
}
