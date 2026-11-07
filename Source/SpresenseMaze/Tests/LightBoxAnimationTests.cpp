#include "Misc/AutomationTest.h"
#include "../LightBoxAnimation.h"
#include "../LightBoxPointData.h"
#include "../LightBoxModels.h"
#include "../LightBoxMotion.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxContinuousClipTest,"SpresenseMaze.LightBox.Animation.ContinuousClip",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxContinuousClipTest::RunTest(const FString&)
{
    FLightBoxStyle S;S.Count=0;S.PointCount=1024;S.StoryHold=0;S.bStoryLoop=true;
    FLightBoxObject O;O.Id=TEXT("actor");O.Shape=TEXT("sphere");O.Position=FVector(0,0,100);O.Size=FVector(80);O.Color=FLinearColor::Green;S.Objects={O};
    FLightBoxStoryFrame F;F.Hold=1;F.Move=1;F.Set=TEXT("[{\"id\":\"actor\",\"position\":[100,0,100],\"color\":\"#FF0000\"}]");S.Story={F};
    const auto L=LightBoxPoints::Build(S);
    TestEqual(TEXT("One stable particle allocation"),L.Points.Num(),1024);
    TestTrue(TEXT("Static geometry is bound for clip playback"),L.bAnimated&&L.Points[0].Vertices.X>=0);
    const auto A=LightBoxPoints::Frame(L,S,.5f,FVector2D::ZeroVector,0,{}),B=LightBoxPoints::Frame(L,S,1.5f,FVector2D::ZeroVector,0,{});
    const auto PA=LightBoxPoints::Destination(L.Points[0],A,A,0),PB=LightBoxPoints::Destination(L.Points[0],B,B,0);
    TestTrue(TEXT("Legacy hold time still moves continuously"),PB.X>PA.X+40);
    const auto Home=LightBoxPoints::Frame(L,S,3.f,FVector2D::ZeroVector,0,{});
    TestTrue(TEXT("Loop returns to the same grain, no reseeding"),LightBoxPoints::Destination(L.Points[0],Home,Home,0).Equals(L.Points[0].Position,.01f));
    TestTrue(TEXT("Colour keyframes are prepared once"),L.PoseColours.Num()==2&&L.PoseColours[1][0].R>L.PoseColours[1][0].G);
    const auto Held=LightBoxPoints::Frame(L,S,10.f,FVector2D::ZeroVector,0,{},0);
    TestTrue(TEXT("Native clock does not advance the clip during assembly"),LightBoxPoints::Destination(L.Points[0],Held,Held,0).Equals(L.Points[0].Position,.01f));
    S.bStoryLoop=false;const auto End=LightBoxAnimation::Interval(S,100);TestEqual(TEXT("Non-looping clips finish at their final pose"),End.From,1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxSkinAnimationTest,"SpresenseMaze.LightBox.Animation.StableSkin",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxSkinAnimationTest::RunTest(const FString&)
{
    FLightBoxObject Rest;Rest.Shape=TEXT("sdf");FLightBoxSdfPart Arm;Arm.Type=TEXT("capsule");Arm.A=FVector::ZeroVector;Arm.B=FVector(0,0,60);Arm.R=FVector(10);Rest.SdfParts={Arm};
    auto Pose=Rest;Pose.SdfParts[0].B=FVector(60,0,0);
    const FVector P(10,0,30);const auto Bind=LightBoxAnimation::Bind(Rest,P);const auto Q=LightBoxAnimation::Skin(Rest,Pose,P,Bind);
    TestTrue(TEXT("Skin follows a joint rotation rather than jumping to a resampled surface"),Q.Equals(FVector(30,0,-10),.01));
    TestTrue(TEXT("Rest pose reproduces the exact original point"),LightBoxAnimation::Skin(Rest,Rest,P,Bind).Equals(P,.0001));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxRigidModelAnimationTest,"SpresenseMaze.LightBox.Animation.RigidModel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxRigidModelAnimationTest::RunTest(const FString&)
{
    FLightBoxStyle S;S.Count=0;S.PointCount=1024;FLightBoxObject O;O.Id=TEXT("tree");O.Shape=TEXT("model");O.Model=TEXT("island_tree_01");O.Size=FVector(180,180,240);O.Position=FVector(0,0,140);O.Animation=TEXT("spin");S.Objects={O};
    if(!TestTrue(TEXT("Model asset available"),LightBoxModels::Load(O.Model).IsValid()))return false;
    const auto L=LightBoxPoints::Build(S);const auto Frame=LightBoxPoints::Frame(L,S,1,FVector2D::ZeroVector,0,{});
    const auto World=FLightBoxMotionSystem::Evaluate(S,1);
    TestTrue(TEXT("Only four affine controls for the whole model"),L.ModelControls[0]>=0&&L.InitialVertices.Num()<100);
    for(int32 I=0;I<FMath::Min(32,L.Points.Num());++I){const auto& P=L.Points[I];if(P.Owner!=TEXT("tree"))continue;
        TestTrue(TEXT("GPU/CPU affine binding follows native rotation"),LightBoxPoints::Destination(P,Frame,Frame,0).Equals(FVector3f(World[0].TransformPosition(FVector(P.LocalCoordinates)*64)),.01f));}
    return true;
}
#endif
