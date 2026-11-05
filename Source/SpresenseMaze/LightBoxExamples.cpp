#include "LightBoxStyle.h"

/** Offline examples also exercise the exact same primitives available to the AI. */
FLightBoxStyle FLightBoxStyle::Example(int32 Index)
{
	if(Index>=7) return Exhibit(Index-7);
	FLightBoxStyle S;
	auto Add=[&](const TCHAR* Id,const TCHAR* Shape,const FVector& P,const FVector& Size,const FLinearColor& Color) -> FLightBoxObject&
	{
		FLightBoxObject& O=S.Objects.AddDefaulted_GetRef(); O.Id=Id; O.Shape=Shape; O.Position=P; O.Size=Size; O.Color=Color; return O;
	};
	if (Index==0)
	{
		S.Title=TEXT("海の群舞"); S.Count=384; S.Behavior=TEXT("flock"); S.ParticleShape=TEXT("fish"); S.Color=FLinearColor(.22f,.5f,.75f); S.Accent=FLinearColor(.7f,.9f,1);
		S.Viscosity=.15f; S.Turbulence=.15f; S.Swirl=0; S.Trail=.12f; S.Groups=2; S.Background=FLinearColor(.002f,.015f,.045f); S.ParticleSize=1.2f;
	}
	else if (Index==1)
	{
		S.Title=TEXT("朝のひと皿"); S.Count=0; S.bShowBox=false; S.Ambient=1.1f; S.Background=FLinearColor(.06f,.10f,.13f); S.Floor=FLinearColor(.13f,.19f,.21f); S.CameraPitch=48; S.CameraZoom=1.2f;
		Add(TEXT("plate"),TEXT("disk"),FVector(0,0,52),FVector(285,245,12),FLinearColor(.55f,.71f,.8f));
		Add(TEXT("rim"),TEXT("torus"),FVector(0,0,61),FVector(280,240,8),FLinearColor(.8f,.93f,1));
		auto& White=Add(TEXT("egg-white"),TEXT("disk"),FVector(0,0,65),FVector(215,170,8),FLinearColor(.95f,.94f,.83f)); White.Wobble=.18f;
		Add(TEXT("yolk"),TEXT("sphere"),FVector(-12,-6,81),FVector(78,73,33),FLinearColor(1,.55f,.015f));
		Add(TEXT("shine"),TEXT("sphere"),FVector(-23,-14,94),FVector(20,12,3),FLinearColor(1,.85f,.38f));
		auto& Herbs=Add(TEXT("herbs"),TEXT("leaf"),FVector(74,12,74),FVector(30,13,3),FLinearColor(.08f,.4f,.12f)); Herbs.Copies=3; Herbs.Layout=TEXT("ring"); Herbs.Spacing=17;
	}
	else if (Index==2)
	{
		S.Title=TEXT("星降る小さな森"); S.Count=160; S.Color=FLinearColor(.85f,.3f,.7f); S.Accent=FLinearColor(1,.8f,.25f); S.Motion=.5f;
		S.Background=FLinearColor(.035f,.015f,.075f); S.Floor=FLinearColor(.065f,.14f,.11f); S.Ambient=1.3f; S.Trail=.3f;
		auto& Trunks=Add(TEXT("trunks"),TEXT("cylinder"),FVector(0,25,95),FVector(22,22,120),FLinearColor(.32f,.15f,.07f)); Trunks.Copies=5; Trunks.Spacing=165;
		auto& Trees=Add(TEXT("canopies"),TEXT("cone"),FVector(0,25,185),FVector(105,105,145),FLinearColor(.07f,.4f,.25f)); Trees.Copies=5; Trees.Spacing=165; Trees.Animation=TEXT("sway"); Trees.Amplitude=4; Trees.Speed=.4f;
		Add(TEXT("house"),TEXT("box"),FVector(0,-60,88),FVector(100,85,100),FLinearColor(.8f,.47f,.64f));
		Add(TEXT("roof"),TEXT("cone"),FVector(0,-60,170),FVector(150,125,85),FLinearColor(.36f,.13f,.55f));
		auto& Window=Add(TEXT("window"),TEXT("sphere"),FVector(0,-104,105),FVector(25,5,30),FLinearColor(1,.66f,.17f)); Window.Emission=2;
		auto& Stars=Add(TEXT("stars"),TEXT("star"),FVector(0,0,300),FVector(23,23,6),FLinearColor(1,.65f,.2f)); Stars.Copies=7; Stars.Spacing=145; Stars.Rotation.X=90; Stars.Animation=TEXT("bob"); Stars.Amplitude=12; Stars.Emission=2; Stars.Reaction=TEXT("shake");
	}
	else if (Index==3)
	{
		S.Title=TEXT("てのひらの宇宙"); S.Count=300; S.Behavior=TEXT("orbit"); S.Formation=TEXT("galaxy"); S.Spread=230; S.Turbulence=.1f; S.Swirl=.4f;
		S.Color=FLinearColor(.2f,.3f,.8f); S.Accent=FLinearColor(.7f,.45f,1); S.ParticleSize=.5f; S.Trail=.3f; S.bShowBox=false; S.Floor=FLinearColor(.002f,.003f,.009f);
		auto& Sun=Add(TEXT("sun"),TEXT("sphere"),FVector(0,0,170),FVector(78),FLinearColor(1,.4f,.04f)); Sun.Emission=1.4f;
		for (int32 I=0; I<4; ++I)
		{
			auto& P=Add(*FString::Printf(TEXT("planet%d"),I),TEXT("sphere"),FVector(0,0,170+I*9),FVector(20+I*7),FLinearColor::MakeFromHSV8(90+I*34,170,240));
			P.Animation=TEXT("orbit"); P.Amplitude=80+I*23; P.Speed=.8f-I*.15f;
		}
		auto& Ring=Add(TEXT("saturn-ring"),TEXT("torus"),FVector(0,0,197),FVector(60,60,3),FLinearColor(.8f,.55f,.25f)); Ring.Animation=TEXT("orbit"); Ring.Amplitude=149; Ring.Speed=.35f;
	}
	else if (Index==4)
	{
		S.Title=TEXT("冬の散歩道"); S.Count=650; S.Behavior=TEXT("snow"); S.Motion=.7f; S.Wind=FVector(.15f,0,0); S.Swirl=0; S.Turbulence=.45f; S.Color=FLinearColor(.8f,.9f,1); S.Accent=FLinearColor(.5f,.7f,1); S.Trail=.05f;
		S.Background=FLinearColor(.025f,.06f,.11f); S.Floor=FLinearColor(.45f,.65f,.8f); S.Ambient=1.1f;
		auto& Trees=Add(TEXT("snow-trees"),TEXT("cone"),FVector(0,40,130),FVector(85,85,185),FLinearColor(.16f,.38f,.3f)); Trees.Copies=8; Trees.Layout=TEXT("ring"); Trees.Spacing=180;
		Add(TEXT("snowman-base"),TEXT("sphere"),FVector(0,-65,72),FVector(72),FLinearColor(.85f,.92f,1));
		Add(TEXT("snowman-head"),TEXT("sphere"),FVector(0,-65,125),FVector(48),FLinearColor(.85f,.92f,1));
		auto& Nose=Add(TEXT("nose"),TEXT("cone"),FVector(0,-93,124),FVector(12,12,30),FLinearColor(.9f,.24f,.03f)); Nose.Rotation.X=90;
		Add(TEXT("hat"),TEXT("cylinder"),FVector(0,-65,163),FVector(40,40,35),FLinearColor(.025f,.04f,.07f));
	}
	else if (Index==5)
	{
		S.Title=TEXT("光のハート"); S.Count=512; S.Behavior=TEXT("form"); S.Formation=TEXT("heart"); S.FormationStrength=3; S.Spread=160; S.Turbulence=.05f; S.Tilt=.15f; S.Trail=.4f;
		S.Color=FLinearColor(1,.08f,.3f); S.Accent=FLinearColor(1,.5f,.7f); S.CameraPitch=18; S.ParticleSize=.65f;
	}
	else
	{
		S.Title=TEXT("夜のまちあかり"); S.Count=256; S.Behavior=TEXT("fireworks"); S.ColorMode=TEXT("rainbow"); S.Gravity=.25f; S.Turbulence=.1f; S.Trail=.8f; S.Swirl=0;
		S.Center=FVector(0,0,250); S.Floor=FLinearColor(.015f,.02f,.045f);
		for (int32 I=0; I<5; ++I)
		{
			auto& O=Add(*FString::Printf(TEXT("building%d"),I),TEXT("box"),FVector((I-2)*77,55,70+I%3*20),FVector(55,65,80+I%3*40),FLinearColor(.04f,.09f+.025f*I,.18f));
			O.Reaction=TEXT("shake"); O.Amplitude=12;
			auto& W=Add(*FString::Printf(TEXT("window%d"),I),TEXT("box"),FVector((I-2)*77,20,75),FVector(8,3,11),FLinearColor(1,.55f,.12f)); W.Copies=6; W.Layout=TEXT("grid"); W.Spacing=15; W.Emission=1;
		}
	}
	if(Index==1) for(auto& O:S.Objects) { O.Position.Z-=46; O.Roughness=O.Id==TEXT("yolk")?.18f:.42f; }
	if(Index==2) {
		S.Objects.RemoveAll([](const auto& O){return O.Id==TEXT("trunks");});
		for(auto& O:S.Objects) if(O.Id==TEXT("canopies")) {O.Shape=TEXT("tree");O.Position.Z=100;O.Size=FVector(110,110,200);O.Animation=TEXT("none");FLightBoxMotion M;M.Type=TEXT("bend");M.Axis=FVector(1,0,0);M.Amplitude=3;M.Period=7;O.Motions.Add(M);O.bFocus=false;}
		S.Turbulence=.4f; S.KeyIntensity=1.2f;S.Ambient=.45f;S.KeyColor=FLinearColor(.55f,.67f,1);S.Bloom=.45f;
		FLightBoxLight L;L.Position=FVector(0,-130,110);L.Color=FLinearColor(1,.50f,.13f);L.Intensity=1800;L.Radius=240;S.Lights.Add(L);
	}
	return S;
}
