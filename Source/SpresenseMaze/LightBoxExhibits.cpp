#include "LightBoxStyle.h"

// These scenes use the public composition vocabulary; the AI can edit every part.
FLightBoxStyle FLightBoxStyle::Exhibit(int32 Index)
{
	FLightBoxStyle S;S.Count=0;S.bShowBox=false;S.Trail=.12f;S.CameraZoom=1;S.CameraPitch=30;S.CameraYaw=-12;S.Ambient=.8f;S.Bloom=.35f;
	S.Background=FLinearColor(.009f,.018f,.031f);S.Floor=FLinearColor(.028f,.05f,.065f);
	auto Add=[&](FString Id,FString Shape,FVector P,FVector Size,FLinearColor Color)->FLightBoxObject& {
		auto& O=S.Objects.AddDefaulted_GetRef();O.Id=Id;O.Shape=Shape;O.Position=P;O.Size=Size;O.Color=Color;return O;
	};
	auto Motion=[](FLightBoxObject& O,FString Type,FVector Axis,float Amp,float Period,float Phase=0) {
		FLightBoxMotion M;M.Type=Type;M.Axis=Axis;M.Amplitude=Amp;M.Period=Period;M.Phase=Phase;O.Motions.Add(M);
	};
	auto Tube=[&](FString Id,TArray<FVector> P,float Radius,FLinearColor Color)->FLightBoxObject& {
		auto& O=Add(Id,TEXT("tube"),FVector::ZeroVector,FVector(1),Color);O.Curve=P;O.Radius=Radius;return O;
	};
	if(Index==0) {
		S.Title=TEXT("潮風の翼");S.Background=FLinearColor(.025f,.095f,.15f);S.Floor=FLinearColor(.018f,.055f,.08f);S.CameraPitch=24;S.CameraYaw=8;S.KeyIntensity=4.5f;
		auto& Sea=Add(TEXT("sea"),TEXT("water"),FVector(0,0,10),FVector(600,400,13),FLinearColor(.018f,.16f,.22f));Sea.bFocus=false;Sea.Roughness=.16f;
		for(int32 I=0;I<4;++I) {
			auto& Bird=Add(FString::Printf(TEXT("gull%d"),I),TEXT("bird"),FVector((I-1.5)*92,(I%2-.5)*82,115+(I%3)*32),FVector(70,145,40),FLinearColor(.94f,.97f,1));
			Bird.Animation=TEXT("fly");Bird.Amplitude=28;Bird.Speed=.75f+I*.12f;Bird.Roughness=.65f;
		}
		for(int32 I=0;I<3;++I) {auto& R=Add(FString::Printf(TEXT("shore%d"),I),TEXT("rock"),FVector(180+I*28,100-I*25,25),FVector(80,65,55),FLinearColor(.12f,.20f,.24f));R.bFocus=false;}
	}
	else if(Index==1) {
		S.Title=TEXT("ひとくちの湯気");S.CameraPitch=24;S.CameraYaw=-8;S.Floor=FLinearColor(.055f,.027f,.016f);S.Background=FLinearColor(.017f,.012f,.022f);S.KeyColor=FLinearColor(1,.73f,.43f);S.FillColor=FLinearColor(.37f,.53f,1);S.CameraFill=.76f;
		auto& Bowl=Add(TEXT("bowl"),TEXT("bowl"),FVector(-50,0,54),FVector(185,166,102),FLinearColor(.12f,.25f,.34f));Bowl.Roughness=.22f;
		Add(TEXT("broth"),TEXT("disk"),FVector(-50,0,94),FVector(151,132,5),FLinearColor(.37f,.16f,.025f)).Roughness=.13f;
		Add(TEXT("bowl-rim"),TEXT("torus"),FVector(-50,0,104),FVector(180,161,5),FLinearColor(.65f,.77f,.77f)).Roughness=.2f;
		Add(TEXT("shirt"),TEXT("sphere"),FVector(70,33,103),FVector(110,68,205),FLinearColor(.065f,.17f,.20f));
		Add(TEXT("neck"),TEXT("sphere"),FVector(62,9,204),FVector(32,35,60),FLinearColor(.76f,.42f,.25f));
		Add(TEXT("face"),TEXT("sphere"),FVector(62,5,253),FVector(96,65,106),FLinearColor(.84f,.53f,.34f)).Roughness=.75f;
		Add(TEXT("hair"),TEXT("sphere"),FVector(62,12,286),FVector(101,70,54),FLinearColor(.025f,.018f,.022f));
		Add(TEXT("mouth"),TEXT("sphere"),FVector(60,-28,232),FVector(29,10,20),FLinearColor(.045f,.01f,.012f));
		Add(TEXT("nose"),TEXT("sphere"),FVector(62,-30,249),FVector(12,12,16),FLinearColor(.78f,.43f,.27f));
		for(int32 I=0;I<2;++I) {
			const float X=I?82:43;
			Add(FString::Printf(TEXT("eye%d"),I),TEXT("sphere"),FVector(X,-23,269),FVector(9,7,6),FLinearColor(.015f,.014f,.021f));
			Tube(FString::Printf(TEXT("brow%d"),I),{FVector(X-7,-23,277),FVector(X,-25,279),FVector(X+7,-23,277)},1.2f,FLinearColor(.035f,.019f,.02f));
			Add(FString::Printf(TEXT("cheek%d"),I),TEXT("sphere"),FVector(I?85:39,-21,248),FVector(15,2,8),FLinearColor(.76f,.27f,.22f));
		}
		for(int32 I=0;I<7;++I) {
			const float D=(I-3)*3.f;
			auto& N=Tube(FString::Printf(TEXT("noodle%d"),I),{FVector(-65+D,-18,96),FVector(-52+D,-16,135),FVector(6+D,-24,163),FVector(43+D*.25f,-31,215),FVector(60+D*.14f,-33,232)},1.65f,FLinearColor(.95f,.69f,.22f));
			Motion(N,TEXT("flow"),FVector::UpVector,1,3.2f,I*.113f);N.Roughness=.3f;
		}
		Tube(TEXT("chopstick-a"),{FVector(-77,-7,113),FVector(30,16,161)},2.1f,FLinearColor(.37f,.16f,.055f));
		Tube(TEXT("chopstick-b"),{FVector(-64,-7,114),FVector(35,20,161)},2.1f,FLinearColor(.37f,.16f,.055f));
		Add(TEXT("egg"),TEXT("sphere"),FVector(-86,18,99),FVector(39,28,9),FLinearColor(.95f,.88f,.68f));
		Add(TEXT("yolk"),TEXT("sphere"),FVector(-86,18,104),FVector(21,18,5),FLinearColor(1,.47f,.018f));
		auto& Herbs=Add(TEXT("spring-onion"),TEXT("torus"),FVector(-24,17,101),FVector(12,12,5),FLinearColor(.13f,.45f,.12f));Herbs.Copies=8;Herbs.Spacing=21;
		FLightBoxLight L;L.Position=FVector(-95,-60,165);L.Color=FLinearColor(1,.47f,.15f);L.Intensity=900;S.Lights.Add(L);
	}
	else if(Index==2) {
		S.Title=TEXT("ひとつになる群れ");S.Count=420;S.ParticleShape=TEXT("fish");S.ParticleSize=.8f;S.Behavior=TEXT("flock");S.Formation=TEXT("fish");S.FormationStrength=3.5f;S.FormationTravel=24;S.Spread=225;S.Center=FVector(0,0,175);
		S.ColorMode=TEXT("leader");S.Color=FLinearColor(.22f,.62f,.77f);S.Accent=FLinearColor(1,.09f,.025f);S.Turbulence=.07f;S.Trail=.08f;S.Viscosity=.2f;S.Swirl=0;S.Tilt=.16f;S.CameraPitch=16;S.CameraYaw=0;
		S.Background=FLinearColor(.001f,.012f,.03f);S.Floor=FLinearColor(.004f,.022f,.04f);S.Glow=.9f;
	}
	else if(Index==3) {
		S.Title=TEXT("木陰のサファリ");S.CameraPitch=25;S.CameraYaw=-8;S.Floor=FLinearColor(.055f,.12f,.07f);S.Background=FLinearColor(.014f,.04f,.045f);S.KeyIntensity=4;S.KeyColor=FLinearColor(1,.82f,.52f);
		Add(TEXT("elephant"),TEXT("elephant"),FVector(-150,-18,58),FVector(139,100,116),FLinearColor(.34f,.43f,.46f)).Rotation.Z=-35;
		Add(TEXT("giraffe"),TEXT("giraffe"),FVector(0,18,142),FVector(120,92,284),FLinearColor(.91f,.58f,.17f)).Rotation.Z=-55;
		Add(TEXT("lion"),TEXT("lion"),FVector(150,-25,46),FVector(124,88,92),FLinearColor(.88f,.51f,.12f)).Rotation.Z=-85;
		for(int32 I=0;I<3;++I) {auto& Tree=Add(FString::Printf(TEXT("tree%d"),I),TEXT("tree"),FVector((I-1)*180,125,88),FVector(108,100,176),FLinearColor(.15f,.37f,.16f));Tree.bFocus=false;Motion(Tree,TEXT("bend"),FVector(1,0,0),3,7,I*.2f);}
		TArray<FVector> RailA,RailB;
		for(int32 I=0;I<=16;++I) {float A=I*2*PI/16;RailA.Add(FVector(FMath::Cos(A)*245,FMath::Sin(A)*150,38));RailB.Add(FVector(FMath::Cos(A)*245,FMath::Sin(A)*150,21));}
		Tube(TEXT("upper-rail"),RailA,2.7f,FLinearColor(.32f,.17f,.07f)).bFocus=false;
		Tube(TEXT("lower-rail"),RailB,2.7f,FLinearColor(.32f,.17f,.07f)).bFocus=false;
		for(int32 I=0;I<12;++I) {const float A=I*2*PI/12;auto& Post=Add(FString::Printf(TEXT("post%d"),I),TEXT("cylinder"),FVector(FMath::Cos(A)*245,FMath::Sin(A)*150,24),FVector(7,7,48),FLinearColor(.35f,.21f,.10f));Post.bFocus=false;}
	}
	else if(Index==4) {
		S.Title=TEXT("ゆっくり、ひらく");S.CameraPitch=28;S.Background=FLinearColor(.019f,.011f,.04f);S.Floor=FLinearColor(.038f,.024f,.06f);
		Add(TEXT("flower-root"),TEXT("group"),FVector(0,0,130),FVector(1),FLinearColor::White);
		Tube(TEXT("stem"),{FVector(0,0,0),FVector(-12,0,55),FVector(0,0,130)},5,FLinearColor(.08f,.35f,.14f));
		Add(TEXT("center"),TEXT("sphere"),FVector(0,0,130),FVector(47,47,32),FLinearColor(1,.63f,.10f)).Roughness=.7f;
		for(int32 I=0;I<9;++I) {const float A=I*2*PI/9;
			auto& Petal=Add(FString::Printf(TEXT("petal%d"),I),TEXT("leaf"),FVector(FMath::Cos(A)*42,FMath::Sin(A)*42,0),FVector(110,110,8),FLinearColor(.76f,.09f+.015f*I,.30f+.04f*I));Petal.Parent=TEXT("flower-root");Petal.Pivot=FVector(-42,0,0);Petal.Rotation.Z=I*360.f/9;
			Motion(Petal,TEXT("rotate"),FVector(-FMath::Sin(A),FMath::Cos(A),0),44,6);Petal.Roughness=.34f;
		}
		Add(TEXT("base"),TEXT("bowl"),FVector(0,0,23),FVector(110,110,46),FLinearColor(.12f,.18f,.22f)).Metallic=.35f;
	}
	else {
		S.Title=TEXT("時を運ぶ装置");S.Background=FLinearColor(.012f,.022f,.028f);S.Floor=FLinearColor(.028f,.045f,.048f);S.CameraPitch=30;
		for(int32 K=0;K<2;++K) {
			const FString Id=FString::Printf(TEXT("rotor%d"),K);auto& Root=Add(Id,TEXT("group"),FVector(K?80:-78,0,125),FVector(1),FLinearColor::White);
			Motion(Root,TEXT("rotate"),FVector(0,1,0),K?-360:360,8);Root.Motions[0].Wave=TEXT("saw");
			auto& Ring=Add(Id+TEXT("ring"),TEXT("torus"),FVector::ZeroVector,FVector(140,140,24),FLinearColor(.64f,.32f,.075f));Ring.Parent=Id;Ring.Rotation.X=90;Ring.Metallic=.8f;Ring.Roughness=.25f;Ring.Radius=12;
			auto& Teeth=Add(Id+TEXT("teeth"),TEXT("box"),FVector::ZeroVector,FVector(19,22,20),FLinearColor(.50f,.23f,.055f));Teeth.Parent=Id;Teeth.Copies=16;Teeth.Layout=TEXT("ring");Teeth.Spacing=76;Teeth.Rotation.X=90;Teeth.Metallic=.8f;
			for(int32 J=0;J<4;++J) {auto& Spoke=Add(Id+FString::Printf(TEXT("spoke%d"),J),TEXT("box"),FVector::ZeroVector,FVector(120,12,12),FLinearColor(.56f,.28f,.08f));Spoke.Parent=Id;Spoke.Rotation.Y=J*45;Spoke.Metallic=.8f;}
			Add(Id+TEXT("stand"),TEXT("cylinder"),FVector(K?80:-78,15,45),FVector(34,40,90),FLinearColor(.12f,.22f,.24f)).Metallic=.6f;
		}
		auto& Traveler=Add(TEXT("traveler"),TEXT("sphere"),FVector(0,-65,45),FVector(27),FLinearColor(.03f,.65f,.74f));Traveler.Metallic=.5f;Traveler.Emission=.25f;
		Motion(Traveler,TEXT("path"),FVector::UpVector,1,6);Traveler.Motions[0].Path={FVector(-180,0,0),FVector(0,-30,20),FVector(180,0,0),FVector(0,45,50)};Traveler.Motions[0].bOrient=false;
		Tube(TEXT("track"),{FVector(-180,-65,45),FVector(0,-95,65),FVector(180,-65,45),FVector(0,-20,95)},2.5f,FLinearColor(.14f,.35f,.38f)).bCurveClosed=true;
	}
	return S;
}
