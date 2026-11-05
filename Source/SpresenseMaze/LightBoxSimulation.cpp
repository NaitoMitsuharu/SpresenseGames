#include "LightBoxSimulation.h"

void FLightBoxSimulation::Reset(int32 Count)
{
	FRandomStream Random(2718);
	Motes.Reset(FMath::Clamp(Count, 0, 1024));
	for (int32 I = 0; I < FMath::Clamp(Count, 0, 1024); ++I)
	{
		FLightMote& P = Motes.AddDefaulted_GetRef();
		P.Position = FVector(Random.FRandRange(-230, 230), Random.FRandRange(-145, 145), Random.FRandRange(65, 315));
		P.Phase = Random.FRandRange(0, 2 * PI); P.Age = I * 0.017f;
		P.Trail.Init(P.Position, 10);
	}
	Burst = 0; Stillness = 0; TrailClock = 0; Time = 0; FlockClock = 1; Configuration.Empty();
}

FString FLightBoxSimulation::Signature(const FLightBoxStyle& S)
{
    return FString::Printf(TEXT("%d:%s:%s:%s:%d:%.3f:%.3f:%.3f:%.3f"),S.Count,*S.Behavior,*S.Formation,*S.FormationNext,S.Groups,S.Center.X,S.Center.Y,S.Center.Z,S.Spread);
}
void FLightBoxSimulation::Reset(const FLightBoxStyle& S)
{
    Reset(S.Count);Configuration=Signature(S);FRandomStream Random(2718);
    for(int32 I=0;I<Motes.Num();++I) {
        auto& P=Motes[I];P.Position=Target(I,Motes.Num(),S,0);P.Velocity=FVector::ZeroVector;
        if(S.Behavior==TEXT("rain")||S.Behavior==TEXT("snow")) {
            P.Position=FVector(S.Center.X+Random.FRandRange(-S.Spread,S.Spread),S.Center.Y+Random.FRandRange(-S.Spread*.65,S.Spread*.65),Random.FRandRange(48,330));P.Velocity.Z=S.Behavior==TEXT("rain")?-180:-18;
        } else if(S.Behavior==TEXT("fire")||S.Behavior==TEXT("fountain")) {
            const float T=Random.FRandRange(0,1.4),Speed=S.Behavior==TEXT("fire")?50:260;
            P.Velocity=FVector(FMath::Sin(P.Phase)*65,FMath::Cos(P.Phase)*65,Speed);
            P.Position=S.Center+P.Velocity*T+FVector(0,0,S.Behavior==TEXT("fire")?40*T*T:-65*T*T);
            P.Velocity.Z+=S.Behavior==TEXT("fire")?80*T:-130*T;
        } else if(S.Behavior==TEXT("fireworks")) {
            const FVector Direction=(P.Position-S.Center).GetSafeNormal(.01,FVector::UpVector);P.Velocity=Direction*(140+Random.FRand()*200);P.Age=Random.FRandRange(0,2.8);P.Position=S.Center+Direction*P.Age*35;
        }
        P.Position.X=FMath::Clamp(P.Position.X,-275.,275.);P.Position.Y=FMath::Clamp(P.Position.Y,-180.,180.);P.Position.Z=FMath::Clamp(P.Position.Z,45.,335.);P.Trail.Init(P.Position,10);
    }
}

