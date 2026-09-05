#include "MazeGenerator.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MazeBallPawn.h"
#include "MazeSaveGame.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundWaveProcedural.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Boost-pad arrow geometry, shared by Rebuild and UpdateBoostArrows so the
	// static head and the scrolling chevrons stay the same shape.
	constexpr float BoostArmYaw = 34.f;     // half-angle between a chevron's two arms
	constexpr float BoostArmSpread = 24.f;  // arm offset from the pad centre line
	constexpr int32 BoostChevrons = 3;      // scrolling chevrons per pad

	enum class EKineticCue : uint8
	{
		Boost,
		GlassBreak,
		Warp,
		WallBreak
	};

	float DeterministicNoise(int32 Sample)
	{
		uint32 X = static_cast<uint32>(Sample) * 747796405u + 2891336453u;
		X = ((X >> ((X >> 28u) + 4u)) ^ X) * 277803737u;
		X = (X >> 22u) ^ X;
		return static_cast<float>(X & 0xffffu) / 32767.5f - 1.f;
	}

	/** Small synthesized cues keep the project self-contained and latency low on Android. */
	void PlayKineticCue(UObject* Context, const FVector& WorldLocation, EKineticCue Cue)
	{
		if (!Context || !Context->GetWorld())
		{
			return;
		}

		constexpr int32 SampleRate = 24000;
		float Duration = 0.3f;
		float Gain = 0.62f;
		switch (Cue)
		{
		case EKineticCue::Boost:      Duration = 0.28f; Gain = 0.50f; break;
		case EKineticCue::GlassBreak: Duration = 0.36f; Gain = 0.68f; break;
		case EKineticCue::Warp:       Duration = 0.52f; Gain = 0.58f; break;
		case EKineticCue::WallBreak:  Duration = 0.44f; Gain = 0.72f; break;
		}

		const int32 SampleCount = FMath::CeilToInt(Duration * SampleRate);
		TArray<int16> PCM;
		PCM.SetNumUninitialized(SampleCount);
		double PhaseA = 0.0;
		double PhaseB = 0.0;
		for (int32 i = 0; i < SampleCount; ++i)
		{
			const float T = static_cast<float>(i) / SampleRate;
			const float P = T / Duration;
			const float Noise = DeterministicNoise(i + static_cast<int32>(Cue) * 7919);
			float Sample = 0.f;

			switch (Cue)
			{
			case EKineticCue::Boost:
			{
				const float Attack = FMath::Min(T / 0.025f, 1.f);
				const float Envelope = Attack * FMath::Pow(1.f - P, 1.35f);
				const float Frequency = 150.f + 1050.f * P * P;
				PhaseA += 2.0 * PI * Frequency / SampleRate;
				PhaseB += 2.0 * PI * (Frequency * 0.48f + 70.f) / SampleRate;
				Sample = Envelope * (0.58f * FMath::Sin(PhaseA) + 0.22f * FMath::Sin(PhaseB) + 0.16f * Noise);
				break;
			}
			case EKineticCue::GlassBreak:
			{
				const float Brittle = FMath::Exp(-T * 13.f);
				PhaseA += 2.0 * PI * (2300.f + 1700.f * P) / SampleRate;
				PhaseB += 2.0 * PI * (4100.f - 900.f * P) / SampleRate;
				Sample = Noise * (0.62f * Brittle + 0.10f * FMath::Exp(-T * 3.2f))
					+ 0.28f * FMath::Sin(PhaseA) * Brittle + 0.16f * FMath::Sin(PhaseB) * FMath::Exp(-T * 8.f);
				break;
			}
			case EKineticCue::Warp:
			{
				const float Attack = FMath::Min(T / 0.045f, 1.f);
				const float Release = FMath::Min((Duration - T) / 0.09f, 1.f);
				const float Frequency = 240.f + 760.f * FMath::Abs(2.f * P - 1.f);
				PhaseA += 2.0 * PI * Frequency / SampleRate;
				PhaseB += 2.0 * PI * (Frequency * 1.51f) / SampleRate;
				Sample = Attack * Release * (0.55f * FMath::Sin(PhaseA) + 0.22f * FMath::Sin(PhaseB));
				break;
			}
			case EKineticCue::WallBreak:
			{
				PhaseA += 2.0 * PI * (96.f - 28.f * P) / SampleRate;
				PhaseB += 2.0 * PI * (690.f - 310.f * P) / SampleRate;
				Sample = 0.58f * Noise * FMath::Exp(-T * 7.f)
					+ 0.52f * FMath::Sin(PhaseA) * FMath::Exp(-T * 5.f)
					+ 0.20f * FMath::Sin(PhaseB) * FMath::Exp(-T * 11.f);
				break;
			}
			}

			Sample *= 1.35f;
			Sample = (Sample / (1.f + FMath::Abs(Sample))) * Gain;
			PCM[i] = static_cast<int16>(FMath::Clamp(Sample, -1.f, 1.f) * 32760.f);
		}

		USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(Context);
		Wave->SetSampleRate(SampleRate);
		Wave->SetNumFrames(SampleCount);
		Wave->NumChannels = 1;
		Wave->Duration = Duration;
		Wave->bLooping = false;
		Wave->QueueAudio(reinterpret_cast<const uint8*>(PCM.GetData()), PCM.Num() * sizeof(int16));
		UGameplayStatics::PlaySoundAtLocation(Context, Wave, WorldLocation);
	}

	UInstancedStaticMeshComponent* MakeISM(AActor* Owner, const TCHAR* Name)
	{
		UInstancedStaticMeshComponent* ISM = Owner->CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		ISM->SetMobility(EComponentMobility::Movable);
		return ISM;
	}
}

