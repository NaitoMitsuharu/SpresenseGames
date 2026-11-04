#include "FlightModels.h"

#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace
{
	const FLinearColor Pearl(0.82f, 0.89f, 0.92f);
	const FLinearColor Carbon(0.035f, 0.052f, 0.068f);
	const FLinearColor Titanium(0.25f, 0.31f, 0.36f);

	struct FSection { float X, W, H, Z; };

	void Loft(FFlightMesh& Mesh, std::initializer_list<FSection> Stations,
		const FVector& Offset = FVector::ZeroVector, int32 Sides = 16,
		const FLinearColor& Color = FLinearColor::White)
	{
		TArray<TArray<FVector>> Rings;
		for (const FSection& St : Stations)
		{
			TArray<FVector>& Ring = Rings.AddDefaulted_GetRef();
			for (int32 I = 0; I < Sides; ++I)
			{
				const double A = I * 2.0 * PI / Sides;
				const double C = FMath::Cos(A), S = FMath::Sin(A);
				Ring.Add(Offset + FVector(St.X,
					St.W * 0.5 * FMath::Sign(C) * FMath::Pow(FMath::Abs(C), 0.65),
					St.Z + St.H * 0.5 * FMath::Sign(S) * FMath::Pow(FMath::Abs(S), 0.65)));
			}
		}
		Mesh.AddLoft(Rings, true, true, Color);
	}

	/** Convex sheet with a real bevel, two closed faces and a thin dark edge.
	 * Unlike airfoil strips this also gives fins and small panels crisp corners. */
	void Plate(FFlightMesh& Mesh, std::initializer_list<FVector> Outline, float Thickness,
		const FLinearColor& Color = FLinearColor::White, float Side = 1.f)
	{
		TArray<FVector> Points;
		FVector Center = FVector::ZeroVector;
		for (FVector P : Outline) { P.Y *= Side; Points.Add(P); Center += P; }
		Center /= Points.Num();
		FVector N = ((Points[1]-Points[0]) ^ (Points[2]-Points[0])).GetSafeNormal();
		if (N.Z < 0) { N = -N; }
		const double Bevel = FMath::Min(Thickness * 0.20f, 3.f);
		TArray<FVector> Top, Bottom;
		for (const FVector& P : Points)
		{
			const FVector Inset = (Center-P).GetSafeNormal() * Bevel;
			Top.Add(P + Inset + N * Thickness * 0.5);
			Bottom.Add(P + Inset - N * Thickness * 0.5);
		}
		auto Face = [&](const TArray<FVector>& Ring, const FVector& Normal)
		{
			const int32 First = Mesh.Verts.Num();
			FVector C = FVector::ZeroVector;
			for (const FVector& P : Ring) { C += P; }
			C /= Ring.Num();
			Mesh.AddVert(C, Normal, FVector2D(0.5,0.5), Color);
			for (const FVector& P : Ring) { Mesh.AddVert(P, Normal, FVector2D(P.X,P.Y)*0.002, Color); }
			for (int32 I=0; I<Ring.Num(); ++I) { Mesh.AddTri(First,First+1+I,First+1+(I+1)%Ring.Num(),Normal); }
		};
		Face(Top, N); Face(Bottom, -N);
		for (int32 I=0; I<Points.Num(); ++I)
		{
			const int32 J = (I+1)%Points.Num();
			const FVector Out = ((Points[I]+Points[J])*0.5-Center).GetSafeNormal();
			Mesh.AddQuad(Top[I],Top[J],Points[J],Points[I],(Out+N).GetSafeNormal(),Color*0.82f);
			Mesh.AddQuad(Points[I],Points[J],Bottom[J],Bottom[I],(Out-N).GetSafeNormal(),Color*0.65f);
		}
	}

	void Bar(FFlightMesh& Mesh, const FVector& A, const FVector& B, float Width, float Depth,
		const FLinearColor& Color = FLinearColor::White)
	{
		const FVector D = B-A;
		Mesh.AddBox(FTransform(FRotationMatrix::MakeFromX(D).ToQuat(),(A+B)*0.5),FVector(D.Size()*0.5,Width*0.5,Depth*0.5),Color);
	}

	UProceduralMeshComponent* Part(AActor* Owner, USceneComponent* Parent, const TCHAR* Name, const FVector& Pivot = FVector::ZeroVector)
	{
		auto* Mesh = NewObject<UProceduralMeshComponent>(Owner, Name);
		Mesh->SetupAttachment(Parent); Mesh->SetRelativeLocation(Pivot);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(true); Mesh->RegisterComponent(); Owner->AddInstanceComponent(Mesh);
		return Mesh;
	}
}