namespace {
FVector FormationPoint(int32 I,int32 Count,const FLightBoxStyle& S,const FString& Name,float Time)
{
	const double U = (I + 0.5) / FMath::Max(1, Count), A = I * 2.3999632297;
	FVector V;
	if(Name==TEXT("fish")) {
		if(I==0) V=FVector(.59,-.055,.18);
		else if(U<.80) { const double Q=U/.80,X=Q*1.45-.60,Rad=FMath::Sqrt(FMath::Max(0.,1-FMath::Square((X-.1)/.76))); V=FVector(X,FMath::Sin(A)*.10,FMath::Cos(A)*Rad*.48*FMath::Sqrt(FMath::Frac(I*.754877))); }
		else { const double Q=(U-.8)/.2;V=FVector(-.60-Q*.43,FMath::Sin(A)*.06,FMath::Cos(A)*(.15+Q*.42)); }
	}
	else if(Name==TEXT("bird")) {
        // A complete silhouette: tapered wings, body, head, beak and tail, rather than just a V.
        if(U<.64) {const double Q=U/.64,Side=Q<.5?-1.:1.,W=FMath::Frac(Q*2),Chord=(1-W)*.24+.025;V=FVector(Side*(.10+W*.88),FMath::Sin(A)*.035,.10+W*.45+FMath::Cos(A)*Chord);}
        else if(U<.88){const double Q=(U-.64)/.24;V=FVector(FMath::Sin(A)*.11,FMath::Cos(A)*.05,-.32+Q*.72);}
        else if(U<.96){const double Q=(U-.88)/.08;V=FVector(FMath::Cos(A)*.12,FMath::Sin(A)*.05,.50+FMath::Sin(Q*PI)*.12);}
        else V=FVector((U-.96)/.04*.22,0,.49);
    }
	else if(Name==TEXT("butterfly")) {const double T=U*2*PI,Q=FMath::Exp(FMath::Cos(T))-2*FMath::Cos(4*T)-FMath::Pow(FMath::Sin(T/12),5);V=FVector(FMath::Sin(T)*Q*.38,FMath::Sin(A)*.04,FMath::Cos(T)*Q*.38);}
	else if(Name==TEXT("flower")) {const double T=U*2*PI,Q=.65+.27*FMath::Cos(T*5);V=FVector(FMath::Cos(T)*Q,FMath::Sin(A)*.06,FMath::Sin(T)*Q);}
	else if(Name==TEXT("custom")&&S.FormationPoints.Num()>1) {const double T=U*S.FormationPoints.Num();const int32 K=FMath::FloorToInt(T);V=FMath::Lerp(S.FormationPoints[K%S.FormationPoints.Num()],S.FormationPoints[(K+1)%S.FormationPoints.Num()],T-K);}
	else if (Name == TEXT("heart"))
	{
		const double T = U * 2 * PI;
		V = FVector(-FMath::Pow(FMath::Sin(T), 3), FMath::Sin(A) * 0.07,
			(13 * FMath::Cos(T) - 5 * FMath::Cos(2*T) - 2 * FMath::Cos(3*T) - FMath::Cos(4*T)) / 16);
	}
	else if (Name == TEXT("ring")) { V = FVector(FMath::Cos(U*2*PI), FMath::Sin(U*2*PI), 0); }
	else if (Name == TEXT("helix")) { V = FVector(FMath::Cos(U*6*PI)*0.65, FMath::Sin(U*6*PI)*0.65, (U-0.5)*1.6); }
	else if (Name == TEXT("star"))
	{
		const double T = U * 10; const int32 Segment = FMath::FloorToInt(T);
		auto Corner = [](int32 N) { const double T = N * PI / 5 + PI/2, R = N % 2 ? 0.43 : 1.; return FVector(FMath::Cos(T)*R, 0, FMath::Sin(T)*R); };
		V = FMath::Lerp(Corner(Segment), Corner(Segment+1), T - Segment);
	}
	else if (Name == TEXT("galaxy")) { const double T = A + FMath::Sqrt(U)*7; V = FVector(FMath::Cos(T)*FMath::Sqrt(U), FMath::Sin(T)*FMath::Sqrt(U), FMath::Sin(A)*0.08); }
	else if (Name == TEXT("torus")) { const double T = U*2*PI; V = FVector((0.7+0.25*FMath::Cos(A))*FMath::Cos(T), (0.7+0.25*FMath::Cos(A))*FMath::Sin(T), FMath::Sin(A)*0.25); }
	else if (Name == TEXT("grid") || Name == TEXT("wave"))
	{
		const int32 N = FMath::CeilToInt(FMath::Sqrt(float(Count)));
		V = FVector((I % N) / float(FMath::Max(1,N-1))*2-1, (I / N) / float(FMath::Max(1,N-1))*2-1,
			Name == TEXT("wave") ? FMath::Sin((I % N)*0.5 + Time)*0.3 : 0);
	}
	else if (Name == TEXT("cube"))
	{
		V = FVector(FMath::Cos(A), FMath::Sin(A), U*2-1); V /= FMath::Max3(FMath::Abs(V.X),FMath::Abs(V.Y),FMath::Abs(V.Z));
	}
	else
	{
		const double Z = 1 - U*2, Rad = FMath::Sqrt(FMath::Max(0., 1-Z*Z));
		V = FVector(FMath::Cos(A)*Rad, FMath::Sin(A)*Rad, Z);
		if (Name == TEXT("volume")) { V *= 0.25 + 0.75 * FMath::Frac(I * 0.6180339887); }
	}
	return V;
	};
 }