AMazeGenerator::AMazeGenerator()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SceneRoot;

	Walls = MakeISM(this, TEXT("Walls"));
	Walls->SetupAttachment(RootComponent);
	Walls->SetCastShadow(true);
	WallTrim = MakeISM(this, TEXT("WallTrim"));
	WallTrim->SetupAttachment(RootComponent);
	WallTrim->SetCastShadow(true);
	WallTrim->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Hurdles = MakeISM(this, TEXT("Hurdles"));
	Hurdles->SetupAttachment(RootComponent);
	Hurdles->SetCastShadow(true);
	FloorTiles = MakeISM(this, TEXT("FloorTiles"));
	FloorTiles->SetupAttachment(RootComponent);
	FloorTilesAlt = MakeISM(this, TEXT("FloorTilesAlt"));
	FloorTilesAlt->SetupAttachment(RootComponent);
	HolePits = MakeISM(this, TEXT("HolePits"));
	HolePits->SetupAttachment(RootComponent);
	HolePits->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	IceTiles = MakeISM(this, TEXT("IceTiles"));
	IceTiles->SetupAttachment(RootComponent);
	MudTiles = MakeISM(this, TEXT("MudTiles"));
	MudTiles->SetupAttachment(RootComponent);
	IceDetails = MakeISM(this, TEXT("IceDetails"));
	IceDetails->SetupAttachment(RootComponent);
	IceDetails->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	IceDetails->SetCastShadow(false);
	MudDetails = MakeISM(this, TEXT("MudDetails"));
	MudDetails->SetupAttachment(RootComponent);
	MudDetails->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MudDetails->SetCastShadow(false);
	BoostTiles = MakeISM(this, TEXT("BoostTiles"));
	BoostTiles->SetupAttachment(RootComponent);
	PadArrows = MakeISM(this, TEXT("PadArrows"));
	PadArrows->SetupAttachment(RootComponent);
	PadArrows->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PadArrows->SetCastShadow(false);
	CrumbleTiles = MakeISM(this, TEXT("CrumbleTiles"));
	CrumbleTiles->SetupAttachment(RootComponent);
	GlassCracks = MakeISM(this, TEXT("GlassCracks"));
	GlassCracks->SetupAttachment(RootComponent);
	GlassCracks->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GlassCracks->SetCastShadow(false);
	GlassShards = MakeISM(this, TEXT("GlassShards"));
	GlassShards->SetupAttachment(RootComponent);
	GlassShards->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GlassShards->SetCastShadow(false);
	BreakableWalls = MakeISM(this, TEXT("BreakableWalls"));
	BreakableWalls->SetupAttachment(RootComponent);
	BreakableWalls->SetCastShadow(true);
	BreakableWalls->SetNotifyRigidBodyCollision(true);
	BreakableWallDetails = MakeISM(this, TEXT("BreakableWallDetails"));
	BreakableWallDetails->SetupAttachment(RootComponent);
	BreakableWallDetails->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BreakableWallDetails->SetCastShadow(false);
	BreakableDebris = MakeISM(this, TEXT("BreakableDebris"));
	BreakableDebris->SetupAttachment(RootComponent);
	BreakableDebris->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BreakableDebris->SetCastShadow(false);
	DoorBlocks = MakeISM(this, TEXT("DoorBlocks"));
	DoorBlocks->SetupAttachment(RootComponent);
	DoorBlocks->SetCastShadow(true);
	ButtonPads = MakeISM(this, TEXT("ButtonPads"));
	ButtonPads->SetupAttachment(RootComponent);
	WarpDiscs = MakeISM(this, TEXT("WarpDiscs"));
	WarpDiscs->SetupAttachment(RootComponent);
	WarpDiscs->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WarpRings = MakeISM(this, TEXT("WarpRings"));
	WarpRings->SetupAttachment(RootComponent);
	WarpRings->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WarpRings->SetCastShadow(false);
	WarpBeacons = MakeISM(this, TEXT("WarpBeacons"));
	WarpBeacons->SetupAttachment(RootComponent);
	WarpBeacons->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WarpBeacons->SetCastShadow(false);
	AccentDetails = MakeISM(this, TEXT("AccentDetails"));
	AccentDetails->SetupAttachment(RootComponent);
	AccentDetails->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AccentDetails->SetCastShadow(false);
	HazardDetails = MakeISM(this, TEXT("HazardDetails"));
	HazardDetails->SetupAttachment(RootComponent);
	HazardDetails->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HazardDetails->SetCastShadow(false);

	// Surface panels are decoration on top of the collision base, so nothing here
	// changes how the ball rolls - the gimmicks read the layout, not physics materials.
	FloorTilesAlt->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	IceTiles->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MudTiles->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoostTiles->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Goal = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Goal"));
	Goal->SetupAttachment(RootComponent);
	Goal->SetMobility(EComponentMobility::Movable);
	Goal->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Goal->SetGenerateOverlapEvents(true);

	GoalBeacon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GoalBeacon"));
	GoalBeacon->SetupAttachment(Goal);
	GoalBeacon->SetMobility(EComponentMobility::Movable);
	GoalBeacon->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	GoalLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("GoalLight"));
	GoalLight->SetupAttachment(Goal);
	GoalLight->SetMobility(EComponentMobility::Movable);
	GoalLight->SetRelativeLocation(FVector(0.f, 0.f, 150.f));
	GoalLight->SetLightColor(FLinearColor(1.f, 0.58f, 0.12f));
	GoalLight->SetIntensity(2200.f);
	GoalLight->SetAttenuationRadius(560.f);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CubeMesh.Succeeded())
	{
		for (UInstancedStaticMeshComponent* ISM : { Walls.Get(), WallTrim.Get(), Hurdles.Get(), FloorTiles.Get(),
			FloorTilesAlt.Get(), HolePits.Get(), IceTiles.Get(), MudTiles.Get(), IceDetails.Get(), MudDetails.Get(),
			BoostTiles.Get(), PadArrows.Get(), CrumbleTiles.Get(), GlassCracks.Get(), GlassShards.Get(),
			BreakableWalls.Get(), BreakableWallDetails.Get(), BreakableDebris.Get(),
			DoorBlocks.Get(), WarpRings.Get(), WarpBeacons.Get(), AccentDetails.Get(), HazardDetails.Get() })
		{
			ISM->SetStaticMesh(CubeMesh.Object);
		}
		Goal->SetStaticMesh(CubeMesh.Object);
	}
	if (CylinderMesh.Succeeded())
	{
		// Discrete mechanisms use a circular silhouette. Ice and mud deliberately
		// keep the cube assigned above so adjacent cells join into full-floor zones.
		Goal->SetStaticMesh(CylinderMesh.Object);
		GoalBeacon->SetStaticMesh(CylinderMesh.Object);
		ButtonPads->SetStaticMesh(CylinderMesh.Object);
		WarpDiscs->SetStaticMesh(CylinderMesh.Object);
		HolePits->SetStaticMesh(CylinderMesh.Object);
		BoostTiles->SetStaticMesh(CylinderMesh.Object);
	}

	// ---------------- Default stages ----------------
	FMazeStageLayout Stage1;
	Stage1.StageName = TEXT("基礎区画");
	Stage1.VisualTheme = EMazeDesignTheme::KineticLab;
	Stage1.Rows = {
		TEXT("##########"),
		TEXT("#S.......#"),
		TEXT("#.####.#.#"),
		TEXT("#....#.#.#"),
		TEXT("####.#.#.#"),
		TEXT("#....#.#.#"),
		TEXT("#.####.#.#"),
		TEXT("#......#G#"),
		TEXT("##########")
	};

	FMazeStageLayout Stage2;
	Stage2.StageName = TEXT("砂岩回廊");
	Stage2.VisualTheme = EMazeDesignTheme::NordicGallery;
	Stage2.Rows = {
		TEXT("############"),
		TEXT("#S..#.H...##"),
		TEXT("###.#.#.#.##"),
		TEXT("#H#.#.#H#.##"),
		TEXT("#.#.###.#.##"),
		TEXT("#.#.#.H.#.##"),
		TEXT("#.#.#.###.##"),
		TEXT("#.....#...G#"),
		TEXT("############")
	};

	FMazeStageLayout Stage3;
	Stage3.StageName = TEXT("炉心区画");
	Stage3.VisualTheme = EMazeDesignTheme::NeonCircuit;
	Stage3.Rows = {
		TEXT("###############"),
		TEXT("#S......#.....#"),
		TEXT("#######H#####.#"),
		TEXT("#.#.....#...H.#"),
		TEXT("#.#O#####.###.#"),
		TEXT("#.#.#..O..#.#.#"),
		TEXT("#.#.###.###.#.#"),
		TEXT("#.#...#.#..H#.#"),
		TEXT("#.###H#.#.#.#O#"),
		TEXT("#.......#G#...#"),
		TEXT("###############")
	};

	FMazeStageLayout Stage4;
	Stage4.StageName = TEXT("低温区画");
	Stage4.VisualTheme = EMazeDesignTheme::AquaResearch;
	// A single winding corridor (not side-rooms) so the ice and mud patches are
	// mandatory, not optional detours - plus one warp pair as an optional
	// shortcut that skips the hurdle+mud stretch for players who find it.
	Stage4.Rows = {
		TEXT("#################"),
		TEXT("#S.IIII..########"),
		TEXT("########.########"),
		TEXT("########.......##"),
		TEXT("#####W########.##"),
		TEXT("#..............##"),
		TEXT("#.###############"),
		TEXT("#H###############"),
		TEXT("#..MMMMMM.#######"),
		TEXT("#########.##W####"),
		TEXT("#########......##"),
		TEXT("##############.##"),
		TEXT("#G.............##")
	};

	FMazeStageLayout Stage5;
	Stage5.StageName = TEXT("加速区画");
	Stage5.VisualTheme = EMazeDesignTheme::SolarIndustrial;
	Stage5.Rows = {
		TEXT("#################"),
		TEXT("#S.>X.C.#......C#"),
		TEXT("#######.#.#####.#"),
		TEXT("#...#.#..^#..G#.#"),
		TEXT("#.#.#.#####C###.#"),
		TEXT("#^#C....>...#v..#"),
		TEXT("#.###.#####.#.###"),
		TEXT("#.#...#..<#.#...#"),
		TEXT("#.#####.#.#####.#"),
		TEXT("#...C...#.....C.#"),
		TEXT("#.###############"),
		TEXT("#...............#"),
		TEXT("#################")
	};

	FMazeStageLayout Stage6;
	Stage6.StageName = TEXT("中央制御室");
	Stage6.VisualTheme = EMazeDesignTheme::MonochromeMuseum;
	Stage6.Rows = {
		TEXT("###############"),
		TEXT("#S.H..#......G#"),
		TEXT("#.###C#######D#"),
		TEXT("#...#W#.......#"),
		TEXT("###.###.#####.#"),
		TEXT("#.....#.#...#H#"),
		TEXT("#.###.#.#.#.#.#"),
		TEXT("#...#.#W..#...#"),
		TEXT("#.#.#####.#.###"),
		TEXT("#.....#...CB..#"),
		TEXT("###############")
	};

	Stages = { Stage1, Stage2, Stage3, Stage4, Stage5, Stage6 };
	MazeLayout = Stage1.Rows;

	// ---------------- Tutorial: one gimmick per tiny stage ----------------
	// Same order the gimmicks first appear in the main stages, so the tutorial
	// doubles as a preview of what is coming.
	auto MakeLesson = [](const TCHAR* Name, const TCHAR* Hint, int32 Icon, TArray<FString>&& Rows)
	{
		FMazeStageLayout L;
		L.StageName = Name;
		L.HintText = Hint;
		L.HintIcon = Icon;
		L.Rows = MoveTemp(Rows);
		return L;
	};

	TutorialStages = {
		MakeLesson(TEXT("移動"), TEXT("端末を傾ける"), 0,
			{ TEXT("##########"),
			  TEXT("#S.......#"),
			  TEXT("########.#"),
			  TEXT("#G.......#"),
			  TEXT("##########") }),

		MakeLesson(TEXT("ジャンプ"), TEXT("端末を持ち上げる"), 1,
			{ TEXT("##########"),
			  TEXT("#S...H..G#"),
			  TEXT("##########") }),

		MakeLesson(TEXT("落下"), TEXT("穴を避ける"), 2,
			{ TEXT("##########"),
			  TEXT("#...O....#"),
			  TEXT("#S......G#"),
			  TEXT("#....O...#"),
			  TEXT("##########") }),

		MakeLesson(TEXT("氷床"), TEXT("滑りやすい"), 3,
			{ TEXT("##########"),
			  TEXT("#S.IIII.G#"),
			  TEXT("##########") }),

		MakeLesson(TEXT("泥床"), TEXT("減速する"), 4,
			{ TEXT("##########"),
			  TEXT("#S.MMMM.G#"),
			  TEXT("##########") }),

		MakeLesson(TEXT("加速"), TEXT("矢印方向へ加速"), 5,
			{ TEXT("##########"),
			  TEXT("#S..>>..G#"),
			  TEXT("##########") }),

		MakeLesson(TEXT("ガラス床"), TEXT("衝撃で割れる"), 6,
			{ TEXT("##########"),
			  TEXT("#S.CCCC.G#"),
			  TEXT("##########") }),

		MakeLesson(TEXT("スイッチ"), TEXT("作動後に扉が開く"), 7,
			{ TEXT("##########"),
			  TEXT("#S......B#"),
			  TEXT("#.########"),
			  TEXT("#..D....G#"),
			  TEXT("##########") }),

		MakeLesson(TEXT("ワープ"), TEXT("接触地点間を移動"), 8,
			{ TEXT("##########"),
			  TEXT("#S..W....#"),
			  TEXT("##########"),
			  TEXT("#G..W....#"),
			  TEXT("##########") })
	};
}

TCHAR AMazeGenerator::GetCellChar(int32 Row, int32 Col) const
{
	if (Row < 0 || Row >= MazeLayout.Num())
	{
		return TEXT('#');
	}
	const FString& Line = MazeLayout[Row];
	if (Col < 0 || Col >= Line.Len())
	{
		return TEXT('#');
	}
	return Line[Col];
}

void AMazeGenerator::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyTheme();
	Rebuild();
}

void AMazeGenerator::BeginPlay()
{
	Super::BeginPlay();

#if !UE_BUILD_SHIPPING
	// Allows device-side visual QA of every stage without completing the earlier
	// mazes: launch a Development build with -MazeStage=1..6.
	int32 RequestedStage = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("MazeStage="), RequestedStage))
	{
		StartAtStage = FMath::Clamp(RequestedStage - 1, 0, FMath::Max(Stages.Num() - 1, 0));
	}
#endif

	Flow = EMazeFlow::Home;
	bTutorialMode = false;
	CurrentStage = FMath::Clamp(StartAtStage, 0, FMath::Max(Stages.Num() - 1, 0));
	TransitionMessage.Empty();
	ApplyStage(CurrentStage);

	const TCHAR* RankingSlot = TEXT("MazeTimes");
	SaveGameData = Cast<UMazeSaveGame>(UGameplayStatics::LoadGameFromSlot(RankingSlot, 0));
	if (!SaveGameData)
	{
		SaveGameData = Cast<UMazeSaveGame>(UGameplayStatics::CreateSaveGameObject(UMazeSaveGame::StaticClass()));
	}

	if (!Goal->OnComponentBeginOverlap.IsAlreadyBound(this, &AMazeGenerator::HandleGoalOverlap))
	{
		Goal->OnComponentBeginOverlap.AddDynamic(this, &AMazeGenerator::HandleGoalOverlap);
	}
	if (!BreakableWalls->OnComponentHit.IsAlreadyBound(this, &AMazeGenerator::HandleBreakableWallHit))
	{
		BreakableWalls->OnComponentHit.AddDynamic(this, &AMazeGenerator::HandleBreakableWallHit);
	}
}

void AMazeGenerator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bImpactSlowMotionActive && FPlatformTime::Seconds() >= ImpactSlowMotionEndRealTime)
	{
		UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
		bImpactSlowMotionActive = false;
	}

	const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const float Pulse = 0.5f + 0.5f * FMath::Sin(T * 4.f);

	if (GoalBeacon)
	{
		const float S = 0.42f + 0.16f * Pulse;
		GoalBeacon->SetRelativeScale3D(FVector(S, S, 0.045f));
	}
	if (GoalLight)
	{
		GoalLight->SetIntensity(1200.f + 1600.f * Pulse);
	}
	if (GoalMID)
	{
		GoalMID->SetVectorParameterValue(TEXT("Color"), AccentColor * (0.65f + 0.35f * Pulse));
	}

	if (HasActorBegunPlay())
	{
		UpdateGimmicks(DeltaSeconds);

		// Ball is only controllable while actually playing (not on Home / clear screens)
		if (AMazeBallPawn* Ball = Cast<AMazeBallPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
		{
			Ball->SetControlEnabled(Flow == EMazeFlow::Playing && !bTransitioning);
		}
	}
}

void AMazeGenerator::StartImpactSlowMotion(double RealSeconds, float TimeDilation)
{
	if (!GetWorld())
	{
		return;
	}
	UGameplayStatics::SetGlobalTimeDilation(this, FMath::Clamp(TimeDilation, 0.05f, 1.f));
	bImpactSlowMotionActive = true;
	ImpactSlowMotionEndRealTime = FMath::Max(ImpactSlowMotionEndRealTime, FPlatformTime::Seconds() + RealSeconds);
}

