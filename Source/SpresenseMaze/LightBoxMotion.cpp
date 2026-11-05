#include "LightBoxMotion.h"
#include "LightBoxGeometry.h"
#include "LightBoxSimulation.h"

bool FLightBoxObject::IsDeforming() const
{
	return (Shape==TEXT("blossom")&&Animation==TEXT("bloom"))||Shape==TEXT("water") || ((Shape==TEXT("bird")||Shape==TEXT("butterfly"))&&(Animation==TEXT("fly")||Animation==TEXT("flutter"))) || Animation==TEXT("flow") || Animation==TEXT("stream") || !StartObject.IsEmpty() || !EndObject.IsEmpty()
		|| Motions.ContainsByPredicate([](const auto& M){return M.Type==TEXT("flow")||M.Type==TEXT("bend")||M.Type==TEXT("ripple")||M.Type==TEXT("stream");});
}

FVector FLightBoxMotionSystem::Curve(const TArray<FVector>& P,double U,bool Closed)
{
	if(P.IsEmpty()) return FVector::ZeroVector;
	if(P.Num()==1) return P[0];
	U=Closed?U-FMath::FloorToDouble(U):FMath::Clamp(U,0.,1.);
	const double T=U*(Closed?P.Num():P.Num()-1); const int32 I=FMath::FloorToInt(T); const double F=T-I;
	auto At=[&](int32 K){return P[Closed?(K%P.Num()+P.Num())%P.Num():FMath::Clamp(K,0,P.Num()-1)];};
	return .5*((2*At(I))+(-At(I-1)+At(I+1))*F+(2*At(I-1)-5*At(I)+4*At(I+1)-At(I+2))*F*F+(-At(I-1)+3*At(I)-3*At(I+1)+At(I+2))*F*F*F);
}

float FLightBoxMotionSystem::Wave(const FLightBoxMotion& M,float Time)
{
	const float U=Time/FMath::Max(.25f,M.Period)+M.Phase;
	if(M.Wave==TEXT("saw")) return FMath::Frac(U);
	if(M.Wave==TEXT("triangle")) return 1-4*FMath::Abs(FMath::Frac(U)-.5f);
	return FMath::Sin(U*2*PI);
}

