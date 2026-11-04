#include "FlightCourse.h"
#include "FlightSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "ProceduralMeshComponent.h"

float AFlightCourse::Proximity(double S, const FVector2D& Off) const
{
	const FPathFrame Fr = Path.Frame(S);
	float Value = FMath::Clamp((500.f - float(Fr.P.Z + Off.Y)) / 280.f, 0.f, 1.f);
	if (SectionAt(S) == EFlightSection::Canyon)
	{
		Value = FMath::Max(Value, FMath::Clamp((float(FMath::Abs(Off.X)) - 1050.f) / 450.f, 0.f, 1.f));
		for (const FFlightHazard& H : Hazards)
		{
			const double Distance = FMath::Sqrt(FMath::Square(S - H.S) + FMath::Square(Off.X - H.X)) - H.Radius - 160.f;
			if (Distance > 0) { Value = FMath::Max(Value, float(FMath::Clamp(1.0 - Distance / 650.0, 0.0, 1.0))); }
		}
	}
	return Value;
}

FVector2D AFlightCourse::CameraClearance(double S, const FVector2D& Off) const
{
	FVector2D Result = Off;
	for (const FFlightHazard& H : Hazards)
	{
		const double Radius = H.Radius + 220;
		const double Along = FMath::Abs(S - H.S);
		const double Across = Result.X - H.X;
		if (Along >= Radius * 2 || FMath::Abs(Across) >= Radius) { continue; }
		const double Ease = FMath::SmoothStep(0.0, 1.0, 1.0 - Along / (Radius * 2));
		Result.X += (Across < 0 ? -1 : 1) * (Radius - FMath::Abs(Across)) * Ease;
	}
	return Result;
}

void AFlightCourse::AwardNotice(int32 Points, const FString& Reason)
{
	if (Points <= 0) { return; }
	ScoreNotice = FString::Printf(TEXT("+%d  %s"), Points, *Reason);
	NoticeTime = 1.7f;
}

void AFlightCourse::NotifyDamage()
{
	Score.Damage();
	ScoreNotice = TEXT("被弾：コンボ解除"); NoticeTime = 1.5f;
}

void AFlightCourse::CommitCheckpoint()
{
	CheckpointScore = Score;
	CheckpointKills = Kills;
	CheckpointRings = RingsPassed;
}

void AFlightCourse::CompleteRun(bool bPreview)
{
	if (Score.bFinished) { return; }
	Score.Finish();
	bNewBest = Score.Total() > BestScore;
	// Preview tours and development validation never change the user's record.
	if (!bPreview && bNewBest)
	{
		UFlightSaveGame* Save = Cast<UFlightSaveGame>(UGameplayStatics::CreateSaveGameObject(UFlightSaveGame::StaticClass()));
		Save->BestScore = Score.Total(); Save->BestGrade = Score.Grade();
		if (UGameplayStatics::SaveGameToSlot(Save, TEXT("FlightRecords_v1"), 0)) { BestScore = Save->BestScore; }
		else { bRecordSaveFailed = true; }
	}
	// The celebration has a stable result and no remaining hostile fire.
	for (FFlightEnemy& E : Enemies) { E.bActive = false; E.Mesh->SetVisibility(false); }
	for (FFlightShot& B : EnemyBullets) { B.bActive = false; }
	Threats.Reset(); IncomingTime = 0;
}
