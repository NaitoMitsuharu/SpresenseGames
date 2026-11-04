#include "FlightCourse.h"
#include "FlightModels.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace
{
	const FLinearColor Ivory(0.63f, 0.65f, 0.60f), Iron(0.11f, 0.16f, 0.18f);
	const FLinearColor Concrete(0.29f, 0.31f, 0.28f), Amber(0.84f, 0.39f, 0.055f);
	const FLinearColor Dark(0.018f, 0.035f, 0.043f), Sand(0.38f, 0.30f, 0.21f);

	struct FSite
	{
		FFlightMesh Ground, Paint, Metal, Glass, Lamps, Water, Foam;
	};

	void Box(FFlightMesh& M, const FVector& P, const FVector& Size, const FLinearColor& Color, const FRotator& Rot = FRotator::ZeroRotator)
	{
		M.AddBox(FTransform(Rot, P), Size * 0.5, Color);
	}
	void Beam(FFlightMesh& M, const FVector& A, const FVector& B, double Thickness, const FLinearColor& Color = Iron)
	{
		Box(M, (A + B) * 0.5, FVector((B - A).Size(), Thickness, Thickness), Color, (B - A).Rotation());
	}
	void Tower(FFlightMesh& M, const FVector& P, const TArray<FVector2D>& Stations, const FLinearColor& Color)
	{
		FFlightMesh T;
		T.AddTube(Stations, FVector::ZeroVector, 32, true, true, Color);
		M.Append(T, FTransform(FRotator(90, 0, 0), P));
	}
	void Rail(FFlightMesh& M, const FVector& A, const FVector& B)
	{
		Beam(M, A + FVector(0, 0, 110), B + FVector(0, 0, 110), 16, Ivory);
		Beam(M, A + FVector(0, 0, 65), B + FVector(0, 0, 65), 10, Ivory);
		const int32 N = FMath::Max(1, FMath::CeilToInt((B - A).Size() / 300));
		for (int32 I = 0; I <= N; ++I)
		{
			const FVector P = FMath::Lerp(A, B, double(I) / N);
			Beam(M, P, P + FVector(0, 0, 120), 14, Ivory);
		}
	}

	void UploadSite(AActor* Owner, USceneComponent* Root, const FPathFrame& Fr, FSite& Site, const TCHAR* Name)
	{
		UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(Owner, Name);
		C->SetupAttachment(Root);
		C->SetRelativeLocationAndRotation(Fr.P, Fr.Rotation());
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCullDistance(110000.f);
		C->RegisterComponent(); Owner->AddInstanceComponent(C);
		UMaterialInterface* Rock = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Flight/M_FlightRock.M_FlightRock"));
		UMaterialInterface* Water = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Flight/M_FlightWaterfall.M_FlightWaterfall"));
		UMaterialInterface* Foam = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Flight/M_FlightFoam.M_FlightFoam"));
		FFlightMesh* Meshes[] = { &Site.Ground, &Site.Paint, &Site.Metal, &Site.Glass, &Site.Lamps, &Site.Water, &Site.Foam };
		UMaterialInterface* Materials[] = {
			Rock, FlightMaterials::Structure(Owner, FLinearColor::White, 0.25f, 0.64f),
			FlightMaterials::Structure(Owner, FLinearColor::White, 0.75f, 0.4f),
			FlightMaterials::Surface(Owner, Dark, 0.3f, 0.12f),
			FlightMaterials::Glow(Owner, FLinearColor(0.5f, 0.8f, 0.9f) * 2.2f), Water, Foam };
		for (int32 I = 0; I < UE_ARRAY_COUNT(Meshes); ++I)
		{
			if (Meshes[I]->Verts.IsEmpty()) { continue; }
			Meshes[I]->RecomputeSmoothNormals(0);
			Meshes[I]->Upload(C, I, Materials[I]);
		}
	}
}