namespace FlightModels
{
	void BuildShip(AActor* Owner, USceneComponent* Parent, FFlightShipParts& Out)
	{
		FFlightMesh Shell, Chassis, Glass, Lamps, Engines, Plumes;
		// A continuous ceramic arrowhead above a graphite keel.
		Loft(Shell, { {-330,94,62,-8},{-255,138,76,-4},{-120,172,88,0},
			{0,164,88,2},{110,130,76,4},{220,84,54,0},{335,34,24,-6},{410,2,3,-10} });
		for (int32 I=0; I<Shell.Verts.Num(); ++I)
		{
			Shell.Colors[I] = Shell.Verts[I].Z < -7 ? Carbon : Pearl;
		}
		for (const float Side : {-1.f,1.f})
		{
			// Broad cranked delta; the gun and trail anchors stay on actual geometry.
			Plate(Shell,{{180,68,-1},{-230,432,-15},{-316,402,-14},{-200,68,-1}},14,Pearl,Side);
			Plate(Chassis,{{156,79,8},{-230,433,-7},{-245,425,-6},{127,89,9}},3,FLinearColor::White,Side);
			Plate(Shell,{{-100,318,-2},{-126,341,-3},{-289,327,-2},{-282,306,-1}},2,FlightPalette::Mint,Side);
			// Recessed maintenance panels and two short identification strokes.
			Plate(Chassis,{{-16,148,6},{-62,190,4},{-209,172,4},{-200,140,6}},2,Titanium,Side);
			for (int32 I=0; I<2; ++I)
			{
				Bar(Shell,FVector(-139-I*17,225*Side,3),FVector(-151-I*17,250*Side,2),5,1.5f,Carbon);
			}
			// Small foreplanes and low outward-canted stabilisers.
			Plate(Shell,{{205,46,4},{117,149,-1},{76,141,-1},{116,56,4}},8,Pearl,Side);
			Plate(Chassis,{{-198,61,29},{-288,132,115},{-354,124,107},{-322,58,26}},9,FLinearColor::White,Side);
			Plate(Shell,{{-282,121,103},{-291,133,116},{-333,129,111},{-326,117,98}},2,FlightPalette::Mint,Side);
			// Integrated rectangular engine bays, with a visible lip around each slot.
			const FVector Offset(0,76*Side,-23);
			Loft(Chassis,{{-342,66,42,0},{-318,76,54,0},{-206,84,62,0},{-60,68,48,0},{18,42,28,0}},Offset,12);
			Loft(Engines,{{-345,53,24,0},{-341,53,24,0}},Offset,12);
			Loft(Plumes,{{-347,49,20,0},{-400,31,11,0},{-455,2,1,0}},Offset,12);
			Out.Exhaust[Side<0?0:1] = FVector(-347,76*Side,-23);
			// Flush weapon rails, rather than protruding circular cannon tubes.
			Loft(Chassis,{{10,23,23,0},{130,21,20,0},{195,13,12,0}},FVector(0,112*Side,-17),8);
			Lamps.AddBox(FTransform(FVector(197,112*Side,-17)),FVector(2,4,3));
			Lamps.AddBox(FTransform(FVector(-245,425*Side,-7)),FVector(9,2,2));
			Bar(Lamps,FVector(-93,79*Side,21),FVector(8,77*Side,24),2,2);
			Out.WingTipL = FVector(-286,-415,-14);
			Out.WingTipR = FVector(-286,415,-14);
		}
		Out.GunL = FVector(201,-112,-17); Out.GunR = FVector(201,112,-17);
		// Smoked, faceted canopy with a graphite gasket; no opaque bubble.
		Loft(Chassis,{{-43,22,12,40},{12,72,40,52},{85,74,51,56},{157,54,39,51},{222,8,9,32}},FVector::ZeroVector,12);
		Loft(Glass,{{-37,15,8,44},{16,63,33,57},{86,65,44,61},{155,46,32,56},{215,4,4,36}},FVector::ZeroVector,12);
		Plate(Chassis,{{-264,-27,37},{-92,-28,45},{-58,0,48},{-92,28,45},{-264,27,37}},2,Titanium);
		for (int32 I=0; I<5; ++I)
		{
			Bar(Shell,FVector(-239+I*19,-19,41+I),FVector(-246+I*19,19,41+I),4,1.5f,Carbon);
		}
		Out.Body = Part(Owner,Parent,TEXT("ShipBody"));
		Shell.Upload(Out.Body,0,FlightMaterials::Surface(Owner,FLinearColor::White,0.24f,0.29f,0.6f));
		Chassis.Upload(Out.Body,1,FlightMaterials::Surface(Owner,Carbon,0.38f,0.46f,0.35f));
		Glass.Upload(Out.Body,2,FlightMaterials::Surface(Owner,FLinearColor(0.012f,0.045f,0.06f),0.48f,0.08f,0.8f,FLinearColor(0,0.008f,0.01f)));
		Out.TrimGlow = FlightMaterials::Glow(Owner,FlightPalette::Mint*2.1f);
		Lamps.Upload(Out.Body,3,Out.TrimGlow);
		Out.PlumeGlow = FlightMaterials::Additive(Owner,FlightPalette::Mint*0.55f);
		Plumes.Upload(Out.Body,4,Out.PlumeGlow);
		Out.EngineGlow = FlightMaterials::Glow(Owner,FlightPalette::Mint*3.2f);
		Engines.Upload(Out.Body,5,Out.EngineGlow);
		for (const float Side : {-1.f,1.f})
		{
			FFlightMesh Aileron, Elevator;
			Plate(Aileron,{{24,-70,0},{-24,70,-3},{-58,70,-3},{-12,-70,0}},5,Carbon,Side);
			auto* A = Part(Owner,Parent,Side<0?TEXT("AileronL"):TEXT("AileronR"),FVector(-276,300*Side,-11));
			Aileron.Upload(A,0,Out.Body->GetMaterial(0));
			(Side<0?Out.AileronL:Out.AileronR)=A;
			Plate(Elevator,{{0,0,0},{-17,68,-3},{-61,68,-3},{-45,0,0}},6,Carbon,Side);
			auto* E = Part(Owner,Parent,Side<0?TEXT("ElevatorL"):TEXT("ElevatorR"),FVector(-210,88*Side,0));
			Elevator.Upload(E,0,Out.Body->GetMaterial(0));
			(Side<0?Out.ElevatorL:Out.ElevatorR)=E;
		}
		Out.Root=Parent;
	}