void AMazeGenerator::ResetBallToStart()
{
	if (AMazeBallPawn* Ball = Cast<AMazeBallPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
	{
		Ball->ResetTo(StartLocation);
	}
}

void AMazeGenerator::StartGame()
{
	bTutorialMode = false;
	CurrentStage = FMath::Clamp(StartAtStage, 0, FMath::Max(Stages.Num() - 1, 0));
	// Fresh run: clear this run's clocked times (-1 = not cleared yet). Only a
	// true all-clear from here submits anything to the persisted ranking.
	RunStageTimes.Init(-1.f, Stages.Num());
	StageStartTime = GetWorld()->GetTimeSeconds();
	ApplyStage(CurrentStage);
	ResetBallToStart();
#if !UE_BUILD_SHIPPING
	// Device-side composition check for the portal without having to replay the
	// route. This is inert during ordinary launches and absent from Shipping.
	if (FParse::Param(FCommandLine::Get(), TEXT("MazeReviewWarp")) && WarpExitLocal.Num() > 0)
	{
		if (AMazeBallPawn* Ball = Cast<AMazeBallPawn>(UGameplayStatics::GetPlayerPawn(this, 0)))
		{
			Ball->ResetTo(GetActorTransform().TransformPosition(WarpExitLocal[0]));
		}
	}
#endif
	Flow = EMazeFlow::Playing;
	OnStageStarted(CurrentStage);
}

void AMazeGenerator::StartTutorial()
{
	if (TutorialStages.Num() == 0)
	{
		StartGame();
		return;
	}
	bTutorialMode = true;
	CurrentStage = 0;
	StageStartTime = GetWorld()->GetTimeSeconds();
	ApplyStage(CurrentStage);
	ResetBallToStart();
	Flow = EMazeFlow::Playing;
	OnStageStarted(CurrentStage);
}

void AMazeGenerator::ChooseNextStage()
{
	CurrentStage = (CurrentStage + 1) % FMath::Max(GetStageCount(), 1);
	StageStartTime = GetWorld()->GetTimeSeconds();
	ApplyStage(CurrentStage);
	ResetBallToStart();
	Flow = EMazeFlow::Playing;
	OnStageStarted(CurrentStage);
}

void AMazeGenerator::GoHome()
{
	Flow = EMazeFlow::Home;
	bTutorialMode = false;
	TransitionMessage.Empty();
	CurrentStage = FMath::Clamp(StartAtStage, 0, FMath::Max(Stages.Num() - 1, 0));
	ApplyStage(CurrentStage);
	ResetBallToStart();
}

int32 AMazeGenerator::GetStageCount() const
{
	return ActiveStages().Num();
}

FString AMazeGenerator::GetStageHint() const
{
	const TArray<FMazeStageLayout>& List = ActiveStages();
	return List.IsValidIndex(CurrentStage) ? List[CurrentStage].HintText : FString();
}

int32 AMazeGenerator::GetStageHintIcon() const
{
	const TArray<FMazeStageLayout>& List = ActiveStages();
	return List.IsValidIndex(CurrentStage) ? List[CurrentStage].HintIcon : -1;
}

void AMazeGenerator::UpdateGimmicks(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	const float Now = World->GetTimeSeconds();

	UpdateBoostArrows(Now);
	UpdateWarpVisuals(Now);

	// ---- Door animation ----
	if (bDoorsOpen && DoorAnim < 1.f)
	{
		DoorAnim = FMath::Min(DoorAnim + DeltaSeconds, 1.f);
		const float Ease = DoorAnim * DoorAnim * (3.f - 2.f * DoorAnim);
		for (int32 i = 0; i < DoorOriginal.Num(); ++i)
		{
			FTransform Tr = DoorOriginal[i];
			Tr.AddToTranslation(FVector(0.f, 0.f, -(WallHeight + 30.f) * Ease));
			DoorBlocks->UpdateInstanceTransform(i, Tr, false, true, true);
		}
	}

	// ---- Glass fracture timers ----
	for (auto It = CrumbleTriggerTime.CreateIterator(); It; ++It)
	{
		const int32 Idx = It.Key();
		const float Elapsed = Now - It.Value();
		const float EffectiveElapsed = Elapsed + CrumbleShockBonus.FindRef(Idx);
		const float Progress = FMath::Clamp(EffectiveElapsed / FMath::Max(CrumbleDelay, 0.05f), 0.f, 1.f);
		if (EffectiveElapsed >= CrumbleDelay)
		{
			FTransform Tr = CrumbleOriginal[Idx];
			Tr.AddToTranslation(FVector(0.f, 0.f, -3000.f));
			CrumbleTiles->UpdateInstanceTransform(Idx, Tr, false, true, true);

			if (const TArray<FTransform>* Cracks = GlassCrackOriginal.Find(Idx))
			{
				const int32 FirstCrack = Idx * 5;
				for (int32 Crack = 0; Crack < Cracks->Num(); ++Crack)
				{
					FTransform Hidden = (*Cracks)[Crack];
					Hidden.SetScale3D(FVector::ZeroVector);
					GlassCracks->UpdateInstanceTransform(FirstCrack + Crack, Hidden, false, false, true);
				}
			}

			// Six bright slivers fan out and fall. They are visual-only; the single
			// pane above remains the authoritative collision surface.
			const FVector PaneCenter = CrumbleOriginal[Idx].GetLocation() + FVector(0.f, 0.f, 14.f);
			PlayKineticCue(this, GetActorTransform().TransformPosition(PaneCenter), EKineticCue::GlassBreak);
			StartImpactSlowMotion(0.085, 0.22f);
			for (int32 Shard = 0; Shard < 6; ++Shard)
			{
				const float Angle = (2.f * PI * Shard / 6.f) + Idx * 0.37f;
				FTransform ShardTransform;
				ShardTransform.SetLocation(PaneCenter + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (24.f + Shard * 4.f));
				ShardTransform.SetRotation(FQuat(FRotator(0.f, FMath::RadiansToDegrees(Angle) + 18.f, 0.f)));
				ShardTransform.SetScale3D(FVector(0.42f, 0.15f + 0.025f * Shard, 0.035f));
				FGlassShardAnim Anim;
				Anim.InstanceIndex = GlassShards->AddInstance(ShardTransform);
				Anim.StartTransform = ShardTransform;
				Anim.Drift = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.45f) * (95.f + Shard * 13.f);
				Anim.StartTime = Now;
				Anim.Spin = 150.f + Shard * 37.f;
				ActiveGlassShards.Add(Anim);
			}
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
			{
				PC->PlayDynamicForceFeedback(0.82f, 0.14f, true, true, true, true);
			}
			CrumbleRespawnAt.Add(Idx, Now + CrumbleRespawnSeconds);
			CrumbleShockBonus.Remove(Idx);
			It.RemoveCurrent();
		}
		else
		{
			// Mechanical vibration and progressively branching cyan fractures.
			FTransform Tr = CrumbleOriginal[Idx];
			Tr.AddToTranslation(FVector(0.f, 0.f, Progress * 3.5f * FMath::Sin(EffectiveElapsed * 52.f)));
			CrumbleTiles->UpdateInstanceTransform(Idx, Tr, false, true, false);
			if (const TArray<FTransform>* Cracks = GlassCrackOriginal.Find(Idx))
			{
				const int32 FirstCrack = Idx * 5;
				for (int32 Crack = 0; Crack < Cracks->Num(); ++Crack)
				{
					const float Branch = FMath::Clamp(Progress * 1.65f - Crack * 0.14f, 0.06f, 1.f);
					FTransform Growing = (*Cracks)[Crack];
					Growing.SetScale3D(Growing.GetScale3D() * Branch);
					GlassCracks->UpdateInstanceTransform(FirstCrack + Crack, Growing, false, false, true);
				}
			}
		}
	}
	for (auto It = CrumbleRespawnAt.CreateIterator(); It; ++It)
	{
		if (Now >= It.Value())
		{
			CrumbleTiles->UpdateInstanceTransform(It.Key(), CrumbleOriginal[It.Key()], false, true, true);
			if (const TArray<FTransform>* Cracks = GlassCrackOriginal.Find(It.Key()))
			{
				const int32 FirstCrack = It.Key() * 5;
				for (int32 Crack = 0; Crack < Cracks->Num(); ++Crack)
				{
					FTransform Hairline = (*Cracks)[Crack];
					Hairline.SetScale3D(Hairline.GetScale3D() * 0.06f);
					GlassCracks->UpdateInstanceTransform(FirstCrack + Crack, Hairline, false, false, true);
				}
			}
			It.RemoveCurrent();
		}
	}
	for (int32 ShardIndex = ActiveGlassShards.Num() - 1; ShardIndex >= 0; --ShardIndex)
	{
		const FGlassShardAnim Anim = ActiveGlassShards[ShardIndex];
		const float Age = Now - Anim.StartTime;
		FTransform Tr = Anim.StartTransform;
		Tr.SetLocation(Anim.StartTransform.GetLocation() + Anim.Drift * Age + FVector(0.f, 0.f, -520.f * Age * Age));
		const FRotator StartRot = Anim.StartTransform.Rotator();
		Tr.SetRotation(FQuat(StartRot + FRotator(Anim.Spin * Age * 0.55f, Anim.Spin * Age, Anim.Spin * Age * 0.35f)));
		if (Age >= 1.15f)
		{
			Tr.SetScale3D(FVector::ZeroVector);
			ActiveGlassShards.RemoveAtSwap(ShardIndex);
		}
		GlassShards->UpdateInstanceTransform(Anim.InstanceIndex, Tr, false, false, true);
	}
	for (int32 DebrisIndex = ActiveBreakableDebris.Num() - 1; DebrisIndex >= 0; --DebrisIndex)
	{
		const FGlassShardAnim Anim = ActiveBreakableDebris[DebrisIndex];
		const float Age = Now - Anim.StartTime;
		FTransform Tr = Anim.StartTransform;
		Tr.SetLocation(Anim.StartTransform.GetLocation() + Anim.Drift * Age + FVector(0.f, 0.f, -430.f * Age * Age));
		const FRotator StartRot = Anim.StartTransform.Rotator();
		Tr.SetRotation(FQuat(StartRot + FRotator(Anim.Spin * Age, Anim.Spin * Age * 0.55f, Anim.Spin * Age * 0.82f)));
		if (Age >= 1.35f)
		{
			Tr.SetScale3D(FVector::ZeroVector);
			ActiveBreakableDebris.RemoveAtSwap(DebrisIndex);
		}
		BreakableDebris->UpdateInstanceTransform(Anim.InstanceIndex, Tr, false, false, true);
	}

	// ---- Ball / tile interactions ----
	AMazeBallPawn* Ball = Cast<AMazeBallPawn>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Ball || !Ball->BallMesh)
	{
		return;
	}

	// A jump from the floor has to clear a hurdle, but a jump taken while already
	// standing on one used to reach over the walls. Hand the ball an absolute
	// ceiling instead of tuning the impulse, so both cases stay correct.
	Ball->JumpCeilingZ = GetActorLocation().Z + WallHeight - JumpClearance;

	const FVector Rel = GetActorTransform().InverseTransformPosition(Ball->GetActorLocation());
	const int32 Col = FMath::FloorToInt(Rel.X / CellSize);
	const int32 Row = FMath::FloorToInt(Rel.Y / CellSize);
	const TCHAR Ch = GetCellChar(Row, Col);
	const bool bNearFloor = Rel.Z > -20.f && Rel.Z < 170.f;

	float ControlScale = 1.f;
	float Damping = 0.3f;
	bool bOnBoost = false;

	if (bNearFloor)
	{
		switch (Ch)
		{
		case TEXT('I'):
			ControlScale = IceControlScale;
			Damping = 0.02f;
			break;
		case TEXT('M'):
		{
			// Shaking the physical board loosens the mud for a brief moment. Touch
			// control still gets the predictable base behaviour.
			const float Escape = FMath::Clamp(Ball->GetMotionShock() / 6.f, 0.f, 1.f);
			ControlScale = FMath::Lerp(MudControlScale, 0.95f, Escape);
			Damping = FMath::Lerp(3.5f, 1.0f, Escape);
			break;
		}
		case TEXT('^'): case TEXT('v'): case TEXT('<'): case TEXT('>'):
		{
			bOnBoost = true;
			const FIntPoint BoostCell(Col, Row);
			if (BoostCell != LastBoostCueCell)
			{
				PlayKineticCue(this, Ball->GetActorLocation(), EKineticCue::Boost);
				LastBoostCueCell = BoostCell;
			}
			FVector Dir = FVector::ZeroVector;
			if (Ch == TEXT('>')) Dir = FVector(1, 0, 0);
			if (Ch == TEXT('<')) Dir = FVector(-1, 0, 0);
			if (Ch == TEXT('v')) Dir = FVector(0, 1, 0);
			if (Ch == TEXT('^')) Dir = FVector(0, -1, 0);
			Ball->BallMesh->AddForce(GetActorTransform().TransformVector(Dir) * BoostAccel, NAME_None, true);
			break;
		}
		case TEXT('C'):
		{
			const FIntPoint Cell(Col, Row);
			if (const int32* Idx = CrumbleIndexByCell.Find(Cell))
			{
				if (!CrumbleTriggerTime.Contains(*Idx) && !CrumbleRespawnAt.Contains(*Idx))
				{
					CrumbleTriggerTime.Add(*Idx, Now);
					CrumbleShockBonus.Add(*Idx, 0.f);
				}
				if (CrumbleTriggerTime.Contains(*Idx))
				{
					const float ShockRate = FMath::Clamp(Ball->GetMotionShock() * 0.45f, 0.f, 3.f);
					CrumbleShockBonus.FindOrAdd(*Idx) += DeltaSeconds * ShockRate;
				}
			}
			break;
		}
		case TEXT('B'):
			if (!bDoorsOpen)
			{
				bDoorsOpen = true;
				DoorAnim = 0.f;
				OnButtonPressed();
				if (ButtonMID)
				{
					ButtonMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.2f, 1.f, 0.35f));
				}
				for (int32 i = 0; i < ButtonOriginal.Num(); ++i)
				{
					FTransform Tr = ButtonOriginal[i];
					Tr.AddToTranslation(FVector(0.f, 0.f, -10.f));
					ButtonPads->UpdateInstanceTransform(i, Tr, false, true, true);
				}
			}
			break;
		default:
			break;
		}
	}
	if (!bOnBoost)
	{
		LastBoostCueCell = FIntPoint(MIN_int32, MIN_int32);
	}

	Ball->SurfaceControlScale = ControlScale;
	Ball->BallMesh->SetLinearDamping(Damping);
	Ball->bOnMud = bNearFloor && Ch == TEXT('M');
	Ball->bStreakActive = Ball->DistanceSinceLastWallHit >= 5.f * CellSize;

	// ---- Warp: fell into a warp hole ----
	if (Rel.Z < -100.f && Rel.Z > -700.f && Now - LastWarpTime > 0.8f)
	{
		for (int32 i = 0; i < WarpCells.Num(); ++i)
		{
			if (WarpCells[i].X == Col && WarpCells[i].Y == Row)
			{
				const int32 Partner = (i % 2 == 0) ? i + 1 : i - 1;
				if (WarpExitLocal.IsValidIndex(Partner))
				{
					LastWarpTime = Now;
					PlayKineticCue(this, Ball->GetActorLocation(), EKineticCue::Warp);
					Ball->ResetTo(GetActorTransform().TransformPosition(WarpExitLocal[Partner]));
				}
				break;
			}
		}
	}
}