FVector FLightBoxSimulation::Target(int32 I,int32 Count,const FLightBoxStyle& S,float Time)
{
    FVector V=FormationPoint(I,Count,S,S.Formation,Time);
    if(S.FormationNext!=TEXT("none")) {
        // Assign each target once. Nearest unused matches keep neighbouring points near one another
        // through a morph instead of sending Fibonacci sphere indices across a star's opposite side.
        static TMap<FString,TArray<int32>> Cache;
        static int32 LastCount=-1;static FString LastFrom,LastTo;static TArray<FVector> LastPoints;static const TArray<int32>* LastMap=nullptr;
        if(!LastMap||LastCount!=Count||LastFrom!=S.Formation||LastTo!=S.FormationNext||LastPoints!=S.FormationPoints) {
        uint32 Hash=0;for(const auto& P:S.FormationPoints)Hash=HashCombine(Hash,GetTypeHash(P));
        const FString Key=FString::Printf(TEXT("%s>%s:%d:%u"),*S.Formation,*S.FormationNext,Count,Hash);
        if(!Cache.Contains(Key)) {
            if(Cache.Num()>=16)Cache.Empty();
            TArray<int32> Map;Map.SetNum(Count);TArray<bool> Used;Used.Init(false,Count);TArray<FVector> Targets;
            for(int32 J=0;J<Count;++J)Targets.Add(FormationPoint(J,Count,S,S.FormationNext,0));
            for(int32 K=0;K<Count;++K){const FVector From=FormationPoint(K,Count,S,S.Formation,0);int32 Best=INDEX_NONE;double Distance=DBL_MAX;
                for(int32 J=0;J<Count;++J)if(!Used[J]){const double D=FVector::DistSquared(From,Targets[J]);if(D<Distance){Distance=D;Best=J;}}
                Map[K]=Best;Used[Best]=true;
            }
            Cache.Add(Key,MoveTemp(Map));
        }
        LastMap=&Cache[Key];LastCount=Count;LastFrom=S.Formation;LastTo=S.FormationNext;LastPoints=S.FormationPoints;
        }
        const float Mix=(1-FMath::Cos(Time*2*PI/S.FormationPeriod))*.5;
        V=FMath::Lerp(V,FormationPoint((*LastMap)[I],Count,S,S.FormationNext,Time),Mix);
    }
    const double R=S.Spread;
	V=FRotator(S.FormationRotation.Y,S.FormationRotation.Z,S.FormationRotation.X).RotateVector(V*FVector(R,R*.75,R*.7));
	return S.Center+V+FVector(FMath::Sin(Time*.34)*S.FormationTravel,FMath::Sin(Time*.21)*S.FormationTravel*.3,FMath::Sin(Time*.43)*S.FormationTravel*.2);
}

void FLightBoxSimulation::Impulse(const FLightBoxStyle& S)
{
	if (S.Shake == TEXT("none")) { return; }
    if(Configuration!=Signature(S))Reset(S);
	Burst = 1.f;
	for (FLightMote& P : Motes) { P.Velocity += (P.Position-S.Center).GetSafeNormal(0.01,FVector::UpVector) * (S.Shake == TEXT("gather") ? -200 : 260); }
}

