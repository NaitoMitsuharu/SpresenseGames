#include "Misc/AutomationTest.h"
#include "../LightBoxDirector.h"
#include "../LightBoxSimulation.h"
#include "../LightBoxGeometry.h"
#include "../LightBoxMotion.h"
#include "../LightBoxJournal.h"
#include "../LightBoxPointData.h"
#include "../LightBoxPointCloud.h"
#include "../LightBoxGame.h"
#include "Components/PrimitiveComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxPointShapeTest,"SpresenseMaze.LightBox.PointSculptureGeometryAndLimits",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxPointShapeTest::RunTest(const FString&)
{
    FLightBoxStyle S;S.bPointCloud=true;S.PointCount=1000000;S.Count=0;
    FLightBoxObject O;O.Shape=TEXT("lathe");O.Color=FLinearColor(.3f,.02f,.6f);O.Size=FVector(100,90,180);O.Position.Z=160;O.Profile={{-.5,0},{-.2,.46},{.1,.5},{.4,.15},{.5,0}};S.Objects.Add(O);
    FLightBoxStyle Parsed;FString Error;
    TestTrue(TEXT("A million is accepted as a ceiling, independently of CPU particles"),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error));
    TestEqual(TEXT("Point budget round trips"),Parsed.PointCount,1000000);TestFalse(TEXT("Floor is optional and off"),Parsed.bPointFloor);
    auto Points=LightBoxPoints::Sample(Parsed);TestEqual(TEXT("Exactly the requested number of surface samples"),Points.Num(),1000000);
    for(int32 I=0;I<Points.Num();I+=997){TestFalse(TEXT("Finite surface positions"),Points[I].Position.ContainsNaN());TestTrue(TEXT("Target remains within the model bounds"),Points[I].Position.Z>=69&&Points[I].Position.Z<=251);}
    S.PointCount=1000001;TestFalse(TEXT("Above the agreed maximum is rejected"),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error));
    S.PointCount=65536;S.Objects[0].Profile[2].X=-.3;TestFalse(TEXT("Folded/unsorted lathe profile is rejected atomically"),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxPointTransitionTest,"SpresenseMaze.LightBox.PointSculptureTransitionContinuity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxPointTransitionTest::RunTest(const FString&)
{
    FLightBoxPoint A,B,C;A.Position=FVector3f(-80,25,60);B.Position=FVector3f(40,5,200);C.Position=FVector3f(5,-50,150);
    for(uint32 I=0;I<128;++I){
        TestTrue(TEXT("No jump at the start"),LightBoxPoints::Position(A,B,I,0,65,1.25,.35).Equals(A.Position,.0001f));
        TestTrue(TEXT("All particles arrive at the new shape"),LightBoxPoints::Position(A,B,I,1,65,1.25,.35).Equals(B.Position,.0001f));
        FLightBoxPoint Snapshot=A;Snapshot.Position=LightBoxPoints::Position(A,B,I,.47,65,1.25,.35);
        TestTrue(TEXT("Retargeting during flight resumes at the current position"),LightBoxPoints::Position(Snapshot,C,I,0,65,1.25,.35).Equals(Snapshot.Position,.0001f));
        TestFalse(TEXT("The journey includes visible curved movement"),Snapshot.Position.Equals(FMath::Lerp(A.Position,B.Position,LightBoxPoints::Ease(I,.47,.35)),.1f));
    }return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxPointLightingTest,"SpresenseMaze.LightBox.PointSculptureLighting",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxPointLightingTest::RunTest(const FString&)
{
    FLightBoxStyle S;S.bPointCloud=true;S.PointCount=256;S.Count=0;S.Ambient=.1f;S.KeyIntensity=.1f;S.Objects.Add(FLightBoxObject());
    const auto Dark=LightBoxPoints::Sample(S);S.Ambient=3;S.KeyIntensity=8;const auto Bright=LightBoxPoints::Sample(S);
    float Before=0,After=0;for(int32 I=0;I<Dark.Num();++I){Before+=Dark[I].Color.GetLuminance();After+=Bright[I].Color.GetLuminance();TestTrue(TEXT("Lighting preserves point destinations"),Dark[I].Position.Equals(Bright[I].Position));}
    TestTrue(TEXT("Ambient and key intensity brighten point sculptures"),After>Before*2);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxPointAppearanceTest,"SpresenseMaze.LightBox.ParticleAppearanceControls",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxPointAppearanceTest::RunTest(const FString&)
{
    FLightBoxStyle S;S.Count=0;S.PointCount=2048;S.Glow=.5f;S.Objects.Add(FLightBoxObject());const auto A=LightBoxPoints::Sample(S);
    S.ParticleSize=2;S.Glow=2;const auto B=LightBoxPoints::Sample(S);float BeforeSize=0,AfterSize=0,BeforeLight=0,AfterLight=0;
    for(int32 I=0;I<A.Num();++I){TestTrue(TEXT("Brightness/size edits preserve the target shape"),A[I].Position.Equals(B[I].Position));BeforeSize+=A[I].Radius;AfterSize+=B[I].Radius;BeforeLight+=A[I].Color.GetLuminance();AfterLight+=B[I].Color.GetLuminance();}
    TestTrue(TEXT("Particle size changes apply to formed surfaces too"),AfterSize>BeforeSize*1.4f);TestTrue(TEXT("Global glow brightens particle surfaces"),AfterLight>BeforeLight*1.4f);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxAnimationModeTest,"SpresenseMaze.LightBox.ParticleOnlyAnimatedSurfaces",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxAnimationModeTest::RunTest(const FString&)
{
    FLightBoxStyle S;S.Count=0;S.PointCount=4096;S.Background=FLinearColor::Black;
    FLightBoxObject Bird;Bird.Shape=TEXT("bird");Bird.Animation=TEXT("fly");Bird.Size=FVector(75,130,38);S.Objects.Add(Bird);FString Why;
    TestTrue(TEXT("Flapping is supported by the particle renderer"),FLightBoxStyle::ValidateIntent(TEXT("カモメが飛ぶ"),S,Why));
    const auto L=LightBoxPoints::Build(S);TestEqual(TEXT("Particle budget is exact"),L.Points.Num(),4096);TestTrue(TEXT("Flying birds have animated point bindings"),L.bAnimated);
    TestTrue(TEXT("Only a smaller set of control vertices is uploaded"),L.InitialVertices.Num()<L.Points.Num());
    const auto A=LightBoxPoints::Frame(L,S,0,FVector2D::ZeroVector,0,{}),B=LightBoxPoints::Frame(L,S,.35f,FVector2D::ZeroVector,0,{});
    int32 Moving=0;for(const auto& P:L.Points){const auto X=LightBoxPoints::Destination(P,A,A,1),Y=LightBoxPoints::Destination(P,B,B,1);Moving+=!X.Equals(Y,.01f);TestFalse(TEXT("Animated point positions are finite"),Y.ContainsNaN());}
    TestTrue(TEXT("The particles themselves fly and deform"),Moving>3000);
    FLightBoxPoint Sample=L.Points[0];const auto Middle=LightBoxPoints::Destination(Sample,A,B,.5f);
    TestTrue(TEXT("GPU frame interpolation is continuous"),Middle.Equals((LightBoxPoints::Destination(Sample,A,A,1)+LightBoxPoints::Destination(Sample,B,B,1))*.5f,.001f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxParticleMigrationTest,"SpresenseMaze.LightBox.ParticleOnlySaveMigration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxParticleMigrationTest::RunTest(const FString&)
{
    auto S=FLightBoxStyle::Example(8);TSharedPtr<FJsonObject> J;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S.ToJson()),J);
    J->GetObjectField(TEXT("pointCloud"))->SetBoolField(TEXT("enabled"),false);J->GetObjectField(TEXT("pointCloud"))->SetNumberField(TEXT("count"),256);J->GetObjectField(TEXT("pointCloud"))->SetBoolField(TEXT("floor"),true);
    FString Old;FJsonSerializer::Serialize(J.ToSharedRef(),TJsonWriterFactory<>::Create(&Old));FLightBoxStyle Read;FString Error;
    TestTrue(TEXT("Previously saved scenes still open"),FLightBoxStyle::Parse(Old,Read,Error));TestTrue(TEXT("Old solid renderer is migrated to particles"),Read.bPointCloud);TestFalse(TEXT("Old floor cannot reactivate solid drawing"),Read.bPointFloor);TestEqual(TEXT("Unused legacy point budget is raised so the migrated scene remains recognizable"),Read.PointCount,FLightBoxStyle::DefaultPointCount);TestEqual(TEXT("Shape descriptions are retained"),Read.Objects.Num(),S.Objects.Num());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxParticleComponentsTest,"SpresenseMaze.LightBox.ParticleOnlyComponents",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxParticleComponentsTest::RunTest(const FString&)
{
    TArray<UPrimitiveComponent*> Components;GetDefault<ALightBoxPawn>()->GetComponents(Components);
    TestEqual(TEXT("Only one scene primitive is constructed"),Components.Num(),1);
    for(auto* C:Components)TestNotNull(TEXT("The only primitive is the particle renderer"),Cast<ULightBoxPointCloud>(C));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxOrbitTest,"SpresenseMaze.LightBox.CameraOrbitAndFraming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxOrbitTest::RunTest(const FString&)
{
    auto S=FLightBoxStyle::Example(5);S.CameraOrbit=12;S.CameraYaw=180;S.CameraPitch=28;
    FLightBoxStyle Parsed;FString Error;TestTrue(TEXT("Full orbit settings parse"),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error));TestEqual(TEXT("Orbit speed survives saving"),Parsed.CameraOrbit,12.f);
    S.CameraOrbit=31;TestFalse(TEXT("Excessive orbit speed rejected"),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error));S.CameraOrbit=12;
    const FBox Box=FLightBoxMotionSystem::Bounds(S);const float Aspect=2340.f/1080;
    for(float Yaw:{0.f,90.f,180.f,270.f,360.f})for(float Pitch:{10.f,28.f,80.f}){
        const auto C=FLightBoxMotionSystem::CameraForBounds(S,Box,Aspect,FVector2D(Yaw,Pitch-S.CameraPitch));
        TestTrue(TEXT("Camera looks at the subject from every side"),FVector::DotProduct(C.GetRotation().GetForwardVector(),(Box.GetCenter()-C.GetLocation()).GetSafeNormal())>.999);
        for(int32 K=0;K<8;++K){const FVector V=C.InverseTransformPosition(FVector(K&1?Box.Max.X:Box.Min.X,K&2?Box.Max.Y:Box.Min.Y,K&4?Box.Max.Z:Box.Min.Z));
            TestTrue(TEXT("Complete animated subject stays in frame during orbit"),V.X>0&&FMath::Abs(V.Y/V.X)<FMath::Tan(FMath::DegreesToRadians(29.))*.89&&FMath::Abs(V.Z/V.X)<FMath::Tan(FMath::DegreesToRadians(29.))/Aspect*.89);}
    }
    const auto Front=FLightBoxMotionSystem::CameraForBounds(S,Box,Aspect),Back=FLightBoxMotionSystem::CameraForBounds(S,Box,Aspect,FVector2D(180,0));
    TestTrue(TEXT("Half orbit actually changes viewing side"),FVector::DotProduct((Front.GetLocation()-Box.GetCenter()).GetSafeNormal2D(),(Back.GetLocation()-Box.GetCenter()).GetSafeNormal2D())<-.99);
    auto Next=S;Next.CameraOrbit=-8;Next.bPointCloud=true;Next.Count=0;FString Why;FLightBoxStyle::CompleteGenerated(TEXT("カメラだけゆっくり回って"),S,Next,Why);
    TestEqual(TEXT("Camera-only command sets orbit"),Next.CameraOrbit,-8.f);TestEqual(TEXT("Camera-only command retains animated rendering"),Next.bPointCloud,S.bPointCloud);TestEqual(TEXT("Camera-only command retains live particles"),Next.Count,S.Count);
    Next.Title=TEXT("見回す景色");Next.Note=TEXT("カメラだけ変更");TestEqual(TEXT("Camera-only edits reuse the running renderer"),Next.RenderContentJson(),S.RenderContentJson());
    Next.Count+=1;TestNotEqual(TEXT("Changing particles rebuilds the scene"),Next.RenderContentJson(),S.RenderContentJson());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxStyleTest, "SpresenseMaze.LightBox.StyleValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightBoxStyleTest::RunTest(const FString&)
{
	FLightBoxStyle Parsed; FString Error;
	for (int32 I = 0; I < 3; ++I)
	{
		const FLightBoxStyle Source = FLightBoxStyle::Preset(I);
		TestTrue(TEXT("Preset round trip"), FLightBoxStyle::Parse(Source.ToJson(), Parsed, Error));
		TestEqual(TEXT("Scene name survives"), Parsed.Title, Source.Title);
		TestTrue(TEXT("Color survives sRGB conversion"), Parsed.Color.Equals(Source.Color, 0.006f));
	}
	const FString Valid = FLightBoxStyle::Preset(1).ToJson();
	TestTrue(TEXT("Markdown fence from a provider"), FLightBoxStyle::Parse(TEXT("```json\n") + Valid + TEXT("\n```"), Parsed, Error));
	const FString Prior = Parsed.ToJson();
	TestFalse(TEXT("Missing properties rejected"), FLightBoxStyle::Parse(TEXT("{\"title\":\"empty\"}"), Parsed, Error));
	TestEqual(TEXT("No partial scene update"), Parsed.ToJson(), Prior);
	TSharedPtr<FJsonObject> O;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Valid), O);
	auto Serialize = [&O]() { FString Json; FJsonSerializer::Serialize(O.ToSharedRef(), TJsonWriterFactory<>::Create(&Json)); return Json; };
	O->SetNumberField(TEXT("motion"), 999);
	TestFalse(TEXT("Out-of-range motion rejected"), FLightBoxStyle::Parse(Serialize(), Parsed, Error));
	O->SetNumberField(TEXT("motion"), 1); O->SetStringField(TEXT("color"), TEXT("#zzzzzz"));
	TestFalse(TEXT("Invalid color rejected"), FLightBoxStyle::Parse(Serialize(), Parsed, Error));
	O->SetStringField(TEXT("color"), TEXT("#0088ff")); O->SetStringField(TEXT("shake"), TEXT("exec"));
	TestFalse(TEXT("Unknown behavior rejected"), FLightBoxStyle::Parse(Serialize(), Parsed, Error));
	TestFalse(TEXT("Truncated or tool-only completion rejected"), FLightBoxStyle::ParseCompletion(TEXT("{\"choices\":[{\"finish_reason\":\"length\",\"message\":{\"content\":\"{}\"}}]}"), Parsed, Error));
	TestFalse(TEXT("Wrong choice type cannot assert/crash"), FLightBoxStyle::ParseCompletion(TEXT("{\"choices\":[42]}"), Parsed, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxEndpointTest, "SpresenseMaze.LightBox.LoopbackEndpoint", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightBoxEndpointTest::RunTest(const FString&)
{
	TestTrue(TEXT("Device loopback"), ULightBoxDirector::ValidateEndpoint(TEXT("http://127.0.0.1:18789")));
	TestTrue(TEXT("Localhost alias"), ULightBoxDirector::ValidateEndpoint(TEXT("http://localhost:18789")));
	for (const TCHAR* Url : { TEXT("http://127.0.0.1.evil:18789"), TEXT("http://127.0.0.1:18789@evil"), TEXT("http://localhost:0"),
		TEXT("http://localhost:65536"), TEXT("http://localhost:18789/path"), TEXT("http://192.168.0.1:18789"), TEXT("file:///tmp/test") })
	{
		TestFalse(TEXT("Non-loopback or malformed endpoint rejected"), ULightBoxDirector::ValidateEndpoint(Url));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxMotionTest, "SpresenseMaze.LightBox.MotionAndShake", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightBoxMotionTest::RunTest(const FString&)
{
	FLightBoxSimulation A, B; A.Reset(); B.Reset();
	const FLightBoxStyle Style = FLightBoxStyle::Preset(0);
	for (int32 I = 0; I < 300; ++I) { A.Step(1.f / 30, FVector2D(1, -1), 0.4f, Style); }
	for (int32 I = 0; I < 1200; ++I) { B.Step(1.f / 120, FVector2D(1, -1), 0.4f, Style); }
	double Worst = 0;
	for (int32 I = 0; I < A.Motes.Num(); ++I)
	{
		const FLightMote& P = A.Motes[I];
		Worst = FMath::Max(Worst, FVector::Distance(P.Position, B.Motes[I].Position));
		TestTrue(TEXT("Finite bounded particles after sustained tilt"), !P.Position.ContainsNaN() && FMath::Abs(P.Position.X) <= 275 && FMath::Abs(P.Position.Y) <= 180 && P.Position.Z >= 45 && P.Position.Z <= 335);
	}
	TestTrue(TEXT("30/120fps produce matching motion"), Worst < 0.2);
	A.Reset(); B.Reset();
	A.Impulse(Style); B.Impulse(FLightBoxStyle::Preset(2));
	const FVector Radial = A.Motes[0].Position - FVector(0, 0, 185);
	TestTrue(TEXT("Scatter moves outward"), (A.Motes[0].Velocity | Radial) > 0);
	TestTrue(TEXT("Gather moves inward"), (B.Motes[0].Velocity | Radial) < 0);
	FLightBoxStyle Quiet = Style; Quiet.Shake = TEXT("none"); B.Reset(); B.Impulse(Quiet);
	TestTrue(TEXT("Disabled shake has no impulse"), B.Motes[0].Velocity.IsZero());
	A.Reset(); B.Reset();
	A.Step(0.1f, FVector2D(1, 0), 0, Style); B.Step(0.1f, FVector2D::ZeroVector, 0, Style);
	TestTrue(TEXT("Right-down tilt moves toward the camera's screen-right"), A.Motes[0].Position.X < B.Motes[0].Position.X);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxSaveTest, "SpresenseMaze.LightBox.SaveRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightBoxSaveTest::RunTest(const FString&)
{
	const FString TestSlot = TEXT("LightBoxAutomation_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ULightBoxSaveGame* Save = NewObject<ULightBoxSaveGame>(); Save->Scene = FLightBoxStyle::Preset(2).ToJson();
	TestTrue(TEXT("Save scene"), UGameplayStatics::SaveGameToSlot(Save, TestSlot, 0));
	ULightBoxSaveGame* Loaded = Cast<ULightBoxSaveGame>(UGameplayStatics::LoadGameFromSlot(TestSlot, 0));
	TestNotNull(TEXT("Load scene"), Loaded);
	if (Loaded) { TestEqual(TEXT("All scene properties survive reload"), Loaded->Scene, Save->Scene); }
	UGameplayStatics::DeleteGameInSlot(TestSlot, 0);
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FWaitLightBox, TFunction<bool()>, Check);
bool FWaitLightBox::Update() { return Check(); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxHttpRoundTripTest, "SpresenseMaze.LightBox.HttpRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightBoxHttpRoundTripTest::RunTest(const FString&)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("LightBoxMockGateway")))
	{
		AddWarning(TEXT("HTTP integration test requires Tools/LightBoxMockGateway.py and -LightBoxMockGateway.")); return true;
	}
	UGameInstance* Instance = NewObject<UGameInstance>();
	ULightBoxDirector* D = NewObject<ULightBoxDirector>(Instance); D->AddToRoot();
	D->Endpoint = TEXT("http://127.0.0.1:18879"); D->BearerToken = TEXT("lightbox-fixture-only");
	D->Submit(TEXT("青い海にして"));
	const double Started = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLightBox([this, D, Started]()
	{
		if (D->bThinking && FPlatformTime::Seconds() - Started < 10) { return false; }
		TestFalse(TEXT("HTTP completed"), D->bThinking);
		TestEqual(TEXT("Validated response applied"), D->GetStyle().Title, FString(TEXT("青い海")));
		D->Submit(TEXT("bad-json")); return true;
	}));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLightBox([this, D, Started]()
	{
		if (D->bThinking && FPlatformTime::Seconds() - Started < 15) { return false; }
		TestEqual(TEXT("Malformed response keeps prior scene"), D->GetStyle().Title, FString(TEXT("青い海")));
		D->Submit(TEXT("slow")); D->SelectPreset(2); return true;
	}));
	const TSharedRef<double> WaitUntil = MakeShared<double>(0);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLightBox([this, D, WaitUntil]()
	{
		if (*WaitUntil == 0) { *WaitUntil = FPlatformTime::Seconds() + 1.5; }
		if (FPlatformTime::Seconds() < *WaitUntil) { return false; }
		TestEqual(TEXT("Cancelled stale request cannot overwrite preset"), D->GetStyle().Title, FString(TEXT("ほたる")));
		TestFalse(TEXT("Cancellation clears pending state"), D->bThinking);
		D->Cancel(); D->RemoveFromRoot(); return true;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxHttpRepairTest, "SpresenseMaze.LightBox.HttpRepairOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightBoxHttpRepairTest::RunTest(const FString&)
{
	if (!FParse::Param(FCommandLine::Get(),TEXT("LightBoxMockGateway"))) { return true; }
	UGameInstance* Instance=NewObject<UGameInstance>(); auto* D=NewObject<ULightBoxDirector>(Instance); D->AddToRoot();
	D->Endpoint=TEXT("http://127.0.0.1:18879"); D->BearerToken=TEXT("lightbox-fixture-only"); D->Submit(TEXT("repair-once"));
	const double Started=FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLightBox([this,D,Started]()
	{
		if (D->bThinking && FPlatformTime::Seconds()-Started<10) { return false; }
		TestFalse(TEXT("Repair finishes"),D->bThinking);
		TestEqual(TEXT("Corrected response is applied"),D->GetStyle().Title,FString(TEXT("修正された景色")));
		TestEqual(TEXT("Rejected first reply did not create a history entry"),D->Revision,1);
		D->Cancel(); D->RemoveFromRoot(); return true;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxCompositionTest, "SpresenseMaze.LightBox.CompositionAndGeometry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightBoxCompositionTest::RunTest(const FString&)
{
	for (int32 I=0; I<13; ++I)
	{
		const FLightBoxStyle S=FLightBoxStyle::Example(I); FLightBoxStyle Parsed; FString Error;
		TestTrue(TEXT("Composed scene round trip"),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error));
		TestEqual(TEXT("Object groups survive"),Parsed.Objects.Num(),S.Objects.Num());
		TestEqual(TEXT("Particle count survives"),Parsed.Count,S.Count);
		for (const FLightBoxObject& O : S.Objects)
		{
			const FLightBoxMesh M=FLightBoxMesh::Build(O);
			TestTrue(TEXT("Geometry is present"),O.Shape==TEXT("group") || M.Vertices.Num()>0 && M.Triangles.Num()>0 && M.Triangles.Num()%3==0);
			for (int32 Index:M.Triangles) { if (Index<0 || Index>=M.Vertices.Num()) { AddError(TEXT("Invalid mesh index")); break; } }
			for (const FVector& V:M.Vertices) { if (V.ContainsNaN()) { AddError(TEXT("Non-finite vertex")); break; } }
		}
		FFileHelper::SaveStringToFile(S.ToJson(),*(FPaths::ProjectSavedDir()/FString::Printf(TEXT("LightBoxExample%d.json"),I)),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	FFileHelper::SaveStringToFile(FLightBoxStyle::Instruction(FLightBoxStyle()),*(FPaths::ProjectSavedDir()/TEXT("LightBoxInstruction.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	const TArray<FVector2D> Concave={FVector2D(-.5,-.5),FVector2D(.5,-.5),FVector2D(0,0),FVector2D(.5,.5),FVector2D(-.5,.5)};
	TestTrue(TEXT("Custom concave polygon triangulates"),FLightBoxMesh::ValidOutline(Concave));
	TestFalse(TEXT("Crossing polygon rejected"),FLightBoxMesh::ValidOutline({FVector2D(-.5,-.5),FVector2D(.5,.5),FVector2D(-.5,.5),FVector2D(.5,-.5)}));
	TestFalse(TEXT("Degenerate polygon rejected"),FLightBoxMesh::ValidOutline({FVector2D(0),FVector2D(0),FVector2D(0)}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxBudgetTest, "SpresenseMaze.LightBox.SceneBudgetsAndAtomicity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightBoxBudgetTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> O; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FLightBoxStyle::Example(1).ToJson()),O);
	auto Json=[&](){FString S;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&S));return S;};
	FLightBoxStyle S=FLightBoxStyle::Example(3); const FString Previous=S.ToJson(); FString Error;
	O->GetObjectField(TEXT("particles"))->SetNumberField(TEXT("count"),1025);
	TestFalse(TEXT("Over-budget particles rejected"),FLightBoxStyle::Parse(Json(),S,Error));
	O->GetObjectField(TEXT("particles"))->SetNumberField(TEXT("count"),12.5);
	TestFalse(TEXT("Fractional count rejected"),FLightBoxStyle::Parse(Json(),S,Error));
	O->GetObjectField(TEXT("particles"))->SetNumberField(TEXT("count"),12);
	O->GetObjectField(TEXT("particles"))->SetStringField(TEXT("behavior"),TEXT("run-code"));
	TestFalse(TEXT("Unknown motion rejected"),FLightBoxStyle::Parse(Json(),S,Error));
	O->GetObjectField(TEXT("particles"))->SetStringField(TEXT("behavior"),TEXT("drift"));
	auto Nodes=O->GetArrayField(TEXT("objects")); const TSharedPtr<FJsonValue> Duplicate=Nodes[0]; Nodes.Add(Duplicate); O->SetArrayField(TEXT("objects"),Nodes);
	TestFalse(TEXT("Duplicate IDs rejected"),FLightBoxStyle::Parse(Json(),S,Error));
	TestEqual(TEXT("Invalid composition leaves every previous field intact"),S.ToJson(),Previous);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxBehaviorsTest, "SpresenseMaze.LightBox.ParticleBehaviors", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightBoxBehaviorsTest::RunTest(const FString&)
{
	for (const TCHAR* Mode:{TEXT("drift"),TEXT("flock"),TEXT("orbit"),TEXT("rain"),TEXT("snow"),TEXT("fire"),TEXT("fountain"),TEXT("fireworks"),TEXT("form")})
	{
		FLightBoxStyle S; S.Count=1024; S.Behavior=Mode; S.Formation=TEXT("heart"); S.Groups=3;
		FLightBoxSimulation Sim; Sim.Reset(S.Count);
		for (int32 I=0; I<180; ++I) { Sim.Step(1.f/30,FVector2D(.3,-.4),.1,S); }
		bool Valid=true;
		for (const FLightMote& P:Sim.Motes) { Valid &= !P.Position.ContainsNaN() && !P.Velocity.ContainsNaN() && P.Position.X>=-275 && P.Position.X<=275 && P.Position.Y>=-180 && P.Position.Y<=180 && P.Position.Z>=45 && P.Position.Z<=335 && P.Trail.Num()==10; }
		TestTrue(FString::Printf(TEXT("%s stays finite and bounded at 1024 particles"),Mode),Valid);
		S.Count=0; Sim.Step(.1,FVector2D::ZeroVector,0,S); TestEqual(TEXT("Particles can be removed"),Sim.Motes.Num(),0);
	}
	FLightBoxStyle Shape; Shape.Count=128; Shape.Behavior=TEXT("form"); Shape.Formation=TEXT("heart"); Shape.Turbulence=0; Shape.Tilt=0; Shape.FormationStrength=3;
	FLightBoxSimulation Sim; Sim.Reset(Shape);for(auto& P:Sim.Motes)P.Position.X+=50; double Before=0,After=0;
	for (int32 I=0; I<Sim.Motes.Num(); ++I) { Before+=FVector::Distance(Sim.Motes[I].Position,Sim.Target(I,Shape.Count,Shape,0)); }
	for (int32 I=0; I<180; ++I) { Sim.Step(1.f/30,FVector2D::ZeroVector,0,Shape); }
	for (int32 I=0; I<Sim.Motes.Num(); ++I) { After+=FVector::Distance(Sim.Motes[I].Position,Sim.Target(I,Shape.Count,Shape,0)); }
	TestTrue(TEXT("Particles really converge to requested shape"),After<Before*.1);
	FLightBoxStyle Flock; Flock.Count=2; Flock.Behavior=TEXT("flock"); Flock.Turbulence=0; Flock.Swirl=0; Flock.Alignment=0; Flock.Separation=0;
	FLightBoxSimulation A,B; A.Reset(Flock); A.Motes[0].Position=FVector(0,0,180); A.Motes[1].Position=FVector(50,0,180); A.Motes[0].Velocity=FVector(0,70,0); B=A;
	Flock.Cohesion=0; A.Step(1.f/120,FVector2D::ZeroVector,0,Flock); Flock.Cohesion=2; B.Step(1.f/120,FVector2D::ZeroVector,0,Flock);
	TestTrue(TEXT("Flocking responds to an actual neighbour, rather than a fixed centre"),B.Motes[0].Velocity.X>A.Motes[0].Velocity.X);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxHistoryTest, "SpresenseMaze.LightBox.History", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightBoxHistoryTest::RunTest(const FString&)
{
	UGameInstance* Instance=NewObject<UGameInstance>(); ULightBoxDirector* D=NewObject<ULightBoxDirector>(Instance);
	D->SelectExample(1); D->SelectExample(3); TestTrue(TEXT("Undo composition"),D->Undo());
	TestEqual(TEXT("Undo restores full egg"),D->GetStyle().Objects.Num(),FLightBoxStyle::Example(1).Objects.Num());
	TestTrue(TEXT("Redo composition"),D->Redo()); TestEqual(TEXT("Redo restores planets"),D->GetStyle().Title,FLightBoxStyle::Example(3).Title);
	D->Undo(); D->SelectExample(2); TestFalse(TEXT("Editing after undo clears redo"),D->CanRedo()); D->Cancel();
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxRealResponsesTest, "SpresenseMaze.LightBox.RealResponseValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLightBoxRealResponsesTest::RunTest(const FString&)
{
	FString Directory;
	if (!FParse::Value(FCommandLine::Get(),TEXT("LightBoxResponses="),Directory)) { return true; }
	TArray<FString> Files; IFileManager::Get().FindFiles(Files,*(Directory/TEXT("*.completion.json")),true,false);
	TestTrue(TEXT("Live response fixtures exist"),Files.Num()>0);
	for (const FString& File:Files)
	{
		FString Json,Error; FLightBoxStyle S;
		FFileHelper::LoadFileToString(Json,*(Directory/File));
		TestTrue(FString::Printf(TEXT("Real OpenClaw response %s: %s"),*File,*Error),FLightBoxStyle::ParseCompletion(Json,S,Error));
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxJournalTest,"SpresenseMaze.LightBox.JournalAndUsage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxJournalTest::RunTest(const FString&)
{
	FLightBoxJournal J; const FString Id=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	FLightBoxStyle Original=FLightBoxStyle::Example(2); Original.Shake=TEXT("none");
	const FString Before=Original.ToJson(); FLightBoxStyle After=Original; After.Shake=TEXT("scatter");
	J.Begin(Id,TEXT("振ったら散るように"),Before,false); J.Finish(Id,After.ToJson(),TEXT("{}"),TEXT("反映済み"),1.2);
	TestEqual(TEXT("Missing usage is not zero credits"),J.Unmetered(),1);
	TestTrue(TEXT("Specific shake difference recorded"),J.Records[0].Difference.Contains(TEXT("振った反応")));
	TestFalse(TEXT("Unchanged objects are not reported as changed"),J.Records[0].Difference.Contains(TEXT("物体")));
	const FString Meter=FString::Printf(TEXT("{\"id\":\"%s\",\"model\":\"gpt-6-luna\",\"input\":3,\"output\":2369,\"cacheRead\":0,\"cacheWrite\":10885,\"total\":13257,\"credits\":0.2545425,\"result\":\"過去ログ\",\"imported\":true}"),*Id);
	TestTrue(TEXT("Real metering parsed"),J.Import(Meter,false)); TestTrue(TEXT("Repeated metering parsed"),J.Import(Meter,false));
	TestEqual(TEXT("Same request not double-counted"),J.Records.Num(),1);
	TestTrue(TEXT("Cache-write-aware estimate retained"),FMath::Abs(J.Credits()-.2545425)<1e-8);
	TestEqual(TEXT("Local applied outcome survives history sync"),J.Records[0].Result,FString(TEXT("反映済み")));
	TestEqual(TEXT("Known usage no longer missing"),J.Unmetered(),0);
	const FString Unknown=FString::Printf(TEXT("{\"id\":\"%s\",\"credits\":null}"),*Id);
	J.Import(Unknown,false); TestTrue(TEXT("Unavailable refresh does not erase known usage"),J.Credits()>0);
	TestTrue(TEXT("Unsafe screenshot name rejected"),FLightBoxJournal::ImagePath(TEXT("../../outside"),TEXT("after")).IsEmpty());

	const FString LateId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	J.Begin(LateId,TEXT("遅い依頼"),Before,false);J.Finish(LateId,FString(),FString(),TEXT("通信失敗"),30);
	auto Late=MakeShared<FJsonObject>();Late->SetStringField(TEXT("id"),LateId);Late->SetStringField(TEXT("after"),After.ToJson());Late->SetNumberField(TEXT("credits"),.2);
	FString Payload;FJsonSerializer::Serialize(Late,TJsonWriterFactory<>::Create(&Payload));J.Import(Payload,false);
	TestTrue(TEXT("Late replies remain available for manual recovery without another inference"),!J.Find(LateId)->After.IsEmpty());
	TestTrue(TEXT("Late reply is honestly labelled not applied"),J.Find(LateId)->Result.Contains(TEXT("未反映")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxAnimationTest,"SpresenseMaze.LightBox.HierarchyAndCurves",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxAnimationTest::RunTest(const FString&)
{
	FLightBoxStyle S;S.Count=0;auto& Root=S.Objects.AddDefaulted_GetRef();Root.Id=TEXT("root");Root.Shape=TEXT("group");Root.Position=FVector(30,20,50);
	FLightBoxMotion Spin;Spin.Type=TEXT("rotate");Spin.Axis=FVector::UpVector;Spin.Amplitude=360;Spin.Period=4;Spin.Wave=TEXT("saw");Root.Motions.Add(Spin);
	auto& Child=S.Objects.AddDefaulted_GetRef();Child.Id=TEXT("arm");Child.Parent=TEXT("root");Child.Position=FVector(40,0,0);Child.Rotation.Z=90;Child.Pivot=FVector(-20,0,0);
	FLightBoxMotion Hinge;Hinge.Type=TEXT("rotate");Hinge.Axis=FVector::RightVector;Hinge.Amplitude=70;Hinge.Period=2;Child.Motions.Add(Hinge);
	const FVector PivotBase=Child.Position+FRotator(0,90,0).RotateVector(Child.Pivot);
	for(float T:{0.f,.2f,.5f,1.f,3.99f}) {
		const auto World=FLightBoxMotionSystem::Evaluate(S,T);
		TestTrue(TEXT("Child hinge stays attached to its moving parent"),World[1].TransformPosition(Child.Pivot).Equals(World[0].TransformPosition(PivotBase),.001));
	}
	const TArray<FVector> Points={FVector(-80,0,0),FVector(0,40,30),FVector(80,0,0),FVector(0,-40,20)};
	TestTrue(TEXT("Closed path has continuous position"),FLightBoxMotionSystem::Curve(Points,.99999,true).Equals(FLightBoxMotionSystem::Curve(Points,.00001,true),.1));
	const FVector D1=FLightBoxMotionSystem::Curve(Points,0,true)-FLightBoxMotionSystem::Curve(Points,.9999,true),D2=FLightBoxMotionSystem::Curve(Points,.0001,true)-FLightBoxMotionSystem::Curve(Points,0,true);
	TestTrue(TEXT("Closed path has continuous tangent"),FVector::DotProduct(D1.GetSafeNormal(),D2.GetSafeNormal())>.999);
	TestTrue(TEXT("Open curve reaches its endpoint"),FLightBoxMotionSystem::Curve(Points,1,false).Equals(Points.Last()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxDeformationTest,"SpresenseMaze.LightBox.VisibleDeformation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxDeformationTest::RunTest(const FString&)
{
	for(int32 Index:{7,8,10,11}) {
		const auto S=FLightBoxStyle::Example(Index); bool Changed=false;
		for(const auto& O:S.Objects) if(O.IsDeforming()) {
			const auto A=FLightBoxMesh::Build(O,.1),B=FLightBoxMesh::Build(O,.4);
			TestEqual(TEXT("Deformation preserves mesh topology"),A.Vertices.Num(),B.Vertices.Num());
			for(int32 I=0;I<B.Vertices.Num();++I) {Changed|=!A.Vertices[I].Equals(B.Vertices[I],.001);TestFalse(TEXT("Finite deformed geometry"),B.Vertices[I].ContainsNaN()||B.Normals[I].ContainsNaN());}
		}
		if(Index!=11) TestTrue(TEXT("Scene contains actual vertex animation"),Changed);
	}
	FLightBoxObject Tube;Tube.Shape=TEXT("tube");Tube.Animation=TEXT("flow");Tube.Curve={FVector(0,0,0),FVector(20,0,50),FVector(50,0,100)};
	const auto A=FLightBoxMesh::Build(Tube,0),B=FLightBoxMesh::Build(Tube,2);
	TestTrue(TEXT("Flow consumes the strand from its source toward its endpoint"),B.Vertices[0].Z>A.Vertices[0].Z+20);
	TestTrue(TEXT("Strand stays attached to its destination"),FVector::Distance(B.Vertices.Last(),Tube.Curve.Last())<Tube.Radius*1.2);
	FLightBoxObject Sphere;const auto Mesh=FLightBoxMesh::Build(Sphere);
	for(int32 I=0;I<Mesh.Triangles.Num();I+=3) {
		const int32 V0=Mesh.Triangles[I],V1=Mesh.Triangles[I+1],V2=Mesh.Triangles[I+2];
		const FVector Face=FVector::CrossProduct(Mesh.Vertices[V1]-Mesh.Vertices[V0],Mesh.Vertices[V2]-Mesh.Vertices[V0]);
		if(Face.SizeSquared()>1e-6) TestTrue(TEXT("UE clockwise faces agree with outward lit normals"),FVector::DotProduct(Face.GetSafeNormal(),(Mesh.Normals[V0]+Mesh.Normals[V1]+Mesh.Normals[V2]).GetSafeNormal())<-.5);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxFormationTest,"SpresenseMaze.LightBox.SchoolSilhouetteAndDistribution",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxFormationTest::RunTest(const FString&)
{
	auto S=FLightBoxStyle::Example(9);S.FormationTravel=0;S.Turbulence=0;S.Tilt=0;
	FLightBoxSimulation Sim;Sim.Reset(S.Count);
	for(int32 K=0;K<480;++K) Sim.Step(1.f/60,FVector2D::ZeroVector,0,S);
	double Error=0;for(int32 I=0;I<S.Count;++I) Error+=FVector::Distance(Sim.Motes[I].Position,FLightBoxSimulation::Target(I,S.Count,S,8));
	TestTrue(TEXT("Neighbour steering retains the giant-fish silhouette"),Error/S.Count<9);
	S.FormationNext=TEXT("heart");const auto A=FLightBoxSimulation::Target(40,S.Count,S,0),B=FLightBoxSimulation::Target(40,S.Count,S,S.FormationPeriod*.5);
	TestTrue(TEXT("Morph changes the target geometry"),FVector::Distance(A,B)>25);
	FLightBoxStyle Drift;Drift.Count=160;Sim.Reset(Drift.Count);
	for(int32 K=0;K<1800;++K) Sim.Step(1.f/30,FVector2D::ZeroVector,0,Drift);
	FVector Mean(0);for(const auto& P:Sim.Motes) Mean+=P.Position;Mean/=Sim.Motes.Num();double Variance=0;for(const auto& P:Sim.Motes) Variance+=FVector::DistSquared(P.Position,Mean);
	TestTrue(TEXT("Unattended drift stays spatially distributed after 60 seconds"),Variance/Sim.Motes.Num()>3000);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxExhibitValidationTest,"SpresenseMaze.LightBox.ExhibitValidationAndFraming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxExhibitValidationTest::RunTest(const FString&)
{
	for(int32 I=7;I<13;++I) {
		const auto S=FLightBoxStyle::Example(I);FLightBoxStyle Parsed;FString Error,Why;
		TestTrue(FString::Printf(TEXT("Exhibit %d schema round trip: %s"),I,*Error),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error,&Why));
		const FBox Box=FLightBoxMotionSystem::Bounds(S);
		for(float Aspect:{16.f/9,2340.f/1080}) {
			const auto Camera=FLightBoxMotionSystem::Camera(S,Aspect);const double TanH=FMath::Tan(FMath::DegreesToRadians(29.));
			for(int32 K=0;K<8;++K) {const FVector P=Camera.InverseTransformPosition(FVector(K&1?Box.Max.X:Box.Min.X,K&2?Box.Max.Y:Box.Min.Y,K&4?Box.Max.Z:Box.Min.Z));
				TestTrue(TEXT("Animated exhibit fits inside the safe camera region"),P.X>0&&FMath::Abs(P.Y/P.X)<TanH*.89&&FMath::Abs(P.Z/P.X)<TanH/Aspect*.89);
			}
		}
	}
	FString Why;FLightBoxStyle Still;Still.Count=0;
	TestFalse(TEXT("Static birds do not pass flying intent"),FLightBoxStyle::ValidateIntent(TEXT("かもめが飛んでいる"),Still,Why));
	TestTrue(TEXT("Real flying exhibit passes"),FLightBoxStyle::ValidateIntent(TEXT("かもめが飛んでいる"),FLightBoxStyle::Example(7),Why));
	TestFalse(TEXT("Static noodles do not pass slurping intent"),FLightBoxStyle::ValidateIntent(TEXT("すすられてるラーメン"),Still,Why));
	TestTrue(TEXT("Flowing strands pass"),FLightBoxStyle::ValidateIntent(TEXT("すすられてるラーメン"),FLightBoxStyle::Example(8),Why));
	TestTrue(TEXT("Shaped school passes Swimmy"),FLightBoxStyle::ValidateIntent(TEXT("スイミーのように"),FLightBoxStyle::Example(9),Why));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxHierarchyValidationTest,"SpresenseMaze.LightBox.RejectInvalidHierarchyAtomically",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxHierarchyValidationTest::RunTest(const FString&)
{
	auto Bad=FLightBoxStyle::Example(11);Bad.Objects[0].Parent=Bad.Objects[0].Id;auto Keep=FLightBoxStyle::Example(8);const auto Before=Keep.ToJson();FString Error;
	TestFalse(TEXT("Self-reference rejected"),FLightBoxStyle::Parse(Bad.ToJson(),Keep,Error));
	Bad.Objects[0].Parent=TEXT("missing");TestFalse(TEXT("Missing parent rejected"),FLightBoxStyle::Parse(Bad.ToJson(),Keep,Error));
	Bad.Objects[0].Parent=Bad.Objects[2].Id;Bad.Objects[2].Parent=Bad.Objects[0].Id;TestFalse(TEXT("Cycle rejected"),FLightBoxStyle::Parse(Bad.ToJson(),Keep,Error));
	TestEqual(TEXT("Rejected scene cannot partially replace the current scene"),Keep.ToJson(),Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxSlowInferenceTest,"SpresenseMaze.LightBox.InferenceSurvivesThirtySecondSilence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxSlowInferenceTest::RunTest(const FString&)
{
	if(!FParse::Param(FCommandLine::Get(),TEXT("LightBoxMockGateway"))) return true;
	auto* Instance=NewObject<UGameInstance>();auto* D=NewObject<ULightBoxDirector>(Instance);D->AddToRoot();
	D->Endpoint=TEXT("http://127.0.0.1:18879");D->BearerToken=TEXT("lightbox-fixture-only");D->Submit(TEXT("delayed-scene"));
	const double Started=FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLightBox([this,D,Started](){
		if(D->bThinking&&FPlatformTime::Seconds()-Started<45) return false;
		TestFalse(TEXT("Delayed inference finishes"),D->bThinking);
		TestTrue(TEXT("Fixture really exceeds the engine default activity timeout"),FPlatformTime::Seconds()-Started>=31);
		TestEqual(TEXT("Complete delayed response still applies"),D->GetStyle().Title,FString(TEXT("青い海")));
		D->Cancel();D->RemoveFromRoot();return true;
	}));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxBoundaryTokenTest,"SpresenseMaze.LightBox.ProviderBoundaryTokens",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxBoundaryTokenTest::RunTest(const FString&)
{
	FLightBoxStyle S,Out;S.Note=TEXT("文字列中の</|assistant|>は保持");FString Error;
	for(const TCHAR* Token:{TEXT("</|assistant|>"),TEXT("<|im_end|>"),TEXT("<|eot_id|>"),TEXT("</s>")}) {
		TestTrue(TEXT("Known leaked terminator is accepted at the boundary"),FLightBoxStyle::Parse(S.ToJson()+Token,Out,Error));
		TestEqual(TEXT("A terminator inside a JSON string is not changed"),Out.Note,S.Note);
	}
	TestFalse(TEXT("Arbitrary trailing instructions remain invalid"),FLightBoxStyle::Parse(S.ToJson()+TEXT(" execute something"),Out,Error));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxStreamTest,"SpresenseMaze.LightBox.ContinuousStreamAndConsumption",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxStreamTest::RunTest(const FString&)
{
	FLightBoxObject O;O.Shape=TEXT("tube");O.Animation=TEXT("stream");O.Curve={FVector(0),FVector(0,0,50),FVector(40,0,100)};O.Radius=2;
	for(float T:{0.f,1.f,2.f,5.f}) {
		const auto Mesh=FLightBoxMesh::Build(O,T);
		TestTrue(TEXT("Continuous material never detaches from its source"),FVector::Dist(Mesh.Vertices[0],O.Curve[0])<3);
		TestTrue(TEXT("Continuous material stays connected to its destination"),FVector::Dist(Mesh.Vertices.Last(),O.Curve.Last())<3);
	}
	const auto A=FLightBoxMesh::Build(O,0),B=FLightBoxMesh::Build(O,.3f);
	TestFalse(TEXT("Continuous material has traveling brightness"),A.Colors[11].Equals(B.Colors[11],.001));
	O.Animation=TEXT("flow");const auto Consumed=FLightBoxMesh::Build(O,2);
	TestTrue(TEXT("Noodle consumption still advances the source"),Consumed.Vertices[0].Z>30);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxAnchorsTest,"SpresenseMaze.LightBox.NamedJointsStayConnected",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxAnchorsTest::RunTest(const FString&)
{
	FLightBoxStyle S;S.Count=0;
	auto& Stem=S.Objects.AddDefaulted_GetRef();Stem.Id=TEXT("stem");Stem.Shape=TEXT("cylinder");Stem.Position=FVector(20,0,50);Stem.Size=FVector(20,20,100);
	FLightBoxMotion Move;Move.Type=TEXT("translate");Move.Axis=FVector::RightVector;Move.Amplitude=20;Stem.Motions.Add(Move);
	auto& Petal=S.Objects.AddDefaulted_GetRef();Petal.Id=TEXT("petal");Petal.Shape=TEXT("petal");Petal.Parent=TEXT("stem");Petal.ParentAnchor=TEXT("z+");Petal.Anchor=TEXT("x-");Petal.Position=FVector::ZeroVector;Petal.Size=FVector(90,40,5);Petal.Rotation.Z=120;
	FLightBoxMotion Turn;Turn.Type=TEXT("rotate");Turn.Axis=FVector(.866,.5,0);Turn.Amplitude=60;Turn.Period=4;Petal.Motions.Add(Turn);
	for(float T:{0.f,.5f,1.f,3.f}) {
		const auto World=FLightBoxMotionSystem::Evaluate(S,T);
		TestTrue(TEXT("Petal root follows stem top throughout opening"),World[1].TransformPosition(FLightBoxMotionSystem::Anchor(S.Objects[1],TEXT("x-"))).Equals(World[0].TransformPosition(FLightBoxMotionSystem::Anchor(S.Objects[0],TEXT("z+"))),.001));
	}
	FLightBoxStyle Parsed;FString Error;TestTrue(TEXT("Anchor scene round trip"),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxBoundCurveTest,"SpresenseMaze.LightBox.StreamFollowsMovingEndpoints",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxBoundCurveTest::RunTest(const FString&)
{
	FLightBoxStyle S;S.Count=0;
	FLightBoxObject Source;Source.Id=TEXT("source");Source.Position=FVector(-100,0,80);Source.Size=FVector(50);Source.Animation=TEXT("bob");S.Objects.Add(Source);
	FLightBoxObject Target;Target.Id=TEXT("mouth");Target.Position=FVector(100,0,140);Target.Size=FVector(30);Target.Animation=TEXT("orbit");S.Objects.Add(Target);
	FLightBoxObject Tube;Tube.Id=TEXT("strand");Tube.Shape=TEXT("tube");Tube.Position=FVector(40,10,60);Tube.Curve={FVector(0),FVector(30,0,50),FVector(60,0,80)};Tube.StartObject=TEXT("source");Tube.StartAnchor=TEXT("z+");Tube.EndObject=TEXT("mouth");Tube.EndOffset=FVector(0,-10,0);S.Objects.Add(Tube);
	for(float T:{0.f,1.f,2.5f,6.f}) {
		const auto World=FLightBoxMotionSystem::Evaluate(S,T);const auto Resolved=FLightBoxMotionSystem::ResolveBindings(S,World);
		TestTrue(TEXT("Source binding follows movement in world space"),World[2].TransformPosition(Resolved[2].Curve[0]).Equals(World[0].TransformPosition(FVector(0,0,25)),.001));
		TestTrue(TEXT("Destination binding follows movement in world space"),World[2].TransformPosition(Resolved[2].Curve.Last()).Equals(World[1].TransformPosition(FVector(0,-10,0)),.001));
	}
	FLightBoxStyle Keep;FString Error;TestTrue(TEXT("Binding scene round trip"),FLightBoxStyle::Parse(S.ToJson(),Keep,Error));const auto Before=Keep.ToJson();S.Objects[2].EndObject=TEXT("missing");
	TestFalse(TEXT("Unknown binding is rejected"),FLightBoxStyle::Parse(S.ToJson(),Keep,Error));TestEqual(TEXT("Rejected binding cannot partly change a scene"),Keep.ToJson(),Before);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxNewShapesTest,"SpresenseMaze.LightBox.NewShapesAreBoundedAndAnimate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxNewShapesTest::RunTest(const FString&)
{
	for(const TCHAR* Shape:{TEXT("butterfly"),TEXT("whale"),TEXT("petal"),TEXT("roof"),TEXT("gear"),TEXT("blossom")}) {
		FLightBoxStyle S;S.Count=0;FLightBoxObject O;O.Id=TEXT("subject");O.Shape=Shape;O.Size=FVector(90,120,30);O.Animation=O.Shape==TEXT("blossom")?TEXT("bloom"):TEXT("flutter");S.Objects.Add(O);
		FLightBoxStyle Parsed;FString Error;TestTrue(TEXT("New vocabulary round trip"),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error));
		const auto A=FLightBoxMesh::Build(O,0),B=FLightBoxMesh::Build(O,.3f);bool Changed=false;
		TestEqual(TEXT("Stable topology"),A.Vertices.Num(),B.Vertices.Num());
		for(int32 I=0;I<B.Vertices.Num();++I){TestFalse(TEXT("Finite points and shading normals"),B.Vertices[I].ContainsNaN()||B.Normals[I].ContainsNaN());Changed|=!A.Vertices[I].Equals(B.Vertices[I],.001);}
		if(O.Shape==TEXT("butterfly")||O.Shape==TEXT("blossom"))TestTrue(TEXT("Butterfly wings visibly deform"),Changed);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxMorphTrackingTest,"SpresenseMaze.LightBox.MorphTracksWithoutInitialScatter",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxMorphTrackingTest::RunTest(const FString&)
{
    for(float Speed:{.4f,1.6f}) {
        FLightBoxStyle S;S.Count=128;S.Behavior=TEXT("form");S.Formation=TEXT("sphere");S.FormationNext=TEXT("star");S.FormationPeriod=8;S.FormationStrength=3;S.Turbulence=0;S.Spread=120;S.Center=FVector(0,0,180);S.Motion=Speed;
        FLightBoxSimulation Sim;Sim.Reset(S);double InitialError=0,WorstRms=0;
        for(int32 I=0;I<S.Count;++I)InitialError+=FVector::DistSquared(Sim.Motes[I].Position,FLightBoxSimulation::Target(I,S.Count,S,0));
        TestTrue(TEXT("Silhouette starts at its designed positions"),InitialError<.01);
        for(int32 Frame=0;Frame<720;++Frame){Sim.Step(1.f/60,FVector2D::ZeroVector,0,S);if(Frame%30==0){double Error=0;for(int32 I=0;I<S.Count;++I)Error+=FVector::DistSquared(Sim.Motes[I].Position,FLightBoxSimulation::Target(I,S.Count,S,(Frame+1)/60.f*Speed));WorstRms=FMath::Max(WorstRms,FMath::Sqrt(Error/S.Count));}}
        TestTrue(FString::Printf(TEXT("Morph RMS tracking error %.2fcm at speed %.1f"),WorstRms,Speed),WorstRms<8);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxEmitterTest,"SpresenseMaze.LightBox.EmitterUsesRequestedHeight",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxEmitterTest::RunTest(const FString&)
{
    for(const TCHAR* Behavior:{TEXT("fire"),TEXT("fountain")}){FLightBoxStyle S;S.Count=32;S.Behavior=Behavior;S.Center=FVector(20,30,140);S.Spread=30;FLightBoxSimulation Sim;Sim.Reset(S);
        TestTrue(TEXT("No full-volume startup explosion"),Sim.Motes[0].Position.Z>=S.Center.Z);
        Sim.Motes[0].Position.Z=340;Sim.Step(.001f,FVector2D::ZeroVector,0,S);TestTrue(TEXT("Respawn at requested emitter, not floor"),FMath::IsNearlyEqual(Sim.Motes[0].Position.Z,S.Center.Z,.1));
        Sim.Motes[0].Position.Z=S.Center.Z-3;Sim.Motes[0].Age=1;Sim.Step(.001f,FVector2D::ZeroVector,0,S);TestTrue(TEXT("Returning water re-emits at the spout before falling through its pool"),FMath::IsNearlyEqual(Sim.Motes[0].Position.Z,S.Center.Z,.1));
    }return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxRestartJournalTest,"SpresenseMaze.LightBox.RestartMarksInterruptedRequest",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxRestartJournalTest::RunTest(const FString&)
{
    FLightBoxJournal Journal;Journal.LastScene=FLightBoxStyle::Preset(1).ToJson();const FString Before=Journal.LastScene;
    Journal.Begin(TEXT("pending"),TEXT("確認"),Before,false);Journal.Records[0].Credits=.2;
    Journal.Begin(TEXT("done"),TEXT("完了済み"),Before,false);Journal.Finish(TEXT("done"),Before,TEXT(""),TEXT("反映済み"),1);
    TestEqual(TEXT("Exactly the interrupted request is marked"),Journal.RecoverInterrupted(),1);
    TestFalse(TEXT("Interrupted request cannot remain waiting"),Journal.Records[0].Result==TEXT("応答待ち"));
    TestEqual(TEXT("Known usage is preserved"),Journal.Records[0].Credits,.2);
    TestEqual(TEXT("Applied request stays applied"),Journal.Records[1].Result,FString(TEXT("反映済み")));
    TestEqual(TEXT("Prior scene remains unchanged"),Journal.LastScene,Before);
    TestEqual(TEXT("Recovery is idempotent"),Journal.RecoverInterrupted(),0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxSchoolIntentTest,"SpresenseMaze.LightBox.IndependentSchoolIntent",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxSchoolIntentTest::RunTest(const FString&)
{
    FLightBoxStyle Base,Next;Next.Count=256;Next.Groups=3;Next.Behavior=TEXT("flock");Next.Formation=TEXT("fish");FString Why;
    FLightBoxStyle Shaped=Next;
    FLightBoxStyle::CompleteGenerated(TEXT("256匹の魚が3つの群れで泳ぐ景色にして"),Base,Next,Why);
    TestEqual(TEXT("Ordinary independent schools do not collapse into one silhouette"),Next.Formation,FString(TEXT("volume")));
    TestEqual(TEXT("Requested group count retained"),Next.Groups,3);TestEqual(TEXT("Requested fish count retained"),Next.Count,256);
    Next=Shaped;FLightBoxStyle::CompleteGenerated(TEXT("魚が3つの群れで大きな魚の形を作って"),Base,Next,Why);
    TestEqual(TEXT("Explicit collective shape stays available"),Next.Formation,FString(TEXT("fish")));
    Next=Shaped;FLightBoxStyle::CompleteGenerated(TEXT("3つの魚の群れの色だけを金色にして"),Base,Next,Why);
    TestEqual(TEXT("Colour edit does not change the existing formation"),Next.Formation,FString(TEXT("fish")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxScopedEditTest,"SpresenseMaze.LightBox.ScopedEditPreservesOtherFields",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxScopedEditTest::RunTest(const FString&)
{
    const auto Base=FLightBoxStyle::Example(8);FString Why;auto Next=Base;Next.Background=FLinearColor::Red;
    TestTrue(TEXT("Background edit accepted"),FLightBoxStyle::CompleteGenerated(TEXT("背景だけを夕焼けにして"),Base,Next,Why));
    Next.Objects[0].Position.X+=10;TestTrue(TEXT("Background edit tolerates model drift by applying only its requested field"),FLightBoxStyle::CompleteGenerated(TEXT("背景だけを夕焼けにして"),Base,Next,Why));
    TestTrue(TEXT("Unrequested geometry is preserved exactly"),Next.Objects[0].Position.Equals(Base.Objects[0].Position));
    Next=Base;Next.Ambient=2;Next.Lights.Add(FLightBoxLight());TestTrue(TEXT("Lighting edit accepted"),FLightBoxStyle::CompleteGenerated(TEXT("照明だけを明るくして"),Base,Next,Why));
    Next.Objects[0].Color=FLinearColor::Red;TestTrue(TEXT("Lighting-only patch is accepted"),FLightBoxStyle::CompleteGenerated(TEXT("照明だけを明るくして"),Base,Next,Why));
    TestTrue(TEXT("Part color remains unchanged"),Next.Objects[0].Color.Equals(Base.Objects[0].Color));
    Next=Base;Next.CameraPitch=50;TestTrue(TEXT("Camera edit accepted"),FLightBoxStyle::CompleteGenerated(TEXT("カメラだけ上から"),Base,Next,Why));
    Next.Count+=1;TestTrue(TEXT("Camera-only patch is accepted"),FLightBoxStyle::CompleteGenerated(TEXT("カメラだけ上から"),Base,Next,Why));TestEqual(TEXT("Camera edit preserves count"),Next.Count,Base.Count);
    auto Sculpture=Base;Sculpture.bPointCloud=true;Sculpture.Count=0;Next=Sculpture;Next.PointCount=1000000;Next.Objects[0].Color=FLinearColor::Red;
    TestTrue(TEXT("Point budget edit accepted"),FLightBoxStyle::CompleteGenerated(TEXT("粒数だけ100万にして"),Sculpture,Next,Why));
    TestEqual(TEXT("Point budget changes the GPU point count"),Next.PointCount,1000000);TestEqual(TEXT("CPU particles remain off"),Next.Count,0);TestTrue(TEXT("Point count edit preserves the subject"),Next.Objects[0].Color.Equals(Sculpture.Objects[0].Color));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxInheritedParticlesTest,"SpresenseMaze.LightBox.InheritedDecorationIsRemoved",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxInheritedParticlesTest::RunTest(const FString&)
{
    FLightBoxStyle Base,Next=Base;FLightBoxObject O;O.Id=TEXT("bird");O.Shape=TEXT("bird");Next.Objects.Add(O);FString Why;
    TestTrue(TEXT("New subject accepted"),FLightBoxStyle::CompleteGenerated(TEXT("二羽の鳥が飛ぶようにして"),Base,Next,Why));TestEqual(TEXT("Copied irrelevant default particles removed"),Next.Count,0);
    Next=Base;Next.Objects.Add(O);FLightBoxStyle::CompleteGenerated(TEXT("鳥と光の粒を置いて"),Base,Next,Why);TestEqual(TEXT("Explicit particles retained"),Next.Count,Base.Count);
    Next=Base;Next.Objects.Add(O);FLightBoxStyle::CompleteGenerated(TEXT("鳥を追加して"),Base,Next,Why);TestEqual(TEXT("Additive edit retains existing particles"),Next.Count,Base.Count);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxLocalAxisTest,"SpresenseMaze.LightBox.LocalAxleAndRoof",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxLocalAxisTest::RunTest(const FString&)
{
    FLightBoxStyle S;S.Count=0;FLightBoxObject O;O.Id=TEXT("wheel");O.Rotation=FVector(90,0,0);FLightBoxMotion M;M.Type=TEXT("rotate");M.Space=TEXT("local");M.Axis=FVector::UpVector;M.Amplitude=90;M.Period=4;O.Motions.Add(M);S.Objects.Add(O);
    const auto A=FLightBoxMotionSystem::Evaluate(S,0),B=FLightBoxMotionSystem::Evaluate(S,1);
    TestTrue(TEXT("Local axle direction stays fixed"),A[0].TransformVector(FVector::UpVector).Equals(B[0].TransformVector(FVector::UpVector),.001));
    O.Shape=TEXT("roof");O.Size=FVector(200,160,70);O.Rotation=FVector::ZeroVector;O.Motions.Reset();const auto Mesh=FLightBoxMesh::Build(O);TestTrue(TEXT("Roof has closed geometry"),Mesh.Triangles.Num()>=24);for(const auto& V:Mesh.Vertices)TestFalse(TEXT("Finite roof"),V.ContainsNaN());S.Objects[0]=O;FLightBoxStyle Parsed;FString Error;TestTrue(TEXT("Roof schema accepted"),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxDeformingFramingTest,"SpresenseMaze.LightBox.DeformingSubjectFitsCamera",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxDeformingFramingTest::RunTest(const FString&)
{
    FLightBoxStyle S;S.Count=0;FLightBoxObject O;O.Id=TEXT("flower");O.Shape=TEXT("blossom");O.Size=FVector(190,190,210);O.Position.Z=105;O.Color=FLinearColor(1,.2f,.5f);O.Animation=TEXT("bloom");O.Amplitude=80;S.Objects.Add(O);
    const float Aspect=2340.f/1080;const auto Camera=FLightBoxMotionSystem::Camera(S,Aspect);double Worst=0;
    for(float T:{0.f,1.f,2.f,3.14f,4.f,6.f}){const auto Mesh=FLightBoxMesh::Build(O,T);const auto World=FLightBoxMotionSystem::Evaluate(S,T);for(const auto& V:Mesh.Vertices){const FVector Q=Camera.InverseTransformPosition(World[0].TransformPosition(V));Worst=FMath::Max(Worst,FMath::Max(FMath::Abs(Q.Y)/Q.X/FMath::Tan(FMath::DegreesToRadians(29.)),FMath::Abs(Q.Z)/Q.X/(FMath::Tan(FMath::DegreesToRadians(29.))/Aspect)));}}
    TestTrue(TEXT("All open/closed petal geometry fits the screen"),Worst<.93);
    const auto Mesh=FLightBoxMesh::Build(O);TestTrue(TEXT("Stem tint is green independent of petal colour"),Mesh.Colors[0].G>Mesh.Colors[0].R*2);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxSensorsTest,"SpresenseMaze.LightBox.PhoneSensors",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxSensorsTest::RunTest(const FString&)
{
	FLightBoxStyle S; S.Count=96; S.Behavior=TEXT("drift"); S.Turbulence=0; S.Swirl=0;
	auto Run=[&](const FLightBoxEnvironment& Env){FLightBoxSimulation Sim;Sim.Reset(S);for(int32 I=0;I<60;++I)Sim.Step(1.f/30,FVector2D::ZeroVector,0,S,Env);return Sim;};
	auto Mean=[](const FLightBoxSimulation& Sim){FVector M(0);for(const auto& P:Sim.Motes)M+=P.Position;return M/FMath::Max(1,Sim.Motes.Num());};
	const FLightBoxSimulation Base=Run(FLightBoxEnvironment());
	FLightBoxSimulation Legacy;Legacy.Reset(S);for(int32 I=0;I<60;++I)Legacy.Step(1.f/30,FVector2D::ZeroVector,0,S);
	bool Same=true;for(int32 I=0;I<Base.Motes.Num();++I)Same&=Base.Motes[I].Position.Equals(Legacy.Motes[I].Position,0.0);
	TestTrue(TEXT("A quiet environment changes nothing"),Same);
	FLightBoxEnvironment Breath;Breath.Wind=FVector(0,1,0);Breath.Flutter=1;
	TestTrue(TEXT("Breath carries the light away from the camera"),Mean(Run(Breath)).Y>Mean(Base).Y+20);
	FLightBoxEnvironment Warm;Warm.Warmth=1;
	TestTrue(TEXT("Warmth makes the light rise"),Mean(Run(Warm)).Z>Mean(Base).Z+15);
	auto Spread=[&](const FLightBoxSimulation& Sim){double D=0;for(const auto& P:Sim.Motes)D+=FVector::Distance(P.Position,S.Center);return D/Sim.Motes.Num();};
	FLightBoxEnvironment Dark;Dark.Gather=1;FLightBoxEnvironment Bright;Bright.Gather=-1;
	TestTrue(TEXT("Darkness or a hand gathers the light"),Spread(Run(Dark))<Spread(Base));
	TestTrue(TEXT("Bright light scatters it"),Spread(Run(Bright))>Spread(Base));

	float W=0;for(int32 I=0;I<90;++I)W=FLightBoxSensorReader::ApproachWarmth(W,true,1.f/30);
	TestTrue(TEXT("A resting palm warms fully in about three seconds"),W>.99f);
	for(int32 I=0;I<90;++I)W=FLightBoxSensorReader::ApproachWarmth(W,false,1.f/30);
	TestTrue(TEXT("It cools more slowly than it warms"),W>.4f&&W<.6f);
	float D,B;FLightBoxSensorReader::LightLevels(300,300,D,B);TestTrue(TEXT("The room level is neutral"),D==0&&B==0);
	FLightBoxSensorReader::LightLevels(20,300,D,B);TestTrue(TEXT("Covering the sensor is dark"),D>.9f&&B==0);
	FLightBoxSensorReader::LightLevels(5000,300,D,B);TestTrue(TEXT("A torch is bright"),B>.9f&&D==0);
	FLightBoxSensorReader::LightLevels(150,300,D,B);TestTrue(TEXT("A passing shadow (2x darker) is ignored"),D==0);
	FLightBoxSensorReader::LightLevels(600,300,D,B);TestTrue(TEXT("A lamp across the room (2x) is ignored"),B==0);
	TestEqual(TEXT("Turning the phone in the Earth's field is not a magnet"),FLightBoxSensorReader::MagnetStrength(60,45),0.f);
	TestTrue(TEXT("A nearby magnet is"),FLightBoxSensorReader::MagnetStrength(500,45)>.9f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxSensorRulesTest,"SpresenseMaze.LightBox.SensorRulesFromWords",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxSensorRulesTest::RunTest(const FString&)
{
	const FLightBoxStyle Base;
	auto WithSensors=[&](const FString& Sensors){FString J=Base.ToJson();J.RemoveFromEnd(TEXT("}"));return J+TEXT(",\"sensors\":")+Sensors+TEXT("}");};
	FLightBoxStyle Quiet;FString Error;
	TestTrue(TEXT("A scene can switch sound off"),FLightBoxStyle::Parse(WithSensors(TEXT("{\"sound\":0}")),Quiet,Error));
	TestEqual(TEXT("Sound is off"),Quiet.Sensors.Sound,0.f);
	TestEqual(TEXT("Unnamed sensors keep their default"),Quiet.Sensors.Light,1.f);
	FLightBoxStyle Bad;
	TestFalse(TEXT("Out of range sensitivity is rejected"),FLightBoxStyle::Parse(WithSensors(TEXT("{\"sound\":3}")),Bad,Error));
	TestFalse(TEXT("Unknown sensors are rejected"),FLightBoxStyle::Parse(WithSensors(TEXT("{\"smell\":1}")),Bad,Error));
	FLightBoxStyle Round;TestTrue(TEXT("Saved scenes keep the rule"),FLightBoxStyle::Parse(Quiet.ToJson(),Round,Error)&&Round.Sensors.Sound==0.f);
	TestEqual(TEXT("Sensitivity never rebuilds the sculpture"),Quiet.RenderContentJson(),Base.RenderContentJson());

	FLightBoxStyle Later;TestTrue(TEXT("A later edit parses"),FLightBoxStyle::Parse(Base.ToJson(),Later,Error));Later.Color=FLinearColor::Red;FString Why;
	FLightBoxStyle::CompleteGenerated(TEXT("色を赤にして"),Quiet,Later,Why);
	TestEqual(TEXT("A later edit that does not mention sensors keeps sound off"),Later.Sensors.Sound,0.f);
	FLightBoxStyle Drift;FLightBoxStyle::Parse(WithSensors(TEXT("{\"sound\":0}")),Drift,Error);Drift.Motion=2.f;
	FLightBoxStyle::CompleteGenerated(TEXT("音に反応しないようにして"),Base,Drift,Why);
	TestEqual(TEXT("A sensor-only request changes only the sensors"),Drift.Motion,Base.Motion);
	TestEqual(TEXT("...and applies them"),Drift.Sensors.Sound,0.f);

	FLightBoxSensorReading R;R.Breath=.8f;R.bClap=true;R.Darkness=.5f;
	FLightBoxSensorGains G;G.Sound=0;G.Light=2;
	const FLightBoxSensorReading F=FLightBoxSensorReader::ApplyGains(R,G);
	TestTrue(TEXT("Sound off drops breath and claps"),F.Breath==0&&!F.bClap);
	TestEqual(TEXT("Light sensitivity scales darkness"),F.Darkness,1.f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxHorseReplyTest,"SpresenseMaze.LightBox.RunningHorseReply",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxHorseReplyTest::RunTest(const FString&)
{
    for (int32 I = 0; I < 2; ++I)
    {
        FString Content;
        const FString Path = FPaths::ProjectDir() / FString::Printf(TEXT("Source/SpresenseMaze/Tests/Fixtures/RunningHorse-%d.json"), I);
        if (!TestTrue(TEXT("Real failed horse reply fixture exists"), FFileHelper::LoadFileToString(Content, *Path))) { return false; }
        auto Message = MakeShared<FJsonObject>(); Message->SetStringField(TEXT("content"), Content);
        auto Choice = MakeShared<FJsonObject>(); Choice->SetStringField(TEXT("finish_reason"), TEXT("stop")); Choice->SetObjectField(TEXT("message"), Message);
        auto Envelope = MakeShared<FJsonObject>(); Envelope->SetArrayField(TEXT("choices"), {MakeShared<FJsonValueObject>(Choice)});
        FString Json; FJsonSerializer::Serialize(Envelope, TJsonWriterFactory<>::Create(&Json));
        FLightBoxStyle Scene; FString Error, Diagnostic;
        if (!TestTrue(FString::Printf(TEXT("Horse reply %d: %s / %s"), I, *Error, *Diagnostic), FLightBoxStyle::ParseCompletion(Json, Scene, Error, &Diagnostic)))
        { AddError(Error + TEXT(" / ") + Diagnostic); continue; }
        if (I == 0) { TestTrue(TEXT("Articulated horse keeps all animation frames"), Scene.Story.Num() >= 4); }
        else { TestTrue(TEXT("The retry retains moving legs"), Scene.Objects.ContainsByPredicate([](const auto& O){return !O.Motions.IsEmpty();})); }
        TestTrue(TEXT("Running intent remains valid"), FLightBoxStyle::ValidateIntent(TEXT("走ってる馬"), Scene, Diagnostic));
        FLightBoxStyle RoundTrip; TestTrue(TEXT("Normalized scene can be saved and restored strictly"), FLightBoxStyle::Parse(Scene.ToJson(), RoundTrip, Error));
    }
    return true;
}

#endif