void AMazeGenerator::HandleBreakableWallHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	AMazeBallPawn* Ball = Cast<AMazeBallPawn>(OtherActor);
	if (!Ball || !Ball->BallMesh || BreakableWallGroups.Num() == 0)
	{
		return;
	}

	int32 GroupIndex = INDEX_NONE;
	if (const int32* Found = BreakableGroupByInstance.Find(Hit.Item))
	{
		GroupIndex = *Found;
	}
	else
	{
		// Some Android physics backends omit the ISM item id. Resolve the nearest
		// group from the contact point so the feature remains deterministic.
		const FVector LocalHit = GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
		float BestDistanceSq = TNumericLimits<float>::Max();
		for (int32 i = 0; i < BreakableWallGroups.Num(); ++i)
		{
			const float DistanceSq = FVector::DistSquared(LocalHit, BreakableWallGroups[i].LocalCenter);
			if (DistanceSq < BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				GroupIndex = i;
			}
		}
	}

	if (!BreakableWallGroups.IsValidIndex(GroupIndex) || BreakableWallGroups[GroupIndex].bBroken)
	{
		return;
	}

	const FVector Velocity = Ball->BallMesh->GetPhysicsLinearVelocity();
	const float NormalSpeed = FMath::Abs(FVector::DotProduct(Velocity, Hit.ImpactNormal));
	const float BreakPower = NormalSpeed + Ball->GetMotionShock() * BreakableWallShockScale;
	if (BreakPower < BreakableWallImpactSpeed)
	{
		return;
	}

	FBreakableWallGroup& Group = BreakableWallGroups[GroupIndex];
	Group.bBroken = true;
	for (int32 i = 0; i < Group.WallInstances.Num(); ++i)
	{
		FTransform Hidden = Group.WallOriginal[i];
		Hidden.AddToTranslation(FVector(0.f, 0.f, -3000.f));
		BreakableWalls->UpdateInstanceTransform(Group.WallInstances[i], Hidden, false, true, true);
	}
	for (int32 i = 0; i < Group.DetailInstances.Num(); ++i)
	{
		FTransform Hidden = Group.DetailOriginal[i];
		Hidden.SetScale3D(FVector::ZeroVector);
		BreakableWallDetails->UpdateInstanceTransform(Group.DetailInstances[i], Hidden, false, false, true);
	}

	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 Piece = 0; Piece < 9; ++Piece)
	{
		const float Angle = (2.f * PI * Piece / 9.f) + GroupIndex * 0.61f;
		FTransform PieceTransform;
		PieceTransform.SetLocation(Group.LocalCenter + FVector(
			FMath::Cos(Angle) * (18.f + Piece * 2.f),
			FMath::Sin(Angle) * (18.f + Piece * 2.f),
			(Piece % 3 - 1) * 30.f));
		PieceTransform.SetRotation(FQuat(FRotator(Piece * 13.f, FMath::RadiansToDegrees(Angle), Piece * 19.f)));
		PieceTransform.SetScale3D(FVector(
			0.24f + (Piece % 2) * 0.10f,
			0.12f + (Piece % 3) * 0.055f,
			0.16f + ((Piece + 1) % 3) * 0.045f));
		FGlassShardAnim Anim;
		Anim.InstanceIndex = BreakableDebris->AddInstance(PieceTransform);
		Anim.StartTransform = PieceTransform;
		Anim.Drift = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.72f) * (150.f + Piece * 13.f);
		Anim.StartTime = Now;
		Anim.Spin = 155.f + Piece * 31.f;
		ActiveBreakableDebris.Add(Anim);
	}

	const FVector WallWorld = GetActorTransform().TransformPosition(Group.LocalCenter);
	PlayKineticCue(this, WallWorld, EKineticCue::WallBreak);
	StartImpactSlowMotion(0.06, 0.30f);
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->PlayDynamicForceFeedback(0.95f, 0.18f, true, true, true, true);
	}
	UE_LOG(LogTemp, Display, TEXT("Breakable wall %d destroyed (power %.1f)"), GroupIndex, BreakPower);
}

FString AMazeGenerator::GetStageName() const
{
	const TArray<FMazeStageLayout>& List = ActiveStages();
	if (List.IsValidIndex(CurrentStage) && !List[CurrentStage].StageName.IsEmpty())
	{
		return List[CurrentStage].StageName;
	}
	return FString::Printf(TEXT("Stage %d"), CurrentStage + 1);
}

FString AMazeGenerator::GetDesignName() const
{
	const EMazeDesignTheme Theme = ResolveDesignTheme();
	switch (Theme)
	{
	case EMazeDesignTheme::NordicGallery: return TEXT("WARM TITANIUM");
	case EMazeDesignTheme::NeonCircuit: return TEXT("DEEP CORE");
	case EMazeDesignTheme::AquaResearch: return TEXT("CRYO ARRAY");
	case EMazeDesignTheme::SolarIndustrial: return TEXT("COPPER FLUX");
	case EMazeDesignTheme::MonochromeMuseum: return TEXT("MONO CONTROL");
	default: return TEXT("KINETIC INSTRUMENT");
	}
}

EMazeDesignTheme AMazeGenerator::ResolveDesignTheme() const
{
	// Force the six comparison identities by stage index. This also upgrades maps
	// saved before VisualTheme was added, whose serialized arrays otherwise give
	// every stage the enum's KineticLab default.
	if (!bTutorialMode && CurrentStage >= 0 && CurrentStage <= 5)
	{
		return static_cast<EMazeDesignTheme>(CurrentStage);
	}

	const TArray<FMazeStageLayout>& List = ActiveStages();
	return List.IsValidIndex(CurrentStage)
		? List[CurrentStage].VisualTheme
		: EMazeDesignTheme::KineticLab;
}