void AFlightCourse::BuildLandmarks()
{
	// Eastern cape: a real landmark silhouette above the archipelago.
	{
		const FPathFrame Fr = Path.Frame(62000);
		const double SeaZ = -Fr.P.Z;
		const FVector Foot(0, 7200, SeaZ + 300);
		FSite S;
		S.Ground.AddEllipsoid(Foot - FVector(0, 0, 450), FVector(4000, 3100, 1050), 40, 16, false, Sand);
		Tower(S.Paint, Foot, { {0,1100}, {250,1050}, {350,720}, {1450,610} }, Ivory);
		Tower(S.Paint, Foot, { {1450,610}, {2000,550} }, Amber);
		Tower(S.Paint, Foot, { {2000,550}, {3000,430} }, Ivory);
		Tower(S.Paint, Foot, { {3000,430}, {3500,390} }, Amber);
		Tower(S.Metal, Foot, { {3500,650}, {3650,650} }, Iron);
		Tower(S.Glass, Foot, { {3650,430}, {4250,430} }, Dark);
		Tower(S.Metal, Foot, { {4250,700}, {4400,700}, {4750,70} }, Iron);
		for (int32 I = 0; I < 12; ++I)
		{
			const double A = I * 2 * PI / 12;
			const FVector R(FMath::Cos(A), FMath::Sin(A), 0);
			Beam(S.Metal, Foot + R * 445 + FVector(0,0,3640), Foot + R * 445 + FVector(0,0,4260), 28);
			Beam(S.Metal, Foot + R * 620 + FVector(0,0,3660), Foot + R * 620 + FVector(0,0,3820), 18, Ivory);
		}
		S.Lamps.AddEllipsoid(Foot + FVector(0,0,3910), FVector(140,140,230), 16, 8);
		Box(S.Paint, Foot + FVector(-1800,-300,350), FVector(1600,1200,700), Ivory);
		Box(S.Metal, Foot + FVector(-1800,-300,750), FVector(1800,1400,150), Iron);
		Box(S.Paint, Foot + FVector(-3000,0,-250), FVector(4000,900,250), Concrete);
		Rail(S.Metal, Foot + FVector(-4800,-430,-100), Foot + FVector(-1000,-430,-100));
		UploadSite(this, Root, Fr, S, TEXT("CapeLighthouse"));
	}

	// Working harbour: quay, gantry crane, warehouses and a moored vessel.
	{
		const FPathFrame Fr = Path.Frame(142000);
		const FVector P(0,-12500,-Fr.P.Z + 180);
		FSite S;
		Box(S.Paint, P, FVector(14000,4800,350), Concrete);
		for (int32 I = -6; I <= 6; ++I)
		{
			Tower(S.Metal, P + FVector(I*1000,2100,-700), {{0,130},{800,130}}, Iron);
			Box(S.Paint, P + FVector(I*1000,2250,210), FVector(450,90,40), Amber);
		}
		for (int32 I = -1; I <= 1; ++I)
		{
			const FVector B = P + FVector(I*4200,-1000,1100);
			Box(S.Paint, B, FVector(3200,2000,1800), Ivory);
			Box(S.Metal, B + FVector(0,0,950), FVector(3400,2200,180), Iron);
			Box(S.Glass, B + FVector(0,1010,-130), FVector(1800,30,1200), Dark);
		}
		const FVector Crane = P + FVector(2500,1800,200);
		for (double X : {-700.0,700.0})
		{
			Beam(S.Metal, Crane + FVector(X,0,0), Crane + FVector(X,0,4300), 140, Amber);
			Beam(S.Metal, Crane + FVector(X,0,0), Crane + FVector(-X,0,2400), 65, Amber);
		}
		Beam(S.Metal, Crane + FVector(0,-1800,4300), Crane + FVector(0,4000,4300), 220, Amber);
		Beam(S.Metal, Crane + FVector(0,3100,4300), Crane + FVector(0,3100,700), 20, Iron);
		Box(S.Metal, Crane + FVector(0,3100,650), FVector(150,150,150), Iron);
		// A bevelled hull, longer than the player's aircraft, gives the harbour scale.
		TArray<TArray<FVector>> Hull;
		const FVector Boat = P + FVector(-2000,4600,-70);
		for (const FVector2D Station : { FVector2D(-2400,200), FVector2D(-1700,750), FVector2D(1300,750), FVector2D(2400,30) })
		{
			Hull.Add({ Boat+FVector(Station.X,-Station.Y,0), Boat+FVector(Station.X,-Station.Y*0.7,-450),
				Boat+FVector(Station.X,Station.Y*0.7,-450), Boat+FVector(Station.X,Station.Y,0),
				Boat+FVector(Station.X,Station.Y,220), Boat+FVector(Station.X,-Station.Y,220) });
		}
		S.Metal.AddLoft(Hull, true, true, Iron);
		Box(S.Paint, Boat + FVector(-800,0,650), FVector(1100,1100,850), Ivory);
		Box(S.Glass, Boat + FVector(-240,0,800), FVector(25,900,250), Dark);
		Rail(S.Metal, P + FVector(-6500,2330,180), P + FVector(6500,2330,180));
		UploadSite(this, Root, Fr, S, TEXT("CoastalHarbour"));
	}

	// A trussed viaduct crosses above the flight corridor; its lowest central
	// member is >30 m above the centre line, never an invisible obstacle.
	{
		const FPathFrame Fr = Path.Frame(364000);
		FSite S;
		Box(S.Paint, FVector(0,0,4900), FVector(1300,14000,240), Concrete);
		Box(S.Metal, FVector(0,0,5060), FVector(1050,14000,60), Iron);
		for (double Side : {-1.0,1.0})
		{
			Box(S.Paint, FVector(0,Side*5000,3700), FVector(1200,850,2400), Concrete);
			Beam(S.Metal, FVector(0,Side*5200,2100), FVector(0,0,4500), 190, Amber);
			Rail(S.Metal, FVector(Side*610,-6900,5060), FVector(Side*610,6900,5060));
		}
		for (int32 I = -5; I <= 5; ++I)
		{
			const double Y = I * 1150;
			for (double X : {-540.0,540.0})
			{
				Beam(S.Metal, FVector(X,Y,5100), FVector(X,Y+1150,6000), 65, Amber);
				Beam(S.Metal, FVector(X,Y,6000), FVector(X,Y+1150,5100), 65, Amber);
				Beam(S.Metal, FVector(X,Y,6000), FVector(X,Y+1150,6000), 80);
			}
			Box(S.Paint, FVector(0,Y,5100), FVector(60,500,12), Ivory);
		}
		UploadSite(this, Root, Fr, S, TEXT("CanyonViaduct"));
	}

	// Water leaves the high plateau and fans into the channel, with spray below.
	{
		const FPathFrame Fr = Path.Frame(414000);
		FSite S;
		S.Water.AddQuad(FVector(-600,3650,4300-Fr.P.Z), FVector(600,3650,4300-Fr.P.Z),
			FVector(1000,2740,25-Fr.P.Z), FVector(-1000,2740,25-Fr.P.Z), -FVector::RightVector);
		S.Foam.AddQuad(FVector(-1600,1400,30-Fr.P.Z), FVector(1600,1400,30-Fr.P.Z),
			FVector(1600,3800,30-Fr.P.Z), FVector(-1600,3800,30-Fr.P.Z), FVector::UpVector);
		UploadSite(this, Root, Fr, S, TEXT("PlateauWaterfall"));
	}

	// Main hangar: arched roof, exposed ribs, a recessed opening and apron markings.
	{
		const FPathFrame Fr = Path.Frame(562000);
		const FVector P(0,9500,-2300);
		FSite S;
		Box(S.Paint, P + FVector(-2500,0,-200), FVector(14000,8500,450), Concrete);
		TArray<TArray<FVector>> Shell;
		for (double X : {-4200.0,4200.0})
		{
			TArray<FVector> Ring;
			for (int32 I = 0; I <= 20; ++I)
			{
				const double A = I * PI / 20;
				Ring.Add(P + FVector(X,3200*FMath::Cos(A),1100+2800*FMath::Sin(A)));
			}
			Ring.Add(P + FVector(X,-3200,0)); Ring.Add(P + FVector(X,3200,0)); Shell.Add(Ring);
		}
		S.Paint.AddLoft(Shell, true, true, Ivory);
		Box(S.Glass, P + FVector(-4215,0,1300), FVector(30,5200,2550), Dark);
		for (double Side : {-1.0,1.0})
		{
			Box(S.Metal, P + FVector(-4250,Side*2700,1400), FVector(160,150,2800), Amber);
		}
		for (int32 I = -3; I <= 3; ++I)
		{
			Box(S.Paint, P + FVector(-6500,I*850,30), FVector(1300,90,20), Ivory);
		}
		Box(S.Metal, P + FVector(-4250,0,2800), FVector(160,5550,150), Amber);
		for (int32 I = -4; I <= 4; ++I)
		{
			const double X = I * 980;
			for (int32 J = 0; J < 20; ++J)
			{
				const double A = J*PI/20, B=(J+1)*PI/20;
				Beam(S.Metal, P+FVector(X,3240*FMath::Cos(A),1100+2840*FMath::Sin(A)),
					P+FVector(X,3240*FMath::Cos(B),1100+2840*FMath::Sin(B)), 45, Iron);
			}
		}
		for (int32 I = -3; I <= 3; ++I)
		{
			Box(S.Lamps, P+FVector(-4350,I*750,2900), FVector(45,160,35), FLinearColor::White);
		}
		Rail(S.Metal, P+FVector(-9400,-4200,30), P+FVector(4500,-4200,30));
		UploadSite(this, Root, Fr, S, TEXT("MainArchedHangar"));
	}

	// High-gain radar: a parabolic dish with receiver and supporting lattice.
	{
		const FPathFrame Fr = Path.Frame(608000);
		const FVector P(0,-9500,-1800);
		FSite S;
		Box(S.Paint, P, FVector(7500,7000,350), Concrete);
		Tower(S.Paint, P, {{0,900},{2500,650},{2800,1100}}, Ivory);
		const FVector Center = P+FVector(0,0,4600);
		const FQuat Q = FRotator(22,180,0).Quaternion();
		constexpr int32 DishRings = 12, Sides = 56;
		for (int32 J = 0; J <= DishRings; ++J)
		{
			const double R = double(J)/DishRings;
			for (int32 I = 0; I <= Sides; ++I)
			{
				const double A = I*2*PI/Sides;
				S.Metal.AddVert(Center+Q.RotateVector(FVector(520*R*R,2100*R*FMath::Cos(A),2100*R*FMath::Sin(A))),
					Q.RotateVector(FVector::ForwardVector), FVector2D(R,I/double(Sides)), Ivory);
			}
		}
		for (int32 J = 0; J < DishRings; ++J)
		{
			for (int32 I = 0; I < Sides; ++I)
			{
				const int32 A = J*(Sides+1)+I;
				S.Metal.AddTri(A,A+1,A+Sides+2,Q.RotateVector(FVector::ForwardVector));
				S.Metal.AddTri(A,A+Sides+2,A+Sides+1,Q.RotateVector(FVector::ForwardVector));
			}
		}
		for (int32 I = 0; I < 4; ++I)
		{
			const double A = I*PI/2;
			Beam(S.Metal, Center+Q.RotateVector(FVector(520,2050*FMath::Cos(A),2050*FMath::Sin(A))),
				Center+Q.RotateVector(FVector(1800,0,0)), 45);
		}
		Box(S.Metal, Center+Q.RotateVector(FVector(1800,0,0)), FVector(220,220,220), Iron);
		for (double Side : {-1.0,1.0})
		{
			Beam(S.Metal, P+FVector(0,Side*1300,150), Center+FVector(0,Side*500,-400), 170, Iron);
		}
		UploadSite(this, Root, Fr, S, TEXT("HighGainRadar"));
	}
}