void FLightBoxSimulation::Step(float Seconds, const FVector2D& Tilt, float Twist, const FLightBoxStyle& S)
{
	if (Motes.Num() != S.Count || Configuration!=Signature(S)) { Reset(S); }
	float Remaining = FMath::Clamp(Seconds, 0.f, 0.1f);
	while (Remaining > UE_SMALL_NUMBER)
	{
		const float Dt = FMath::Min(Remaining, 1.f/120.f); Remaining -= Dt; Time += Dt; FlockClock += Dt;
		Burst = FMath::Max(0.f, Burst-Dt*1.5f);
		Stillness = FMath::Clamp(Stillness+(Tilt.Size()<0.12 && FMath::Abs(Twist)<0.1 ? Dt*0.2f : -Dt),0.f,1.f);
		if (S.Behavior == TEXT("flock") && FlockClock >= 1.f/30.f) { UpdateFlock(S); FlockClock = 0; }
		const bool Falling = S.Behavior == TEXT("rain") || S.Behavior == TEXT("snow");
		const bool Rising = S.Behavior == TEXT("fire") || S.Behavior == TEXT("fountain");
		const float Damping = FMath::Lerp(0.7f,5.f,S.Viscosity);
		for (int32 I = 0; I < Motes.Num(); ++I)
		{
			FLightMote& P = Motes[I]; P.Age += Dt*S.Motion;
			const FVector Delta = P.Position-S.Center; const float T = Time*S.Motion+P.Phase;
			FVector Force(-Tilt.X*240.f*S.Tilt,Tilt.Y*240.f*S.Tilt,0);
			Force += S.Wind*100; Force.Z -= S.Gravity*170;
			if (S.Behavior == TEXT("flock"))
			{
				const float Group = (I % S.Groups)*2*PI/S.Groups;
				const FVector Leader = S.Center + FVector(FMath::Cos(Time*0.28+Group)*145,FMath::Sin(Time*0.28+Group)*95,FMath::Sin(Time*0.43+Group)*48);
				if(S.Formation!=TEXT("volume")) {const FVector Goal=Target(I,Motes.Num(),S,Time*S.Motion),GoalVelocity=(Target(I,Motes.Num(),S,Time*S.Motion+.025f)-Goal)/.025f;const float K=12+S.FormationStrength*6;Force+=(Goal-P.Position)*K+(GoalVelocity-P.Velocity)*(2*FMath::Sqrt(K))+P.FlockForce*.01;}
				else Force += P.FlockForce + (Leader-P.Position)*0.65 - Delta*0.08;
			}
			else if (S.Behavior == TEXT("form")) {const FVector Goal=Target(I,Motes.Num(),S,Time*S.Motion),GoalVelocity=(Target(I,Motes.Num(),S,Time*S.Motion+.025f)-Goal)/.025f;const float K=12+S.FormationStrength*6;Force+=(Goal-P.Position)*K+(GoalVelocity-P.Velocity)*(2*FMath::Sqrt(K));}
			else if (S.Behavior == TEXT("orbit"))
			{
				const FVector Offset = Target(I,Motes.Num(),S,Time*S.Motion)-S.Center;
				const float Angle = Time*S.Motion*(S.Swirl*0.6+0.3);
				Force += (S.Center + FVector(Offset.X*FMath::Cos(Angle)-Offset.Y*FMath::Sin(Angle),Offset.X*FMath::Sin(Angle)+Offset.Y*FMath::Cos(Angle),Offset.Z)-P.Position)*4;
			}
			else if (Falling) { Force.Z -= S.Behavior == TEXT("rain") ? 550 : 70; }
			else if (Rising) { Force.Z += S.Behavior == TEXT("fire") ? 200 : -130; Force.X -= Delta.X*0.1; Force.Y -= Delta.Y*0.1; }
			else if (S.Behavior != TEXT("fireworks")) { Force += (Target(I,Motes.Num(),S,0)-P.Position)*(.45f+S.Attraction*.7f); }
			if (!Falling && S.Behavior != TEXT("form") && !(S.Behavior==TEXT("flock")&&S.Formation!=TEXT("volume"))) { Force += FVector(-Delta.Y,Delta.X,0)*(S.Swirl+Twist*0.35f)*0.65f; }
			Force += FVector(FMath::Sin(T*1.13f),FMath::Cos(T*0.87f),FMath::Sin(T*0.72f))*(45+S.Motion*55)*S.Turbulence*((S.Behavior==TEXT("form")||(S.Behavior==TEXT("flock")&&S.Formation!=TEXT("volume")))?.15f:1.f);
			const float Drag = S.Behavior == TEXT("fireworks") || S.Behavior == TEXT("fountain") ? 0.22f : S.Behavior == TEXT("flock") ? 0.45f+S.Viscosity : Damping;
			const bool Shaped=S.Behavior==TEXT("form")||(S.Behavior==TEXT("flock")&&S.Formation!=TEXT("volume"));
            const float IntegrateDt=Shaped?Dt*S.Motion:Dt;
            P.Velocity = ((P.Velocity+Force*IntegrateDt)*FMath::Exp(-(Shaped?0.f:Drag)*IntegrateDt)).GetClampedToMaxSize(450);
			if (S.Behavior == TEXT("flock") && S.Formation==TEXT("volume"))
			{
				// Schools keep swimming instead of stopping at their attractor.
				const FVector Direction=P.Velocity.GetSafeNormal(.01,FVector(FMath::Cos(P.Phase),FMath::Sin(P.Phase),0));
				P.Velocity=Direction*FMath::Clamp(P.Velocity.Size(),65.,220.);
			}
			P.Position += P.Velocity*Dt*S.Motion;
			const bool Respawn = (Falling && P.Position.Z<46) || (Rising && (P.Position.Z>332 || (P.Age>0.4f && P.Position.Z<S.Center.Z))) || (S.Behavior == TEXT("fireworks") && P.Age>2.8f);
			if (Respawn)
			{
				P.Age = 0;
				if (Falling) { P.Position = FVector(FMath::Sin(P.Phase*13)*260,FMath::Cos(P.Phase*7)*170,330); P.Velocity = FVector(0,0,-30); }
				else if (S.Behavior == TEXT("fireworks")) { P.Position = S.Center; P.Velocity = (Target(I,Motes.Num(),S,Time*S.Motion)-S.Center).GetSafeNormal()*(140+FMath::Frac(I*0.618f)*200); }
				else { P.Position = FVector(S.Center.X+FMath::Sin(P.Phase*7)*S.Spread*0.22,S.Center.Y+FMath::Cos(P.Phase*11)*S.Spread*0.15,S.Center.Z); P.Velocity = FVector(FMath::Sin(P.Phase)*65,FMath::Cos(P.Phase)*65,S.Behavior==TEXT("fire")?50:260); }
				P.Trail.Init(P.Position,10);
			}
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				const float Min[] = {-275,-180,45}, Max[] = {275,180,335};
				if (P.Position[Axis]<Min[Axis] || P.Position[Axis]>Max[Axis]) { P.Position[Axis] = FMath::Clamp(P.Position[Axis],Min[Axis],Max[Axis]); P.Velocity[Axis] *= -0.65f; }
			}
		}
		TrailClock += Dt;
		if (TrailClock >= 0.04f)
		{
			TrailClock -= 0.04f;
			for (FLightMote& P : Motes) { P.Trail.RemoveAt(0,1,EAllowShrinking::No); P.Trail.Add(P.Position); }
		}
	}
}