void AMazeGenerator::ApplyTheme()
{
	const EMazeDesignTheme Theme = ResolveDesignTheme();

	FLinearColor SunTint = FLinearColor::White;
	FLinearColor IceColor(0.55f, 0.82f, 1.f);
	// Warm, visibly-brown default so mud reads as mud rather than dark rock; each
	// theme below brightens its own tint proportionally.
	FLinearColor MudColor(0.22f, 0.14f, 0.08f);
	FLinearColor DoorColor(0.32f, 0.24f, 0.11f);
	float GlowScale = 1.f;
	float WallRoughness = 0.16f;
	float FloorRoughness = 0.32f;

	// Six zones of one precision-instrument family. Neutral surfaces shift in
	// temperature, while the three functional colours never change meaning.
	switch (Theme)
	{
	case EMazeDesignTheme::NordicGallery:
		FloorBaseColor = FLinearColor(0.014f, 0.013f, 0.011f);
		FloorPanelColor = FLinearColor(0.105f, 0.09f, 0.07f);
		WallColor = FLinearColor(0.028f, 0.024f, 0.02f);
		WallCapColor = FLinearColor(0.058f, 0.05f, 0.041f);
		AccentColor = FLinearColor(1.f, 0.58f, 0.12f);
		HazardColor = FLinearColor(0.95f, 0.22f, 0.16f);
		TransportColor = FLinearColor(0.05f, 0.68f, 0.75f);
		WallCapInset = 14.f;
		NeutralFloorInset = 0.f;
		WallCapThickness = 9.f;
		SunTint = FLinearColor(1.f, 0.86f, 0.7f);
		MudColor = FLinearColor(0.30f, 0.20f, 0.11f);
		DoorColor = FLinearColor(0.38f, 0.24f, 0.1f);
		GlowScale = 0.82f;
		WallRoughness = 0.17f;
		FloorRoughness = 0.31f;
		break;

	case EMazeDesignTheme::NeonCircuit:
		FloorBaseColor = FLinearColor(0.003f, 0.005f, 0.011f);
		FloorPanelColor = FLinearColor(0.035f, 0.046f, 0.08f);
		WallColor = FLinearColor(0.011f, 0.009f, 0.024f);
		WallCapColor = FLinearColor(0.043f, 0.023f, 0.075f);
		AccentColor = FLinearColor(1.f, 0.44f, 0.03f);
		HazardColor = FLinearColor(1.f, 0.05f, 0.22f);
		TransportColor = FLinearColor(0.f, 0.85f, 1.f);
		WallCapInset = 10.f;
		NeutralFloorInset = 0.f;
		WallCapThickness = 7.f;
		SunTint = FLinearColor(0.48f, 0.58f, 1.f);
		IceColor = FLinearColor(0.25f, 0.65f, 1.f);
		MudColor = FLinearColor(0.10f, 0.04f, 0.13f);
		DoorColor = FLinearColor(0.18f, 0.035f, 0.3f);
		GlowScale = 1.12f;
		WallRoughness = 0.12f;
		FloorRoughness = 0.24f;
		break;

	case EMazeDesignTheme::AquaResearch:
		FloorBaseColor = FLinearColor(0.004f, 0.015f, 0.021f);
		FloorPanelColor = FLinearColor(0.043f, 0.095f, 0.105f);
		WallColor = FLinearColor(0.011f, 0.027f, 0.032f);
		WallCapColor = FLinearColor(0.032f, 0.068f, 0.074f);
		AccentColor = FLinearColor(1.f, 0.62f, 0.16f);
		HazardColor = FLinearColor(1.f, 0.24f, 0.2f);
		TransportColor = FLinearColor(0.02f, 0.82f, 0.95f);
		WallCapInset = 13.f;
		NeutralFloorInset = 0.f;
		WallCapThickness = 8.f;
		SunTint = FLinearColor(0.64f, 0.86f, 1.f);
		IceColor = FLinearColor(0.12f, 0.28f, 0.35f);
		MudColor = FLinearColor(0.11f, 0.22f, 0.2f);
		DoorColor = FLinearColor(0.06f, 0.32f, 0.3f);
		GlowScale = 0.95f;
		WallRoughness = 0.12f;
		FloorRoughness = 0.25f;
		break;

	case EMazeDesignTheme::SolarIndustrial:
		FloorBaseColor = FLinearColor(0.011f, 0.007f, 0.004f);
		FloorPanelColor = FLinearColor(0.1f, 0.052f, 0.021f);
		WallColor = FLinearColor(0.032f, 0.016f, 0.007f);
		WallCapColor = FLinearColor(0.07f, 0.032f, 0.011f);
		AccentColor = FLinearColor(1.f, 0.64f, 0.04f);
		HazardColor = FLinearColor(1.f, 0.12f, 0.04f);
		TransportColor = FLinearColor(0.08f, 0.62f, 1.f);
		WallCapInset = 10.f;
		NeutralFloorInset = 0.f;
		WallCapThickness = 6.f;
		SunTint = FLinearColor(1.f, 0.62f, 0.34f);
		IceColor = FLinearColor(0.3f, 0.68f, 1.f);
		MudColor = FLinearColor(0.26f, 0.09f, 0.025f);
		DoorColor = FLinearColor(0.42f, 0.15f, 0.025f);
		GlowScale = 0.98f;
		WallRoughness = 0.18f;
		FloorRoughness = 0.3f;
		break;

	case EMazeDesignTheme::MonochromeMuseum:
		FloorBaseColor = FLinearColor(0.011f, 0.014f, 0.018f);
		FloorPanelColor = FLinearColor(0.115f, 0.13f, 0.155f);
		WallColor = FLinearColor(0.023f, 0.028f, 0.037f);
		WallCapColor = FLinearColor(0.06f, 0.07f, 0.085f);
		AccentColor = FLinearColor(1.f, 0.72f, 0.02f);
		HazardColor = FLinearColor(0.96f, 0.12f, 0.16f);
		TransportColor = FLinearColor(0.02f, 0.55f, 1.f);
		WallCapInset = 16.f;
		NeutralFloorInset = 0.f;
		WallCapThickness = 10.f;
		SunTint = FLinearColor(0.88f, 0.94f, 1.f);
		IceColor = FLinearColor(0.16f, 0.25f, 0.38f);
		MudColor = FLinearColor(0.19f, 0.19f, 0.19f);
		DoorColor = FLinearColor(0.08f, 0.085f, 0.09f);
		GlowScale = 0.82f;
		WallRoughness = 0.1f;
		FloorRoughness = 0.24f;
		break;

	default: // Modern Kinetic Lab
		FloorBaseColor = FLinearColor(0.007f, 0.011f, 0.017f);
		FloorPanelColor = FLinearColor(0.08f, 0.1f, 0.13f);
		WallColor = FLinearColor(0.018f, 0.026f, 0.038f);
		WallCapColor = FLinearColor(0.045f, 0.058f, 0.075f);
		AccentColor = FLinearColor(1.f, 0.56f, 0.09f);
		HazardColor = FLinearColor(1.f, 0.2f, 0.15f);
		TransportColor = FLinearColor(0.04f, 0.7f, 1.f);
		WallCapInset = 13.f;
		NeutralFloorInset = 0.f;
		WallCapThickness = 8.f;
		break;
	}

	// The authored tile material contains procedural noise. Even on merged meshes
	// that noise reads as stone blocks on a phone. BasicShapeMaterial is a plain,
	// texture-free PBR surface with Color and Roughness parameters, which gives
	// every theme a clean continuous finish and a controlled specular highlight.
	UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!BaseMat)
	{
		BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_MazeTile.M_MazeTile"));
	}
	if (!BaseMat)
	{
		return;
	}
	UMaterialInterface* GlowMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_MazeGlow.M_MazeGlow"));
	UMaterialInterface* Glow = GlowMat ? GlowMat : BaseMat;

	auto EnsureMID = [&](TObjectPtr<UMaterialInstanceDynamic>& MID, UInstancedStaticMeshComponent* ISM, UMaterialInterface* Base)
	{
		if (!MID)
		{
			MID = UMaterialInstanceDynamic::Create(Base, this);
			ISM->SetMaterial(0, MID);
		}
	};

	EnsureMID(WallMID, Walls, BaseMat);
	EnsureMID(TrimMID, WallTrim, BaseMat);
	EnsureMID(FloorMID, FloorTiles, BaseMat);
	EnsureMID(FloorAltMID, FloorTilesAlt, BaseMat);
	EnsureMID(HurdleMID, Hurdles, BaseMat);
	EnsureMID(PitMID, HolePits, BaseMat);
	EnsureMID(MudMID, MudTiles, BaseMat);
	EnsureMID(IceDetailMID, IceDetails, Glow);
	EnsureMID(MudDetailMID, MudDetails, BaseMat);
	EnsureMID(CrumbleMID, CrumbleTiles, BaseMat);
	EnsureMID(GlassCrackMID, GlassCracks, Glow);
	GlassShards->SetMaterial(0, GlassCrackMID);
	EnsureMID(BreakableMID, BreakableWalls, BaseMat);
	EnsureMID(BreakableGlowMID, BreakableWallDetails, Glow);
	BreakableDebris->SetMaterial(0, BreakableMID);
	EnsureMID(DoorMID, DoorBlocks, BaseMat);
	EnsureMID(IceMID, IceTiles, BaseMat);
	EnsureMID(BoostMID, BoostTiles, Glow);
	EnsureMID(PadArrowMID, PadArrows, Glow);
	EnsureMID(ButtonMID, ButtonPads, Glow);
	EnsureMID(WarpMID, WarpDiscs, Glow);
	WarpRings->SetMaterial(0, WarpMID);
	WarpBeacons->SetMaterial(0, WarpMID);
	EnsureMID(AccentMID, AccentDetails, Glow);
	EnsureMID(HazardMID, HazardDetails, Glow);
	if (!GoalMID)
	{
		GoalMID = UMaterialInstanceDynamic::Create(Glow, this);
		Goal->SetMaterial(0, GoalMID);
		GoalBeacon->SetMaterial(0, GoalMID);
	}

	// M_MazeGlow multiplies its Color into emissive, so glowing parts are fed a
	// dimmed colour and the multiplier does the work.
	auto Emissive = [](const FLinearColor& C, float Intensity) { return C * Intensity; };

	// Engine versions have shipped the plain shape material with either Color or
	// BaseColor as its exposed vector name. Setting both is harmless and avoids
	// falling back to a white surface (which made the light tint dominate).
	auto SetSurfaceColor = [](UMaterialInstanceDynamic* MID, const FLinearColor& C)
	{
		MID->SetVectorParameterValue(TEXT("Color"), C);
		MID->SetVectorParameterValue(TEXT("BaseColor"), C);
	};
	SetSurfaceColor(WallMID, WallColor);
	SetSurfaceColor(TrimMID, WallCapColor);
	SetSurfaceColor(FloorMID, FloorPanelColor);
	SetSurfaceColor(FloorAltMID, FloorBaseColor);
	SetSurfaceColor(HurdleMID, WallColor * 1.35f);
	SetSurfaceColor(PitMID, FLinearColor(0.005f, 0.005f, 0.008f));
	SetSurfaceColor(MudMID, MudColor);
	SetSurfaceColor(IceMID, IceColor);
	SetSurfaceColor(CrumbleMID, FLinearColor(0.055f, 0.20f, 0.28f));
	SetSurfaceColor(BreakableMID, FLinearColor(0.42f, 0.21f, 0.055f));
	SetSurfaceColor(MudDetailMID, MudColor * 0.38f);
	SetSurfaceColor(DoorMID, DoorColor);

	// Plain surfaces still need different tactile readings: walls are glossy and
	// reflective, while the floor remains slightly rougher so the ball is legible.
	WallMID->SetScalarParameterValue(TEXT("Roughness"), WallRoughness);
	TrimMID->SetScalarParameterValue(TEXT("Roughness"), 0.11f);
	TrimMID->SetScalarParameterValue(TEXT("Metallic"), 0.72f);
	WallMID->SetScalarParameterValue(TEXT("Metallic"), 0.28f);
	HurdleMID->SetScalarParameterValue(TEXT("Roughness"), WallRoughness + 0.04f);
	DoorMID->SetScalarParameterValue(TEXT("Roughness"), WallRoughness + 0.03f);
	FloorMID->SetScalarParameterValue(TEXT("Roughness"), FloorRoughness);
	FloorAltMID->SetScalarParameterValue(TEXT("Roughness"), 0.4f);
	MudMID->SetScalarParameterValue(TEXT("Roughness"), 0.8f);
	MudDetailMID->SetScalarParameterValue(TEXT("Roughness"), 1.f);
	IceMID->SetScalarParameterValue(TEXT("Roughness"), 0.04f);
	IceMID->SetScalarParameterValue(TEXT("Metallic"), 0.12f);
	CrumbleMID->SetScalarParameterValue(TEXT("Roughness"), 0.025f);
	CrumbleMID->SetScalarParameterValue(TEXT("Metallic"), 0.18f);
	BreakableMID->SetScalarParameterValue(TEXT("Roughness"), 0.31f);
	BreakableMID->SetScalarParameterValue(TEXT("Metallic"), 0.38f);
	PitMID->SetScalarParameterValue(TEXT("Roughness"), 0.48f);

	BoostMID->SetVectorParameterValue(TEXT("Color"), Emissive(TransportColor, 0.05f * GlowScale));
	PadArrowMID->SetVectorParameterValue(TEXT("Color"), Emissive(TransportColor, 0.75f * GlowScale));
	WarpMID->SetVectorParameterValue(TEXT("Color"), Emissive(TransportColor, 0.85f * GlowScale));
	ButtonMID->SetVectorParameterValue(TEXT("Color"), Emissive(AccentColor, 0.8f * GlowScale));
	AccentMID->SetVectorParameterValue(TEXT("Color"), Emissive(AccentColor, 0.7f * GlowScale));
	HazardMID->SetVectorParameterValue(TEXT("Color"), Emissive(HazardColor, 0.7f * GlowScale));
	IceDetailMID->SetVectorParameterValue(TEXT("Color"), Emissive(FLinearColor(0.48f, 0.9f, 1.f), 0.16f * GlowScale));
	GlassCrackMID->SetVectorParameterValue(TEXT("Color"), Emissive(FLinearColor(0.45f, 0.9f, 1.f), 0.52f * GlowScale));
	BreakableGlowMID->SetVectorParameterValue(TEXT("Color"), Emissive(FLinearColor(1.f, 0.48f, 0.06f), 0.82f * GlowScale));
	GoalMID->SetVectorParameterValue(TEXT("Color"), Emissive(AccentColor, 0.9f * GlowScale));
	GoalLight->SetLightColor(AccentColor);

	// A subtle lighting temperature is part of each comparison preset.
	if (UWorld* World = GetWorld())
	{
		TActorIterator<ADirectionalLight> It(World);
		if (It)
		{
			// Keep a small temperature shift, but let the material palette—not a
			// saturated global light—define each design and preserve wall/floor contrast.
			It->GetLightComponent()->SetLightColor(FMath::Lerp(FLinearColor::White, SunTint, 0.12f));
		}
	}
}

