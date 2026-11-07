#include "Misc/AutomationTest.h"
#include "../MazeToyGeometry.h"
#include "../FlightModels.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMazeToyShellTest,"SpresenseMaze.Maze.RoundedToyShell",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMazeToyShellTest::RunTest(const FString&)
{
	for (const FVector Half : {FVector(125,125,125),FVector(125,1125,125),FVector(875,250,125)})
	{
		FFlightMesh Mesh; MazeToyGeometry::RoundedBox(Mesh,FVector::ZeroVector,Half,32);
		TestTrue(TEXT("Shell has geometry"),Mesh.Verts.Num()>0 && Mesh.Tris.Num()>0);
		for (int32 I=0; I<Mesh.Verts.Num(); ++I)
		{
			TestTrue(TEXT("Shell stays inside collision footprint"),(Mesh.Verts[I].GetAbs()-Half).GetMax()<0.001);
			TestTrue(TEXT("Finite unit normals preserve smooth PBR highlights"),!Mesh.Normals[I].ContainsNaN() && FMath::IsNearlyEqual(Mesh.Normals[I].Size(),1.0,0.001));
		}
		for (int32 I=0; I<Mesh.Tris.Num(); I+=3)
		{
			const int32 A=Mesh.Tris[I],B=Mesh.Tris[I+1],C=Mesh.Tris[I+2];
			const FVector Face=-((Mesh.Verts[B]-Mesh.Verts[A])^(Mesh.Verts[C]-Mesh.Verts[A]));
			TestTrue(TEXT("Rounded triangles have nonzero area and outward winding"),Face.SizeSquared()>0.001 && (Face|(Mesh.Normals[A]+Mesh.Normals[B]+Mesh.Normals[C]))>0);
		}
	}
	return true;
}
#endif
