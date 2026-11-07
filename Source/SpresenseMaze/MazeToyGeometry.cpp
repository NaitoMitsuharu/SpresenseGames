#include "MazeToyGeometry.h"
#include "FlightModels.h"

void MazeToyGeometry::RoundedBox(FFlightMesh& Mesh, const FVector& Center, const FVector& Half, float Radius)
{
	const double R=FMath::Clamp(double(Radius),0.1,Half.GetMin()*0.8);
	const FVector Inner=Half-FVector(R);
	// Subdivide only the curved edges; a long wall's flat middle remains a quad.
	auto Samples=[&](double Extent)
	{
		TArray<double> Values;
		constexpr int32 Steps=5;
		for (int32 I=0; I<=Steps; ++I) { Values.Add(-Extent+R*I/Steps); }
		for (int32 I=0; I<=Steps; ++I) { Values.Add(Extent-R+R*I/Steps); }
		return Values;
	};
	for (int32 Axis=0; Axis<3; ++Axis)
	{
		const int32 U=(Axis+1)%3,V=(Axis+2)%3;
		const TArray<double> Us=Samples(Half[U]), Vs=Samples(Half[V]);
		for (double Side : {-1.0,1.0})
		{
			const int32 First=Mesh.Verts.Num();
			for (double Y : Vs) for (double X : Us)
			{
				FVector P=FVector::ZeroVector; P[Axis]=Side*Half[Axis]; P[U]=X; P[V]=Y;
				const FVector Anchor=P.BoundToBox(-Inner,Inner);
				const FVector Normal=(P-Anchor).GetSafeNormal();
				Mesh.AddVert(Center+Anchor+Normal*R,Normal,FVector2D(X,Y)*0.002);
			}
			for (int32 Y=0; Y<Vs.Num()-1; ++Y) for (int32 X=0; X<Us.Num()-1; ++X)
			{
				const int32 A=First+Y*Us.Num()+X,B=A+1,C=A+Us.Num(),D=C+1;
				Mesh.AddTri(A,B,D,Mesh.Normals[A]+Mesh.Normals[B]+Mesh.Normals[D]);
				Mesh.AddTri(A,D,C,Mesh.Normals[A]+Mesh.Normals[D]+Mesh.Normals[C]);
			}
		}
	}
}