void FLightBoxSimulation::UpdateFlock(const FLightBoxStyle& S)
{
	TMap<FIntVector,TArray<int32>> Grid;
	auto Cell = [](const FVector& P) { return FIntVector(FMath::FloorToInt(P.X/65),FMath::FloorToInt(P.Y/65),FMath::FloorToInt(P.Z/65)); };
	for (int32 I=0; I<Motes.Num(); ++I) { Grid.FindOrAdd(Cell(Motes[I].Position)).Add(I); }
	for (int32 I=0; I<Motes.Num(); ++I)
	{
		FLightMote& P = Motes[I]; FVector Center(0), Velocity(0), Avoid(0); int32 Neighbours=0;
		const FIntVector C = Cell(P.Position);
		for (int32 X=-1; X<=1; ++X) for (int32 Y=-1; Y<=1; ++Y) for (int32 Z=-1; Z<=1; ++Z)
		{
			if (const TArray<int32>* Bucket = Grid.Find(C+FIntVector(X,Y,Z)))
			{
				for (int32 J : *Bucket)
				{
					if (I==J || I%S.Groups!=J%S.Groups || Neighbours>=48) { continue; }
					const FVector D = P.Position-Motes[J].Position; const double D2 = D.SizeSquared();
					if (D2>85*85) { continue; }
					Center += Motes[J].Position; Velocity += Motes[J].Velocity; ++Neighbours;
					if (D2<45*45) { Avoid += D/FMath::Max(4.,D2); }
				}
			}
		}
		P.FlockForce = Neighbours ? (Center/Neighbours-P.Position)*S.Cohesion*.8 + (Velocity/Neighbours-P.Velocity)*S.Alignment*1.3 + Avoid*S.Separation*800 : FVector::ZeroVector;
		P.FlockForce = P.FlockForce.GetClampedToMaxSize(500);
	}
}
