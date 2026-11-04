#include "Misc/AutomationTest.h"
#include "../FlightScore.h"
#include "../FlightSaveGame.h"
#include "../FlightModels.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightAirframeTest, "SpresenseMaze.Flight.AirframeGeometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFlightAirframeTest::RunTest(const FString&)
{
	const FFlightMesh* Meshes[]={&FlightModels::DroneHull(),&FlightModels::DroneArmor(),&FlightModels::DroneGlow(),
		&FlightModels::TurretHull(),&FlightModels::TurretArmor(),&FlightModels::TurretGlow(),
		&FlightModels::BossHull(),&FlightModels::BossArmor(),&FlightModels::BossGlow()};
	for (const FFlightMesh* M:Meshes)
	{
		bool bValid=M->Verts.Num()>0 && M->Tris.Num()>0 && M->Tris.Num()%3==0
			&& M->Verts.Num()==M->Normals.Num() && M->Verts.Num()==M->Colors.Num() && M->Verts.Num()==M->UVs.Num();
		for (int32 I=0; I<M->Verts.Num() && bValid; ++I)
		{
			bValid=!M->Verts[I].ContainsNaN() && !M->Normals[I].ContainsNaN()
				&& M->Normals[I].SizeSquared()>0.9 && M->Verts[I].GetAbsMax()<2000;
		}
		for (int32 I=0; I<M->Tris.Num() && bValid; I+=3)
		{
			const int32 A=M->Tris[I], B=M->Tris[I+1], C=M->Tris[I+2];
			bValid=M->Verts.IsValidIndex(A) && M->Verts.IsValidIndex(B) && M->Verts.IsValidIndex(C);
			if (bValid)
			{
				const FVector Face=-((M->Verts[B]-M->Verts[A])^(M->Verts[C]-M->Verts[A]));
				bValid=Face.SizeSquared()>0.0001 && (Face|(M->Normals[A]+M->Normals[B]+M->Normals[C]))>0;
			}
		}
		TestTrue(TEXT("Airframe mesh has finite attributes, valid non-degenerate triangles and outward normals"),bValid);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightFlickTest, "SpresenseMaze.Flight.FlickGesture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFlightFlickTest::RunTest(const FString&)
{
	FlightHandling::FFlickGesture Flick;
	Flick.Update(0,0.016f); Flick.Update(0.9f,0.08f);
	TestEqual(TEXT("Quick right-and-return"),Flick.Update(0,0.08f),1);
	TestEqual(TEXT("Rest cannot retrigger"),Flick.Update(0,0.08f),0);
	Flick.Update(-0.95f,0.08f);
	TestEqual(TEXT("Quick left-and-return"),Flick.Update(0,0.08f),-1);
	Flick.Update(0.9f,0.08f);
	for (int32 I=0; I<30; ++I) { Flick.Update(0.9f,0.016f); }
	TestEqual(TEXT("Sustained steering is not a flick"),Flick.Update(0,0.08f),0);
	Flick.Reset(); Flick.Update(1,0.016f);
	TestEqual(TEXT("Reconnection at tilt cannot trigger"),Flick.Update(0,0.016f),0);
	double D30=0,D120=0;
	for (int32 I=0; I<30; ++I) { D30+=640*(FlightHandling::DodgeEase((I+1)/30.0)-FlightHandling::DodgeEase(I/30.0)); }
	for (int32 I=0; I<120; ++I) { D120+=640*(FlightHandling::DodgeEase((I+1)/120.0)-FlightHandling::DodgeEase(I/120.0)); }
	TestTrue(TEXT("Dodge travel is frame-rate independent"),FMath::IsNearlyEqual(D30,D120,0.001) && FMath::IsNearlyEqual(D30,640.0,0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightRecordTest, "SpresenseMaze.Flight.RecordPersistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFlightRecordTest::RunTest(const FString&)
{
	const FString Slot = TEXT("FlightAutomation_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UFlightSaveGame* Save = Cast<UFlightSaveGame>(UGameplayStatics::CreateSaveGameObject(UFlightSaveGame::StaticClass()));
	Save->BestScore = 31250; Save->BestGrade = TEXT("S");
	TestTrue(TEXT("Record saved"), UGameplayStatics::SaveGameToSlot(Save, Slot, 0));
	const UFlightSaveGame* Loaded = Cast<UFlightSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	TestNotNull(TEXT("Record loaded"), Loaded);
	if (Loaded)
	{
		TestEqual(TEXT("Score survives reload"), Loaded->BestScore, 31250);
		TestEqual(TEXT("Grade survives reload"), Loaded->BestGrade, FString(TEXT("S")));
		TestEqual(TEXT("Record schema"), Loaded->ScoreVersion, 1);
	}
	TestTrue(TEXT("Temporary test record removed"), UGameplayStatics::DeleteGameInSlot(Slot, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightScoreRollbackTest, "SpresenseMaze.Flight.CheckpointScore", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFlightScoreRollbackTest::RunTest(const FString&)
{
	FFlightScore Run;
	Run.Award(100, true);
	const FFlightScore Checkpoint = Run;
	Run.Award(250, false); Run.CloseFlight(10000, 1.f, true); Run.Damage();
	Run.Restore(Checkpoint);
	TestEqual(TEXT("Replayed progress cannot retain its points"), Run.Total(), Checkpoint.Total());
	TestEqual(TEXT("Damage survives retry"), Run.DamageCount, 1);
	TestEqual(TEXT("Retry recorded"), Run.Retries, 1);
	Run.Finish(); const int32 Final = Run.Total(); Run.Finish(); Run.Award(100, true); Run.Graze();
	TestEqual(TEXT("Result cannot change or double-pay"), Run.Total(), Final);
	TestEqual(TEXT("Damaged run has completion bonus only"), Run.BonusPoints, 5000);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightRewardsTest, "SpresenseMaze.Flight.Rewards", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFlightRewardsTest::RunTest(const FString&)
{
	FFlightScore Run;
	TestEqual(TEXT("Sustained low flight pays"), Run.CloseFlight(5000, 0.5f, true), 25);
	TestEqual(TEXT("Same section cannot pay twice"), Run.CloseFlight(5000, 1.f, true), 0);
	TestEqual(TEXT("Arena cannot farm proximity"), Run.CloseFlight(700000, 1.f, true), 0);
	for (int32 I = 0; I < 5; ++I) { Run.Award(100, true); }
	TestTrue(TEXT("Chain increases multiplier"), Run.Multiplier() > 1.f);
	Run.Tick(6.1f); TestEqual(TEXT("Chain expires"), Run.Combo, 0);
	Run.Finish(); TestEqual(TEXT("Perfect bonus"), Run.BonusPoints, 8000);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlightHandlingTest, "SpresenseMaze.Flight.Handling", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFlightHandlingTest::RunTest(const FString&)
{
	TestEqual(TEXT("Dead zone"), FlightHandling::MapTilt(2.f, 2.5f, 26.f), 0.f);
	TestEqual(TEXT("Full tilt"), FlightHandling::MapTilt(26.f, 2.5f, 26.f), 1.f);
	TestTrue(TEXT("Symmetric tilt"), FMath::IsNearlyEqual(FlightHandling::MapTilt(12.f, 2.5f, 26.f), -FlightHandling::MapTilt(-12.f, 2.5f, 26.f)));
	double V30 = 0, V120 = 0, P30 = 0, P120 = 0;
	for (int32 I = 0; I < 30; ++I) { P30 += FlightHandling::Step(V30, 2600, 1.0 / 30, 7.5); }
	for (int32 I = 0; I < 120; ++I) { P120 += FlightHandling::Step(V120, 2600, 1.0 / 120, 7.5); }
	TestTrue(TEXT("Same displacement at 30 and 120 fps"), FMath::Abs(P30 - P120) < 0.01);
	TestTrue(TEXT("Same velocity at 30 and 120 fps"), FMath::Abs(V30 - V120) < 0.01);
	return true;
}
#endif