TArray<FTransform> FLightBoxMotionSystem::Evaluate(const FLightBoxStyle& S,float Time,const FVector2D& Tilt,float Burst)
{
	TArray<FTransform> Result; Result.SetNum(S.Objects.Num()); TArray<uint8> Done; Done.Init(0,S.Objects.Num());
	TFunction<FTransform(int32,int32)> Visit=[&](int32 I,int32 Depth)->FTransform
	{
		if(Done[I]==2) return Result[I];
		if(Done[I]==1||Depth>8) return FTransform::Identity;
		Done[I]=1; const auto& O=S.Objects[I]; const float T=Time*O.Speed,Phase=GetTypeHash(O.Id)%1000*.013f;
		FVector P=O.Position,Scale(1); const FQuat BaseRotation=FRotator(O.Rotation.Y,O.Rotation.Z,O.Rotation.X).Quaternion(); FQuat R=BaseRotation;
		if(O.Animation==TEXT("spinLocal")) R=R*FQuat(FVector::UpVector,FMath::DegreesToRadians(T*50));
		else if(O.Animation==TEXT("spin")) R=FQuat(FVector::UpVector,FMath::DegreesToRadians(T*50))*R;
		else if(O.Animation==TEXT("bob")) P.Z+=FMath::Sin(T*2+Phase)*O.Amplitude;
		else if(O.Animation==TEXT("orbit")) P+=FVector(FMath::Cos(T),FMath::Sin(T),0)*O.Amplitude;
		else if(O.Animation==TEXT("pulse")) Scale*=1+FMath::Sin(T*2+Phase)*FMath::Min(O.Amplitude/150,.75f);
		else if(O.Animation==TEXT("wave")) P+=FVector(FMath::Sin(T+Phase),0,FMath::Sin(T*2+Phase)*.5)*O.Amplitude;
		else if(O.Animation==TEXT("sway")) R=FQuat(FVector::ForwardVector,FMath::DegreesToRadians(FMath::Sin(T*2+Phase)*FMath::Min(O.Amplitude,60.f)))*R;
		else if(O.Animation==TEXT("path")&&O.Path.Num()>1) {
			const double U=T*.12; P=Curve(O.Path,U,true); const FVector D=Curve(O.Path,U+.002,true)-P; if(!D.IsNearlyZero()) R=D.Rotation().Quaternion()*R;
		}
		else if(O.Animation==TEXT("fly")||O.Animation==TEXT("swim")) {
			const float A=T*.45f+Phase; const FVector Offset(FMath::Sin(A)*O.Amplitude,FMath::Sin(A*2)*O.Amplitude*.35,FMath::Cos(A*1.3)*O.Amplitude*.25);
			const FVector D(FMath::Cos(A),FMath::Cos(A*2)*.7,-FMath::Sin(A*1.3)*.325);
			P+=Offset; R=D.Rotation().Quaternion()*R;
		}
		for(const auto& M:O.Motions) {
			const FVector Axis=M.Axis.GetSafeNormal(); const float V=Wave(M,Time)*M.Amplitude;
			if(M.Type==TEXT("translate")) P+=(M.Space==TEXT("local")?BaseRotation.RotateVector(Axis):Axis)*V;
			else if(M.Type==TEXT("rotate")) {const FQuat Q(Axis,FMath::DegreesToRadians(V));R=M.Space==TEXT("local")?R*Q:Q*R;}
			else if(M.Type==TEXT("scale")) Scale*=FVector(1)+M.Axis*FMath::Clamp(V,-.9f,4.f);
			else if(M.Type==TEXT("orbit")) {
				const FVector OrbitAxis=M.Space==TEXT("local")?BaseRotation.RotateVector(Axis):Axis;
				const FVector A=FVector::CrossProduct(OrbitAxis,FMath::Abs(OrbitAxis.Z)<.9?FVector::UpVector:FVector::ForwardVector).GetSafeNormal(),B=FVector::CrossProduct(OrbitAxis,A);
				const float U=(Time/M.Period+M.Phase)*2*PI; P+=(A*FMath::Cos(U)+B*FMath::Sin(U))*M.Amplitude;
			}
			else if(M.Type==TEXT("path")) {
				const double U=Time/M.Period+M.Phase; const double TPath=M.bClosed?U:(1-FMath::Cos(U*2*PI))*.5;
				P+=Curve(M.Path,TPath,M.bClosed);
				const FVector D=Curve(M.Path,M.bClosed?TPath+.002:FMath::Min(TPath+.002,1.),M.bClosed)-Curve(M.Path,M.bClosed?TPath-.002:FMath::Max(0.,TPath-.002),M.bClosed);
				if(M.bOrient&&!D.IsNearlyZero()) R=D.Rotation().Quaternion()*R;
			}
		}
		if(O.Reaction==TEXT("tilt")||O.Reaction==TEXT("both")) {P+=FVector(-Tilt.X,Tilt.Y,0)*45;R=FRotator(Tilt.Y*15,0,Tilt.X*15).Quaternion()*R;}
		if(O.Reaction==TEXT("shake")||O.Reaction==TEXT("both")) P.Z+=FMath::Abs(FMath::Sin(Burst*PI*3+Phase))*Burst*FMath::Max(25.f,O.Amplitude);
		const FVector Pivot=O.Anchor==TEXT("center")?O.Pivot:Anchor(O,O.Anchor);
		FVector Location=O.Anchor==TEXT("center")?P+BaseRotation.RotateVector(Pivot)-R.RotateVector(Pivot*Scale):P-R.RotateVector(Pivot*Scale);
		if(!O.Parent.IsEmpty()&&O.ParentAnchor!=TEXT("center")) {
			const auto* ParentObject=S.Objects.FindByPredicate([&](const auto& X){return X.Id==O.Parent;});if(ParentObject)Location+=Anchor(*ParentObject,O.ParentAnchor);
		}
		FTransform Local(R,Location,Scale);
		if(!O.Parent.IsEmpty()) {
			const int32 Parent=S.Objects.IndexOfByPredicate([&](const auto& X){return X.Id==O.Parent;});
			if(Parent!=INDEX_NONE) Local=Local*Visit(Parent,Depth+1);
		}
		Result[I]=Local;Done[I]=2;return Local;
	};
	for(int32 I=0;I<S.Objects.Num();++I) Visit(I,0);
	return Result;
}

