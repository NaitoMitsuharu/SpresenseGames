#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MazeGenerator.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;
class UMazeSaveGame;

/**
 * One maze stage: layout rows + theme colors.
 * Layout chars:
 *   '#' wall            '.' floor          'S' start          'G' goal
 *   'H' hurdle (jump)   'O' hole (fall = respawn)
 *   'I' ice (slippery)  'M' mud (slow)
 *   '^' 'v' '<' '>' boost pad (pushes in that direction)
 *   'C' glass floor (fractures and shatters shortly after contact)
 *   'X' breakable wall (high-speed impact or a physical jolt breaks it)
 *   'B' button          'D' door (opens when a button is pressed)
 *   'W' warp hole (fall in -> pop out next to its paired 'W')
 */
UENUM(BlueprintType)
enum class EMazeFlow : uint8
{
	Home,        // title screen: START / CALIBRATE
	Playing,     // in a stage
	ClearChoice, // stage cleared: NEXT STAGE / HOME
	AllClearRanking // every stage cleared: per-stage time ranking, HOME only
};

/** Comparison-ready visual identities. Gameplay and collision are unchanged. */
UENUM(BlueprintType)
enum class EMazeDesignTheme : uint8
{
	KineticLab,
	NordicGallery,
	NeonCircuit,
	AquaResearch,
	SolarIndustrial,
	MonochromeMuseum
};

USTRUCT(BlueprintType)
struct FMazeStageLayout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	TArray<FString> Rows;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	FString StageName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Design")
	EMazeDesignTheme VisualTheme = EMazeDesignTheme::KineticLab;

	/** Short lesson caption shown at the bottom of the screen (tutorial stages) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	FString HintText;

	/** Pictogram drawn next to HintText; -1 draws no icon. See AMazeHUD::DrawLessonIcon */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	int32 HintIcon = -1;
};

/**
 * One boost pad's arrow decoration: 3 chevrons that scroll along the push
 * direction plus a static arrowhead, so the direction reads at a glance and
 * the motion confirms it. Instances live in the PadArrows ISM.
 */
struct FBoostArrowGroup
{
	FVector LocalCenter = FVector::ZeroVector;
	float BaseYaw = 0.f;
	int32 FirstInstance = 0;
};

/** One short-lived visual shard emitted when a glass floor breaks. */
struct FGlassShardAnim
{
	int32 InstanceIndex = INDEX_NONE;
	FTransform StartTransform;
	FVector Drift = FVector::ZeroVector;
	float StartTime = 0.f;
	float Spin = 0.f;
};

/** A segmented weak wall and the glowing fracture marks attached to it. */
struct FBreakableWallGroup
{
	FVector LocalCenter = FVector::ZeroVector;
	TArray<int32> WallInstances;
	TArray<FTransform> WallOriginal;
	TArray<int32> DetailInstances;
	TArray<FTransform> DetailOriginal;
	bool bBroken = false;
};

UCLASS()
class SPRESENSEMAZE_API AMazeGenerator : public AActor
{
	GENERATED_BODY()

public:
	AMazeGenerator();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	TArray<FString> MazeLayout;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	TArray<FMazeStageLayout> Stages;

	/** Short one-gimmick-each stages played from the HOME screen's TUTORIAL button */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	TArray<FMazeStageLayout> TutorialStages;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	int32 CurrentStage = 0;

	/** Debug: start play from this stage (0-based) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	int32 StartAtStage = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	FString TransitionMessage;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	float TransitionStartTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	float CelebrationSeconds = 3.5f;

	// ---- Ranking ----
	/** This run's captured stage-clear times (seconds), index = stage; -1 = not yet cleared this run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ranking")
	TArray<float> RunStageTimes;

	/** Persisted top-5 times per stage; only written to disk on a true all-clear. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ranking")
	TObjectPtr<UMazeSaveGame> SaveGameData;

	// ---- Theme ----
	// One palette for the whole game. Stages are told apart by their layout and
	// the gimmicks in them, not by a colour change, so the meaning of a colour
	// never shifts underneath the player:
	//   amber = you interact with it,  red = it hurts you,  cyan = it moves you.

	/** Deep graphite structural base beneath the continuous floor surface */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme")
	FLinearColor FloorBaseColor = FLinearColor(0.022f, 0.03f, 0.04f);

