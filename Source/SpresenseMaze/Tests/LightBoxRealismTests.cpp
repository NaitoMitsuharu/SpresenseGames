#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../LightBoxPointData.h"
#include "../LightBoxGeometry.h"

namespace
{
    FLightBoxStyle Single(const FLightBoxObject& O)
    {
        FLightBoxStyle S;S.Count=0;S.PointCount=20000;S.Background=FLinearColor::Black;S.Objects={O};return S;
    }
    FLightBoxObject Body()
    {
        FLightBoxObject O;O.Id=TEXT("body");O.Shape=TEXT("sphere");O.Position=FVector(0,0,150);O.Size=FVector(100);O.Color=FLinearColor(.8f,.8f,.8f);return O;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxDetailSchemaTest,"SpresenseMaze.LightBox.Realism.DetailAndCrystalSchema",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxDetailSchemaTest::RunTest(const FString&)
{
    auto O=Body();O.Detail=TEXT("fur");O.DetailLength=5;auto S=Single(O);S.PointStyle=TEXT("crystal");
    FLightBoxStyle Parsed;FString Error;
    TestTrue(TEXT("Detail and crystal style round trip"),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error));
    TestEqual(TEXT("Detail kind kept"),Parsed.Objects[0].Detail,FString(TEXT("fur")));
    TestEqual(TEXT("Detail length kept"),Parsed.Objects[0].DetailLength,5.f);
    TestEqual(TEXT("Crystal style kept"),Parsed.PointStyle,FString(TEXT("crystal")));
    TestFalse(TEXT("Plain scenes do not grow new keys"),Single(Body()).ToJson().Contains(TEXT("\"detail\""))||Single(Body()).ToJson().Contains(TEXT("\"style\"")));
    FString Bad=S.ToJson().Replace(TEXT("\"fur\""),TEXT("\"hair\""));
    TestFalse(TEXT("Unknown detail kinds fail atomically"),FLightBoxStyle::Parse(Bad,Parsed,Error));
    Bad=S.ToJson().Replace(TEXT("\"crystal\""),TEXT("\"glass\""));
    TestFalse(TEXT("Unknown point styles fail atomically"),FLightBoxStyle::Parse(Bad,Parsed,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxFurTest,"SpresenseMaze.LightBox.Realism.FurStrandsLeaveTheSurface",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxFurTest::RunTest(const FString&)
{
    auto Measure=[](const FLightBoxStyle& S,double& Mean,double& Spread){
        const auto Points=LightBoxPoints::Sample(S);double Sum=0,Sq=0;int32 N=0;
        for(const auto& P:Points){if(P.Owner!=TEXT("body"))continue;const double D=FVector3f::Distance(P.Position,FVector3f(0,0,150));Sum+=D;Sq+=D*D;++N;}
        Mean=Sum/FMath::Max(1,N);Spread=FMath::Sqrt(FMath::Max(0.,Sq/FMath::Max(1,N)-Mean*Mean));
    };
    double SmoothMean,SmoothSpread,FurMean,FurSpread;
    Measure(Single(Body()),SmoothMean,SmoothSpread);
    auto Furry=Body();Furry.Detail=TEXT("fur");Furry.DetailLength=8;Measure(Single(Furry),FurMean,FurSpread);
    TestTrue(TEXT("A bare surface stays on its radius"),FMath::Abs(SmoothMean-50)<1.5&&SmoothSpread<1.5);
    TestTrue(TEXT("Fur grains stand outside the skin"),FurMean>SmoothMean+2);
    TestTrue(TEXT("Fur forms a volume of strands, not a second shell"),FurSpread>1.5);
    for(const auto& P:LightBoxPoints::Sample(Single(Furry)))TestFalse(TEXT("Finite fur grains"),P.Position.ContainsNaN());
    auto Moving=Furry;Moving.Animation=TEXT("spin");
    double MovingMean,MovingSpread;Measure(Single(Moving),MovingMean,MovingSpread);
    TestTrue(TEXT("Animated parts stay bound to their animated surface"),FMath::Abs(MovingMean-SmoothMean)<1.5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxLoftLightingTest,"SpresenseMaze.LightBox.Realism.LoftReceivesKeyLight",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxLoftLightingTest::RunTest(const FString&)
{
    // Every common body primitive must be lit from the light's side, not from inside.
    for(const TCHAR* Shape:{TEXT("loft"),TEXT("lathe"),TEXT("sphere"),TEXT("box"),TEXT("cylinder"),TEXT("rock")}){
        FLightBoxObject O;O.Id=TEXT("body");O.Shape=Shape;O.Position=FVector(0,0,150);O.Size=FVector(140,100,100);O.Color=FLinearColor(.7f,.7f,.7f);
        O.Sections={FVector(-.5,0,0),FVector(-.3,.35,.35),FVector(0,.5,.5),FVector(.3,.35,.35),FVector(.5,0,0)};
        O.Profile={FVector2D(-.5,0),FVector2D(-.3,.4),FVector2D(0,.5),FVector2D(.3,.4),FVector2D(.5,0)};
        auto S=Single(O);S.KeyDirection=FVector(-45,90,0); // Light from above.
        double Top=0,Bottom=0;int32 NT=0,NB=0;
        for(const auto& P:LightBoxPoints::Sample(S)){if(P.Position.Z>180){Top+=P.Color.GetLuminance();++NT;}else if(P.Position.Z<120){Bottom+=P.Color.GetLuminance();++NB;}}
        Top/=FMath::Max(1,NT);Bottom/=FMath::Max(1,NB);
        TestTrue(FString::Printf(TEXT("The lit side of a %s is brighter than its shadow side (%.3f vs %.3f)"),Shape,Top,Bottom),Top>Bottom*1.25);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxTreeTest,"SpresenseMaze.LightBox.Realism.ProceduralBranchingTree",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxTreeTest::RunTest(const FString&)
{
    FLightBoxObject O;O.Id=TEXT("oak");O.Shape=TEXT("tree");O.Position=FVector(0,0,150);O.Size=FVector(240,240,300);O.Color=FLinearColor(.25f,.5f,.2f);
    const auto Mesh=FLightBoxMesh::Build(O);
    int32 Leaves=0;for(const auto& C:Mesh.Colors)Leaves+=C.A<.75f;
    TestTrue(TEXT("Many limbs and leaf lobes, not a few round crowns"),Mesh.Vertices.Num()>5000&&Leaves>2500);
    TestTrue(TEXT("Bark exists alongside foliage"),Leaves<Mesh.Colors.Num());
    TestTrue(TEXT("Tree stays within its declared cost"),Mesh.Vertices.Num()<=8600);
    for(const auto& V:Mesh.Vertices){TestFalse(TEXT("Finite branches"),V.ContainsNaN());TestTrue(TEXT("Tree fills exactly its box"),FMath::Abs(V.X)<=120.01&&FMath::Abs(V.Y)<=120.01&&FMath::Abs(V.Z)<=150.01);}
    const auto Again=FLightBoxMesh::Build(O);TestTrue(TEXT("Same id gives the same tree"),Again.Vertices.Num()==Mesh.Vertices.Num()&&Again.Vertices[100].Equals(Mesh.Vertices[100]));
    auto Other=O;Other.Id=TEXT("maple");TestFalse(TEXT("Different trees grow differently"),FLightBoxMesh::Build(Other).Vertices[200].Equals(Mesh.Vertices[200]));
    auto S=Single(O);int32 Green=0,Total=0;
    for(const auto& P:LightBoxPoints::Sample(S))if(P.Owner==TEXT("oak")){++Total;Green+=P.Color.G>P.Color.R*1.1f;}
    TestTrue(TEXT("Leaves dominate the sampled tree"),Green>Total/2);
    FLightBoxStyle Parsed;FString Error;auto Forest=S;Forest.Objects[0].Copies=28;Forest.Objects[0].Layout=TEXT("scatter");
    TestTrue(TEXT("A 28-tree forest is still within the geometry budget"),FLightBoxStyle::Parse(Forest.ToJson(),Parsed,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxCrystalTest,"SpresenseMaze.LightBox.Realism.CrystalEngraving",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxCrystalTest::RunTest(const FString&)
{
    auto O=Body();O.Color=FLinearColor(.9f,.3f,.1f);auto S=Single(O);S.KeyDirection=FVector(-45,90,0);
    const auto Plain=LightBoxPoints::Sample(S);S.PointStyle=TEXT("crystal");const auto Glass=LightBoxPoints::Sample(S);
    TestEqual(TEXT("Same particle budget"),Glass.Num(),Plain.Num());
    double PlainRadius=0,GlassRadius=0;int32 Cool=0,Top=0,Bottom=0,Inner=0;
    for(const auto& P:Plain)PlainRadius+=P.Radius;
    for(const auto& P:Glass){GlassRadius+=P.Radius;Cool+=P.Color.B>=P.Color.R;Top+=P.Position.Z>170;Bottom+=P.Position.Z<130;Inner+=FVector3f::Distance(P.Position,FVector3f(0,0,150))<47;}
    TestTrue(TEXT("Engraving dots are finer"),GlassRadius<PlainRadius*.7);
    TestTrue(TEXT("Engraving is cool white regardless of the authored colour"),Cool>Glass.Num()*9/10);
    TestTrue(TEXT("Stippling puts more dots on the lit side"),Top>Bottom*1.15);
    TestTrue(TEXT("A faint inner volume exists"),Inner>Glass.Num()/50&&Inner<Glass.Num()/5);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxMergeTest,"SpresenseMaze.LightBox.Realism.OverlappingPartsMerge",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxMergeTest::RunTest(const FString&)
{
    // A head sphere sunk halfway into a body sphere: no grains may remain inside the other part.
    auto Body=::Body();FLightBoxObject Head=Body;Head.Id=TEXT("head");Head.Position=FVector(55,0,150);Head.Size=FVector(70);
    FLightBoxStyle S;S.Count=0;S.PointCount=20000;S.Background=FLinearColor::Black;S.Objects={Body,Head};
    int32 Buried=0,Total=0;
    for(const auto& P:LightBoxPoints::Sample(S)){++Total;if(P.Radius<=0)continue;
        if(P.Owner==TEXT("body")&&FVector3f::Distance(P.Position,FVector3f(55,0,150))<35*.85f)++Buried;
        if(P.Owner==TEXT("head")&&FVector3f::Distance(P.Position,FVector3f(0,0,150))<50*.85f)++Buried;}
    TestEqual(TEXT("Every visible grain is on the outer skin of the union"),Buried,0);
    TestEqual(TEXT("The budget is kept"),Total,20000);
    auto Moving=S;Moving.Objects[1].Animation=TEXT("bob");int32 Kept=0;
    for(const auto& P:LightBoxPoints::Sample(Moving))if(P.Owner==TEXT("head")&&FVector3f::Distance(P.Position,FVector3f(0,0,150))<50*.85f)++Kept;
    TestTrue(TEXT("A moving part never loses grains to a body it will move away from"),Kept>0);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "../LightBoxModels.h"
#include "../LightBoxMotion.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxModelTest,"SpresenseMaze.LightBox.Realism.RealModelParticles",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxModelTest::RunTest(const FString&)
{
    const auto Cloud=LightBoxModels::Load(TEXT("island_tree_01"));
    if(!TestTrue(TEXT("Bundled tree model loads"),Cloud.IsValid()))return false;
    TestTrue(TEXT("Many real points"),Cloud->Positions.Num()>=100000);
    TestTrue(TEXT("Unknown ids never load"),!LightBoxModels::Load(TEXT("../secret")).IsValid());
    TArray<uint8> Bad={'L','B','P','T',1,0,0,0};TestFalse(TEXT("Truncated files are rejected"),LightBoxModels::Parse(Bad).IsValid());

    FLightBoxObject O;O.Id=TEXT("tree");O.Shape=TEXT("model");O.Model=TEXT("island_tree_01");O.Position=FVector(0,0,150);O.Size=FVector(300,300,300);
    FLightBoxStyle S;S.Count=0;S.PointCount=60000;S.Background=FLinearColor::Black;S.Objects={O};
    FLightBoxStyle Parsed;FString Error;TestTrue(TEXT("Model scene round trips"),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error)&&Parsed.Objects[0].Model==TEXT("island_tree_01"));
    auto Wrong=S;Wrong.Objects[0].Model=TEXT("panda_realistic");TestFalse(TEXT("Models outside the catalog fail atomically"),FLightBoxStyle::Parse(Wrong.ToJson(),Parsed,Error));
    const auto Points=LightBoxPoints::Sample(S);FBox Box(ForceInit);int32 Green=0;
    for(const auto& P:Points){Box+=FVector(P.Position);Green+=P.Color.G>P.Color.B;}
    TestEqual(TEXT("Budget used by the model"),Points.Num(),60000);
    const FVector E=Box.GetSize();
    TestTrue(TEXT("Fits its box with true proportions"),E.GetMax()<=301&&E.GetMax()>=280&&FMath::Abs(E.X/E.Z-473./500.)<.08);
    TestTrue(TEXT("Texture colours carried over (leafy)"),Green>Points.Num()/2);
    TestTrue(TEXT("Instruction lists the catalog"),FLightBoxStyle::Instruction(FLightBoxStyle()).Contains(TEXT("island_tree_01 (")));
    TestTrue(TEXT("Framing sees the model's box"),FLightBoxMotionSystem::Bounds(S).GetSize().Z>250);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "../LightBoxSdf.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxSdfTest,"SpresenseMaze.LightBox.Realism.SdfSculpting",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxSdfTest::RunTest(const FString&)
{
    auto Part=[](const TCHAR* Type,FVector A,FVector R,float K=8){FLightBoxSdfPart S;S.Type=Type;S.A=A;S.R=R;S.Blend=K;return S;};
    // Exact primitives.
    TestTrue(TEXT("Ellipsoid surface"),FMath::Abs(LightBoxSdf::Distance({Part(TEXT("ellipsoid"),FVector::ZeroVector,FVector(40,20,10))},FVector(40,0,0),FLinearColor::White))<.01f);
    FLightBoxSdfPart Leg=Part(TEXT("capsule"),FVector::ZeroVector,FVector(10,4,0));Leg.B=FVector(0,0,-50);
    TestTrue(TEXT("Tapered capsule: thick at a"),FMath::Abs(LightBoxSdf::Distance({Leg},FVector(10,0,0),FLinearColor::White))<.05f);
    TestTrue(TEXT("Tapered capsule: thin at b"),FMath::Abs(LightBoxSdf::Distance({Leg},FVector(4,0,-50),FLinearColor::White))<.05f);
    // Smooth union fills the crease between two touching balls; a cut carves.
    const TArray<FLightBoxSdfPart> Two={Part(TEXT("ellipsoid"),FVector(-20,0,0),FVector(25),12),Part(TEXT("ellipsoid"),FVector(20,0,0),FVector(25),12)};
    TestTrue(TEXT("Blend thickens the neck between parts"),LightBoxSdf::Distance(Two,FVector(0,0,16),FLinearColor::White)<0);
    auto Cut=Two;FLightBoxSdfPart Hole=Part(TEXT("ellipsoid"),FVector(-45,0,0),FVector(8),2);Hole.bCut=true;Cut.Add(Hole);
    TestTrue(TEXT("Cut carves a socket"),LightBoxSdf::Distance(Cut,FVector(-42,0,0),FLinearColor::White)>0);
    // Colour follows parts and blends across the seam.
    auto Coloured=Two;Coloured[1].bHasColor=true;Coloured[1].Color=FLinearColor::Black;FLinearColor C;
    LightBoxSdf::Distance(Coloured,FVector(-45,0,0),FLinearColor::White,&C);TestTrue(TEXT("Own colour away from seam"),C.R>.9f);
    LightBoxSdf::Distance(Coloured,FVector(0,0,20),FLinearColor::White,&C);TestTrue(TEXT("Soft colour seam"),C.R>.2f&&C.R<.8f);
    // Mesh: one closed skin, no buried inner shell, normals outward, within budget.
    TArray<FLightBoxSdfPart> Nested={Part(TEXT("ellipsoid"),FVector::ZeroVector,FVector(50)),Part(TEXT("ellipsoid"),FVector(5,0,0),FVector(20))};
    const double Start=FPlatformTime::Seconds();const auto M=LightBoxSdf::Mesh(Nested,FLinearColor::White);const double Seconds=FPlatformTime::Seconds()-Start;
    int32 Inner=0,Outward=0;for(int32 I=0;I<M->Vertices.Num();++I){Inner+=M->Vertices[I].Size()<45;Outward+=FVector::DotProduct(M->Normals[I],M->Vertices[I])>0;}
    TestTrue(TEXT("Surface mesh exists"),M->Vertices.Num()>1000&&M->Triangles.Num()>2000);
    TestEqual(TEXT("A part inside another leaves no hidden shell"),Inner,0);
    TestTrue(TEXT("Normals face outward"),Outward>M->Vertices.Num()*99/100);
    TestTrue(TEXT("Vertex budget"),M->Vertices.Num()<=LightBoxSdf::MaxVertices);
    for(const auto& V:M->Vertices)if(!TestTrue(TEXT("Vertices sit on the sphere"),FMath::Abs(V.Size()-50)<1.f))break;
    AddInfo(FString::Printf(TEXT("SDF mesh %d verts in %.0f ms"),M->Vertices.Num(),Seconds*1000));
    TestTrue(TEXT("Cached: second build is the same object"),&*LightBoxSdf::Mesh(Nested,FLinearColor::White)==&*M);
    // Scene round trip, validation and sampling.
    FLightBoxObject O;O.Id=TEXT("bear");O.Shape=TEXT("sdf");O.Position=FVector(0,0,120);O.Size=FVector(150);O.SdfParts=Coloured;O.Detail=TEXT("fur");O.DetailLength=3;
    FLightBoxStyle S;S.Count=0;S.PointCount=30000;S.Background=FLinearColor::Black;S.Objects={O};
    FLightBoxStyle Parsed;FString Error;
    TestTrue(TEXT("SDF scene round trips"),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error)&&Parsed.Objects[0].SdfParts.Num()==2&&Parsed.Objects[0].SdfParts[1].bHasColor);
    TestFalse(TEXT("Unknown part types fail"),FLightBoxStyle::Parse(S.ToJson().Replace(TEXT("\"ellipsoid\""),TEXT("\"blob\"")),Parsed,Error));
    auto Empty=S;Empty.Objects[0].SdfParts.Reset();TestFalse(TEXT("An sdf needs parts"),FLightBoxStyle::Parse(Empty.ToJson(),Parsed,Error));
    int32 Dark=0,Total=0;for(const auto& P:LightBoxPoints::Sample(S)){++Total;Dark+=P.Color.GetLuminance()<.15f;}
    TestTrue(TEXT("Particles carry the per-part colours"),Dark>Total/5&&Dark<Total*4/5);
    TestTrue(TEXT("Instruction teaches sdf"),FLightBoxStyle::Instruction(FLightBoxStyle()).Contains(TEXT("SDF SCULPTING")));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "../LightBoxDirector.h"
#include "Engine/GameInstance.h"
#include "Tests/AutomationCommon.h"
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FWaitAutoRefine,TFunction<bool()>,Check);
bool FWaitAutoRefine::Update(){return Check();}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxAutoRefineTest,"SpresenseMaze.LightBox.Realism.AutoRefineSchedule",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxAutoRefineTest::RunTest(const FString&)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("LightBoxMockGateway"))){AddWarning(TEXT("Auto-refine test requires Tools/LightBoxMockGateway.py and -LightBoxMockGateway."));return true;}
    UGameInstance* Instance=NewObject<UGameInstance>();ULightBoxDirector* D=NewObject<ULightBoxDirector>(Instance);D->AddToRoot();
    D->Endpoint=TEXT("http://127.0.0.1:18879");D->BearerToken=TEXT("lightbox-fixture-only");
    D->AutoRefinePasses=1;D->Submit(TEXT("青い海にして"));const double Started=FPlatformTime::Seconds();
    ADD_LATENT_AUTOMATION_COMMAND(FWaitAutoRefine([this,D,Started](){
        if(D->bThinking&&FPlatformTime::Seconds()-Started<10)return false;
        TestEqual(TEXT("Scene applied first"),D->GetStyle().Title,FString(TEXT("青い海")));
        TestTrue(TEXT("A review pass is scheduled after a new request"),D->IsAutoRefinePending());
        TestTrue(TEXT("It waits for the particles to finish forming"),D->RefineAt>=FPlatformTime::Seconds()+D->GetStyle().AssembleSeconds*.5);
        TestEqual(TEXT("It remembers the user's words"),D->RefineWords,FString(TEXT("青い海にして")));
        D->SelectPreset(2);TestFalse(TEXT("Choosing another scene cancels the pending review"),D->IsAutoRefinePending());
        D->AutoRefinePasses=0;D->Submit(TEXT("青い海にして"));return true;}));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitAutoRefine([this,D,Started](){
        if(D->bThinking&&FPlatformTime::Seconds()-Started<20)return false;
        TestFalse(TEXT("Turned off: no review is scheduled"),D->IsAutoRefinePending());
        D->RemoveFromRoot();return true;}));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxStoryboardTest,"SpresenseMaze.LightBox.Realism.Storyboard",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxStoryboardTest::RunTest(const FString&)
{
    FLightBoxObject O;O.Id=TEXT("panda");O.Shape=TEXT("sdf");O.Position=FVector(0,0,120);O.Size=FVector(150);
    FLightBoxSdfPart Body;Body.A=FVector::ZeroVector;Body.R=FVector(60,35,35);FLightBoxSdfPart Arm;Arm.Type=TEXT("capsule");Arm.A=FVector(40,-25,0);Arm.B=FVector(45,-30,-50);Arm.R=FVector(10,7,0);
    O.SdfParts={Body,Arm};FLightBoxObject Bamboo;Bamboo.Id=TEXT("bamboo");Bamboo.Shape=TEXT("cylinder");Bamboo.Position=FVector(60,-40,60);Bamboo.Size=FVector(4,4,60);
    FLightBoxStyle S;S.Count=0;S.Objects={O,Bamboo};
    const FString Json=S.ToJson().LeftChop(1)+TEXT(",\"storyboard\":{\"hold\":1,\"loop\":true,\"frames\":[{\"move\":1,\"hold\":2,\"set\":[{\"id\":\"panda\",\"patch\":[{\"i\":1,\"b\":[55,-30,40]}]},{\"id\":\"bamboo\",\"position\":[60,-40,150]}]},{\"move\":0.8,\"hold\":1,\"set\":[{\"id\":\"bamboo\",\"color\":\"#FF0000\"}]}]}}");
    FLightBoxStyle Parsed;FString Error,Why;
    if(!TestTrue(TEXT("Storyboard parses"),FLightBoxStyle::Parse(Json,Parsed,Error,&Why))){AddInfo(Why);return false;}
    TestEqual(TEXT("Two frames"),Parsed.Story.Num(),2);
    FLightBoxStyle Round;TestTrue(TEXT("Round trips"),FLightBoxStyle::Parse(Parsed.ToJson(),Round,Error)&&Round.Story.Num()==2&&Round.ToJson()==Parsed.ToJson());
    FLightBoxStyle P0,P1,P2;TestTrue(TEXT("Poses resolve"),Parsed.Pose(0,P0)&&Parsed.Pose(1,P1)&&Parsed.Pose(2,P2));
    TestTrue(TEXT("Patch raises the arm only"),P1.Objects[0].SdfParts[1].B.Equals(FVector(55,-30,40))&&P1.Objects[0].SdfParts[1].A.Equals(Arm.A)&&P1.Objects[0].SdfParts[0].R.Equals(Body.R));
    TestTrue(TEXT("Field override moves the bamboo"),P1.Objects[1].Position.Equals(FVector(60,-40,150)));
    TestTrue(TEXT("Frames override the base, not each other"),P2.Objects[1].Position.Equals(Bamboo.Position)&&P2.Objects[0].SdfParts[1].B.Equals(Arm.B));
    TestTrue(TEXT("Poses carry no nested storyboard"),P1.Story.IsEmpty());
    TestEqual(TEXT("Transition length comes from the frame"),P2.AssembleSeconds,.8f);
    float Move;
    TestEqual(TEXT("Base first"),Parsed.StoryTargetAt(.5f,Move),0);
    TestEqual(TEXT("Then frame 1"),Parsed.StoryTargetAt(1.5f,Move),1);TestEqual(TEXT("with its move"),Move,1.f);
    TestEqual(TEXT("Then frame 2"),Parsed.StoryTargetAt(4.2f,Move),2);
    TestEqual(TEXT("Loop flies home"),Parsed.StoryTargetAt(6.0f,Move),0);TestEqual(TEXT("home uses the first move"),Move,1.f);
    TestEqual(TEXT("And repeats"),Parsed.StoryTargetAt(6.8f+1.5f,Move),1);
    TestFalse(TEXT("Unknown ids fail"),FLightBoxStyle::Parse(Json.Replace(TEXT("\"id\":\"bamboo\",\"color\""),TEXT("\"id\":\"ghost\",\"color\"")),Parsed,Error));
    TestFalse(TEXT("Bad patch index fails"),FLightBoxStyle::Parse(Json.Replace(TEXT("\"i\":1"),TEXT("\"i\":9")),Parsed,Error,&Why));
    TestTrue(TEXT("Diagnostic names the frame"),Why.Contains(TEXT("frame 1")));
    TestFalse(TEXT("A pose must itself be valid"),FLightBoxStyle::Parse(Json.Replace(TEXT("\"color\":\"#FF0000\""),TEXT("\"color\":\"red\"")),Parsed,Error));
    TestTrue(TEXT("Instruction teaches storyboard"),FLightBoxStyle::Instruction(FLightBoxStyle()).Contains(TEXT("STORYBOARD")));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "../LightBoxPerformance.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxPointBudgetCalibrationTest,"SpresenseMaze.LightBox.Realism.PointBudgetCalibration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxPointBudgetCalibrationTest::RunTest(const FString&)
{
    using namespace LightBoxPerformance;
    auto Sample=[](int32 Count,float Fps,float P95){FSample S;S.Count=Count;S.Fps=Fps;S.P95Ms=P95;return S;};
    // A fast phone: 45 fps up to 131k -> 131k.
    TestEqual(TEXT("Fast phone keeps 45 fps"),Choose({Sample(32768,60,17),Sample(65536,59,18),Sample(98304,56,20),Sample(131072,52,22),Sample(196608,40,30)}),131072);
    // A slow phone: 45 fps only at 32k (below the density floor), 30 fps up to 98k -> 98k.
    TestEqual(TEXT("Slow phone falls back to 30 fps"),Choose({Sample(32768,55,20),Sample(65536,44,26),Sample(98304,36,34),Sample(131072,22,50)}),98304);
    TestEqual(TEXT("Hopeless phone takes the smallest level"),Choose({Sample(32768,20,60)}),32768);
    TestFalse(TEXT("Headroom: exactly 30 fps is not enough"),Holds(Sample(65536,30,34),30));
    TestFalse(TEXT("Hitches fail even with a good average"),Holds(Sample(65536,58,80),45));
    const auto M=Measure({16.7f,16.7f,16.7f,33.3f},1000);TestTrue(TEXT("Measure averages and takes p95"),FMath::IsNearlyEqual(M.Fps,1000.f/20.85f,.1f)&&FMath::IsNearlyEqual(M.P95Ms,33.3f,.01f));
    FLightBoxStyle Parsed;FString Error;TestTrue(TEXT("Calibration scene is a valid scene"),FLightBoxStyle::Parse(Scene(65536).ToJson(),Parsed,Error)&&Parsed.PointCount==65536);
    TestTrue(TEXT("Calibration scene animates (typical per-frame work)"),LightBoxPoints::Build(Scene(4096)).bAnimated);
    const FString Rules=FLightBoxStyle::Instruction(FLightBoxStyle(),65536);
    TestTrue(TEXT("AI is told this phone's default"),Rules.Contains(TEXT("Default to65536"))&&Rules.Contains(TEXT("up to65536 on this device")));
    TestFalse(TEXT("No stale 131072 default left in the rules"),Rules.Left(Rules.Find(TEXT("Current scene:"))).Contains(TEXT("131072")));
    return true;
}
#endif
