#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlightScore.h"
#include "FlightCourse.generated.h"

class AFlightPawn;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class UProceduralMeshComponent;
class UStaticMesh;

/** Alien-planet sky, sun, fog and exposure shared by the flight course and the title backdrop. */
namespace FlightEnvironment
{
	void Build(AActor* Owner, USceneComponent* Root, bool bWithPlanet);
}

/** Position and orthonormal frame on the course centre line. */
struct FPathFrame
{
	FVector P = FVector::ZeroVector;
	FVector F = FVector::ForwardVector;
	FVector R = FVector::RightVector;
	FVector U = FVector::UpVector;

	FVector ToWorld(double DX, double X, double Z) const { return P + F * DX + R * X + U * Z; }
	FQuat Rotation() const { return FRotationMatrix::MakeFromXZ(F, U).ToQuat(); }
};

/**
 * Course centre line, parameterised by distance s (cm). Built by integrating a
 * heading/height schedule, then closed with a circular arena that loops
 * forever so the final encounter never runs out of track.
 */
class FFlightPath
{
public:
	void Build();
	FPathFrame Frame(double S) const;
	double ArenaStart() const { return LoopStart; }

private:
	static constexpr double Step = 200.0;
	TArray<FVector> Points;
	double LoopStart = 0.0;
	double ArenaRadius = 0.0;
	FVector ArenaCenter = FVector::ZeroVector;
	double ArenaHeading = 0.0;
	double ArenaZ = 0.0;
};

namespace FlightCue
{
	enum : uint8 { Laser, SmallBoom, BigBoom, Ring, Heal, Damage, Warning, EnemyShot, Charge, Dodge };
}

UENUM()
enum class EFlightSection : uint8
{
	Sea,
	Canyon,
	SkyBase,
	Boss
};

/** Path-space object: S along the course, Off = (right, up) from the centre line. */
struct FFlightEnemy
{
	bool bActive = false;
	uint8 Kind = 0;       // 0 drone, 1 turret, 2 boss
	uint8 Pattern = 0;
	double S = 0.0;
	FVector2D Off = FVector2D::ZeroVector;
	FVector2D Vel = FVector2D::ZeroVector;
	double SpeedS = 0.0;  // absolute ds/dt
	float HP = 1.f;
	float MaxHP = 1.f;
	float Age = 0.f;
	float FireTimer = 0.f;
	float HitFlash = 0.f;
	bool bCharging = false;
	FVector2D LockedTarget = FVector2D::ZeroVector;
	FVector2D Anchor = FVector2D::ZeroVector;
	TObjectPtr<UProceduralMeshComponent> Mesh = nullptr;
	TObjectPtr<UMaterialInstanceDynamic> GlowMID = nullptr;
};

struct FFlightShot
{
	bool bActive = false;
	double S = 0.0;
	FVector2D Off = FVector2D::ZeroVector;
	double SpeedS = 0.0;
	FVector2D Vel = FVector2D::ZeroVector;
	float Life = 0.f;
};

struct FFlightRing
{
	double S = 0.0;
	FVector2D Off = FVector2D::ZeroVector;
	bool bPassed = false;
	bool bResolved = false;
	float Flash = 0.f;
	TObjectPtr<UProceduralMeshComponent> Mesh = nullptr;
	TObjectPtr<UMaterialInstanceDynamic> MID = nullptr;
};

struct FFlightSpawn
{
	double TriggerS = 0.0;
	uint8 Kind = 0;
	uint8 Pattern = 0;
	int32 Count = 1;
	FVector2D Off = FVector2D::ZeroVector;
	bool bDone = false;
	bool bAnnounced = false;
};

struct FFlightThreat
{
	double S = 0;
	FVector2D Off, Target;
	float Charge01 = 0;
};

struct FFlightHazard
{
	double S = 0.0;
	float X = 0.f;
	float Radius = 0.f;
};

struct FFlightParticle
{
	FVector Pos = FVector::ZeroVector;
	FVector Vel = FVector::ZeroVector;
	FRotator Rot = FRotator::ZeroRotator;
	FRotator Spin = FRotator::ZeroRotator;
	float Age = 0.f;
	float Life = 1.f;
	float Size = 1.f;
	bool bActive = false;
};

struct FFlightBlast
{
	FVector Pos = FVector::ZeroVector;
	float Age = 0.f;
	float Life = 0.6f;
	float Size = 1.f;
	bool bActive = false;
};

/**
 * Everything the ship flies through: track, scenery, enemies, rings, bolts,
 * bullets and effects. Objects live in path space so aiming, collisions and
 * ring checks are exact along a curving course. The pawn drives Simulate()
 * only while the game is running, so pausing freezes the whole world.
 */