void AMazeGenerator::ApplyStage(int32 StageIndex)
{
	const TArray<FMazeStageLayout>& List = ActiveStages();
	if (List.IsValidIndex(StageIndex) && List[StageIndex].Rows.Num() > 0)
	{
		MazeLayout = List[StageIndex].Rows;
	}
	ApplyTheme();
	Rebuild();
}

void AMazeGenerator::Rebuild()
{
	for (UInstancedStaticMeshComponent* ISM : { Walls.Get(), WallTrim.Get(), Hurdles.Get(), FloorTiles.Get(),
		FloorTilesAlt.Get(), HolePits.Get(), IceTiles.Get(), MudTiles.Get(), IceDetails.Get(), MudDetails.Get(), BoostTiles.Get(),
		PadArrows.Get(), CrumbleTiles.Get(), GlassCracks.Get(), GlassShards.Get(), BreakableWalls.Get(),
		BreakableWallDetails.Get(), BreakableDebris.Get(), DoorBlocks.Get(), ButtonPads.Get(), WarpDiscs.Get(),
		WarpRings.Get(), WarpBeacons.Get(),
		AccentDetails.Get(), HazardDetails.Get() })
	{
		ISM->ClearInstances();
	}
	CrumbleIndexByCell.Empty();
	CrumbleOriginal.Empty();
	CrumbleTriggerTime.Empty();
	CrumbleShockBonus.Empty();
	CrumbleRespawnAt.Empty();
	GlassCrackOriginal.Empty();
	ActiveGlassShards.Empty();
	BreakableWallGroups.Empty();
	BreakableGroupByInstance.Empty();
	ActiveBreakableDebris.Empty();
	DoorOriginal.Empty();
	ButtonOriginal.Empty();
	bDoorsOpen = false;
	DoorAnim = 0.f;
	BoostArrows.Empty();
	WarpCells.Empty();
	WarpExitLocal.Empty();
	LastBoostCueCell = FIntPoint(MIN_int32, MIN_int32);
	if (bImpactSlowMotionActive)
	{
		UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
		bImpactSlowMotionActive = false;
		ImpactSlowMotionEndRealTime = 0.0;
	}
	if (ButtonMID)
	{
		ButtonMID->SetVectorParameterValue(TEXT("Color"), AccentColor * 0.72f);
	}

	const int32 NumRows = MazeLayout.Num();
	int32 NumCols = 0;
	for (const FString& Row : MazeLayout)
	{
		NumCols = FMath::Max(NumCols, Row.Len());
	}
	if (NumRows == 0 || NumCols == 0)
	{
		return;
	}

	const float CubeSize = 100.f;
	const float TileScaleXY = CellSize / CubeSize;
	const float FloorThickness = 20.f;

	auto TileBox = [&](UInstancedStaticMeshComponent* ISM, const FVector& Center, float SX, float SY, float SZ, float Yaw = 0.f) -> int32
	{
		FTransform T;
		T.SetLocation(Center);
		T.SetRotation(FQuat(FRotator(0.f, Yaw, 0.f)));
		T.SetScale3D(FVector(SX / CubeSize, SY / CubeSize, SZ / CubeSize));
		return ISM->AddInstance(T);
	};

	// Modern Kinetic Lab: ordinary floors are emitted as continuous horizontal
	// runs instead of one inset square per maze cell. Special surfaces remain
	// discrete inlays so their gameplay meaning is still visible immediately.
	const float PanelInset = 18.f;
	auto AddPanel = [&](UInstancedStaticMeshComponent* ISM, const FVector& FloorPos, float Lift = 0.f)
	{
		TileBox(ISM, FloorPos + FVector(0.f, 0.f, FloorThickness * 0.5f + 2.f + Lift),
			CellSize - PanelInset, CellSize - PanelInset, 4.f);
	};
	auto AddZoneSurface = [&](UInstancedStaticMeshComponent* ISM, const FVector& FloorPos, float Lift = 0.f)
	{
		// Slight overlap removes cell seams and lets a run of I/M cells read as one room.
		TileBox(ISM, FloorPos + FVector(0.f, 0.f, FloorThickness * 0.5f + 2.f + Lift),
			CellSize + 1.f, CellSize + 1.f, 5.f);
	};

	// A low, continuous studio deck masks the checkerboard floor stored in the
	// legacy level. The tighter plinth then makes the maze read as one instrument.
	TileBox(FloorTilesAlt,
		FVector(NumCols * CellSize * 0.5f, NumRows * CellSize * 0.5f, -109.f),
		12000.f, 12000.f, 20.f);
	TileBox(FloorTilesAlt,
		FVector(NumCols * CellSize * 0.5f, NumRows * CellSize * 0.5f, -58.f),
		NumCols * CellSize + 96.f, NumRows * CellSize + 96.f, 96.f);
	// Open corner brackets mark an interaction area without recreating the cell grid.
	auto AddCornerMarkers = [&](UInstancedStaticMeshComponent* ISM, const FVector& CellCenter,
		float Outer, float Length, float Thick, float Z)
	{
		for (int32 SX = -1; SX <= 1; SX += 2)
		{
			for (int32 SY = -1; SY <= 1; SY += 2)
			{
				TileBox(ISM, CellCenter + FVector(SX * (Outer - Length * 0.5f), SY * Outer, Z),
					Length, Thick, 4.f);
				TileBox(ISM, CellCenter + FVector(SX * Outer, SY * (Outer - Length * 0.5f), Z),
					Thick, Length, 4.f);
			}
		}
	};

	auto AddHoleRim = [&](const FVector& CellCenter)
	{
		const float HoleHalf = 85.f;
		const float RimW = CellSize * 0.5f - HoleHalf;
		const FVector ZOff(0.f, 0.f, -FloorThickness * 0.5f);
		TileBox(FloorTiles, CellCenter + FVector(0.f, -(HoleHalf + RimW * 0.5f), 0.f) + ZOff, CellSize, RimW, FloorThickness);
		TileBox(FloorTiles, CellCenter + FVector(0.f, +(HoleHalf + RimW * 0.5f), 0.f) + ZOff, CellSize, RimW, FloorThickness);
		TileBox(FloorTiles, CellCenter + FVector(-(HoleHalf + RimW * 0.5f), 0.f, 0.f) + ZOff, RimW, HoleHalf * 2.f, FloorThickness);
		TileBox(FloorTiles, CellCenter + FVector(+(HoleHalf + RimW * 0.5f), 0.f, 0.f) + ZOff, RimW, HoleHalf * 2.f, FloorThickness);
		const float D = HoleHalf * 0.88f;
		const float ChordLen = HoleHalf * 1.6f;
		const FVector Drop(0.f, 0.f, -1.5f);
		TileBox(FloorTiles, CellCenter + FVector(-D, -D, 0.f) + ZOff + Drop, ChordLen, 34.f, FloorThickness - 1.5f, -45.f);
		TileBox(FloorTiles, CellCenter + FVector(+D, -D, 0.f) + ZOff + Drop, ChordLen, 34.f, FloorThickness - 1.5f, +45.f);
		TileBox(FloorTiles, CellCenter + FVector(-D, +D, 0.f) + ZOff + Drop, ChordLen, 34.f, FloorThickness - 1.5f, +45.f);
		TileBox(FloorTiles, CellCenter + FVector(+D, +D, 0.f) + ZOff + Drop, ChordLen, 34.f, FloorThickness - 1.5f, -45.f);
	};

	// Merge rectangular groups of wall cells. This removes the repeated block
	// silhouette while preserving the exact same collision footprint.
	TArray<uint8> WallUsed;
	WallUsed.Init(0, NumRows * NumCols);
	for (int32 Row = 0; Row < NumRows; ++Row)
	{
		for (int32 Col = 0; Col < NumCols; ++Col)
		{
			const int32 First = Row * NumCols + Col;
			if (WallUsed[First] || GetCellChar(Row, Col) != TEXT('#'))
			{
				continue;
			}

			int32 Width = 0;
			while (Col + Width < NumCols && !WallUsed[Row * NumCols + Col + Width]
				&& GetCellChar(Row, Col + Width) == TEXT('#'))
			{
				++Width;
			}

			int32 Height = 1;
			while (Row + Height < NumRows)
			{
				bool bCanExtend = true;
				for (int32 X = 0; X < Width; ++X)
				{
					const int32 Index = (Row + Height) * NumCols + Col + X;
					if (WallUsed[Index] || GetCellChar(Row + Height, Col + X) != TEXT('#'))
					{
						bCanExtend = false;
						break;
					}
				}
				if (!bCanExtend)
				{
					break;
				}
				++Height;
			}

			for (int32 Y = 0; Y < Height; ++Y)
			{
				for (int32 X = 0; X < Width; ++X)
				{
					WallUsed[(Row + Y) * NumCols + Col + X] = 1;
				}
			}

			const float SizeX = Width * CellSize;
			const float SizeY = Height * CellSize;
			const FVector Center((Col + Width * 0.5f) * CellSize,
				(Row + Height * 0.5f) * CellSize, 0.f);
			// Three shallow inset tiers form a machined chamfer. They preserve the
			// continuous footprint but remove the hard toy-block silhouette.
			constexpr int32 BevelSteps = 3;
			const float BevelHeight = FMath::Max(WallCapThickness, 6.f);
			const float BodyHeight = WallHeight - BevelHeight * BevelSteps;
			TileBox(Walls, Center + FVector(0.f, 0.f, BodyHeight * 0.5f), SizeX, SizeY, BodyHeight);
			for (int32 Step = 0; Step < BevelSteps; ++Step)
			{
				const float Alpha = static_cast<float>(Step + 1) / static_cast<float>(BevelSteps);
				const float Inset = FMath::Min(WallCapInset * Alpha, FMath::Min(SizeX, SizeY) * 0.2f);
				TileBox(WallTrim,
					Center + FVector(0.f, 0.f, BodyHeight + BevelHeight * (Step + 0.5f)),
					SizeX - Inset * 2.f, SizeY - Inset * 2.f, BevelHeight);
			}
		}
	}

	// Greedily merge floor cells in both axes. Co-planar row strips can still show
	// hairline seams after mobile TAA/upscaling; larger rectangles remove those
	// edges and reduce the number of instances at the same time.
	auto HasFloorBase = [](TCHAR Ch)
	{
		return Ch != TEXT('#') && Ch != TEXT('O') && Ch != TEXT('W') && Ch != TEXT('C');
	};
	TArray<uint8> FloorUsed;
	FloorUsed.Init(0, NumRows * NumCols);
	for (int32 Row = 0; Row < NumRows; ++Row)
	{
		for (int32 Col = 0; Col < NumCols; ++Col)
		{
			const int32 First = Row * NumCols + Col;
			if (FloorUsed[First] || !HasFloorBase(GetCellChar(Row, Col)))
			{
				continue;
			}

			int32 Width = 0;
			while (Col + Width < NumCols
				&& !FloorUsed[Row * NumCols + Col + Width]
				&& HasFloorBase(GetCellChar(Row, Col + Width)))
			{
				++Width;
			}

			int32 Height = 1;
			while (Row + Height < NumRows)
			{
				bool bCanExtend = true;
				for (int32 X = 0; X < Width; ++X)
				{
					const int32 Index = (Row + Height) * NumCols + Col + X;
					if (FloorUsed[Index] || !HasFloorBase(GetCellChar(Row + Height, Col + X)))
					{
						bCanExtend = false;
						break;
					}
				}
				if (!bCanExtend)
				{
					break;
				}
				++Height;
			}

			for (int32 Y = 0; Y < Height; ++Y)
			{
				for (int32 X = 0; X < Width; ++X)
				{
					FloorUsed[(Row + Y) * NumCols + Col + X] = 1;
				}
			}

			const float SizeX = Width * CellSize;
			const float SizeY = Height * CellSize;
			const FVector Center((Col + Width * 0.5f) * CellSize,
				(Row + Height * 0.5f) * CellSize, -FloorThickness * 0.5f);
			TileBox(FloorTiles, Center, SizeX, SizeY, FloorThickness);
		}
	}

	bool bGoalFound = false;

	for (int32 Row = 0; Row < NumRows; ++Row)
	{
		for (int32 Col = 0; Col < NumCols; ++Col)
		{
			const TCHAR Ch = GetCellChar(Row, Col);
			const FVector CellCenter((Col + 0.5f) * CellSize, (Row + 0.5f) * CellSize, 0.f);
			const FVector FloorPos = CellCenter - FVector(0.f, 0.f, FloorThickness * 0.5f);

			switch (Ch)
			{
			case TEXT('#'):
				break;
			case TEXT('H'):
			{
				// A narrow cross-corridor beam reads as a hurdle rather than a block.
				const bool bCorridorNS = GetCellChar(Row, Col - 1) == TEXT('#')
					&& GetCellChar(Row, Col + 1) == TEXT('#');
				const float SX = bCorridorNS ? CellSize * 0.82f : 32.f;
				const float SY = bCorridorNS ? 32.f : CellSize * 0.82f;
				TileBox(Hurdles, CellCenter + FVector(0.f, 0.f, HurdleHeight * 0.5f), SX, SY, HurdleHeight);
				TileBox(AccentDetails, CellCenter + FVector(0.f, 0.f, HurdleHeight + 3.f),
					SX + 8.f, SY + 8.f, 7.f);
				break;
			}
			case TEXT('O'):
			{
				AddHoleRim(CellCenter);
				AddCornerMarkers(HazardDetails, CellCenter, 92.f, 42.f, 9.f, 12.f);
				TileBox(HolePits, CellCenter - FVector(0.f, 0.f, 450.f), CellSize * 0.9f, CellSize * 0.9f, 20.f);
				break;
			}
		case TEXT('W'):
			{
				AddHoleRim(CellCenter);
				WarpCells.Add(FIntPoint(Col, Row));
				// A bright surface just below the opening keeps the drop mechanic, while
				// the raised segmented halo and vertical beacons read as machinery rather
				// than as another hazard pit.
				FTransform T;
				T.SetLocation(CellCenter - FVector(0.f, 0.f, 48.f));
				T.SetScale3D(FVector(1.42f, 1.42f, 0.035f));
				WarpDiscs->AddInstance(T);

				for (int32 Segment = 0; Segment < 12; ++Segment)
				{
					const float Angle = 2.f * PI * static_cast<float>(Segment) / 12.f;
					const FVector Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
					TileBox(WarpRings, CellCenter + Radial * 103.f + FVector(0.f, 0.f, 18.f),
						42.f, 10.f, 8.f, FMath::RadiansToDegrees(Angle) + 90.f);
				}
				for (int32 Beacon = 0; Beacon < 4; ++Beacon)
				{
					const float Angle = 2.f * PI * static_cast<float>(Beacon) / 4.f + PI * 0.25f;
					const FVector Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
					TileBox(WarpBeacons, CellCenter + Radial * 72.f + FVector(0.f, 0.f, 72.f),
						7.f, 7.f, 82.f);
				}
				break;
			}
			case TEXT('I'):
				AddZoneSurface(IceTiles, FloorPos);
				// Sparse branching scratches catch the specular highlight without
				// drawing a border around every floor cell.
				if (((Row * 3 + Col) & 1) == 0)
				{
					const float Yaw = ((Row + Col) & 2) ? 28.f : -32.f;
					TileBox(IceDetails, CellCenter + FVector(-18.f, 8.f, 5.f), 112.f, 4.f, 2.f, Yaw);
					TileBox(IceDetails, CellCenter + FVector(35.f, -25.f, 5.f), 66.f, 3.f, 2.f, Yaw + 72.f);
				}
				break;
			case TEXT('M'):
				AddZoneSurface(MudTiles, FloorPos, -2.5f);
				// Irregular squashed clumps (placed/rotated from a per-cell Row/Col
				// hash, not straight grooves) read as churned, uneven earth.
				for (int32 Clump = 0; Clump < 4; ++Clump)
				{
					const int32 Hash = Row * 7 + Col * 13 + Clump * 5;
					const float OffsetX = static_cast<float>((Hash % 5) - 2) * 22.f;
					const float OffsetY = static_cast<float>(((Hash / 5) % 5) - 2) * 22.f;
					const float Yaw = static_cast<float>((Hash * 37) % 180);
					const float Size = 34.f + static_cast<float>(Hash % 4) * 6.f;
					TileBox(MudDetails, CellCenter + FVector(OffsetX, OffsetY, 3.f + static_cast<float>(Hash % 3)),
						Size, Size * 0.72f, 2.5f + static_cast<float>(Hash % 3), Yaw);
				}
				break;
			case TEXT('^'): case TEXT('v'): case TEXT('<'): case TEXT('>'):
			{
				AddPanel(BoostTiles, FloorPos);

				// The pad has to read as one big arrow, not a pattern. Three parts:
				//   1. a tail bar down the centre line, so there is an obvious "back"
				//   2. a big static arrowhead at the exit edge - fixes the direction
				//      even in a screenshot
				//   3. three chevrons that scroll toward the head (UpdateBoostArrows),
				//      which is what actually reads at a glance while playing
				float BaseYaw = 0.f;
				if (Ch == TEXT('v')) BaseYaw = 90.f;
				if (Ch == TEXT('<')) BaseYaw = 180.f;
				if (Ch == TEXT('^')) BaseYaw = -90.f;
				const FRotator BaseRot(0.f, BaseYaw, 0.f);

				const float HeadAlong = CellSize * 0.26f;

				// 1. tail bar, stopping where the head begins
				{
					const FVector Off = BaseRot.RotateVector(FVector(-CellSize * 0.12f, 0.f, 0.f));
					TileBox(PadArrows, CellCenter + Off + FVector(0.f, 0.f, 6.f),
						CellSize * 0.62f, 20.f, 12.f, BaseYaw);
				}

				// 2. static arrowhead: two arms meeting at a tip
				for (int32 k = 0; k < 2; ++k)
				{
					const float Side = (k == 0) ? -BoostArmSpread : BoostArmSpread;
					const FVector Off = BaseRot.RotateVector(FVector(HeadAlong, Side, 0.f));
					const float SlabYaw = BaseYaw + ((k == 0) ? BoostArmYaw : -BoostArmYaw);
					TileBox(PadArrows, CellCenter + Off + FVector(0.f, 0.f, 8.f),
						104.f, 24.f, 16.f, SlabYaw);
				}

				// 3. scrolling chevrons - placeholders, placed by UpdateBoostArrows
				FBoostArrowGroup Group;
				Group.LocalCenter = CellCenter;
				Group.BaseYaw = BaseYaw;
				Group.FirstInstance = PadArrows->GetInstanceCount();
				for (int32 i = 0; i < BoostChevrons * 2; ++i)
				{
					TileBox(PadArrows, CellCenter, 1.f, 1.f, 1.f, BaseYaw);
				}
				BoostArrows.Add(Group);
				break;
			}
			case TEXT('C'):
			{
				// A thin cyan frame establishes an engineered glass panel. Fractures
				// start almost invisible and grow after the ball touches the pane.
				AddCornerMarkers(PadArrows, CellCenter, 106.f, 52.f, 6.f, 5.f);
				const int32 Idx = TileBox(CrumbleTiles, FloorPos, CellSize - 10.f, CellSize - 10.f, FloorThickness);
				CrumbleIndexByCell.Add(FIntPoint(Col, Row), Idx);
				FTransform T;
				T.SetLocation(FloorPos);
				T.SetScale3D(FVector((CellSize - 10.f) / CubeSize, (CellSize - 10.f) / CubeSize, FloorThickness / CubeSize));
				CrumbleOriginal.Add(T);

				TArray<FTransform> CrackSet;
				const FVector Offsets[] = {
					FVector(-42.f, -24.f, 4.f), FVector(12.f, 4.f, 4.f), FVector(54.f, 35.f, 4.f),
					FVector(-5.f, 46.f, 4.f), FVector(31.f, -48.f, 4.f)
				};
				const float Yaws[] = { 24.f, -31.f, 41.f, 73.f, -68.f };
				const float Lengths[] = { 92.f, 108.f, 70.f, 62.f, 58.f };
				for (int32 Crack = 0; Crack < 5; ++Crack)
				{
					const int32 CrackIdx = TileBox(GlassCracks, CellCenter + Offsets[Crack], Lengths[Crack], 5.f, 3.f, Yaws[Crack]);
					FTransform Original;
					GlassCracks->GetInstanceTransform(CrackIdx, Original, false);
					CrackSet.Add(Original);
					Original.SetScale3D(Original.GetScale3D() * 0.06f);
					GlassCracks->UpdateInstanceTransform(CrackIdx, Original, false, false, true);
				}
				GlassCrackOriginal.Add(Idx, MoveTemp(CrackSet));
				break;
			}
			case TEXT('X'):
			{
				// A weak wall is deliberately unlike the structural walls: it is lower,
				// thin, split into three uneven amber plates, and carries a large glowing
				// fracture X on both faces. The silhouette reads as breakable before impact.
				FBreakableWallGroup Group;
				const float WeakHeight = WallHeight * 0.70f;
				Group.LocalCenter = CellCenter + FVector(0.f, 0.f, WeakHeight * 0.5f);
				const bool bCorridorNS = GetCellChar(Row, Col - 1) == TEXT('#')
					&& GetCellChar(Row, Col + 1) == TEXT('#');
				const FVector Tangent = bCorridorNS ? FVector::ForwardVector : FVector::RightVector;
				const FVector FaceNormal = bCorridorNS ? FVector::RightVector : FVector::ForwardVector;
				const float Span = CellSize * 0.80f;
				const float PlateSpan = Span / 3.f - 9.f;
				const float Thickness = 34.f;

				for (int32 Plate = -1; Plate <= 1; ++Plate)
				{
					const float PlateHeight = WeakHeight * (Plate == 0 ? 0.92f : (Plate < 0 ? 0.78f : 0.84f));
					const FVector PlateCenter = CellCenter + Tangent * (Plate * Span / 3.f)
						+ FVector(0.f, 0.f, PlateHeight * 0.5f);
					const float SX = bCorridorNS ? PlateSpan : Thickness;
					const float SY = bCorridorNS ? Thickness : PlateSpan;
					const int32 Instance = TileBox(BreakableWalls, PlateCenter, SX, SY, PlateHeight);
					FTransform Original;
					BreakableWalls->GetInstanceTransform(Instance, Original, false);
					Group.WallInstances.Add(Instance);
					Group.WallOriginal.Add(Original);
				}

				for (int32 Face = -1; Face <= 1; Face += 2)
				{
					for (int32 Slash = -1; Slash <= 1; Slash += 2)
					{
						const FVector Direction = (Tangent * 0.78f + FVector::UpVector * (0.62f * Slash)).GetSafeNormal();
						FTransform Mark;
						Mark.SetLocation(Group.LocalCenter + FaceNormal * Face * (Thickness * 0.5f + 4.f));
						Mark.SetRotation(FQuat::FindBetweenNormals(FVector::ForwardVector, Direction));
						Mark.SetScale3D(FVector(1.55f, 0.075f, 0.075f));
						const int32 Detail = BreakableWallDetails->AddInstance(Mark);
						Group.DetailInstances.Add(Detail);
						Group.DetailOriginal.Add(Mark);
					}
				}

				const int32 GroupIndex = BreakableWallGroups.Add(MoveTemp(Group));
				for (const int32 Instance : BreakableWallGroups[GroupIndex].WallInstances)
				{
					BreakableGroupByInstance.Add(Instance, GroupIndex);
				}
				break;
			}
			case TEXT('D'):
			{
				// Portcullis-style gate: two posts + horizontal bars, so it reads as
				// "a gate that can open" at first sight. All parts share the DoorBlocks
				// ISM and slide down together when the button is pressed.
				const bool bCorridorNS = GetCellChar(Row, Col - 1) == TEXT('#') && GetCellChar(Row, Col + 1) == TEXT('#');
				const float Thick = 44.f;
				auto AddDoorPart = [&](const FVector& Offset, float AlongLen, float PartH, float ZC)
				{
					const float SX = bCorridorNS ? AlongLen : Thick;
					const float SY = bCorridorNS ? Thick : AlongLen;
					const FVector Off = bCorridorNS ? Offset : FVector(Offset.Y, Offset.X, Offset.Z);
					TileBox(DoorBlocks, CellCenter + Off + FVector(0.f, 0.f, ZC), SX, SY, PartH);
					FTransform T;
					T.SetLocation(CellCenter + Off + FVector(0.f, 0.f, ZC));
					T.SetScale3D(FVector(SX / CubeSize, SY / CubeSize, PartH / CubeSize));
					DoorOriginal.Add(T);
				};
				// posts (full height, at both ends of the gate)
				AddDoorPart(FVector(-(CellSize * 0.5f - 18.f), 0.f, 0.f), 36.f, WallHeight, WallHeight * 0.5f);
				AddDoorPart(FVector(+(CellSize * 0.5f - 18.f), 0.f, 0.f), 36.f, WallHeight, WallHeight * 0.5f);
				// horizontal bars
				for (int32 Bar = 0; Bar < 4; ++Bar)
				{
					AddDoorPart(FVector(0.f, 0.f, 0.f), CellSize - 20.f, 28.f, 35.f + Bar * 62.f);
				}
				break;
			}
			case TEXT('B'):
			{
				FTransform T;
				T.SetLocation(CellCenter + FVector(0.f, 0.f, 8.f));
				T.SetScale3D(FVector(1.3f, 1.3f, 0.16f));
				ButtonPads->AddInstance(T);
				ButtonOriginal.Add(T);
				break;
			}
			case TEXT('S'):
				AddCornerMarkers(AccentDetails, CellCenter, 94.f, 34.f, 6.f, 4.f);
				StartLocation = GetActorTransform().TransformPosition(CellCenter + FVector(0.f, 0.f, 100.f));
				break;
			case TEXT('G'):
			{
				AddCornerMarkers(AccentDetails, CellCenter, 94.f, 44.f, 9.f, 4.f);
				bGoalFound = true;
				Goal->SetRelativeLocation(CellCenter + FVector(0.f, 0.f, 5.f));
				Goal->SetRelativeScale3D(FVector(TileScaleXY * 0.8f, TileScaleXY * 0.8f, 0.1f));
				GoalLocation = GetActorTransform().TransformPosition(CellCenter);
				GoalBeacon->SetRelativeLocation(FVector(0.f, 0.f, 58.f));
				break;
			}
			default: // '.'
				break;
			}
		}
	}

	// Compute warp exit points (first walkable neighbor of each warp cell)
	for (const FIntPoint& WC : WarpCells)
	{
		FVector Exit((WC.X + 0.5f) * CellSize, (WC.Y + 0.5f) * CellSize, 120.f);
		const int32 DR[4] = { 0, 0, 1, -1 };
		const int32 DC[4] = { 1, -1, 0, 0 };
		for (int32 k = 0; k < 4; ++k)
		{
			const TCHAR N = GetCellChar(WC.Y + DR[k], WC.X + DC[k]);
			if (N != TEXT('#') && N != TEXT('O') && N != TEXT('W') && N != TEXT('D'))
			{
				Exit = FVector((WC.X + DC[k] + 0.5f) * CellSize, (WC.Y + DR[k] + 0.5f) * CellSize, 120.f);
				break;
			}
		}
		WarpExitLocal.Add(Exit);
	}

	Goal->SetVisibility(bGoalFound);
	GoalBeacon->SetVisibility(bGoalFound);
	GoalLight->SetVisibility(bGoalFound);
	Goal->SetGenerateOverlapEvents(bGoalFound);

	// Place the chevrons once so the pads look right in the editor too, where
	// Tick (and therefore the scrolling) does not run.
	UpdateBoostArrows(0.f);
	UpdateWarpVisuals(0.f);
}

