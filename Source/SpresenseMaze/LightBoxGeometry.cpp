#include "LightBoxGeometry.h"
#include "LightBoxMotion.h"

namespace
{
	double Cross(const FVector2D& A,const FVector2D& B,const FVector2D& C) { return (B.X-A.X)*(C.Y-A.Y)-(B.Y-A.Y)*(C.X-A.X); }
	bool Inside(const FVector2D& P,const FVector2D& A,const FVector2D& B,const FVector2D& C) { return Cross(A,B,P)>=-1e-8 && Cross(B,C,P)>=-1e-8 && Cross(C,A,P)>=-1e-8; }
	TArray<int32> Triangulate(const TArray<FVector2D>& P)
	{
		TArray<int32> Indices, Remaining; double Area=0;
		for (int32 I=0; I<P.Num(); ++I) { const FVector2D& B=P[(I+1)%P.Num()]; Area+=P[I].X*B.Y-B.X*P[I].Y; }
		for (int32 I=0; I<P.Num(); ++I) { Remaining.Add(Area>0?I:P.Num()-1-I); }
		while (Remaining.Num()>2)
		{
			bool Found=false;
			for (int32 I=0; I<Remaining.Num(); ++I)
			{
				const int32 A=Remaining[(I+Remaining.Num()-1)%Remaining.Num()],B=Remaining[I],C=Remaining[(I+1)%Remaining.Num()];
				if (Cross(P[A],P[B],P[C])<=1e-9) { continue; }
				bool Occupied=false;
				for (int32 J:Remaining) { if (J!=A && J!=B && J!=C && Inside(P[J],P[A],P[B],P[C])) { Occupied=true; break; } }
				if (Occupied) { continue; }
				Indices.Append({A,B,C}); Remaining.RemoveAt(I); Found=true; break;
			}
			if (!Found) { return {}; }
		}
		return Indices;
	}
}

bool FLightBoxMesh::ValidOutline(const TArray<FVector2D>& P)
{
	if (P.Num()<3 || P.Num()>32) { return false; }
	for (int32 I=0; I<P.Num(); ++I)
	{
		const int32 I2=(I+1)%P.Num(); if (FVector2D::DistSquared(P[I],P[I2])<1e-8) { return false; }
		for (int32 J=I+1; J<P.Num(); ++J)
		{
			const int32 J2=(J+1)%P.Num(); if (I2==J || J2==I) { continue; }
			const double A=Cross(P[I],P[I2],P[J]),B=Cross(P[I],P[I2],P[J2]),C=Cross(P[J],P[J2],P[I]),D=Cross(P[J],P[J2],P[I2]);
			if (A*B<=0 && C*D<=0 && FMath::Max(P[I].X,P[I2].X)>=FMath::Min(P[J].X,P[J2].X) && FMath::Max(P[J].X,P[J2].X)>=FMath::Min(P[I].X,P[I2].X)
				&& FMath::Max(P[I].Y,P[I2].Y)>=FMath::Min(P[J].Y,P[J2].Y) && FMath::Max(P[J].Y,P[J2].Y)>=FMath::Min(P[I].Y,P[I2].Y)) { return false; }
		}
	}
	return Triangulate(P).Num()==(P.Num()-2)*3;
}


FLinearColor FLightBoxMesh::PatternColor(const FLightBoxObject& O,const FVector& Local,const FLinearColor& Lit)
{
    if(O.Pattern==TEXT("none")&&!O.bBelly)return Lit;
    const FVector P=Local/O.Size;FLinearColor Tint=O.Color;const float S=O.PatternScale;
    const double A=FMath::Frac(P.X*S+.5)-.5,B=FMath::Frac((P.Y+P.Z*.71)*S+.5)-.5;
    const bool Mark=O.Pattern==TEXT("spots")?(A*A+B*B<.035):O.Pattern==TEXT("stripes")?FMath::Sin((P.X+P.Z*.25)*S*2*PI)>.6:O.Pattern==TEXT("patches")?(FMath::Sin(P.X*S*PI)*FMath::Cos(P.Y*S*PI)>.25):false;
    if(Mark)Tint=O.PatternColor;
    if(O.bBelly&&P.Z<-.12)Tint=O.BellyColor;
    return Lit*FLinearColor(Tint.R/FMath::Max(.025f,O.Color.R),Tint.G/FMath::Max(.025f,O.Color.G),Tint.B/FMath::Max(.025f,O.Color.B),1);
}