	/** Satin graphite walkable surface */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme")
	FLinearColor FloorPanelColor = FLinearColor(0.11f, 0.135f, 0.165f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme")
	FLinearColor WallColor = FLinearColor(0.095f, 0.115f, 0.145f);

	/** Satin-aluminium inset along the top of each continuous wall run */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme")
	FLinearColor WallCapColor = FLinearColor(0.43f, 0.5f, 0.59f);

	/** Things you act on: goal, button, hurdle markings */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme")
	FLinearColor AccentColor = FLinearColor(1.f, 0.56f, 0.09f);

	/** Things that punish you: holes */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme")
	FLinearColor HazardColor = FLinearColor(1.f, 0.2f, 0.15f);

	/** Things that move you: boost pads, warps */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme")
	FLinearColor TransportColor = FLinearColor(0.04f, 0.7f, 1.f);

	/** Inset of the metallic top cap, creating a machined dark edge around walls */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme", meta = (ClampMin = "0.0", ClampMax = "40.0"))
	float WallCapInset = 12.f;

	/** Width of the dark reveal around continuous neutral floor runs */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float NeutralFloorInset = 0.f;

	/** Thickness of the metal or illuminated wall-top element */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme", meta = (ClampMin = "3.0", ClampMax = "24.0"))
	float WallCapThickness = 7.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	float CellSize = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	float WallHeight = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	float HurdleHeight = 60.f;

	/**
	 * How far (cm) below the top of the walls a jump is allowed to carry the ball.
	 * Fed to the pawn every tick as an absolute ceiling, so a jump taken while
	 * standing on a hurdle is automatically shortened and can never clear a wall.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Maze")
	float JumpClearance = 20.f;

	// ---- Gimmick tuning ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gimmicks") float BoostAccel = 3200.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gimmicks") float CrumbleDelay = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gimmicks") float CrumbleRespawnSeconds = 3.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gimmicks") float IceControlScale = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gimmicks") float MudControlScale = 0.55f;
	/** Minimum normal impact speed required to smash an X wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gimmicks") float BreakableWallImpactSpeed = 290.f;
	/** An IMU jolt contributes this many cm/s per m/s^2 toward wall break power. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gimmicks") float BreakableWallShockScale = 32.f;

	// ---- Components ----
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> Walls;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> WallTrim;      // reserved legacy component
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> Hurdles;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> FloorTiles;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> FloorTilesAlt; // reserved legacy component
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> HolePits;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> IceTiles;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> MudTiles;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> IceDetails;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> MudDetails;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> BoostTiles;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> PadArrows; // glowing chevrons on boost pads
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> CrumbleTiles;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> GlassCracks;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> GlassShards;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> BreakableWalls;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> BreakableWallDetails;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> BreakableDebris;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> DoorBlocks;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> ButtonPads;    // cylinder
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> WarpDiscs;     // luminous portal surface
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> WarpRings;     // raised rotating segments
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> WarpBeacons;   // floating vertical markers
	/** Glowing amber markings: hurdle bands, goal frame, start pad */
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> AccentDetails;
	/** Glowing red markings: hole rims */
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UInstancedStaticMeshComponent> HazardDetails;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UStaticMeshComponent> Goal;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UStaticMeshComponent> GoalBeacon;
	UPROPERTY(VisibleAnywhere, Category = "Maze") TObjectPtr<UPointLightComponent> GoalLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	FVector StartLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Maze")
	FVector GoalLocation = FVector::ZeroVector;

	UFUNCTION(BlueprintImplementableEvent, Category = "Maze") void OnGoalReached(AActor* Ball);
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze") void OnStageStarted(int32 StageIndex);
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze") void OnAllStagesCleared();
	UFUNCTION(BlueprintImplementableEvent, Category = "Maze") void OnButtonPressed();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Maze")
	FString GetStageName() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Design")
	FString GetDesignName() const;

	/** Current game-flow state (Home / Playing / ClearChoice) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flow")
	EMazeFlow Flow = EMazeFlow::Home;

	/** True while the tutorial stage list is being played instead of the main one */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Flow")
	bool bTutorialMode = false;

	UFUNCTION(BlueprintCallable, Category = "Flow") void StartGame();
	UFUNCTION(BlueprintCallable, Category = "Flow") void StartTutorial();
	UFUNCTION(BlueprintCallable, Category = "Flow") void ChooseNextStage();
	UFUNCTION(BlueprintCallable, Category = "Flow") void GoHome();

	/** Lesson text of the current stage (empty outside the tutorial) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Maze")
	FString GetStageHint() const;

	/** Pictogram id of the current stage, or -1 for none */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Maze")
	int32 GetStageHintIcon() const;

	/** Number of stages in the list currently being played */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Maze")
	int32 GetStageCount() const;

	/** Stage list currently in play: the tutorial list or the main one */
	const TArray<FMazeStageLayout>& ActiveStages() const { return bTutorialMode ? TutorialStages : Stages; }

protected:
	UFUNCTION()
	void HandleGoalOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void FinishTransition();
	void ApplyStage(int32 StageIndex);
	void ApplyTheme();
	void Rebuild();
	void UpdateGimmicks(float DeltaSeconds);
	void UpdateBoostArrows(float Time);
	void UpdateWarpVisuals(float Time);
	void StartImpactSlowMotion(double RealSeconds, float TimeDilation);
	void ResetBallToStart();
	TCHAR GetCellChar(int32 Row, int32 Col) const;
	EMazeDesignTheme ResolveDesignTheme() const;

	UFUNCTION()
	void HandleBreakableWallHit(UPrimitiveComponent* HitComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

private:
	FTimerHandle TransitionTimer;
	TWeakObjectPtr<AActor> PendingBall;
	bool bTransitioning = false;
	float LastAdvanceTime = -100.f;
	float StageStartTime = 0.f;

	// Gimmick state (rebuilt per stage)
	TMap<FIntPoint, int32> CrumbleIndexByCell;   // (Col,Row) -> instance index
	TArray<FTransform> CrumbleOriginal;
	TMap<int32, float> CrumbleTriggerTime;       // index -> world time triggered
	TMap<int32, float> CrumbleShockBonus;        // acceleration shortens time to shatter
	TMap<int32, float> CrumbleRespawnAt;         // index -> world time to restore
	TMap<int32, TArray<FTransform>> GlassCrackOriginal; // base tile index -> crack segments
	TArray<FGlassShardAnim> ActiveGlassShards;
	TArray<FBreakableWallGroup> BreakableWallGroups;
	TMap<int32, int32> BreakableGroupByInstance;
	TArray<FGlassShardAnim> ActiveBreakableDebris;
	TArray<FTransform> DoorOriginal;
	TArray<FTransform> ButtonOriginal;
	bool bDoorsOpen = false;
	float DoorAnim = 0.f;
	TArray<FBoostArrowGroup> BoostArrows;        // animated chevrons, one group per boost cell
	TArray<FIntPoint> WarpCells;                 // (Col,Row), paired 0-1, 2-3, ...
	TArray<FVector> WarpExitLocal;               // safe exit point next to each warp
	float LastWarpTime = -100.f;
	FIntPoint LastBoostCueCell = FIntPoint(MIN_int32, MIN_int32);
	bool bImpactSlowMotionActive = false;
	double ImpactSlowMotionEndRealTime = 0.0;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> WallMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> TrimMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FloorMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FloorAltMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> HurdleMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PitMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GoalMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> IceMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MudMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> IceDetailMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MudDetailMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BoostMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PadArrowMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CrumbleMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GlassCrackMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BreakableMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BreakableGlowMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DoorMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ButtonMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> WarpMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> AccentMID;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> HazardMID;
};