void AMazeGenerator::UpdateBoostArrows(float Time)
{
	if (!PadArrows || BoostArrows.Num() == 0)
	{
		return;
	}

	const float CycleSeconds = 1.1f;
	const float TravelStart = -CellSize * 0.44f; // entry edge of the pad
	const float TravelEnd = CellSize * 0.06f;    // stops short of the static head
	const int32 NumInstances = PadArrows->GetInstanceCount();
	bool bTouched = false;

	for (const FBoostArrowGroup& Group : BoostArrows)
	{
		const FRotator BaseRot(0.f, Group.BaseYaw, 0.f);
		for (int32 Chev = 0; Chev < BoostChevrons; ++Chev)
		{
			const float Phase = FMath::Frac(Time / CycleSeconds + float(Chev) / float(BoostChevrons));
			const float Along = FMath::Lerp(TravelStart, TravelEnd, Phase);
			// Swell in and shrink out so the loop never pops at the wrap point
			const float Scale = FMath::Clamp(FMath::Sin(Phase * PI) * 1.4f, 0.12f, 1.f);

			for (int32 k = 0; k < 2; ++k)
			{
				const int32 Index = Group.FirstInstance + Chev * 2 + k;
				if (Index >= NumInstances)
				{
					continue;
				}
				const float Side = ((k == 0) ? -BoostArmSpread : BoostArmSpread) * Scale;
				const FVector Off = BaseRot.RotateVector(FVector(Along, Side, 0.f));
				const float SlabYaw = Group.BaseYaw + ((k == 0) ? BoostArmYaw : -BoostArmYaw);

				FTransform T;
				T.SetLocation(Group.LocalCenter + Off + FVector(0.f, 0.f, 5.f));
				T.SetRotation(FQuat(FRotator(0.f, SlabYaw, 0.f)));
				T.SetScale3D(FVector(0.82f * Scale, 0.18f, 0.09f));
				PadArrows->UpdateInstanceTransform(Index, T, false, false, true);
				bTouched = true;
			}
		}
	}
	if (bTouched)
	{
		PadArrows->MarkRenderStateDirty();
	}
}