FLightBoxMesh FLightBoxMesh::Build(const FLightBoxObject& O,float Time)
{
	FLightBoxMesh Unit;
	auto Vertex=[&](const FVector& P,const FLinearColor& C=FLinearColor::White,const FVector2D& UV=FVector2D::ZeroVector) {Unit.Colors.Add(C);Unit.UVs.Add(UV);Unit.Normals.Add(FVector::ZeroVector);return Unit.Vertices.Add(P);};
	auto Tri=[&](int32 A,int32 B,int32 C){Unit.Triangles.Append({A,B,C});};
	auto Surface=[&](int32 W,int32 H,TFunction<FVector(double,double)> Point,const FLinearColor& Color=FLinearColor::White)
	{
		const int32 Offset=Unit.Vertices.Num();
		for(int32 Y=0;Y<=H;++Y) for(int32 X=0;X<=W;++X) Vertex(Point(double(X)/W,double(Y)/H),Color,FVector2D(double(X)/W,double(Y)/H));
		for(int32 Y=0;Y<H;++Y) for(int32 X=0;X<W;++X) {const int32 A=Offset+Y*(W+1)+X,B=A+1,C=A+W+1,D=C+1;Tri(A,C,B);Tri(B,C,D);}
	};
	auto Ball=[&](FVector P,FVector Size,FLinearColor C=FLinearColor::White,int32 N=20) {
		const int32 First=Unit.Vertices.Num();
		Surface(N,12,[=](double U,double V){const double A=U*2*PI,B=V*PI;return P+FVector(FMath::Sin(B)*FMath::Cos(A),FMath::Sin(B)*FMath::Sin(A),FMath::Cos(B))*Size*.5;},C);
		for(int32 I=First;I<Unit.Vertices.Num();++I) Unit.Normals[I]=((Unit.Vertices[I]-P)/(Size*Size)).GetSafeNormal();
	};
	const bool Continuous=O.Animation==TEXT("stream")||O.Motions.ContainsByPredicate([](const auto& M){return M.Type==TEXT("stream");});
	auto Tube=[&](const TArray<FVector>& Curve,double Radius,FLinearColor Color,int32 Segments,bool Flow=false,float Phase=0)
	{
		const double Progress=Flow?FMath::Frac(Time*.24+Phase):0;
		Surface(8,Segments,[&](double V,double U){
			const double S=FMath::Clamp(U+Progress,0.,1.); FVector P=FLightBoxMotionSystem::Curve(Curve,S,O.bCurveClosed&&!Flow);
			const FVector D=(FLightBoxMotionSystem::Curve(Curve,O.bCurveClosed?S+.002:FMath::Min(1.,S+.002),O.bCurveClosed&&!Flow)-FLightBoxMotionSystem::Curve(Curve,O.bCurveClosed?S-.002:FMath::Max(0.,S-.002),O.bCurveClosed&&!Flow)).GetSafeNormal(.001,FVector::UpVector);
			const FVector N=FVector::CrossProduct(D,FMath::Abs(D.Z)>.9?FVector::RightVector:FVector::UpVector).GetSafeNormal(),B=FVector::CrossProduct(D,N);
			const double R=Radius*((Flow||Continuous)?.88+.12*FMath::Sin(U*30-Time*9):1);
			if(Flow) P+=N*FMath::Sin(S*14-Time*6+Phase*PI)*Radius*.8*FMath::Sin(S*PI);
			return P+(N*FMath::Cos(V*2*PI)+-B*FMath::Sin(V*2*PI))*R;
		},Color);
	};
	if(O.Shape==TEXT("group")) return Unit;
    if(O.Shape==TEXT("loft")&&O.Sections.Num()>=3) {
        Surface(32,64,[&](double U,double V){
            const double X=FMath::Lerp(O.Sections[0].X,O.Sections.Last().X,V);int32 I=0;
            while(I+2<O.Sections.Num()&&O.Sections[I+1].X<X)++I;
            const auto A=O.Sections[I],B=O.Sections[I+1];const double T=FMath::Clamp((X-A.X)/(B.X-A.X),0.,1.);
            const auto Prev=O.Sections[FMath::Max(0,I-1)],Next=O.Sections[FMath::Min(O.Sections.Num()-1,I+2)];
            auto Radius=[&](int32 Axis){const double MA=(B[Axis]-Prev[Axis])/(B.X-Prev.X),MB=(Next[Axis]-A[Axis])/(Next.X-A.X);return FMath::Clamp(FMath::CubicInterp(A[Axis],MA*(B.X-A.X),B[Axis],MB*(B.X-A.X),T),0.,.5);};
            FVector P(X,Radius(1)*FMath::Cos(U*2*PI),Radius(2)*FMath::Sin(U*2*PI));
            if(O.Curve.Num()>1){const FVector C=FLightBoxMotionSystem::Curve(O.Curve,V,false);P.Y+=C.Y/O.Size.Y;P.Z+=C.Z/O.Size.Z;}
            return P;
        });
    }
	else if(O.Shape==TEXT("butterfly")) {
		Ball(FVector(-.03,0,0),FVector(.66,.065,.25),FLinearColor(.12,.10,.08));Ball(FVector(.32,0,.01),FVector(.14,.09,.20),FLinearColor(.15,.12,.09));
		for(double Side:{-1.,1.}) {
			const int32 First=Unit.Vertices.Num();
			Ball(FVector(.13,Side*.255,.03),FVector(.61,.48,.055),FLinearColor(1,.87,.62));
			Ball(FVector(-.23,Side*.22,.02),FVector(.43,.43,.065),FLinearColor(.78,.65,.50));
			for(int32 K=0;K<3;++K) Ball(FVector(.10-K*.11,Side*(.33-K*.015),.065),FVector(.06,.05,.012),FLinearColor(.08,.055,.04),10);
			const float Angle=(O.Animation==TEXT("fly")||O.Animation==TEXT("flutter"))?FMath::Sin(Time*O.Speed*8)*.67f*Side:0;
			const FQuat R(FVector::ForwardVector,Angle);
			for(int32 I=First;I<Unit.Vertices.Num();++I){Unit.Vertices[I]=R.RotateVector(Unit.Vertices[I]);Unit.Normals[I]=R.RotateVector(Unit.Normals[I]);}
			Tube({FVector(.34,Side*.025,.05),FVector(.43,Side*.08,.12),FVector(.45,Side*.12,.14)},.008,FLinearColor(.09,.07,.05),7);
		}
	}
	else if(O.Shape==TEXT("whale")) {
		Ball(FVector(.02,0,0),FVector(.84,.48,.67),FLinearColor(.78,.9,1));
		Ball(FVector(.05,0,-.15),FVector(.70,.39,.30),FLinearColor(1,1,.94));
		for(double Side:{-1.,1.}) {
			Ball(FVector(-.42,Side*.16,-.02),FVector(.27,.38,.09),FLinearColor(.66,.81,.94));
			Ball(FVector(.01,Side*.24,-.19),FVector(.26,.20,.07),FLinearColor(.62,.78,.90));
			Ball(FVector(.31,Side*.19,.06),FVector(.045,.035,.06),FLinearColor(.015,.02,.03),10);
			Tube({FVector(.34,Side*.12,-.09),FVector(.39,Side*.08,-.11),FVector(.40,0,-.12)},.008,FLinearColor(.08,.14,.2),7);
		}
	}
	else if(O.Shape==TEXT("petal")) {
		for(double Side:{-1.,1.})Surface(28,10,[=](double U,double V){const double W=FMath::Pow(FMath::Max(0.,FMath::Sin(U*PI)),.72)*.5,Y=(.5-V)*2*W*Side;
			return FVector(U-.5,Y,Side*.5*FMath::Sin(U*PI)*FMath::Sqrt(FMath::Max(0.,1-FMath::Square(2*V-1))));});
	}

    else if(O.Shape==TEXT("gear")) {
        auto Outer=[](double U){return FMath::Frac(U*12)<.52?.5:.425;};
        Surface(192,1,[&](double U,double V){const double A=U*2*PI,R=Outer(U);return FVector(R*FMath::Cos(A),R*FMath::Sin(A),.5-V);});
        Surface(96,1,[&](double U,double V){const double A=U*2*PI;return FVector(.15*FMath::Cos(A),.15*FMath::Sin(A),V-.5);});
        for(double Side:{-1.,1.})Surface(192,1,[&](double U,double V){const double A=U*2*PI*Side,R=FMath::Lerp(.15,Outer(U),V);return FVector(R*FMath::Cos(A),R*FMath::Sin(A),Side*.5);});
    }
    else if(O.Shape==TEXT("blossom")) {
        // One connected articulated assembly. The model chooses size/colour/speed, not twelve hinges.
        auto Tint=[&](const FLinearColor& Desired){return Desired;};
        Tube({FVector(0,0,-.5),FVector(.01,0,-.1),FVector(0,0,.23)},.018,Tint(FLinearColor(.10,.34,.065)),12);
        for(double Side:{-1.,1.})Ball(FVector(Side*.105,0,-.12+Side*.04),FVector(.24,.07,.045),Tint(FLinearColor(.15,.48,.08)));
        Ball(FVector(0,0,.25),FVector(.17,.17,.09),Tint(FLinearColor(.94,.57,.055)));
        for(int32 K=0;K<10;++K) {
            const int32 First=Unit.Vertices.Num();const double A=K*2*PI/10;
            Ball(FVector(.235,0,0),FVector(.47,.185,.045),O.Color,20);
            const float Angle=O.Animation==TEXT("bloom")?(1-FMath::Cos(Time*O.Speed))*.5f*FMath::DegreesToRadians(FMath::Clamp(O.Amplitude,15.f,80.f)):0;
            const FQuat R=FQuat(FVector::UpVector,A)*FQuat(-FVector::RightVector,Angle);
            for(int32 I=First;I<Unit.Vertices.Num();++I){auto V=Unit.Vertices[I];V.Z+=FMath::Sin(FMath::Clamp(V.X/.47,0.,1.)*PI)*.04;Unit.Vertices[I]=R.RotateVector(V)+FVector(0,0,.25);Unit.Normals[I]=R.RotateVector(Unit.Normals[I]);}
        }
    }
	else if(O.Shape==TEXT("roof")) {
        auto Face=[&](std::initializer_list<FVector> Points){const int32 First=Unit.Vertices.Num();for(auto P:Points)Vertex(P);for(int32 K=1;K<int32(Points.size())-1;++K)Tri(First,First+K,First+K+1);};
        Face({FVector(-.5,-.5,-.5),FVector(.5,-.5,-.5),FVector(0,-.5,.5)});
        Face({FVector(.5,.5,-.5),FVector(-.5,.5,-.5),FVector(0,.5,.5)});
        Face({FVector(-.5,.5,-.5),FVector(-.5,-.5,-.5),FVector(0,-.5,.5),FVector(0,.5,.5)});
        Face({FVector(.5,-.5,-.5),FVector(.5,.5,-.5),FVector(0,.5,.5),FVector(0,-.5,.5)});
        Face({FVector(-.5,-.5,-.5),FVector(-.5,.5,-.5),FVector(.5,.5,-.5),FVector(.5,-.5,-.5)});
    }
    else if(O.Shape==TEXT("bird")) {
		Ball(FVector(-.02,0,0),FVector(.57,.18,.52));Ball(FVector(.27,0,.12),FVector(.21,.145,.43));
		Ball(FVector(.398,0,.075),FVector(.18,.075,.10),FLinearColor(1,.62,.12));
		for(double Side:{-1.,1.}) {
			Ball(FVector(.30,Side*.066,.19),FVector(.035,.019,.075),FLinearColor(.012,.018,.025),12);
			const double Flap=(O.Animation==TEXT("fly")||O.Animation==TEXT("flutter"))?FMath::Sin(Time*O.Speed*7)*.42:0;
			Surface(14,3,[=](double U,double V){const double Y=.065+U*.435,Chord=(1-U*.72)*.24;return FVector(.05-U*.29+(V-.5)*Chord,Side*Y,.08+FMath::Pow(U,1.3)*Flap+.035*FMath::Sin(U*PI));},FLinearColor(.83,.9,1));
		}
		Ball(FVector(-.32,0,.01),FVector(.28,.23,.10),FLinearColor(.75,.82,.9));
	}
	else if(O.Shape==TEXT("tree")) {
		Tube({FVector(0,0,-.5),FVector(.018,-.01,0),FVector(-.02,.025,.38)},.048,FLinearColor(.42,.22,.10),10);
		for(int32 I=0;I<7;++I) {const double A=I*2.39996,Z=-.02+(I%3)*.12;const FVector P(FMath::Cos(A)*.25,FMath::Sin(A)*.25,Z+.10);
			Tube({FVector(0,0,-.1),P*.75,P},.016,FLinearColor(.42,.24,.11),5);
			Ball(P,FVector(.48,.48,.42),FLinearColor(.60+(I%2)*.16,.85,.57+(I%3)*.07),12);
		} Ball(FVector(0,0,.32),FVector(.48,.46,.38),FLinearColor(.72,.95,.65),16);
	}
	else if(O.Shape==TEXT("elephant")||O.Shape==TEXT("giraffe")||O.Shape==TEXT("lion")) {
		const bool G=O.Shape==TEXT("giraffe"),E=O.Shape==TEXT("elephant");
		const FLinearColor Dark=G?FLinearColor(.38,.13,.035):FLinearColor(.35,.24,.12),Black(.012,.018,.022);
		Ball(FVector(-.06,0,G?-.09:0),FVector(.68,.66,G?.27:.52));
		for(double X:{-.27,.18}) for(double Y:{-.22,.22}) {
			Ball(FVector(X,Y,G?-.31:-.31),FVector(G?.08:.13,G?.12:.18,G?.39:.39));
			Ball(FVector(X+.015,Y,-.47),FVector(G?.12:.17,G?.15:.2,.06),E?FLinearColor(.62,.64,.63):Dark);
		}
		const FVector Head=G?FVector(.29,0,.40):FVector(.32,0,.13);
		if(G) Ball(FVector(.21,0,.18),FVector(.15,.25,.60));
		if(!E&&!G) Ball(Head+FVector(-.06,0,0),FVector(.28,.69,.60),FLinearColor(.46,.20,.065));
		Ball(Head,FVector(G?.32:.35,G?.26:.47,G?.16:.38));
		for(double Side:{-1.,1.}) {
			Ball(Head+FVector(-.07,Side*(E?.30:.17),G?.055:.13),FVector(E?.12:.12,E?.32:.11,E?.42:.15));
			Ball(Head+FVector(G?.09:.10,Side*(G?.116:.199),.045),FVector(.035,.022,G?.022:.045),Black,12);
			if(G) {Tube({Head+FVector(-.04,Side*.065,.065),Head+FVector(-.06,Side*.065,.14)},.014,Dark,3);}
		}
		if(E) {Tube({Head+FVector(.15,0,-.04),Head+FVector(.23,0,-.18),Head+FVector(.25,0,-.45),Head+FVector(.35,0,-.48)},.065,FLinearColor(.9,.93,.95),18);}
		else {Ball(Head+FVector(.16,0,-.018),FVector(.15,.25,.13),FLinearColor(.95,.78,.49));Ball(Head+FVector(.23,0,.008),FVector(.04,.07,.045),Black,12);}
		Tube({FVector(-.36,0,.02),FVector(-.48,.02,-.06),FVector(-.48,.10,-.19)},.018,Dark,10);
		if(G) for(int32 I=0;I<16;++I) {const double Z=I<10?-.13+(I%3)*.055:.08+(I-10)*.05;
			Ball(FVector(I<10?-.28+(I%5)*.10:.217,(I%2?1:-1)*(I<10?.316:.114),Z),FVector(.065,.025,.045),Dark,10);}
	}
	else if(O.Shape==TEXT("sphere")||O.Shape==TEXT("rock")||O.Shape==TEXT("fish")) {
		if(O.Shape==TEXT("fish")) {
			Ball(FVector(.07,0,0),FVector(.72,.32,.65),FLinearColor(.8,.91,1));
			const int32 A=Vertex(FVector(-.22,0,0)),B=Vertex(FVector(-.5,.02,.44)),C=Vertex(FVector(-.5,-.02,-.44));Tri(A,B,C);Tri(C,B,A);
			for(double Side:{-1.,1.}) Ball(FVector(.31,Side*.13,.09),FVector(.06,.03,.07),FLinearColor(.015,.02,.03),10);
		} else if(O.Shape==TEXT("rock")) {
			Surface(16,10,[&](double U,double V){const double A=U*2*PI,B=V*PI,R=.46*(1+.12*FMath::Sin(A*5+B*3)+.08*FMath::Cos(A*3-B*4));return FVector(FMath::Sin(B)*FMath::Cos(A),FMath::Sin(B)*FMath::Sin(A),FMath::Cos(B))*R;},FLinearColor(.82,.9,1));
		} else Ball(FVector::ZeroVector,FVector(1));
	}
	else if(O.Shape==TEXT("tube")||O.Shape==TEXT("ribbon")) {
		bool Flow=O.Animation==TEXT("flow");float Period=4,Phase=0;
		for(const auto& M:O.Motions) if(M.Type==TEXT("flow")||M.Type==TEXT("stream")) {Flow=M.Type==TEXT("flow");Period=M.Period;Phase=M.Phase;}
		const float RealTime=Time;Time=Time*(4.1666667f/Period); // Tube's progress is in cycles.
		if(O.Shape==TEXT("tube")) Tube(O.Curve,O.Radius,FLinearColor::White,56,Flow,Phase);
		else Surface(3,56,[&](double V,double U){const double S=FMath::Clamp(U+(Flow?FMath::Frac(Time*.24+Phase):0),0.,1.);return FLightBoxMotionSystem::Curve(O.Curve,S)+FVector(0,(V-.5)*O.Radius*2,0);});
		if(Continuous)for(int32 I=0;I<Unit.Colors.Num();++I) {
			const float U=Unit.UVs[I].Y;const float Band=.65f+.35f*FMath::Pow(FMath::Sin(U*30-Time*8+Phase*2*PI)*.5f+.5f,3.f);
			Unit.Colors[I]*=Band;
		}
		Time=RealTime;
	}
	else if(O.Shape==TEXT("water")) {
		Surface(36,24,[&](double U,double V){return FVector(U-.5,.5-V,(FMath::Sin(U*17+Time*.9)*.30+FMath::Sin(V*23+U*4-Time*1.2)*.22+FMath::Sin((U+V)*35+Time*1.5)*.08));});
	}
	else if(O.Shape==TEXT("bowl")) {
		const TArray<FVector2D> Profile={{0,-.48},{.20,-.48},{.28,-.40},{.43,.04},{.50,.40},{.50,.47},{.47,.50},{.435,.44},{.40,.08},{.24,-.34},{0,-.34}};
		Surface(40,40,[&](double U,double V){const double T=V*(Profile.Num()-1);const int32 K=FMath::Min(Profile.Num()-2,FMath::FloorToInt(T));const FVector2D P=FMath::Lerp(Profile[K],Profile[K+1],T-K);return FVector(P.X*FMath::Cos(U*2*PI),-P.X*FMath::Sin(U*2*PI),P.Y);});
	}
    else if(O.Shape==TEXT("lathe")&&O.Profile.Num()>=3) Surface(64,48,[&](double U,double V){
        const double Z=FMath::Lerp(O.Profile[0].X,O.Profile.Last().X,V);int32 J=1;while(J<O.Profile.Num()-1&&O.Profile[J].X<Z)++J;
        const auto A=O.Profile[J-1],B=O.Profile[J];const double D=FMath::Max(.00001,B.X-A.X),T=(Z-A.X)/D;
        const auto Prev=O.Profile[FMath::Max(0,J-2)],Next=O.Profile[FMath::Min(O.Profile.Num()-1,J+1)];
        const double MA=(B.Y-Prev.Y)/FMath::Max(.00001,B.X-Prev.X),MB=(Next.Y-A.Y)/FMath::Max(.00001,Next.X-A.X);
        const double R=FMath::Clamp(FMath::CubicInterp(A.Y,MA*D,B.Y,MB*D,T),0.,.5);
        FVector P(R*FMath::Cos(U*2*PI),R*FMath::Sin(U*2*PI),Z);
        if(O.Curve.Num()>1){const FVector C=FLightBoxMotionSystem::Curve(O.Curve,V,false);P.X+=C.X/O.Size.X;P.Y+=C.Y/O.Size.Y;}
        return P;
    });
	else if(O.Shape==TEXT("torus")) Surface(36,12,[&](double U,double V){const double A=U*2*PI,B=V*2*PI,Minor=FMath::Clamp(O.Radius/FMath::Max(1.,FMath::Min(O.Size.X,O.Size.Y)),.015,.24),R=.5-Minor+Minor*FMath::Cos(B);return FVector(R*FMath::Cos(A),R*FMath::Sin(A),.5*FMath::Sin(B));});
	else {
		TArray<FVector2D> Outline;
		if(O.Shape==TEXT("box")) Outline={FVector2D(-.5,-.5),FVector2D(.5,-.5),FVector2D(.5,.5),FVector2D(-.5,.5)};
		else if(O.Shape==TEXT("polygon")) Outline=O.Outline;
		else for(int32 I=0,N=O.Shape==TEXT("star")?10:O.Shape==TEXT("flower")?60:40;I<N;++I) {
			const double A=I*2*PI/N;FVector2D V;
			if(O.Shape==TEXT("heart")) V=FVector2D(FMath::Pow(FMath::Sin(A),3)*.5,(13*FMath::Cos(A)-5*FMath::Cos(2*A)-2*FMath::Cos(3*A)-FMath::Cos(4*A))*.5/17);
			else if(O.Shape==TEXT("leaf")) V=FVector2D(FMath::Cos(A)*.5,FMath::Sin(A)*FMath::Abs(FMath::Sin(A))*.27);
			else {double R=O.Shape==TEXT("flower")?.35+.15*FMath::Cos(A*5):O.Shape==TEXT("star")&&I%2?.22:.5;R*=1+O.Wobble*(FMath::Sin(A*3+1)*.5+FMath::Cos(A*5)*.3+FMath::Sin(A*7)*.2);V=FVector2D(FMath::Cos(A)*R,FMath::Sin(A)*R);}Outline.Add(V);
		}
		if(O.Shape==TEXT("cone")) {const int32 Top=Vertex(FVector(0,0,.5)),Bottom=Vertex(FVector(0,0,-.5));for(auto P:Outline) Vertex(FVector(P,-.5));for(int32 I=0;I<Outline.Num();++I){const int32 A=2+I,B=2+(I+1)%Outline.Num();Tri(Top,A,B);Tri(Bottom,B,A);}}
		else {
			for(double Z:{-.5,.5}) for(auto P:Outline) Vertex(FVector(P,Z));const int32 N=Outline.Num();const auto Cap=Triangulate(Outline);
			for(int32 I=0;I<Cap.Num();I+=3){Tri(Cap[I+2],Cap[I+1],Cap[I]);Tri(Cap[I]+N,Cap[I+1]+N,Cap[I+2]+N);}
			for(int32 I=0;I<N;++I){const int32 J=(I+1)%N;const int32 A=Vertex(FVector(Outline[I],-.5)),B=Vertex(FVector(Outline[J],-.5)),C=Vertex(FVector(Outline[I],.5)),D=Vertex(FVector(Outline[J],.5));Tri(A,B,C);Tri(B,D,C);}
		}
	}
	FLightBoxMesh Result;FRandomStream Random(GetTypeHash(O.Id));const bool CurveUnits=O.Shape==TEXT("tube")||O.Shape==TEXT("ribbon");
	for(int32 Copy=0;Copy<O.Copies;++Copy) {
		const double U=double(Copy)/FMath::Max(1,O.Copies),A=U*2*PI;FVector Offset(0);
		if(O.Copies>1) {
			if(O.Layout==TEXT("line")) Offset.X=(Copy-(O.Copies-1)*.5)*O.Spacing;
			else if(O.Layout==TEXT("grid")){const int32 N=FMath::CeilToInt(FMath::Sqrt(float(O.Copies)));Offset=FVector(Copy%N-(N-1)*.5,Copy/N-(N-1)*.5,0)*O.Spacing;}
			else if(O.Layout==TEXT("scatter")) Offset=FVector(Random.FRandRange(-1,1),Random.FRandRange(-1,1),Random.FRandRange(-.25,.25))*O.Spacing;
			else if(O.Layout==TEXT("spiral")) Offset=FVector(FMath::Cos(A*2)*U,FMath::Sin(A*2)*U,U*.5)*O.Spacing;
			else Offset=FVector(FMath::Cos(A),FMath::Sin(A),0)*O.Spacing;
		}
		const int32 Base=Result.Vertices.Num();
		for(int32 I=0;I<Unit.Vertices.Num();++I) {
			FVector V=Unit.Vertices[I]*(CurveUnits?FVector(1):O.Size);
			if(O.Shape==TEXT("water"))V.Z=Unit.Vertices[I].Z*FMath::Min(O.Size.Z,12.);
			if(O.Shape==TEXT("petal"))V.Z+=FMath::Sin((Unit.Vertices[I].X+.5)*PI)*O.Size.X*.12;
			for(const auto& M:O.Motions) {
				if(M.Type==TEXT("bend")) {const float Weight=FMath::Clamp(float((V-O.Pivot).Z/FMath::Max(1.,O.Size.Z)),0.f,1.f);V=O.Pivot+FQuat(M.Axis.GetSafeNormal(),FMath::DegreesToRadians(FLightBoxMotionSystem::Wave(M,Time)*M.Amplitude*Weight)).RotateVector(V-O.Pivot);}
				if(M.Type==TEXT("ripple")) V+=M.Axis.GetSafeNormal()*FMath::Sin((V.X+V.Y)*.05-Time*2*PI/M.Period+M.Phase*2*PI)*M.Amplitude;
			}
			Result.Vertices.Add(V+Offset); Result.Colors.Add(Unit.Colors[I]);Result.UVs.Add(Unit.UVs[I]);
		} for(int32 I:Unit.Triangles) Result.Triangles.Add(Base+I);
	}
	Result.Normals.Init(FVector::ZeroVector,Result.Vertices.Num());
	for(int32 I=0;I<Result.Triangles.Num();I+=3) {const int32 A=Result.Triangles[I],B=Result.Triangles[I+1],C=Result.Triangles[I+2];const FVector N=FVector::CrossProduct(Result.Vertices[B]-Result.Vertices[A],Result.Vertices[C]-Result.Vertices[A]);Result.Normals[A]+=N;Result.Normals[B]+=N;Result.Normals[C]+=N;}
	for(int32 I=0;I<Result.Normals.Num();++I) {
		const FVector Explicit=Unit.Normals[I%Unit.Normals.Num()];
		if(!Explicit.IsNearlyZero()&&!O.Motions.ContainsByPredicate([](const auto& M){return M.Type==TEXT("bend");})) Result.Normals[I]=Explicit/(CurveUnits?FVector(1):O.Size);
		Result.Normals[I]=Result.Normals[I].GetSafeNormal(.0001,FVector::UpVector);
	}
	// UE front faces use clockwise winding. Keep the outward shading normals above;
	// otherwise a two-sided lit material flips them and lights the insides of every shape.
	for(int32 I=0;I<Result.Triangles.Num();I+=3) Swap(Result.Triangles[I+1],Result.Triangles[I+2]);
	return Result;
}