UCLASS()
class SPRESENSEMAZE_API AFlightCourse : public AActor
{
	GENERATED_BODY()

public:
	AFlightCourse();
	virtual void BeginPlay() override;

	FFlightPath Path;

	/** Section start distances; the pawn respawns at the last one reached. */
	TArray<double> Checkpoints;
	EFlightSection SectionAt(double S) const;
	static FString SectionName(EFlightSection Section);

	/** Advance the world by Dt with the ship at ShipS / ShipOff. */
	void Simulate(float Dt, AFlightPawn* Ship);

	/** Spawn a player bolt from Off travelling along Dir (path space, unit). */
	void FireBolt(double S, const FVector2D& Off, const FVector& Dir, double ShipSpeed);

	/** Clear transient objects and rewind spawns/rings to checkpoint S. */
	void ResetFrom(double S);

	/** Visual-only effects keep animating while paused (they read better). */
	void TickEffects(float Dt);
	void SpawnExplosion(const FVector& World, float Scale);

	/** Synthesised cue (see FlightCue). */
	void PlayCue(uint8 Cue, float Pitch = 1.f, float Volume = 1.f);
	float Proximity(double S, const FVector2D& Off) const;
	FVector2D CameraClearance(double S, const FVector2D& Off) const;
	void CommitCheckpoint();
	void CompleteRun(bool bPreview);
	void NotifyDamage();
	void AwardNotice(int32 Points, const FString& Reason);
	FFlightScore Score;
	int32 BestScore = 0;
	bool bNewBest = false;
	bool bRecordSaveFailed = false;
	FString ScoreNotice, IncomingFormation;
	float NoticeTime = 0, IncomingTime = 0;
	TArray<FFlightThreat> Threats;

	// HUD data
	int32 RingsPassed = 0;
	int32 RingsTotal = 0;
	int32 RingStreak = 0;
	int32 Kills = 0;
	bool bBossActive = false;
	bool bBossDefeated = false;
	float BossHealth01 = 0.f;
	float RingPulse = 0.f;

	/** Pool sizes (lower on Android so the frame stays inside 16.6 ms). */
	int32 MaxEnemies = 14;
	int32 MaxShots = 40;
	int32 MaxBullets = 56;
	int32 MaxDebris = 140;

	static constexpr double ShipSpeed = 4600.0;
	static constexpr float LimitX = 1500.f;
	static constexpr float LimitZ = 760.f;

private:
	void BuildEnvironment();
	void BuildScenery();
	void BuildLandmarks();
	void BuildSchedule();
	void BuildRings();
	void BuildPools();

	void SpawnEnemy(const FFlightSpawn& Spawn, double ShipS, int32 Index);
	FFlightEnemy* AllocEnemy(uint8 Kind);
	void FireBullet(const FFlightEnemy& E, const FVector2D& Target, double ShipS, float LeadTime, const FVector2D& Spread = FVector2D::ZeroVector);
	void KillEnemy(FFlightEnemy& E, bool bByPlayer);
	void EmitDebris(const FVector& World, int32 Count, float Speed, float Size);
	void UpdateEnemyTransforms();
	void UpdateShotInstances(UInstancedStaticMeshComponent* ISM, const TArray<FFlightShot>& Shots, const FVector& Scale, bool bAlignToVelocity);
	void UpdateSeaDetail(const FVector& CameraPos);
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Sea;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Clouds;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> BaseHull;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> BaseDecks;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> BaseLights;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Whitecaps;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Bolts;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Bullets;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Debris;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Blasts;
	UPROPERTY() TObjectPtr<UPointLightComponent> BlastLight;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> BossMesh;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BossGlowMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BlastMID;
	UPROPERTY() TArray<TObjectPtr<UObject>> KeepAlive;

	TArray<FFlightEnemy> Enemies;
	FFlightEnemy Boss;
	TArray<FFlightShot> Shots;
	TArray<FFlightShot> EnemyBullets;
	TArray<FFlightRing> Rings;
	TArray<FFlightSpawn> Schedule;
	TArray<FFlightHazard> Hazards;
	TArray<FFlightParticle> Particles;
	TArray<FFlightBlast> BlastPool;
	int32 NextParticle = 0;
	float BlastLightTime = 0.f;
	double LastShipS = 0.0;
	double BossDefeatTime = -1.0;
	FVector LastCameraPos = FVector::ZeroVector;
	float LaserCueTimer = 0.f;
	float WorldTime = 0.f;
	FFlightScore CheckpointScore;
	int32 CheckpointKills = 0, CheckpointRings = 0;
};