void AMazeGenerator::UpdateWarpVisuals(float Time)
{
	if (!WarpRings || !WarpBeacons || WarpCells.Num() == 0)
	{
		return;
	}

	constexpr int32 SegmentsPerWarp = 12;
	constexpr int32 BeaconsPerWarp = 4;
	const float Spin = Time * 34.f;
	bool bRingDirty = false;
	bool bBeaconDirty = false;

	for (int32 WarpIndex = 0; WarpIndex < WarpCells.Num(); ++WarpIndex)
	{
		const FIntPoint Cell = WarpCells[WarpIndex];
		const FVector Center((Cell.X + 0.5f) * CellSize, (Cell.Y + 0.5f) * CellSize, 0.f);
		for (int32 Segment = 0; Segment < SegmentsPerWarp; ++Segment)
		{
			const float Degrees = Spin + 360.f * static_cast<float>(Segment) / static_cast<float>(SegmentsPerWarp);
			const float Angle = FMath::DegreesToRadians(Degrees);
			const FVector Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
			FTransform T(FRotator(0.f, Degrees + 90.f, 0.f),
				Center + Radial * 103.f + FVector(0.f, 0.f, 18.f), FVector(0.42f, 0.10f, 0.08f));
			WarpRings->UpdateInstanceTransform(WarpIndex * SegmentsPerWarp + Segment, T, false, false, true);
			bRingDirty = true;
		}

		for (int32 Beacon = 0; Beacon < BeaconsPerWarp; ++Beacon)
		{
			const float BaseAngle = 2.f * PI * static_cast<float>(Beacon) / static_cast<float>(BeaconsPerWarp) + PI * 0.25f;
			const FVector Radial(FMath::Cos(BaseAngle), FMath::Sin(BaseAngle), 0.f);
			const float Phase = Time * 3.6f + Beacon * PI * 0.5f;
			const float Height = 70.f + 22.f * (0.5f + 0.5f * FMath::Sin(Phase));
			const float BeamHeight = 62.f + 28.f * (0.5f + 0.5f * FMath::Sin(Phase));
			FTransform T(FRotator::ZeroRotator, Center + Radial * 72.f + FVector(0.f, 0.f, Height),
				FVector(0.07f, 0.07f, BeamHeight / 100.f));
			WarpBeacons->UpdateInstanceTransform(WarpIndex * BeaconsPerWarp + Beacon, T, false, false, true);
			bBeaconDirty = true;
		}
	}

	if (bRingDirty)
	{
		WarpRings->MarkRenderStateDirty();
	}
	if (bBeaconDirty)
	{
		WarpBeacons->MarkRenderStateDirty();
	}
}

void AMazeGenerator::HandleGoalOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	AMazeBallPawn* Ball = Cast<AMazeBallPawn>(OtherActor);
	if (!Ball || bTransitioning || Flow != EMazeFlow::Playing)
	{
		return;
	}
	bTransitioning = true;
	PendingBall = Ball;

	Ball->SetControlEnabled(false);
	OnGoalReached(Ball);

	if (!bTutorialMode && RunStageTimes.IsValidIndex(CurrentStage))
	{
		RunStageTimes[CurrentStage] = GetWorld()->GetTimeSeconds() - StageStartTime;
	}

	const int32 TotalStages = FMath::Max(GetStageCount(), 1);
	const bool bAllClear = (CurrentStage + 1 >= TotalStages);

	if (bTutorialMode)
	{
		// Lessons chain straight into each other, so keep the break short and light
		TransitionMessage = bAllClear ? TEXT("GUIDE COMPLETE") : TEXT("OK");
	}
	else if (bAllClear)
	{
		TransitionMessage = TEXT("ALL CLEAR");
		OnAllStagesCleared();

		// Only a true all-clear persists times - going home mid-run never saves.
		if (SaveGameData)
		{
			for (int32 i = 0; i < RunStageTimes.Num(); ++i)
			{
				if (RunStageTimes[i] >= 0.f)
				{
					SaveGameData->SubmitStageTime(i, RunStageTimes[i]);
				}
			}
			UGameplayStatics::SaveGameToSlot(SaveGameData, TEXT("MazeTimes"), 0);
		}
	}
	else
	{
		TransitionMessage = FString::Printf(TEXT("STAGE %02d CLEAR"), CurrentStage + 1);
	}
	TransitionStartTime = GetWorld()->GetTimeSeconds();

	// The final stage gets a longer, fancier celebration before returning home
	float Hold = bAllClear ? CelebrationSeconds * 2.f : CelebrationSeconds;
	if (bTutorialMode)
	{
		Hold = bAllClear ? CelebrationSeconds : CelebrationSeconds * 0.5f;
	}
	GetWorld()->GetTimerManager().SetTimer(
		TransitionTimer, this, &AMazeGenerator::FinishTransition, Hold, false);
}

void AMazeGenerator::FinishTransition()
{
	TransitionMessage.Empty();
	PendingBall = nullptr;
	bTransitioning = false;

	const int32 TotalStages = FMath::Max(GetStageCount(), 1);
	if (CurrentStage + 1 >= TotalStages)
	{
		if (bTutorialMode)
		{
			// The tutorial has no ranking screen - straight back to the title.
			GoHome();
		}
		else
		{
			// True all-clear: show the per-stage time ranking; only "Home" from
			// there returns to the title screen.
			Flow = EMazeFlow::AllClearRanking;
		}
	}
	else if (bTutorialMode)
	{
		// Chain lessons directly without inserting another menu.
		ChooseNextStage();
	}
	else
	{
		// Let the player choose: next stage or home
		Flow = EMazeFlow::ClearChoice;
	}
}