	const FFlightMesh& DroneHull()
	{
		static const FFlightMesh Mesh=[]
		{
			FFlightMesh M;
			Loft(M,{{-148,74,30,-2},{-96,116,50,0},{-5,102,58,2},{87,59,31,0},{166,3,3,-7}},FVector::ZeroVector,12);
			for (const float Side : {-1.f,1.f})
			{
				Plate(M,{{36,43,0},{126,237,19},{31,255,14},{-110,54,-3}},12,FLinearColor::White,Side);
				Loft(M,{{-153,29,23,0},{-80,44,34,0},{-13,26,20,0}},FVector(0,59*Side,-9),8);
				Plate(M,{{-76,30,21},{-125,69,67},{-164,62,57},{-141,28,12}},6,FLinearColor::White,Side);
			}
			M.AddBox(FTransform(FVector(115,0,12)),FVector(10,35,16));
			return M;
		}(); return Mesh;
	}

	const FFlightMesh& DroneArmor()
	{
		static const FFlightMesh Mesh=[]
		{
			FFlightMesh M;
			for (const float Side : {-1.f,1.f})
			{
				Plate(M,{{25,62,8},{108,225,26},{43,237,23},{-79,67,7}},4,FLinearColor(0.73f,0.81f,0.87f),Side);
				Plate(M,{{28,165,20},{62,219,26},{42,225,25},{-4,168,18}},2,FLinearColor(0.28f,0.35f,0.42f),Side);
				Bar(M,FVector(-51,72*Side,12),FVector(1,131*Side,18),4,2,FLinearColor(0.35f,0.42f,0.48f));
			}
			Plate(M,{{135,0,29},{40,32,42},{-88,27,37},{-120,0,29},{-88,-27,37},{40,-32,42}},4);
			return M;
		}(); return Mesh;
	}

	const FFlightMesh& DroneGlow()
	{
		static const FFlightMesh Mesh=[]
		{
			FFlightMesh M;
			// A thin split visor and inset wing-tip lights retain their shape at a distance.
			for (const float Side : {-1.f,1.f})
			{
				M.AddBox(FTransform(FRotator(0,0,Side*9),FVector(127,18*Side,15)),FVector(2.5f,14,3.5f));
				Bar(M,FVector(115,226*Side,23),FVector(93,234*Side,22),3,3);
				M.AddBox(FTransform(FVector(-155,59*Side,-9)),FVector(2,10,5));
			}
			return M;
		}(); return Mesh;
	}

	const FFlightMesh& TurretHull()
	{
		static const FFlightMesh Mesh=[]
		{
			FFlightMesh M;
			for (int32 Level=0; Level<2; ++Level)
			{
				const float R=Level?119.f:152.f, Z=Level?-32.f:-65.f;
				TArray<FVector> Ring;
				for (int32 I=0; I<6; ++I) { const float A=I*PI/3; Ring.Add(FVector(R*FMath::Cos(A),R*FMath::Sin(A),Z)); }
				// Vertical-axis hexagonal base; use the loft builder for the chamfered tiers.
				TArray<TArray<FVector>> Rings;
				for (const float DZ : {-12.f,12.f}) { TArray<FVector> Copy=Ring; for (FVector& P:Copy) { P.Z+=DZ; } Rings.Add(Copy); }
				M.AddLoft(Rings,true,true);
			}
			Loft(M,{{-90,100,55,21},{-30,164,100,27},{55,147,80,30},{103,63,41,33}},FVector::ZeroVector,8);
			for (const float Side : {-1.f,1.f})
			{
				Loft(M,{{12,32,32,0},{110,29,28,0},{229,21,20,0}},FVector(0,46*Side,42),8);
			}
			return M;
		}(); return Mesh;
	}

