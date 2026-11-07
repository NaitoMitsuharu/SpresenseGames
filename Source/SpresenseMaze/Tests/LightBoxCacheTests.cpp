#include "Misc/AutomationTest.h"
#include "../LightBoxPointData.h"
#include "../LightBoxPointCloud.h"
#include "Math/Float16Color.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxDestinationCacheTest,"SpresenseMaze.LightBox.Animation.DestinationCache",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxDestinationCacheTest::RunTest(const FString&)
{
    auto Half=[](float V){return FFloat16(V).GetFloat();};
    auto Quantise=[&](FVector4f V){return FVector4f(Half(V.X),Half(V.Y),Half(V.Z),Half(V.W));};
    TArray<FVector4f> Rest={FVector4f(10,-20,100,1),FVector4f(30,10,120,1),FVector4f(-10,15,140,1),FVector4f(10,-20,164,1)};
    TArray<FLightBoxPoint> Points;
    for(int32 I=0;I<96;++I){FLightBoxPoint P;P.Vertices=FIntVector(0,1,2);P.Weights=FVector2f(float(I%9)/12,float(I%7)/24);P.Color=FLinearColor(.2f,.6f,.8f);P.Radius=.7f;
        P.Position=LightBoxPoints::Destination(P,Rest,Rest,0);Points.Add(P);}
    auto Model=Points[0];Model.AffineFourth=3;Model.LocalCoordinates=FVector3f(.2f,.3f,.4f);Model.Position=LightBoxPoints::Destination(Model,Rest,Rest,0);Points.Add(Model);
    auto Static=Points[0];Static.Vertices=FIntVector(-1);Points.Add(Static);
    const auto Bindings=LightBoxPoints::Bindings(Points);
    float MaxError=0;
    for(int32 Frame=0;Frame<32;++Frame){auto A=Rest,B=Rest;
        for(int32 J=0;J<4;++J){A[J].X+=FMath::Sin(Frame*.2f+J)*12;B[J].X+=FMath::Sin((Frame+1)*.2f+J)*12;A[J].Z+=FMath::Cos(Frame*.2f+J)*8;B[J].Z+=FMath::Cos((Frame+1)*.2f+J)*8;A[J]=Quantise(A[J]);B[J]=Quantise(B[J]);}
        const auto From=LightBoxPoints::Bake(Bindings,A),To=LightBoxPoints::Bake(Bindings,B);
        TestEqual(TEXT("Every grain remains present"),To.Num(),Points.Num());
        for(int32 I=0;I<Points.Num();++I){auto GPU=Points[I];GPU.Weights=FVector2f(Half(GPU.Weights.X),Half(GPU.Weights.Y));GPU.LocalCoordinates=FVector3f(Half(GPU.LocalCoordinates.X),Half(GPU.LocalCoordinates.Y),Half(GPU.LocalCoordinates.Z));
            for(float Mix:{0.f,.17f,.5f,.91f,1.f}){const auto Delta=FMath::Lerp(Quantise(From[I]),Quantise(To[I]),Mix);
                const auto Cached=Bindings[I].Rest+FVector3f(Delta);
                const auto Expected=GPU.Vertices.X<0?Bindings[I].Rest:LightBoxPoints::Destination(GPU,A,B,Mix);
                MaxError=FMath::Max(MaxError,FVector3f::Distance(Cached,Expected));
                TestTrue(TEXT("Uniform grain size survives interpolation"),FMath::IsNearlyEqual(Delta.W,1.f,.001f));}
        }
    }
    TestTrue(TEXT("Cached trajectory stays within 0.02 cm of the original GPU skin"),MaxError<.02f);
    AddInfo(FString::Printf(TEXT("Maximum trajectory deviation: %.6f cm"),MaxError));
    const auto Cache=LightBoxPoints::Bake(Bindings,Rest);
    TestTrue(TEXT("Static grains remain exactly where they were"),Cache.Last().Equals(FVector4f(0,0,0,1)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxBillboardCoverageTest,"SpresenseMaze.LightBox.Animation.BillboardCoverage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxBillboardCoverageTest::RunTest(const FString&)
{
    auto* Cloud=NewObject<ULightBoxPointCloud>();Cloud->ResizePoints(32);UStaticMesh* Mesh=Cloud->GetStaticMesh();
    if(!TestNotNull(TEXT("Runtime billboard batch exists"),Mesh))return false;
    TestEqual(TEXT("All logical grains remain present"),Cloud->PointTotal(),32);
    TestEqual(TEXT("One instance submits all grains"),Cloud->GetInstanceCount(),1);
    TestEqual(TEXT("One primitive per grain"),Mesh->GetNumTriangles(0),32);
    TestEqual(TEXT("Three vertices per grain"),Mesh->GetNumVertices(0),96);
    const auto& Buffer=Mesh->GetRenderData()->LODResources[0].VertexBuffers.StaticMeshVertexBuffer;
    TestTrue(TEXT("Grain IDs retain full floating-point precision"),Buffer.GetUseFullPrecisionUVs());
    TestEqual(TEXT("Last grain keeps its own lookup ID"),Buffer.GetVertexUV(95,1).X,31.f);
    FVector2f UV[3];for(int32 I=0;I<3;++I)UV[I]=Buffer.GetVertexUV(I,0);
    auto Cross=[](FVector2f A,FVector2f B){return A.X*B.Y-A.Y*B.X;};
    for(int32 I=0;I<128;++I){const float Angle=I*2*PI/128;const FVector2f P(.5f+.5f*FMath::Cos(Angle),.5f+.5f*FMath::Sin(Angle));
        float Sign=0;for(int32 J=0;J<3;++J){const float Side=Cross(UV[(J+1)%3]-UV[J],P-UV[J]);if(FMath::Abs(Side)>.00001f){if(Sign!=0)TestTrue(TEXT("The entire original circular mask remains inside the triangle"),Sign*Side>=0);Sign=Side;}}}
    Cloud->ResizePoints(32);TestTrue(TEXT("Scene changes at the same grain count reuse the batch"),Cloud->GetStaticMesh()==Mesh);
    Cloud->ResizePoints(64);TestEqual(TEXT("A changed budget keeps the new full grain count"),Cloud->PointTotal(),64);TestEqual(TEXT("Resized batch contains every billboard"),Cloud->GetStaticMesh()->GetNumTriangles(0),64);
    Cloud->HideCloud();TestEqual(TEXT("Hiding clears the logical grain count"),Cloud->PointTotal(),0);TestEqual(TEXT("Hiding clears the render instance"),Cloud->GetInstanceCount(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightBoxDepthOrderTest,"SpresenseMaze.LightBox.Animation.DepthOrder",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLightBoxDepthOrderTest::RunTest(const FString&)
{
    TArray<FVector3f> Positions;for(int32 I=0;I<512;++I)Positions.Add(FVector3f(I%13,(I*73)%512,100));
    const auto Order=LightBoxPoints::DepthOrder(Positions,FVector3f(0,1,0));
    TestEqual(TEXT("Depth ordering retains the full particle count"),Order.Num(),Positions.Num());
    TSet<int32> Seen;float Last=-MAX_flt;
    for(float V:Order){const int32 I=int32(V);TestTrue(TEXT("Each original particle ID appears exactly once"),Positions.IsValidIndex(I)&&!Seen.Contains(I));Seen.Add(I);
        TestTrue(TEXT("Near grains are submitted before far grains"),Positions[I].Y>=Last);Last=Positions[I].Y;}
    const auto Reverse=LightBoxPoints::DepthOrder(Positions,FVector3f(0,-1,0));
    TestTrue(TEXT("Ordering follows a turned camera"),Positions[int32(Reverse[0])].Y>Positions[int32(Reverse.Last())].Y);
    TestTrue(TEXT("An empty cloud is valid"),LightBoxPoints::DepthOrder({},FVector3f(0,1,0)).IsEmpty());
    return true;
}
#endif