FBox FLightBoxMotionSystem::Bounds(const FLightBoxStyle& S)
{
	FBox Box(ForceInit);
	TArray<FBox> Local;
	const auto Initial=ResolveBindings(S,Evaluate(S,0));
	for(const auto& O:Initial) { FBox B(ForceInit); if(O.bFocus) for(auto V:FLightBoxMesh::Build(O).Vertices) B+=V; Local.Add(B); }
	// Sample the complete periods, including nested motion; framing stays fixed during playback.
	for(int32 K=0;K<17;++K) {
		float T=K*.5f;
		if(K>8) {float Period=4;for(const auto& O:S.Objects) for(const auto& M:O.Motions) Period=FMath::Max(Period,M.Period);T=(K-8)*Period/8;}
		const auto World=Evaluate(S,T);
		const auto Resolved=ResolveBindings(S,World);
		for(int32 I=0;I<Local.Num();++I) if(Local[I].IsValid) {
			if(S.Objects[I].IsDeforming()){FBox B(ForceInit);for(auto V:FLightBoxMesh::Build(Resolved[I],T).Vertices)B+=V;Box+=B.TransformBy(World[I]);}
			else Box+=Local[I].TransformBy(World[I]);
		}
	}
	if(S.Count>0) {
		if(S.Formation!=TEXT("volume")) {
			for(int32 K=0;K<9;++K) for(int32 I=0;I<S.Count;++I) Box+=FLightBoxSimulation::Target(I,S.Count,S,K*S.FormationPeriod/8);
			Box=Box.ExpandBy(9*S.ParticleSize);
		} else {const float Spread=FMath::Min(S.Spread,225.f); const FVector Extent(Spread+S.FormationTravel,Spread*.65f,Spread*.65f);Box+=FBox(S.Center-Extent,S.Center+Extent);}
	}
	if(!Box.IsValid) Box=FBox(FVector(-100,-70,40),FVector(100,70,240));
	return Box.ExpandBy(9);
}

FTransform FLightBoxMotionSystem::Camera(const FLightBoxStyle& S,float Aspect,FVector* OutFocus)
{
	const FBox Box=Bounds(S); const FVector Focus=S.bAutoFrame?Box.GetCenter():S.CameraFocus;
	const float Yaw=FMath::DegreesToRadians(S.CameraYaw),Pitch=FMath::DegreesToRadians(S.CameraPitch);
	const FVector Back(FMath::Sin(Yaw)*FMath::Cos(Pitch),-FMath::Cos(Yaw)*FMath::Cos(Pitch),FMath::Sin(Pitch));
	const FQuat Rotation=(-Back).Rotation().Quaternion();
	float Distance=1300/S.CameraZoom;
	if(S.bAutoFrame) {
		const double TanH=FMath::Tan(FMath::DegreesToRadians(58.)*.5),TanV=TanH/FMath::Max(.6f,Aspect);
		Distance=180;
		for(int32 I=0;I<8;++I) {
			const FVector Corner(I&1?Box.Max.X:Box.Min.X,I&2?Box.Max.Y:Box.Min.Y,I&4?Box.Max.Z:Box.Min.Z);
			const FVector P=Rotation.UnrotateVector(Corner-Focus);
			Distance=FMath::Max(double(Distance),FMath::Max(FMath::Abs(P.Y)/(TanH*S.CameraFill*.95),FMath::Abs(P.Z)/(TanV*S.CameraFill))-P.X);
		}
		Distance=FMath::Clamp(Distance/FMath::Clamp(S.CameraZoom,.85f,1.f),180.f,2400.f);
	}
	if(OutFocus) *OutFocus=Focus;
	return FTransform(Rotation,Focus+Back*Distance);
}

FVector FLightBoxMotionSystem::Anchor(const FLightBoxObject& O,const FString& Name)
{
	if(Name==TEXT("center"))return FVector::ZeroVector;
	FBox Box(-O.Size*.5,O.Size*.5);
	if((O.Shape==TEXT("tube")||O.Shape==TEXT("ribbon"))&&O.Curve.Num()) {Box.Init();for(auto P:O.Curve)Box+=P;}
	FVector P=Box.GetCenter();const int32 Axis=Name.StartsWith(TEXT("x"))?0:Name.StartsWith(TEXT("y"))?1:2;P[Axis]=Name.EndsWith(TEXT("-"))?Box.Min[Axis]:Box.Max[Axis];return P;
}
TArray<FLightBoxObject> FLightBoxMotionSystem::ResolveBindings(const FLightBoxStyle& S,const TArray<FTransform>& World)
{
	TArray<FLightBoxObject> Out=S.Objects;
	for(int32 I=0;I<Out.Num();++I) {
		auto& O=Out[I];if(O.Curve.Num()<2||(O.StartObject.IsEmpty()&&O.EndObject.IsEmpty()))continue;
		auto Delta=[&](const FString& Id,const FString& Point,const FVector& Offset,const FVector& Previous) {
			const int32 Target=S.Objects.IndexOfByPredicate([&](const auto& N){return N.Id==Id;});
			return Target==INDEX_NONE?FVector::ZeroVector:World[I].InverseTransformPosition(World[Target].TransformPosition(Anchor(S.Objects[Target],Point)+Offset))-Previous;
		};
		const FVector A=Delta(O.StartObject,O.StartAnchor,O.StartOffset,O.Curve[0]),B=Delta(O.EndObject,O.EndAnchor,O.EndOffset,O.Curve.Last());
		for(int32 K=0;K<O.Curve.Num();++K){const float U=float(K)/(O.Curve.Num()-1);O.Curve[K]+=FMath::Lerp(A,B,FMath::SmoothStep(0.f,1.f,U));}
	}
	return Out;
}