	const FFlightMesh& TurretArmor()
	{
		static const FFlightMesh Mesh=[]
		{
			FFlightMesh M;
			Plate(M,{{85,0,85},{33,65,96},{-67,53,88},{-81,0,83},{-67,-53,88},{33,-65,96}},9);
			for (const float Side : {-1.f,1.f})
			{
				Plate(M,{{50,74,39},{-61,73,42},{-74,113,-18},{40,121,-18}},7,FLinearColor(0.66f,0.75f,0.82f),Side);
				Bar(M,FVector(114,46*Side,58),FVector(204,46*Side,54),12,3,FLinearColor(0.75f,0.84f,0.90f));
			}
			return M;
		}(); return Mesh;
	}

	const FFlightMesh& TurretGlow()
	{
		static const FFlightMesh Mesh=[]
		{
			FFlightMesh M;
			M.AddBox(FTransform(FVector(105,0,42)),FVector(2,22,4));
			for (const float Side : {-1.f,1.f})
			{
				M.AddBox(FTransform(FVector(231,46*Side,42)),FVector(2,6,5));
				Bar(M,FVector(-30,117*Side,-40),FVector(30,117*Side,-40),3,3);
			}
			return M;
		}(); return Mesh;
	}

	const FFlightMesh& BossHull()
	{
		static const FFlightMesh Mesh=[]
		{
			FFlightMesh M;
			Loft(M,{{-690,430,118,-18},{-490,700,190,-4},{-150,730,245,0},
				{185,510,225,0},{460,290,155,-4},{720,6,9,-25}},FVector::ZeroVector,16);
			for (const float Side : {-1.f,1.f})
			{
				Plate(M,{{264,200,3},{-250,1150,27},{-620,1250,35},{-590,260,-10}},55,FLinearColor::White,Side);
				Loft(M,{{-690,112,90,0},{-530,197,144,0},{-190,226,174,0},{160,179,112,0},{360,76,65,0}},FVector(0,645*Side,-42),12);
				Loft(M,{{70,60,52,0},{310,46,38,0},{480,30,27,0}},FVector(0,685*Side,-103),8);
				Plate(M,{{-190,250,104},{-450,380,250},{-640,350,229},{-554,218,77}},22,FLinearColor::White,Side);
			}
			M.AddBox(FTransform(FVector(558,0,25)),FVector(24,154,26));
			return M;
		}(); return Mesh;
	}

	const FFlightMesh& BossArmor()
	{
		static const FFlightMesh Mesh=[]
		{
			FFlightMesh M;
			Plate(M,{{590,0,112},{199,140,153},{-365,190,146},{-517,0,131},{-365,-190,146},{199,-140,153}},14);
			for (const float Side : {-1.f,1.f})
			{
				Plate(M,{{233,224,48},{-23,638,76},{-405,630,81},{-468,285,93}},18,FLinearColor(0.86f,0.92f,0.96f),Side);
				Plate(M,{{-104,761,67},{-303,1112,65},{-553,1188,70},{-510,780,69}},14,FLinearColor(0.52f,0.63f,0.72f),Side);
				Plate(M,{{126,650,27},{-202,726,47},{-490,694,28},{-459,583,28}},13,FLinearColor(0.66f,0.76f,0.84f),Side);
				for (int32 I=0; I<4; ++I)
				{
					Bar(M,FVector(-298+I*48,350*Side,108),FVector(-323+I*48,471*Side,98),12,4,FLinearColor(0.16f,0.22f,0.28f));
				}
			}
			return M;
		}(); return Mesh;
	}

	const FFlightMesh& BossGlow()
	{
		static const FFlightMesh Mesh=[]
		{
			FFlightMesh M;
			for (const float Side : {-1.f,1.f})
			{
				M.AddBox(FTransform(FVector(585,83*Side,25)),FVector(3,62,7));
				M.AddBox(FTransform(FVector(484,685*Side,-103)),FVector(3,9,8));
				M.AddBox(FTransform(FVector(-693,645*Side,-42)),FVector(3,44,12));
				Bar(M,FVector(-72,671*Side,51),FVector(-399,696*Side,47),5,5);
				Bar(M,FVector(-302,1123*Side,66),FVector(-490,1180*Side,69),5,5);
			}
			return M;
		}(); return Mesh;
	}
}
