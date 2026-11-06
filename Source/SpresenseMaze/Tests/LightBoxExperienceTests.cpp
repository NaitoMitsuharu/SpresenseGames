#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "../LightBoxPointData.h"
#include "../LightBoxGeometry.h"
#include "../LightBoxDirector.h"
#include "ImageUtils.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxDefaultDensityTest,"SpresenseMaze.LightBox.DefaultDensityAndSparseScenes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxDefaultDensityTest::RunTest(const FString&)
{
    FLightBoxStyle S;TestEqual(TEXT("New scenes default to twice the previous density"),S.PointCount,131072);
    S.Count=0;S.Background=FLinearColor::Black;S.Objects.Add(FLightBoxObject());
    TestEqual(TEXT("A default sculpture uses the new full budget"),LightBoxPoints::Sample(S).Num(),131072);
    FLightBoxStyle Parsed;FString Error;TSharedPtr<FJsonObject> Json;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S.ToJson()),Json);Json->RemoveField(TEXT("pointCloud"));FString Missing;FJsonSerializer::Serialize(Json.ToSharedRef(),TJsonWriterFactory<>::Create(&Missing));
    TestTrue(TEXT("Descriptions without an explicit budget still open"),FLightBoxStyle::Parse(Missing,Parsed,Error));TestEqual(TEXT("Omitted budget uses the new default"),Parsed.PointCount,131072);
    S.PointCount=32768;TestTrue(TEXT("Explicit smaller saved scenes still open"),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error));TestEqual(TEXT("A saved smaller budget is preserved"),Parsed.PointCount,32768);
    S=FLightBoxStyle();S.Count=24;TestEqual(TEXT("A few independent lights are not multiplied to the sculpture budget"),LightBoxPoints::Sample(S).Num(),24);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxDifferenceTest,"SpresenseMaze.LightBox.DifferenceKeepsUnchangedParts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxDifferenceTest::RunTest(const FString&)
{
    FLightBoxStyle A;A.Count=0;A.PointCount=4096;FLightBoxObject O;O.Id=TEXT("body");O.Shape=TEXT("bird");O.Size=FVector(75,130,38);O.Animation=TEXT("fly");A.Objects.Add(O);
    const auto Old=LightBoxPoints::Build(A,2);auto B=A;B.Objects[0].Color=FLinearColor::Red;
    TestEqual(TEXT("Colour edits preserve the camera instruction"),A.CameraContentJson(),B.CameraContentJson());
    auto R=LightBoxPoints::Transfer(Old.Points,LightBoxPoints::Build(B,3).Points,A,B);
    TestEqual(TEXT("Colour changes retain every target"),R.Retained,Old.Points.Num());
    for(int32 I=0;I<R.To.Num();I+=37){TestTrue(TEXT("Different live poses retain the same surface bindings"),R.From[I].Vertices==R.To[I].Vertices&&R.From[I].Weights.Equals(R.To[I].Weights,.00001f));auto Target=R.To[I];Target.Position.X+=30;TestTrue(TEXT("An untouched flying part continues its live motion while another edit assembles"),LightBoxPoints::Position(R.From[I],Target,I,.25f,65,1,.35f).Equals(Target.Position));}
    O.Id=TEXT("new-part");O.Shape=TEXT("sphere");O.Position.X=160;B=A;B.Objects.Add(O);R=LightBoxPoints::Transfer(Old.Points,LightBoxPoints::Build(B,2).Points,A,B);
    TestTrue(TEXT("Adding a part reuses the unchanged part's samples"),R.Retained>1000);
    TestEqual(TEXT("Total budget is retained without duplicate ghost particles"),R.To.Num(),4096);
    B=A;B.Objects[0].Position.X+=80;R=LightBoxPoints::Transfer(Old.Points,LightBoxPoints::Build(B,2).Points,A,B);
    TestTrue(TEXT("Moving a part is animated, not falsely classified as unchanged"),R.Traveling>1000);
    B=A;B.PointCount=1024;R=LightBoxPoints::Transfer(Old.Points,LightBoxPoints::Build(B,2).Points,A,B);
    for(int32 I=1024;I<R.To.Num();I+=31){TestTrue(TEXT("Removed points fade where they are, without gathering"),R.From[I].Position.Equals(R.To[I].Position));TestEqual(TEXT("Removed points fade to zero size"),R.To[I].Radius,0.f);}
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxAttractionTest,"SpresenseMaze.LightBox.LocalAttractionWithoutCentralGather",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxAttractionTest::RunTest(const FString&)
{
    TArray<FLightBoxPoint> Old,Next;constexpr int32 N=65536;
    for(int32 I=0;I<N;++I){FLightBoxPoint P;P.Position=FVector3f((I%256)-128,(I/256)-128,100);Old.Add(P);}
    for(int32 I=N-1;I>=0;--I)Next.Add(Old[I]);
    const auto R=LightBoxPoints::Transfer(Old,Next,FLightBoxStyle(),FLightBoxStyle());double Travel=0;
    for(int32 I=0;I<N;++I)Travel+=FVector3f::Distance(R.From[I].Position,R.To[I].Position);
    TestTrue(TEXT("Spatial matching avoids opposite-side crossings for reordered point sets"),Travel/N<2);
    FLightBoxPoint A,B;A.Position=FVector3f(200,20,80);B=A;B.Position.X+=100;
    float Last=1000;
    for(int32 Step=0;Step<=20;++Step){const auto P=LightBoxPoints::Position(A,B,3,Step/20.f,22,.15f,0);const float Distance=FVector3f::Distance(P,B.Position);TestTrue(TEXT("Attraction moves toward its own destination continuously"),Distance<=Last+.001f);Last=Distance;TestTrue(TEXT("No detour through the origin"),P.X>=199);}
    const auto Spawn=LightBoxPoints::Scatter(8,B,FVector::ZeroVector);TestTrue(TEXT("New points originate near their individual target"),FVector3f::Distance(Spawn.Position,B.Position)<=66);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxLoftTest,"SpresenseMaze.LightBox.AnatomicalSectionsAndMarkings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxLoftTest::RunTest(const FString&)
{
    FLightBoxStyle S;S.Count=0;FLightBoxObject O;O.Id=TEXT("body");O.Shape=TEXT("loft");O.Size=FVector(280,100,80);O.Color=FLinearColor(.2f,.4f,.6f);O.Pattern=TEXT("spots");O.bBelly=true;
    O.Sections={FVector(-.5,0,0),FVector(-.4,.08,.12),FVector(-.1,.32,.4),FVector(.25,.5,.3),FVector(.45,.42,.2),FVector(.5,0,0)};S.Objects.Add(O);
    FLightBoxStyle Parsed;FString Error;TestTrue(TEXT("New sections and markings round trip"),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error));TestEqual(TEXT("Sections are retained"),Parsed.Objects[0].Sections.Num(),6);
    const auto Mesh=FLightBoxMesh::Build(O);TestTrue(TEXT("Detailed continuous body, no rendered component"),Mesh.Vertices.Num()>2000);for(const auto& V:Mesh.Vertices){TestFalse(TEXT("Finite anatomical surface"),V.ContainsNaN());TestTrue(TEXT("Cross-sections stay bounded"),FMath::Abs(V.Y)<=51&&FMath::Abs(V.Z)<=41);}
    int32 Marked=0;for(int32 I=0;I<400;++I){const auto C=FLightBoxMesh::PatternColor(O,FVector((I%20)*14-140,(I/20)*4-40,10),O.Color);Marked+=!C.Equals(O.Color,.001f);}TestTrue(TEXT("Fine spots colour individual samples"),Marked>0&&Marked<400);
    const auto Belly=FLightBoxMesh::PatternColor(O,FVector(0,0,-30),O.Color);TestTrue(TEXT("Underside has its own colour"),Belly.R>O.Color.R*2);
    S.Objects[0].Sections[2].X=-.45;TestFalse(TEXT("Unsorted cross-sections fail atomically"),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error));
    S.Objects[0]=O;S.Objects[0].Sections[1].Y=-.1;TestFalse(TEXT("Negative radii fail"),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxUsageGroupTest,"SpresenseMaze.LightBox.HonestProgressAndGroupedCredits",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxUsageGroupTest::RunTest(const FString&)
{
    auto* Instance=NewObject<UGameInstance>();auto* D=NewObject<ULightBoxDirector>(Instance);D->UsageGroup=TEXT("root");D->Journal.Begin(TEXT("first"),TEXT("依頼"),TEXT(""),false,TEXT("root"));
    TestTrue(TEXT("Unmetered usage is pending, never zero"),D->CurrentCreditsLabel().Contains(TEXT("集計中")));
    D->Journal.Records[0].Credits=.2;D->Journal.Begin(TEXT("repair"),TEXT("修正"),TEXT(""),true,TEXT("root"));TestTrue(TEXT("Partial usage is labelled partial"),D->CurrentCreditsLabel().Contains(TEXT("＋集計中")));
    D->Journal.Records[1].Credits=.15;TestTrue(TEXT("Repair credits count toward one request"),D->CurrentCreditsLabel().Contains(TEXT("0.3500 cr")));TestFalse(TEXT("No dollar text"),D->CurrentCreditsLabel().Contains(TEXT("$")));
    D->bThinking=true;D->OperationStarted=FPlatformTime::Seconds()-21;D->Status=TEXT("AIが景色を設計中");TestTrue(TEXT("Shows elapsed time"),D->ProgressLabel().Contains(TEXT("21秒")));TestFalse(TEXT("Does not invent AI percentages"),D->ProgressLabel().Contains(TEXT("%")));
    D->Cancel();TestTrue(TEXT("Cancellation clears ongoing progress"),D->ProgressLabel().IsEmpty());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxAttachmentTest,"SpresenseMaze.LightBox.AttachmentRejectsWorldCoordinateMistakes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxAttachmentTest::RunTest(const FString&)
{
    FLightBoxStyle S;S.Count=0;FLightBoxObject Body;Body.Id=TEXT("body");Body.Position=FVector(0,0,100);Body.Size=FVector(100,60,50);S.Objects.Add(Body);
    auto Fin=Body;Fin.Id=TEXT("fin");Fin.Parent=TEXT("body");Fin.Position=FVector(0,-20,90);Fin.Size=FVector(30,40,6);Fin.bAttached=true;S.Objects.Add(Fin);
    FLightBoxStyle Parsed;FString Error,Why;TestTrue(TEXT("Attachment flag is serialized"),FLightBoxStyle::Parse(S.ToJson(),Parsed,Error));TestTrue(TEXT("Attachment requirement survives"),Parsed.Objects[1].bAttached);
    TestFalse(TEXT("A duplicated parent world height is rejected"),FLightBoxStyle::ValidateIntent(TEXT("動物"),S,Why));TestTrue(TEXT("Diagnostic identifies the offending part"),Why.Contains(TEXT("fin")));
    S.Objects[1].Position.Z=-10;TestTrue(TEXT("Connected local-space part accepted"),FLightBoxStyle::ValidateIntent(TEXT("動物"),S,Why));
    S.Objects[1].bAttached=false;S.Objects[1].Position.Z=90;TestTrue(TEXT("Intentionally detached orbit/assembly children stay possible"),FLightBoxStyle::ValidateIntent(TEXT("動物"),S,Why));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxScreenshotJournalTest,"SpresenseMaze.LightBox.ImageReviewRetainsBeforeScreenshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxScreenshotJournalTest::RunTest(const FString&)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("LightBoxMockGateway")))return true;
    auto* Instance=NewObject<UGameInstance>();auto* D=NewObject<ULightBoxDirector>(Instance);D->AddToRoot();
    D->Endpoint=TEXT("http://127.0.0.1:18879");D->BearerToken=TEXT("lightbox-fixture-only");D->bJournalEnabled=true;
    TArray<FColor> Pixels;Pixels.Add(FColor::Red);TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(1,1,Pixels,Png);
    D->ReviewImage=TEXT("data:image/png;base64,")+FBase64::Encode(Png.GetData(),int32(Png.Num()));D->OperationStarted=FPlatformTime::Seconds();D->RequestScene(TEXT("画像の造形を確認"));
    const FString File=FLightBoxJournal::ImagePath(D->ActiveRequestId,TEXT("before"));TArray<uint8> Read;
    TestTrue(TEXT("The supplied capture is saved without a nested viewport screenshot"),FFileHelper::LoadFileToArray(Read,*File));TestEqual(TEXT("PNG capture is complete"),Read.Num(),int32(Png.Num()));
    D->Cancel();D->RemoveFromRoot();if(!File.IsEmpty())IFileManager::Get().Delete(*File);return true;
}
#endif
