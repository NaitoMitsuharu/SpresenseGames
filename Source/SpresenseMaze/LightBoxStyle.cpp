#include "LightBoxStyle.h"
#include "LightBoxModels.h"

#include "Dom/JsonObject.h"
#include "LightBoxGeometry.h"
#include "LightBoxMotion.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FString Hex(const FLinearColor& Color) { return TEXT("#") + Color.ToFColorSRGB().ToHex().Left(6); }
	bool ReadColor(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FLinearColor& Out)
	{
		FString Value;
		if (!Object->TryGetStringField(Key, Value) || Value.Len() != 7 || Value[0] != TEXT('#')) { return false; }
		for (int32 I = 1; I < 7; ++I) { if (!FChar::IsHexDigit(Value[I])) { return false; } }
		Out = FLinearColor::FromSRGBColor(FColor::FromHex(Value));
		return true;
	}
	using FObject = TSharedPtr<FJsonObject>;
	TArray<TSharedPtr<FJsonValue>> Vec(const FVector& V)
	{
		return { MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z) };
	}
	bool Number(const FObject& O, const TCHAR* K, float& Out, double Min, double Max, bool Required = false)
	{
		if (!O->HasField(K)) { return !Required; }
		double V;
		if (!O->TryGetNumberField(K, V) || !FMath::IsFinite(V) || V < Min || V > Max) { return false; }
		Out = V; return true;
	}
	bool Integer(const FObject& O, const TCHAR* K, int32& Out, int32 Min, int32 Max)
	{
		float V = Out;
		if (!Number(O, K, V, Min, Max) || V != FMath::FloorToFloat(V)) { return false; }
		Out = static_cast<int32>(V); return true;
	}
	bool Vector(const FObject& O, const TCHAR* K, FVector& Out, double Min, double Max, bool Required = false)
	{
		if (!O->HasField(K)) { return !Required; }
		const TArray<TSharedPtr<FJsonValue>>* A;
		if (!O->TryGetArrayField(K, A) || A->Num() != 3) { return false; }
		for (int32 I = 0; I < 3; ++I)
		{
			double V;
			if (!(*A)[I]->TryGetNumber(V) || !FMath::IsFinite(V) || V < Min || V > Max) { return false; }
			Out[I] = V;
		}
		return true;
	}
	bool Choice(const FObject& O, const TCHAR* K, FString& Out, std::initializer_list<const TCHAR*> Choices)
	{
		if (!O->HasField(K)) { return true; }
		if (!O->TryGetStringField(K, Out)) { return false; }
		for (const TCHAR* C : Choices) { if (Out == C) { return true; } }
		return false;
	}
	bool OptionalColor(const FObject& O, const TCHAR* K, FLinearColor& Out) { return !O->HasField(K) || ReadColor(O, K, Out); }
	bool KnownFields(const FObject& O, std::initializer_list<const TCHAR*> Names)
	{
		for (const auto& Pair : O->Values)
		{
			bool Found = false;
			for (const TCHAR* N : Names) { Found |= Pair.Key == N; }
			if (!Found) { return false; }
		}
		return true;
	}
	bool Points(const FObject& O,const TCHAR* Key,TArray<FVector>& Out,int32 MinCount,int32 MaxCount,double Limit=600)
	{
		if (!O->HasField(Key)) return true;
		const TArray<TSharedPtr<FJsonValue>>* A;
		if (!O->TryGetArrayField(Key,A) || A->Num()<MinCount || A->Num()>MaxCount) return false;
		for (const auto& V:*A) { auto T=MakeShared<FJsonObject>(); T->SetField(TEXT("v"),V); FVector P; if(!Vector(T,TEXT("v"),P,-Limit,Limit,true)) return false; Out.Add(P); }
		return true;
	}
	TArray<TSharedPtr<FJsonValue>> JsonPoints(const TArray<FVector>& Points)
	{
		TArray<TSharedPtr<FJsonValue>> A; for(const FVector& P:Points) A.Add(MakeShared<FJsonValueArray>(Vec(P))); return A;
	}
	bool ReadSdfParts(const FObject& O,TArray<FLightBoxSdfPart>& Out)
	{
		if(!O->HasField(TEXT("parts")))return true;
		const TArray<TSharedPtr<FJsonValue>>* A;if(!O->TryGetArrayField(TEXT("parts"),A)||A->Num()<1||A->Num()>LightBoxSdf::MaxParts)return false;
		bool bAdd=false;
		for(const auto& V:*A){
			if(V->Type!=EJson::Object)return false;const FObject J=V->AsObject();FLightBoxSdfPart S;
			if(!KnownFields(J,{TEXT("type"),TEXT("a"),TEXT("b"),TEXT("r"),TEXT("rot"),TEXT("k"),TEXT("round"),TEXT("cut"),TEXT("smooth"),TEXT("color")})
				||!Choice(J,TEXT("type"),S.Type,{TEXT("ellipsoid"),TEXT("capsule"),TEXT("box"),TEXT("torus")})
				||!Vector(J,TEXT("a"),S.A,-400,400,true)||!Vector(J,TEXT("b"),S.B,-400,400,S.Type==TEXT("capsule"))||!Vector(J,TEXT("rot"),S.Rotation,-360,360)
				||!Number(J,TEXT("k"),S.Blend,0,60)||!Number(J,TEXT("round"),S.Round,0,100)
				||(J->HasField(TEXT("cut"))&&!J->TryGetBoolField(TEXT("cut"),S.bCut))||(J->HasField(TEXT("smooth"))&&!J->TryGetBoolField(TEXT("smooth"),S.bSmooth)))return false;
			if(J->HasField(TEXT("color"))){if(!ReadColor(J,TEXT("color"),S.Color))return false;S.bHasColor=true;}
			const TArray<TSharedPtr<FJsonValue>>* R;if(!J->TryGetArrayField(TEXT("r"),R)||R->Num()<1||R->Num()>3)return false;
			double Radii[3];for(int32 I=0;I<R->Num();++I)if(!(*R)[I]->TryGetNumber(Radii[I])||!FMath::IsFinite(Radii[I])||Radii[I]<.2||Radii[I]>300)return false;
			for(int32 I=R->Num();I<3;++I)Radii[I]=Radii[R->Num()-1];
			S.R=FVector(Radii[0],Radii[1],Radii[2]);
			if(S.Type==TEXT("box")&&S.Round>=S.R.GetMin())S.Round=S.R.GetMin()*.95f;
			bAdd|=!S.bCut;Out.Add(MoveTemp(S));
		}
		return bAdd;
	}
	TArray<TSharedPtr<FJsonValue>> SdfPartsJson(const TArray<FLightBoxSdfPart>& Parts)
	{
		TArray<TSharedPtr<FJsonValue>> A;
		for(const auto& S:Parts){auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("type"),S.Type);J->SetArrayField(TEXT("a"),Vec(S.A));
			if(S.Type==TEXT("capsule"))J->SetArrayField(TEXT("b"),Vec(S.B));
			TArray<TSharedPtr<FJsonValue>> R;const int32 Count=S.Type==TEXT("capsule")||S.Type==TEXT("torus")?2:3;for(int32 I=0;I<Count;++I)R.Add(MakeShared<FJsonValueNumber>(S.R[I]));J->SetArrayField(TEXT("r"),R);
			if(!S.Rotation.IsZero())J->SetArrayField(TEXT("rot"),Vec(S.Rotation));
			J->SetNumberField(TEXT("k"),S.Blend);if(S.Round>0)J->SetNumberField(TEXT("round"),S.Round);
			if(S.bCut)J->SetBoolField(TEXT("cut"),true);if(S.bSmooth)J->SetBoolField(TEXT("smooth"),true);if(S.bHasColor)J->SetStringField(TEXT("color"),Hex(S.Color));
			A.Add(MakeShared<FJsonValueObject>(J));}
		return A;
	}
	TSharedRef<FJsonObject> ObjectJson(const FLightBoxObject& N)
	{
		auto O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), N.Id); O->SetStringField(TEXT("shape"), N.Shape); O->SetStringField(TEXT("color"), Hex(N.Color));
		O->SetArrayField(TEXT("position"), Vec(N.Position)); O->SetArrayField(TEXT("size"), Vec(N.Size)); O->SetArrayField(TEXT("rotation"), Vec(N.Rotation));
		O->SetNumberField(TEXT("emission"), N.Emission); O->SetNumberField(TEXT("wobble"), N.Wobble);
		O->SetNumberField(TEXT("copies"), N.Copies); O->SetStringField(TEXT("layout"), N.Layout); O->SetNumberField(TEXT("spacing"), N.Spacing);
		O->SetStringField(TEXT("animation"), N.Animation); O->SetNumberField(TEXT("speed"), N.Speed); O->SetNumberField(TEXT("amplitude"), N.Amplitude);
		O->SetStringField(TEXT("reaction"), N.Reaction);
		O->SetStringField(TEXT("anchor"),N.Anchor);O->SetStringField(TEXT("parentAnchor"),N.ParentAnchor);
		if(N.Sections.Num()) O->SetArrayField(TEXT("sections"),JsonPoints(N.Sections));
        O->SetStringField(TEXT("pattern"),N.Pattern);O->SetStringField(TEXT("patternColor"),Hex(N.PatternColor));O->SetNumberField(TEXT("patternScale"),N.PatternScale);O->SetBoolField(TEXT("belly"),N.bBelly);O->SetStringField(TEXT("bellyColor"),Hex(N.BellyColor));
        if(!N.Model.IsEmpty())O->SetStringField(TEXT("model"),N.Model);
        if(N.SdfParts.Num())O->SetArrayField(TEXT("parts"),SdfPartsJson(N.SdfParts));
        if(N.Detail!=TEXT("none")){O->SetStringField(TEXT("detail"),N.Detail);O->SetNumberField(TEXT("detailLength"),N.DetailLength);}
        if(N.Profile.Num()) { TArray<TSharedPtr<FJsonValue>> A;for(auto P:N.Profile)A.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(P.X),MakeShared<FJsonValueNumber>(P.Y)}));O->SetArrayField(TEXT("profile"),A); }
		if(!N.StartObject.IsEmpty()) {O->SetStringField(TEXT("startObject"),N.StartObject);O->SetStringField(TEXT("startAnchor"),N.StartAnchor);O->SetArrayField(TEXT("startOffset"),Vec(N.StartOffset));}
		if(!N.EndObject.IsEmpty()) {O->SetStringField(TEXT("endObject"),N.EndObject);O->SetStringField(TEXT("endAnchor"),N.EndAnchor);O->SetArrayField(TEXT("endOffset"),Vec(N.EndOffset));}
		O->SetBoolField(TEXT("closed"),N.bCurveClosed); O->SetStringField(TEXT("parent"),N.Parent); O->SetArrayField(TEXT("pivot"),Vec(N.Pivot));
        O->SetBoolField(TEXT("attached"),N.bAttached);
		O->SetNumberField(TEXT("roughness"),N.Roughness); O->SetNumberField(TEXT("metallic"),N.Metallic); O->SetNumberField(TEXT("radius"),N.Radius); O->SetBoolField(TEXT("focus"),N.bFocus);
		if(N.Curve.Num()) O->SetArrayField(TEXT("curve"),JsonPoints(N.Curve));
		TArray<TSharedPtr<FJsonValue>> Motions;
		for(const FLightBoxMotion& M:N.Motions) {
			auto A=MakeShared<FJsonObject>(); A->SetStringField(TEXT("type"),M.Type); A->SetStringField(TEXT("wave"),M.Wave);A->SetStringField(TEXT("space"),M.Space); A->SetArrayField(TEXT("axis"),Vec(M.Axis));
			A->SetNumberField(TEXT("amplitude"),M.Amplitude); A->SetNumberField(TEXT("period"),M.Period); A->SetNumberField(TEXT("phase"),M.Phase);
			A->SetBoolField(TEXT("closed"),M.bClosed); A->SetBoolField(TEXT("orient"),M.bOrient); if(M.Path.Num()) A->SetArrayField(TEXT("path"),JsonPoints(M.Path));
			Motions.Add(MakeShared<FJsonValueObject>(A));
		} if(Motions.Num()) O->SetArrayField(TEXT("motions"),Motions);
		TArray<TSharedPtr<FJsonValue>> Path, Outline;
		for (const FVector& P : N.Path) { Path.Add(MakeShared<FJsonValueArray>(Vec(P))); }
		for (const FVector2D& P : N.Outline) { Outline.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y) })); }
		if (Path.Num()) { O->SetArrayField(TEXT("path"), Path); }
		if (Outline.Num()) { O->SetArrayField(TEXT("outline"), Outline); }
		return O;
	}
}

FLightBoxStyle FLightBoxStyle::Preset(int32 Index)
{
	FLightBoxStyle S;
	if (Index == 1)
	{
		S.Title = TEXT("深海"); S.Color = FLinearColor(0.08f, 0.45f, 1.f);
		S.Accent = FLinearColor(0.25f, 1.f, 0.78f); S.Motion = 0.55f;
		S.Viscosity = 0.82f; S.Swirl = 0.25f; S.Attraction = 0.15f; S.Trail = 0.95f;
	}
	else if (Index == 2)
	{
		S.Title = TEXT("ほたる"); S.Color = FLinearColor(0.58f, 1.f, 0.18f);
		S.Accent = FLinearColor(1.f, 0.72f, 0.15f); S.Motion = 0.7f;
		S.Swirl = -0.3f; S.Attraction = 0.8f; S.Tilt = 0.55f; S.Shake = TEXT("gather");
		S.Trail = 0.3f;
	}
	return S;
}

FString FLightBoxStyle::ToJson() const
{
	const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("title"), Title);
	O->SetStringField(TEXT("color"), Hex(Color)); O->SetStringField(TEXT("accent"), Hex(Accent));
	O->SetNumberField(TEXT("motion"), Motion); O->SetNumberField(TEXT("viscosity"), Viscosity);
	O->SetNumberField(TEXT("swirl"), Swirl); O->SetNumberField(TEXT("attraction"), Attraction);
	O->SetNumberField(TEXT("tilt"), Tilt); O->SetNumberField(TEXT("glow"), Glow);
	O->SetNumberField(TEXT("trail"), Trail); O->SetNumberField(TEXT("shakeThreshold"), ShakeThreshold);
	O->SetStringField(TEXT("shake"), Shake);
	if (!Sensors.IsDefault())
	{
		auto G = MakeShared<FJsonObject>();
		G->SetNumberField(TEXT("sound"), Sensors.Sound); G->SetNumberField(TEXT("warmth"), Sensors.Warmth);
		G->SetNumberField(TEXT("light"), Sensors.Light); G->SetNumberField(TEXT("hand"), Sensors.Hand);
		G->SetNumberField(TEXT("magnet"), Sensors.Magnet); O->SetObjectField(TEXT("sensors"), G);
	}
	O->SetNumberField(TEXT("schema"), 3); O->SetStringField(TEXT("note"), Note);
	if(Story.Num()){
		auto Board=MakeShared<FJsonObject>();Board->SetNumberField(TEXT("hold"),StoryHold);Board->SetBoolField(TEXT("loop"),bStoryLoop);TArray<TSharedPtr<FJsonValue>> Frames;
		for(const auto& F:Story){auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("hold"),F.Hold);J->SetNumberField(TEXT("move"),F.Move);
			TArray<TSharedPtr<FJsonValue>> Set;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(F.Set),Set);J->SetArrayField(TEXT("set"),Set);Frames.Add(MakeShared<FJsonValueObject>(J));}
		Board->SetArrayField(TEXT("frames"),Frames);O->SetObjectField(TEXT("storyboard"),Board);
	}
	auto Cloud=MakeShared<FJsonObject>();Cloud->SetBoolField(TEXT("enabled"),true);Cloud->SetBoolField(TEXT("floor"),false);Cloud->SetNumberField(TEXT("count"),PointCount);
	Cloud->SetNumberField(TEXT("duration"),AssembleSeconds);Cloud->SetNumberField(TEXT("arc"),AssembleArc);Cloud->SetNumberField(TEXT("twist"),AssembleTwist);Cloud->SetNumberField(TEXT("stagger"),AssembleStagger);if(PointStyle!=TEXT("color"))Cloud->SetStringField(TEXT("style"),PointStyle);O->SetObjectField(TEXT("pointCloud"),Cloud);
	auto P = MakeShared<FJsonObject>();
	P->SetNumberField(TEXT("count"), Count); P->SetNumberField(TEXT("size"), ParticleSize);
	P->SetStringField(TEXT("behavior"), Behavior); P->SetStringField(TEXT("formation"), Formation);
	P->SetStringField(TEXT("shape"), ParticleShape); P->SetStringField(TEXT("colorMode"), ColorMode);
	P->SetArrayField(TEXT("center"), Vec(Center)); P->SetArrayField(TEXT("wind"), Vec(Wind));
	P->SetNumberField(TEXT("gravity"), Gravity); P->SetNumberField(TEXT("turbulence"), Turbulence);
	P->SetNumberField(TEXT("spread"), Spread); P->SetNumberField(TEXT("formationStrength"), FormationStrength);
	P->SetNumberField(TEXT("cohesion"), Cohesion); P->SetNumberField(TEXT("alignment"), Alignment);
	P->SetNumberField(TEXT("separation"), Separation); P->SetNumberField(TEXT("groups"), Groups);
	P->SetStringField(TEXT("formationNext"),FormationNext); P->SetNumberField(TEXT("formationPeriod"),FormationPeriod); P->SetNumberField(TEXT("travel"),FormationTravel);
	P->SetArrayField(TEXT("formationRotation"),Vec(FormationRotation)); if(FormationPoints.Num()) P->SetArrayField(TEXT("points"),JsonPoints(FormationPoints));
	O->SetObjectField(TEXT("particles"), P);
	auto World = MakeShared<FJsonObject>();
	World->SetStringField(TEXT("background"), Hex(Background)); World->SetStringField(TEXT("floor"), Hex(Floor));
	World->SetNumberField(TEXT("ambient"), Ambient); World->SetNumberField(TEXT("bloom"), Bloom); World->SetBoolField(TEXT("box"), bShowBox);
	World->SetStringField(TEXT("keyColor"),Hex(KeyColor)); World->SetStringField(TEXT("fillColor"),Hex(FillColor)); World->SetNumberField(TEXT("keyIntensity"),KeyIntensity); World->SetArrayField(TEXT("keyDirection"),Vec(KeyDirection));
	TArray<TSharedPtr<FJsonValue>> LightValues;
	for(const FLightBoxLight& L:Lights) { auto J=MakeShared<FJsonObject>(); J->SetArrayField(TEXT("position"),Vec(L.Position)); J->SetStringField(TEXT("color"),Hex(L.Color)); J->SetNumberField(TEXT("intensity"),L.Intensity); J->SetNumberField(TEXT("radius"),L.Radius); LightValues.Add(MakeShared<FJsonValueObject>(J)); }
	World->SetArrayField(TEXT("lights"),LightValues);
	O->SetObjectField(TEXT("world"), World);
	auto Camera = MakeShared<FJsonObject>();
	Camera->SetNumberField(TEXT("orbit"), CameraOrbit); Camera->SetNumberField(TEXT("yaw"), CameraYaw); Camera->SetNumberField(TEXT("pitch"), CameraPitch); Camera->SetNumberField(TEXT("zoom"), CameraZoom);
	Camera->SetBoolField(TEXT("autoFrame"),bAutoFrame); Camera->SetNumberField(TEXT("fill"),CameraFill); Camera->SetArrayField(TEXT("focus"),Vec(CameraFocus));
	O->SetObjectField(TEXT("camera"), Camera);
	TArray<TSharedPtr<FJsonValue>> Nodes;
	for (const FLightBoxObject& N : Objects) { Nodes.Add(MakeShared<FJsonValueObject>(ObjectJson(N))); }
	O->SetArrayField(TEXT("objects"), Nodes);
	FString Result;
	FJsonSerializer::Serialize(O, TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Result));
	return Result;
}

FString FLightBoxStyle::RenderContentJson() const
{
	TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ToJson()),O);
	O->RemoveField(TEXT("camera"));O->RemoveField(TEXT("title"));O->RemoveField(TEXT("note"));O->RemoveField(TEXT("sensors")); // sensitivity never rebuilds the sculpture
	FString Json;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&Json));return Json;
}

bool FLightBoxStyle::Parse(const FString& Json, FLightBoxStyle& Out, FString& Error, FString* Diagnostic)
{
	if (Diagnostic) { *Diagnostic = TEXT("Malformed scene JSON or missing/out-of-range required base field. Follow the complete schema exactly."); }
	if (Json.Len() > 65536) { Error = TEXT("返答が長すぎます。景色を分けてお願いしてください"); return false; }
	FString Body = Json.TrimStartAndEnd();
	// Some providers leak a chat-template terminator after a valid JSON object.
	// Strip only exact boundary tokens; arbitrary trailing prose still fails validation.
	for(int32 Pass=0;Pass<4;++Pass) {
		bool Removed=false;
		for(const TCHAR* Token:{TEXT("</|assistant|>"),TEXT("<|im_end|>"),TEXT("<|eot_id|>"),TEXT("</s>")})
			if(Body.RemoveFromEnd(Token,ESearchCase::CaseSensitive)) {Body=Body.TrimStartAndEnd();Removed=true;break;}
		if(!Removed)break;
	}
	if (Body.StartsWith(TEXT("```")) && Body.EndsWith(TEXT("```")))
	{
		int32 Newline;
		if (Body.FindChar(TEXT('\n'), Newline)) { Body = Body.Mid(Newline + 1, Body.Len() - Newline - 4).TrimStartAndEnd(); }
	}
	TSharedPtr<FJsonObject> O;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), O) || !O.IsValid())
	{
		Error = TEXT("景色を読み取れませんでした。もう一度お試しください"); return false;
	}
	FLightBoxStyle Candidate;
	if (!KnownFields(O, { TEXT("title"), TEXT("color"), TEXT("accent"), TEXT("motion"), TEXT("viscosity"), TEXT("swirl"), TEXT("attraction"), TEXT("tilt"), TEXT("glow"), TEXT("trail"), TEXT("shakeThreshold"), TEXT("shake"), TEXT("schema"), TEXT("note"), TEXT("particles"), TEXT("world"), TEXT("camera"), TEXT("objects"), TEXT("pointCloud"), TEXT("sensors"), TEXT("storyboard") }))
	{ Error = TEXT("まだ対応していない景色の操作が含まれています"); return false; }
	if (!O->TryGetStringField(TEXT("title"), Candidate.Title) || Candidate.Title.IsEmpty() || Candidate.Title.Len() > 24
		|| Candidate.Title.Contains(TEXT("\n")) || Candidate.Title.Contains(TEXT("\r")) || Candidate.Title.Contains(TEXT("\t"))
		|| !ReadColor(O, TEXT("color"), Candidate.Color) || !ReadColor(O, TEXT("accent"), Candidate.Accent))
	{
		Error = TEXT("景色の名前や色を読み取れませんでした"); return false;
	}
	auto RequiredNumber = [&O](const TCHAR* Key, float& Value, double Min, double Max)
	{
		double N;
		if (!O->TryGetNumberField(Key, N) || !FMath::IsFinite(N) || N < Min || N > Max) { return false; }
		Value = static_cast<float>(N); return true;
	};
	if (!RequiredNumber(TEXT("motion"), Candidate.Motion, 0.2, 2.0)
		|| !RequiredNumber(TEXT("viscosity"), Candidate.Viscosity, 0.0, 1.0)
		|| !RequiredNumber(TEXT("swirl"), Candidate.Swirl, -2.0, 2.0)
		|| !RequiredNumber(TEXT("attraction"), Candidate.Attraction, 0.0, 1.0)
		|| !RequiredNumber(TEXT("tilt"), Candidate.Tilt, 0.0, 2.0)
		|| !RequiredNumber(TEXT("glow"), Candidate.Glow, 0.3, 2.0)
		|| !RequiredNumber(TEXT("trail"), Candidate.Trail, 0.0, 1.0)
		|| !RequiredNumber(TEXT("shakeThreshold"), Candidate.ShakeThreshold, 1.3, 3.0)
		|| !O->TryGetStringField(TEXT("shake"), Candidate.Shake)
		|| !(Candidate.Shake == TEXT("scatter") || Candidate.Shake == TEXT("gather") || Candidate.Shake == TEXT("none")))
	{
		Error = TEXT("動き方を読み取れませんでした。別の言い方でお試しください"); return false;
	}
	if (O->HasField(TEXT("schema")))
	{
		double Version; if (!O->TryGetNumberField(TEXT("schema"), Version) || (Version != 2 && Version != 3)) { Error = TEXT("景色の形式に対応していません"); return false; }
	}
	if (O->HasField(TEXT("note")) && (!O->TryGetStringField(TEXT("note"), Candidate.Note) || Candidate.Note.Len() > 100 || Candidate.Note.Contains(TEXT("\n"))))
	{ Error = TEXT("景色の説明を読み取れませんでした"); return false; }
	if (O->HasField(TEXT("sensors")))
	{
		const TSharedPtr<FJsonObject>* G = nullptr;
		bool bValid = O->TryGetObjectField(TEXT("sensors"), G) && G && KnownFields(*G, { TEXT("sound"), TEXT("warmth"), TEXT("light"), TEXT("hand"), TEXT("magnet") });
		auto Gain = [&](const TCHAR* Key, float& Value)
		{
			if (!bValid || !(*G)->HasField(Key)) { return; }
			double N; bValid = (*G)->TryGetNumberField(Key, N) && FMath::IsFinite(N) && N >= 0.0 && N <= 2.0;
			if (bValid) { Value = static_cast<float>(N); }
		};
		Gain(TEXT("sound"), Candidate.Sensors.Sound); Gain(TEXT("warmth"), Candidate.Sensors.Warmth); Gain(TEXT("light"), Candidate.Sensors.Light);
		Gain(TEXT("hand"), Candidate.Sensors.Hand); Gain(TEXT("magnet"), Candidate.Sensors.Magnet);
		if (!bValid) { Error = TEXT("センサーの反応の強さを読み取れませんでした"); return false; }
		Candidate.bSensorsSpecified = true;
	}
	FString Context;
	auto Extended = [&]() -> bool
	{
		const FObject* P;
		if(O->HasField(TEXT("pointCloud"))) {
			Context=TEXT("pointCloud: enabled/floor booleans,count integer256..1000000,duration1..15,arc0..200,twist-4..4,stagger0...75,style color/crystal");
			if(!O->TryGetObjectField(TEXT("pointCloud"),P)||!KnownFields(*P,{TEXT("enabled"),TEXT("floor"),TEXT("count"),TEXT("duration"),TEXT("arc"),TEXT("twist"),TEXT("stagger"),TEXT("style")})
				||!Choice(*P,TEXT("style"),Candidate.PointStyle,{TEXT("color"),TEXT("crystal")})
				||!(*P)->TryGetBoolField(TEXT("enabled"),Candidate.bPointCloud)
				||((*P)->HasField(TEXT("floor"))&&!(*P)->TryGetBoolField(TEXT("floor"),Candidate.bPointFloor))
				||!Integer(*P,TEXT("count"),Candidate.PointCount,256,1000000)||!Number(*P,TEXT("duration"),Candidate.AssembleSeconds,1,15)
				||!Number(*P,TEXT("arc"),Candidate.AssembleArc,0,200)||!Number(*P,TEXT("twist"),Candidate.AssembleTwist,-4,4)||!Number(*P,TEXT("stagger"),Candidate.AssembleStagger,0,.75))return false;
		}
		if (O->HasField(TEXT("particles")))
		{
			Context = TEXT("particles: invalid field/enum/range. count INTEGER 0..1024,size .25..5,center x/y -260..260,z45..320 (even count0),wind +-2,spread20..250,formationStrength0..4,groups INTEGER1..6,travel0..80,formationPeriod2..60,rotation+-180");
			if (!O->TryGetObjectField(TEXT("particles"), P) || !KnownFields(*P, { TEXT("count"), TEXT("size"), TEXT("behavior"), TEXT("formation"), TEXT("shape"), TEXT("colorMode"), TEXT("center"), TEXT("wind"), TEXT("gravity"), TEXT("turbulence"), TEXT("spread"), TEXT("formationStrength"), TEXT("cohesion"), TEXT("alignment"), TEXT("separation"), TEXT("groups"), TEXT("points"), TEXT("formationNext"), TEXT("formationPeriod"), TEXT("formationRotation"), TEXT("travel") })
				|| !Integer(*P, TEXT("count"), Candidate.Count, 0, 1024) || !Number(*P, TEXT("size"), Candidate.ParticleSize, 0.25, 5)
				|| !Choice(*P, TEXT("behavior"), Candidate.Behavior, { TEXT("drift"), TEXT("flock"), TEXT("orbit"), TEXT("rain"), TEXT("snow"), TEXT("fire"), TEXT("fountain"), TEXT("fireworks"), TEXT("form") })
				|| !Choice(*P, TEXT("formation"), Candidate.Formation, { TEXT("volume"), TEXT("sphere"), TEXT("ring"), TEXT("heart"), TEXT("star"), TEXT("helix"), TEXT("galaxy"), TEXT("grid"), TEXT("wave"), TEXT("torus"), TEXT("cube"), TEXT("fish"), TEXT("bird"), TEXT("butterfly"), TEXT("flower"), TEXT("custom") })
				|| !Choice(*P, TEXT("shape"), Candidate.ParticleShape, { TEXT("sphere"), TEXT("fish") })
				|| !Choice(*P, TEXT("colorMode"), Candidate.ColorMode, { TEXT("solid"), TEXT("alternate"), TEXT("rainbow"), TEXT("height"), TEXT("speed"),TEXT("leader") })
				|| !Vector(*P, TEXT("center"), Candidate.Center, -260, 320) || Candidate.Center.Z < 45 || FMath::Abs(Candidate.Center.X) > 260 || FMath::Abs(Candidate.Center.Y) > 260
				|| !Vector(*P, TEXT("wind"), Candidate.Wind, -2, 2) || !Number(*P, TEXT("gravity"), Candidate.Gravity, -2, 2)
				|| !Number(*P, TEXT("turbulence"), Candidate.Turbulence, 0, 2) || !Number(*P, TEXT("spread"), Candidate.Spread, 20, 250)
				|| !Number(*P, TEXT("formationStrength"), Candidate.FormationStrength, 0, 4)
				|| !Number(*P, TEXT("cohesion"), Candidate.Cohesion, 0, 2) || !Number(*P, TEXT("alignment"), Candidate.Alignment, 0, 2)
				|| !Number(*P, TEXT("separation"), Candidate.Separation, 0, 2) || !Integer(*P, TEXT("groups"), Candidate.Groups, 1, 6)
				|| !Points(*P,TEXT("points"),Candidate.FormationPoints,2,128,1)
				|| !Choice(*P,TEXT("formationNext"),Candidate.FormationNext,{TEXT("none"),TEXT("volume"),TEXT("sphere"),TEXT("ring"),TEXT("heart"),TEXT("star"),TEXT("helix"),TEXT("galaxy"),TEXT("grid"),TEXT("wave"),TEXT("torus"),TEXT("cube"),TEXT("fish"),TEXT("bird"),TEXT("butterfly"),TEXT("flower"),TEXT("custom")})
				|| !Number(*P,TEXT("formationPeriod"),Candidate.FormationPeriod,2,60) || !Number(*P,TEXT("travel"),Candidate.FormationTravel,0,80)
				|| !Vector(*P,TEXT("formationRotation"),Candidate.FormationRotation,-180,180)
				|| ((Candidate.Formation==TEXT("custom")||Candidate.FormationNext==TEXT("custom"))&&Candidate.FormationPoints.Num()<2)) { return false; }
		}
		if (O->HasField(TEXT("world")))
		{
			Context = TEXT("world: background/floor must be #RRGGBB; ambient 0.1..3, bloom 0..2, box boolean");
			if (!O->TryGetObjectField(TEXT("world"), P) || !KnownFields(*P, { TEXT("background"), TEXT("floor"), TEXT("ambient"), TEXT("bloom"), TEXT("box"),TEXT("keyColor"),TEXT("fillColor"),TEXT("keyIntensity"),TEXT("keyDirection"),TEXT("lights") })
				|| !OptionalColor(*P, TEXT("background"), Candidate.Background) || !OptionalColor(*P, TEXT("floor"), Candidate.Floor)
				|| !Number(*P, TEXT("ambient"), Candidate.Ambient, 0.1, 3) || !Number(*P, TEXT("bloom"), Candidate.Bloom, 0, 2)
				|| !OptionalColor(*P,TEXT("keyColor"),Candidate.KeyColor) || !OptionalColor(*P,TEXT("fillColor"),Candidate.FillColor)
				|| !Number(*P,TEXT("keyIntensity"),Candidate.KeyIntensity,.1,12) || !Vector(*P,TEXT("keyDirection"),Candidate.KeyDirection,-180,180)
				|| ((*P)->HasField(TEXT("box")) && !(*P)->TryGetBoolField(TEXT("box"), Candidate.bShowBox))) { return false; }
			if((*P)->HasField(TEXT("lights"))) {
				const TArray<TSharedPtr<FJsonValue>>* A; if(!(*P)->TryGetArrayField(TEXT("lights"),A)||A->Num()>4) return false;
				for(const auto& V:*A) { if(V->Type!=EJson::Object) return false; auto J=V->AsObject(); FLightBoxLight L;
					if(!KnownFields(J,{TEXT("position"),TEXT("color"),TEXT("intensity"),TEXT("radius")})||!Vector(J,TEXT("position"),L.Position,-600,600)||!ReadColor(J,TEXT("color"),L.Color)||!Number(J,TEXT("intensity"),L.Intensity,0,20000)||!Number(J,TEXT("radius"),L.Radius,50,1200)) return false;
					Candidate.Lights.Add(L);
				}
			}
		}
		if (O->HasField(TEXT("camera")))
		{
			Context = TEXT("camera: yaw -360..360, pitch 10..85, orbit -30..30 deg/sec, zoom 0.65..2.2,fill .4...88");
			if (!O->TryGetObjectField(TEXT("camera"), P) || !KnownFields(*P, { TEXT("yaw"), TEXT("pitch"), TEXT("zoom"),TEXT("autoFrame"),TEXT("fill"),TEXT("focus"),TEXT("orbit") })
				|| !Number(*P, TEXT("yaw"), Candidate.CameraYaw, -360, 360) || !Number(*P, TEXT("pitch"), Candidate.CameraPitch, 10, 85) || !Number(*P,TEXT("orbit"),Candidate.CameraOrbit,-30,30)
				|| !Number(*P, TEXT("zoom"), Candidate.CameraZoom, 0.65, 2.2)
				|| !Number(*P,TEXT("fill"),Candidate.CameraFill,.4,.88) || !Vector(*P,TEXT("focus"),Candidate.CameraFocus,-400,450)
				|| ((*P)->HasField(TEXT("autoFrame"))&&!(*P)->TryGetBoolField(TEXT("autoFrame"),Candidate.bAutoFrame))) { return false; }
		}
		if (O->HasField(TEXT("objects")))
		{
			Context = TEXT("objects: at most 32 groups, unique ids, at most 256 total copies");
			const TArray<TSharedPtr<FJsonValue>>* Nodes;
			if (!O->TryGetArrayField(TEXT("objects"), Nodes) || Nodes->Num() > 48) { return false; }
			int32 Copies = 0; TSet<FString> Ids;
			for (const auto& V : *Nodes)
			{
				if (V->Type != EJson::Object) { return false; }
				const FObject N = V->AsObject(); FLightBoxObject Obj;
				FString Id; N->TryGetStringField(TEXT("id"), Id);
				Context = TEXT("object ") + Id + TEXT(": check allowed shape/animation/layout/reaction, color, required position/size, ranges and unique id. For custom shapes use polygon outline.");
				if (!KnownFields(N, { TEXT("id"), TEXT("shape"), TEXT("color"), TEXT("position"), TEXT("size"), TEXT("rotation"), TEXT("emission"), TEXT("wobble"), TEXT("copies"), TEXT("layout"), TEXT("spacing"), TEXT("animation"), TEXT("speed"), TEXT("amplitude"), TEXT("reaction"), TEXT("path"), TEXT("outline"),TEXT("parent"),TEXT("pivot"),TEXT("roughness"),TEXT("metallic"),TEXT("radius"),TEXT("curve"),TEXT("motions"),TEXT("focus"),TEXT("closed"),TEXT("anchor"),TEXT("parentAnchor"),TEXT("startObject"),TEXT("endObject"),TEXT("startAnchor"),TEXT("endAnchor"),TEXT("startOffset"),TEXT("endOffset"),TEXT("profile"),TEXT("sections"),TEXT("attached"),TEXT("pattern"),TEXT("patternColor"),TEXT("patternScale"),TEXT("belly"),TEXT("bellyColor"),TEXT("detail"),TEXT("detailLength"),TEXT("model"),TEXT("parts") })
					|| !N->TryGetStringField(TEXT("id"), Obj.Id) || Obj.Id.IsEmpty() || Obj.Id.Len() > 32 || Ids.Contains(Obj.Id)
					|| !N->HasField(TEXT("shape")) || !Choice(N, TEXT("shape"), Obj.Shape, { TEXT("sphere"), TEXT("box"), TEXT("cylinder"), TEXT("cone"), TEXT("torus"), TEXT("disk"), TEXT("star"), TEXT("heart"), TEXT("leaf"), TEXT("fish"), TEXT("flower"), TEXT("polygon"),TEXT("group"),TEXT("bird"),TEXT("bowl"),TEXT("tube"),TEXT("ribbon"),TEXT("water"),TEXT("rock"),TEXT("tree"),TEXT("elephant"),TEXT("giraffe"),TEXT("lion"),TEXT("butterfly"),TEXT("whale"),TEXT("petal"),TEXT("roof"),TEXT("gear"),TEXT("blossom"),TEXT("lathe"),TEXT("loft"),TEXT("model"),TEXT("sdf") })
					|| !ReadColor(N, TEXT("color"), Obj.Color) || !Vector(N, TEXT("position"), Obj.Position, -400, 450, true)
					|| !Vector(N, TEXT("size"), Obj.Size, 1, 600, true) || !Vector(N, TEXT("rotation"), Obj.Rotation, -360, 360)
					|| !Number(N, TEXT("emission"), Obj.Emission, 0, 8) || !Number(N, TEXT("wobble"), Obj.Wobble, 0, 0.35)
					|| !Integer(N, TEXT("copies"), Obj.Copies, 1, 32) || !Number(N, TEXT("spacing"), Obj.Spacing, 0, 240)
					|| !Choice(N, TEXT("layout"), Obj.Layout, { TEXT("ring"), TEXT("line"), TEXT("grid"), TEXT("spiral"), TEXT("scatter") })
					|| !Choice(N, TEXT("animation"), Obj.Animation, { TEXT("none"), TEXT("spin"), TEXT("bob"), TEXT("orbit"), TEXT("pulse"), TEXT("wave"), TEXT("sway"), TEXT("path"),TEXT("fly"),TEXT("swim"),TEXT("flow"),TEXT("flutter"),TEXT("stream"),TEXT("spinLocal"),TEXT("bloom") })
					|| !Number(N, TEXT("speed"), Obj.Speed, -3, 3) || !Number(N, TEXT("amplitude"), Obj.Amplitude, 0, 150)
					|| !Choice(N, TEXT("reaction"), Obj.Reaction, { TEXT("none"), TEXT("tilt"), TEXT("shake"), TEXT("both") })) { return false; }
				if(!Vector(N,TEXT("pivot"),Obj.Pivot,-600,600)||!Number(N,TEXT("roughness"),Obj.Roughness,.08,1)||!Number(N,TEXT("metallic"),Obj.Metallic,0,1)||!Number(N,TEXT("radius"),Obj.Radius,.3,40)
					|| (N->HasField(TEXT("parent"))&&(!N->TryGetStringField(TEXT("parent"),Obj.Parent)||Obj.Parent.Len()>32))
					|| (N->HasField(TEXT("closed"))&&!N->TryGetBoolField(TEXT("closed"),Obj.bCurveClosed))
                    || (N->HasField(TEXT("attached"))&&!N->TryGetBoolField(TEXT("attached"),Obj.bAttached))
					|| (N->HasField(TEXT("focus"))&&!N->TryGetBoolField(TEXT("focus"),Obj.bFocus))||!Points(N,TEXT("curve"),Obj.Curve,2,24)) return false;
				if((Obj.Shape==TEXT("tube")||Obj.Shape==TEXT("ribbon"))&&Obj.Curve.Num()<2) return false;
				if(N->HasField(TEXT("profile"))) {
                    const TArray<TSharedPtr<FJsonValue>>* A;if(!N->TryGetArrayField(TEXT("profile"),A)||A->Num()<3||A->Num()>32)return false;
                    double Last=-1;
                    for(const auto& Value:*A){const TArray<TSharedPtr<FJsonValue>>* Pair;double Z,R;
                        if(!Value->TryGetArray(Pair)||Pair->Num()!=2||!(*Pair)[0]->TryGetNumber(Z)||!(*Pair)[1]->TryGetNumber(R)||!FMath::IsFinite(Z)||!FMath::IsFinite(R)||Z<-.5||Z>.5||Z<=Last||R<0||R>.5)return false;
                        Last=Z;Obj.Profile.Add(FVector2D(Z,R));}
                    if(Obj.Profile[0].Y>0||Obj.Profile.Last().Y>0)return false;
                }
                if(Obj.Shape==TEXT("lathe")&&Obj.Profile.Num()<3)return false;
                if(!Points(N,TEXT("sections"),Obj.Sections,3,24,1)||!Choice(N,TEXT("pattern"),Obj.Pattern,{TEXT("none"),TEXT("spots"),TEXT("stripes"),TEXT("patches")})
                    ||(N->HasField(TEXT("patternColor"))&&!ReadColor(N,TEXT("patternColor"),Obj.PatternColor))||(N->HasField(TEXT("bellyColor"))&&!ReadColor(N,TEXT("bellyColor"),Obj.BellyColor))||!Number(N,TEXT("patternScale"),Obj.PatternScale,2,32)
                    ||(N->HasField(TEXT("belly"))&&!N->TryGetBoolField(TEXT("belly"),Obj.bBelly))
                    ||!Choice(N,TEXT("detail"),Obj.Detail,{TEXT("none"),TEXT("fur"),TEXT("foliage"),TEXT("bark"),TEXT("feather"),TEXT("grass")})||!Number(N,TEXT("detailLength"),Obj.DetailLength,.5,40)
                    ||(N->HasField(TEXT("model"))&&!N->TryGetStringField(TEXT("model"),Obj.Model)))return false;
                if(Obj.Shape==TEXT("model")&&!LightBoxModels::IsKnown(Obj.Model))return false;
                if(!ReadSdfParts(N,Obj.SdfParts)||(Obj.Shape==TEXT("sdf")&&Obj.SdfParts.IsEmpty()))return false;
                for(int32 J=0;J<Obj.Sections.Num();++J){const auto Section=Obj.Sections[J];if(FMath::Abs(Section.X)>.5||Section.Y<0||Section.Y>.5||Section.Z<0||Section.Z>.5||(J>0&&Section.X-Obj.Sections[J-1].X<.005))return false;}
                if(Obj.Shape==TEXT("loft")&&(Obj.Sections.Num()<3||Obj.Sections[0].Y>0||Obj.Sections[0].Z>0||Obj.Sections.Last().Y>0||Obj.Sections.Last().Z>0))return false;

				if(N->HasField(TEXT("motions"))) {
					const TArray<TSharedPtr<FJsonValue>>* A; if(!N->TryGetArrayField(TEXT("motions"),A)||A->Num()>6) return false;
					for(const auto& V2:*A) {
						if(V2->Type!=EJson::Object) return false; auto J=V2->AsObject(); FLightBoxMotion M;
						if(!KnownFields(J,{TEXT("type"),TEXT("wave"),TEXT("axis"),TEXT("amplitude"),TEXT("period"),TEXT("phase"),TEXT("path"),TEXT("closed"),TEXT("orient"),TEXT("space")})
							||!Choice(J,TEXT("type"),M.Type,{TEXT("translate"),TEXT("rotate"),TEXT("scale"),TEXT("orbit"),TEXT("path"),TEXT("flow"),TEXT("bend"),TEXT("ripple"),TEXT("stream")})
							||!Choice(J,TEXT("space"),M.Space,{TEXT("parent"),TEXT("local")})
							||!Choice(J,TEXT("wave"),M.Wave,{TEXT("sine"),TEXT("triangle"),TEXT("saw")})||!Vector(J,TEXT("axis"),M.Axis,-1,1)||(M.Axis.IsNearlyZero()&&M.Type!=TEXT("path")&&M.Type!=TEXT("flow")&&M.Type!=TEXT("stream"))
							||!Number(J,TEXT("amplitude"),M.Amplitude,-360,360)||!Number(J,TEXT("period"),M.Period,.25,60)||!Number(J,TEXT("phase"),M.Phase,-16,16)
							||!Points(J,TEXT("path"),M.Path,2,24)|| (M.Type==TEXT("path")&&M.Path.Num()<2)
							|| (J->HasField(TEXT("closed"))&&!J->TryGetBoolField(TEXT("closed"),M.bClosed)) || (J->HasField(TEXT("orient"))&&!J->TryGetBoolField(TEXT("orient"),M.bOrient))) return false;
						if((M.Type==TEXT("flow")||M.Type==TEXT("stream"))&&Obj.Shape!=TEXT("tube")&&Obj.Shape!=TEXT("ribbon")) return false;
						Obj.Motions.Add(M);
					}
				}
				if((Obj.Animation==TEXT("flow")||Obj.Animation==TEXT("stream"))&&Obj.Shape!=TEXT("tube")&&Obj.Shape!=TEXT("ribbon")) return false;
				auto ReadAnchor=[&](const TCHAR* Key,FString& Value){return Choice(N,Key,Value,{TEXT("center"),TEXT("x-"),TEXT("x+"),TEXT("y-"),TEXT("y+"),TEXT("z-"),TEXT("z+")});};
				if(!ReadAnchor(TEXT("anchor"),Obj.Anchor)||!ReadAnchor(TEXT("parentAnchor"),Obj.ParentAnchor)||!ReadAnchor(TEXT("startAnchor"),Obj.StartAnchor)||!ReadAnchor(TEXT("endAnchor"),Obj.EndAnchor)
					||!Vector(N,TEXT("startOffset"),Obj.StartOffset,-600,600)||!Vector(N,TEXT("endOffset"),Obj.EndOffset,-600,600)) return false;
				for(auto Pair:{TPair<const TCHAR*,FString*>(TEXT("startObject"),&Obj.StartObject),TPair<const TCHAR*,FString*>(TEXT("endObject"),&Obj.EndObject)})
					if(N->HasField(Pair.Key)&&(!N->TryGetStringField(Pair.Key,*Pair.Value)||Pair.Value->Len()>32)) return false;
				if((!Obj.StartObject.IsEmpty()||!Obj.EndObject.IsEmpty())&&Obj.Shape!=TEXT("tube")&&Obj.Shape!=TEXT("ribbon")) return false;
				Ids.Add(Obj.Id); Copies += Obj.Copies; if (Copies > 256) { return false; }
				if (N->HasField(TEXT("path")))
				{
					const TArray<TSharedPtr<FJsonValue>>* Points;
					if (!N->TryGetArrayField(TEXT("path"), Points) || Points->Num() < 2 || Points->Num() > 16) { return false; }
					for (const auto& Point : *Points)
					{
						auto Temp = MakeShared<FJsonObject>(); Temp->SetField(TEXT("p"), Point); FVector Position;
						if (!Vector(Temp, TEXT("p"), Position, -400, 450, true)) { return false; }
						Obj.Path.Add(Position);
					}
				}
				if (Obj.Animation == TEXT("path") && Obj.Path.Num() < 2 && !Obj.Motions.ContainsByPredicate([](const auto& M){return M.Type==TEXT("path");})) { return false; }
				if (N->HasField(TEXT("outline")))
				{
					const TArray<TSharedPtr<FJsonValue>>* Points;
					if (!N->TryGetArrayField(TEXT("outline"), Points) || Points->Num() < 3 || Points->Num() > 32) { return false; }
					for (const auto& Point : *Points)
					{
						const TArray<TSharedPtr<FJsonValue>>* A; double X, Y;
						if (!Point->TryGetArray(A) || A->Num() != 2 || !(*A)[0]->TryGetNumber(X) || !(*A)[1]->TryGetNumber(Y)
						|| !FMath::IsFinite(X) || !FMath::IsFinite(Y) || FMath::Abs(X) > 0.5 || FMath::Abs(Y) > 0.5) { return false; }
						Obj.Outline.Add(FVector2D(X, Y));
					}
				}
				if (Obj.Shape == TEXT("polygon") && !FLightBoxMesh::ValidOutline(Obj.Outline)) { return false; }
				Candidate.Objects.Add(MoveTemp(Obj));
			}
		}
		Context=TEXT("Invalid parent reference, cycle/depth over 8, or geometry budget exceeded. Use group objects as parents; reduce copies/detail.");
		int32 Vertices=0,DynamicVertices=0;
		for(const auto& Obj:Candidate.Objects) {
			if((!Obj.StartObject.IsEmpty()&&!Candidate.Objects.ContainsByPredicate([&](const auto& X){return X.Id==Obj.StartObject;}))||(!Obj.EndObject.IsEmpty()&&!Candidate.Objects.ContainsByPredicate([&](const auto& X){return X.Id==Obj.EndObject;}))) return false;
			TSet<FString> Chain; const FLightBoxObject* Node=&Obj;
			while(Node) { if(Chain.Contains(Node->Id)||Chain.Num()>=8) return false; Chain.Add(Node->Id); if(Node->Parent.IsEmpty()) break;
				const FString ParentId=Node->Parent; Node=Candidate.Objects.FindByPredicate([&](const auto& X){return X.Id==ParentId;}); if(!Node) return false; }
			const int32 Cost=Obj.Copies*(Obj.Shape==TEXT("group")?0:Obj.Shape==TEXT("model")?8:Obj.Shape==TEXT("sdf")?LightBoxSdf::MaxVertices:Obj.Shape==TEXT("tree")?8600:Obj.Shape==TEXT("bird")?2200:Obj.Shape==TEXT("blossom")?6500:Obj.Shape==TEXT("butterfly")?5000:Obj.Shape==TEXT("whale")?3000:Obj.Shape==TEXT("elephant")||Obj.Shape==TEXT("giraffe")||Obj.Shape==TEXT("lion")?6500:1200);
			Vertices+=Cost; if(Obj.IsDeforming()) DynamicVertices+=Cost;
		} if(Vertices>250000||DynamicVertices>60000) return false;
		return true;
	};
	if (!Extended()) { if (Diagnostic) { *Diagnostic = Context; } Error = TEXT("景色の形や数を読み取れませんでした。少し分けてお願いしてください"); return false; }
	if(!Candidate.bPointCloud)Candidate.PointCount=FMath::Max(DefaultPointCount,Candidate.PointCount);
	Candidate.bPointCloud=true;Candidate.bPointFloor=false;
	if(O->HasField(TEXT("storyboard"))){
		// Keyframes are overrides of the base scene; every resolved pose must itself be a valid scene.
		const FObject* B=nullptr;const TArray<TSharedPtr<FJsonValue>>* Frames=nullptr;bool Ok=O->TryGetObjectField(TEXT("storyboard"),B)&&KnownFields(*B,{TEXT("hold"),TEXT("loop"),TEXT("frames")})
			&&Number(*B,TEXT("hold"),Candidate.StoryHold,0,20)&&(!(*B)->HasField(TEXT("loop"))||(*B)->TryGetBoolField(TEXT("loop"),Candidate.bStoryLoop))
			&&(*B)->TryGetArrayField(TEXT("frames"),Frames)&&Frames->Num()>=1&&Frames->Num()<=8;
		for(int32 I=0;Ok&&I<Frames->Num();++I){
			const auto& V=(*Frames)[I];FLightBoxStoryFrame F;const TArray<TSharedPtr<FJsonValue>>* Set=nullptr;
			Ok=V->Type==EJson::Object&&KnownFields(V->AsObject(),{TEXT("hold"),TEXT("move"),TEXT("set")})&&Number(V->AsObject(),TEXT("hold"),F.Hold,0,20)&&Number(V->AsObject(),TEXT("move"),F.Move,.3,10)
				&&V->AsObject()->TryGetArrayField(TEXT("set"),Set)&&Set->Num()>=1&&Set->Num()<=16;
			for(int32 K=0;Ok&&K<Set->Num();++K){FString Id;Ok=(*Set)[K]->Type==EJson::Object&&(*Set)[K]->AsObject()->TryGetStringField(TEXT("id"),Id)&&Candidate.Objects.ContainsByPredicate([&](const auto& X){return X.Id==Id;});}
			if(Ok){FJsonSerializer::Serialize(*Set,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&F.Set));Candidate.Story.Add(MoveTemp(F));}
		}
		FString Why;
		for(int32 I=1;Ok&&I<=Candidate.Story.Num();++I){FLightBoxStyle Resolved;if(!Candidate.Pose(I,Resolved,&Why)){Ok=false;Why=FString::Printf(TEXT("storyboard frame %d: "),I)+Why;}}
		if(!Ok){if(Diagnostic)*Diagnostic=Why.IsEmpty()?TEXT("storyboard: {hold0..20,loop,frames:[1..8 {hold0..20,move.3..10,set:[1..16 {id of an EXISTING object,...fields to override,patch:[{i:part index,...sdf part fields}]}]}]}"):Why;
			Error=TEXT("景色の動きの流れを読み取れませんでした");return false;}
	}
	Out = Candidate; // Atomic: a malformed response never partly changes the scene.
	Error.Reset();
	return true;
}

bool FLightBoxStyle::ParseCompletion(const FString& Json, FLightBoxStyle& Out, FString& Error, FString* Diagnostic)
{
	if (Diagnostic) { *Diagnostic = TEXT("Return a complete scene JSON object; the previous reply was malformed or truncated."); }
	TSharedPtr<FJsonObject> O;
	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (Json.Len() > 131072 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), O) || !O.IsValid()
		|| !O->TryGetArrayField(TEXT("choices"), Choices) || Choices->Num() != 1 || !(*Choices)[0].IsValid())
	{
		Error = TEXT("演出の返答を読み取れませんでした"); return false;
	}
	const TSharedPtr<FJsonObject>* Message = nullptr;
	const TSharedPtr<FJsonObject> Choice = (*Choices)[0]->Type == EJson::Object ? (*Choices)[0]->AsObject() : nullptr;
	FString Content, Finish;
	if (!Choice.IsValid() || !Choice->TryGetStringField(TEXT("finish_reason"), Finish) || Finish != TEXT("stop")
		|| !Choice->TryGetObjectField(TEXT("message"), Message) || !(*Message)->TryGetStringField(TEXT("content"), Content))
	{
		Error = TEXT("演出が完成しませんでした。もう一度お試しください"); return false;
	}
    TSharedPtr<FJsonObject> Scene;
    const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
    if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Content), Scene) && Scene.IsValid()
        && Scene->TryGetArrayField(TEXT("objects"), Nodes))
    {
        bool Changed = false;
        for (const auto& Node : *Nodes)
        {
            if (!Node.IsValid() || Node->Type != EJson::Object) { continue; }
            auto Object = Node->AsObject(); FString Shape;
            // A non-uniformly scaled sphere is exactly an ellipsoid in this renderer.
            if (Object->TryGetStringField(TEXT("shape"), Shape) && Shape == TEXT("ellipsoid"))
            { Object->SetStringField(TEXT("shape"), TEXT("sphere")); Changed = true; }
            const TSharedPtr<FJsonObject>* Story = nullptr;
            // A single object's storyboard has only one possible owner. Never merge ambiguous tracks
            // or replace an explicit root storyboard; unknown fields/ranges remain strict errors.
            if (Nodes->Num() == 1 && !Scene->HasField(TEXT("storyboard")) && Object->TryGetObjectField(TEXT("storyboard"), Story))
            { Scene->SetObjectField(TEXT("storyboard"), *Story); Object->RemoveField(TEXT("storyboard")); Changed = true; }
        }
        if (Changed) { Content.Reset(); FJsonSerializer::Serialize(Scene.ToSharedRef(), TJsonWriterFactory<>::Create(&Content)); }
    }
    return Parse(Content, Out, Error, Diagnostic);
}

FString FLightBoxStyle::ObjectMotionKey(const FLightBoxObject& Object)
{
    auto O=ObjectJson(Object);
    for(const TCHAR* Key:{TEXT("color"),TEXT("emission"),TEXT("roughness"),TEXT("metallic"),TEXT("focus"),TEXT("attached"),TEXT("pattern"),TEXT("patternColor"),TEXT("patternScale"),TEXT("belly"),TEXT("bellyColor")})O->RemoveField(Key);
    FString Json;FJsonSerializer::Serialize(O,TJsonWriterFactory<>::Create(&Json));return Json;
}

FString FLightBoxStyle::CameraContentJson() const
{
    TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ToJson()),O);
    FString Json;FJsonSerializer::Serialize(O->GetObjectField(TEXT("camera")).ToSharedRef(),TJsonWriterFactory<>::Create(&Json));return Json;
}

namespace
{
    FString ModelCatalog()
    {
        FString Out;for(const auto& E:LightBoxModels::Catalog())Out+=FString::Printf(TEXT("%s%s (%s)"),Out.IsEmpty()?TEXT(""):TEXT("; "),E.Id,E.Description);
        return Out;
    }
}

FString FLightBoxStyle::Instruction(const FLightBoxStyle& Current,int32 Budget)
{
	return FString(TEXT("You compose polished animated miniature exhibits from Japanese requests. Return ONE compact complete JSON, no prose/markdown/tools/code. "
		"EVERYTHING IS PARTICLES. A named concrete subject, even when the user says particles gather into its shape, means a complete FILLED THREE-DIMENSIONAL point sculpture: use objects target surfaces (lathe, tube, sphere, polygon, etc) and particles.count:0. objects are mathematical particle-placement recipes, NOT rendered 3D models; DO use them. pointCloud.count controls sculpture density. Use custom point contours ONLY for an explicit outline/line drawing; never substitute a flat contour for a complete tangible subject. particles.count is only for independent freely moving lights/weather/schools, NOT the sculpture itself. "
		"Distinguish a NEW SUBJECT from an EDIT. Reset irrelevant independent lights for a new subject, count:0 unless separately requested fireflies, weather, free stars or schools. Do not inherit decorative particles from the previous scene. "
		"For edits (only/keep/as-is/add/change-speed), preserve every unmentioned object, motion and light exactly. Keep ids stable; make the smallest requested change. "
        "PARENT COORDINATES ARE LOCAL: for body position[0,0,100], a fin at world[20,-30,90] must use parent:body,position:[20,-30,-10], NOT [20,-30,90]. Never add the body's world height twice. Set attached:true for anatomical parts, eyes, fins, ears, limbs, stems or panels that must touch their parent; detached required connections are rejected. Prefer a group root at the subject's world position, with all part positions local to that root. Parent group does not itself have a physical surface; ensure its sibling parts visibly meet. Use a low oblique camera with a large subject, not a distant tiny body inside a huge decorative tank. "
		"When the requested SUBJECT is made of light (light ring, neon, glowing shapes, 光輪, 光の輪), make that subject visibly emissive (emission 1.2..3), not merely a metallic object lit from outside. Keep ordinary objects non-emissive. "
		"Requested motion must be VISIBLE: a uniform ring/cylinder spinning around its symmetry axis appears stationary; use a tilted rotation axis or a small contrasting rim detail. A uniform sphere needs a visible surface detail or an orbit to show movement. "
		"QUALITY: Before writing JSON, silently identify the subject's 4-6 defining anatomical/structural features, proportions, silhouette and markings. Spend geometry and points on these, not unrelated decorations. Preserve stable part IDs on edits. A real place name is a semantic interpretation from your knowledge, NOT a verified replica; note this briefly. Never claim to have searched photos or accurately reproduced a real facility. Do not make a generic whale into a whale shark by changing its colour. Broad flattened head, tapering body, correctly attached pectoral/dorsal fins, vertical tail, eyes/mouth and ordered pale spots define a whale shark. A giant panda is a heavy bear: pear-shaped barrel body about 1.6x its height in length, head only about a third of body length with a broad skull and protruding muzzle, small rounded black ears, drooping teardrop black eye patches, black legs continuous with a black shoulder saddle, big plantigrade paws; do not omit the face or hide limbs behind unrelated scenery. These are examples of feature-based design, not fixed presets. REALISM TARGET: the result must read like a museum-grade naturalistic sculpture or a 3D laser-engraved crystal figure, NOT a chibi, mascot, toy, plush, cartoon, emoji or low-poly model. Use true adult proportions (never an oversized head or stubby generic limbs), real silhouette landmarks (shoulder hump, haunch, neck, jaw, brow, muzzle, knees/elbows, paws/hooves/claws, tail root) and asymmetric natural pose. A hero animal or plant normally needs 14..30 parts: loft torso with 10..24 sections, a head LOFT along X whose sections model skull, brow, cheeks and protruding muzzle (not a sphere), ears, eyes/nostrils, and each limb as ONE lathe (vertical local Z; profile widest at shoulder/hip, narrowing through knee/wrist, slight flare at the foot) or loft, with a separate flattened paw/hoof. NEVER stack separate spheres as limb segments and never leave pinched necks or gaps between body parts. Overlapping solid parts (sphere/box/cylinder/loft/lathe) are MERGED: surfaces buried inside another part are not drawn, so overlap limbs 20..35% into the torso, head into neck, muzzle into head, to get one continuous skin. Surface micro-structure is REQUIRED for organic subjects: object detail:fur/feather/foliage/bark/grass with detailLength cm (fur 1.5..6 short coat, 6..12 shaggy; feather 1..4; foliage 4..14; bark 1..4; grass 3..15). Detail displaces particles into real strands/leaf clumps/ridges; it is applied to parts that are not animated, so keep a showcase subject still unless motion was requested. Do not put fur on eyes, nose, claws or hard objects. "
        "For unfamiliar species/objects, construct the shape with loft/lathe/curves instead of stretching a mismatched built-in animal. loft: sections:[[x,halfWidth,halfHeight],...] 3..24 strictly ascending x within[-.5,.5], width/height0...5, first and last width/height0 to close. Local longitudinal axis+X, cross-section YZ; size scales it. Use >=7 sections for a broad head and tapering tail; curve optionally bends centreline using local cm (Y/Z offsets). Connect fins/leaves/panels with polygon outline and hierarchy. All samples are mathematical point targets, no solid model is rendered. "
        "Surface markings OPTIONAL: pattern:none/spots/stripes/patches,patternColor:#RRGGBB,patternScale:2..32,belly:true/false,bellyColor:#RRGGBB. Markings colour individual particles in local coordinates. Use spots on a continuous body instead of hundreds of floating spot spheres. Reserve separate geometry for distinctive face/eye patches, mouths, fins and ears. Before output, check silhouette from both front and side, symmetry where appropriate, physical contacts and hierarchy, readable eyes/mouth, correct tail/wing orientation and enough contrast. Limit scene to the requested subject and useful context. "
        "OPTIONAL sensors:{sound,warmth,light,hand,magnet} each 0..2: how strongly the PHONE sensors move this scene (sound=breath and claps on the microphone, warmth=a palm resting on the screen, light=ambient light sensor, hand=proximity sensor, magnet=magnetometer). 1 is default, 0 ignores that sensor, 2 is extra sensitive. Include it ONLY when the user asks how the scene reacts to sound/voice/breath/claps, touch/warmth, brightness, a hand or a magnet; otherwise OMIT it and the current values are kept. Device tilt is controlled by tilt, shaking by shake. "
        "REQUIRED base: schema:3,title:Japanese 1..24 chars,color/accent:#RRGGBB,motion:.2..2,viscosity:0..1,swirl:-2..2,attraction:0..1,tilt:0..2,glow:.3..2,trail:0..1,shakeThreshold:1.3..3,shake:scatter/gather/none,note:Japanese<=100 chars. "
		"PARTICLE-ONLY RENDERER: ALL scenery, animals, water, sky, suns and lights are drawn exclusively as discrete camera-facing particles. No solid mesh, floor, backdrop, grid, reflection or alternate renderer exists. Normally pointCloud:{enabled:true,count:131072,floor:false,duration:3,arc:22,twist:.15,stagger:.18}. Count is the TOTAL ceiling, up to{BUDGET} on this device; Default to131072 for a NEW sculpture, even if the previous scene used65536. Preserve the existing budget on edits unless the user asks to change it. Smaller budgets are allowed when requested or deliberately chosen for a sparse design; independent free lights only use particles.count0..1024. Never duplicate sparse lights just to fill the sculpture budget. objects are mathematical target surfaces, NEVER rendered models. Their animation and motion channels move the particles themselves, including bird wing deformation, waves, flowing tubes and hierarchy motion. particles.count0..1024 adds freely simulated point lights within the same total budget; use for fireflies, stars, snow, flock etc, or0 for a pure sculpture. Large surfaces should be focus:false so they do not consume the particles needed by the main subject. No automatic solid ground is drawn; add a particle surface object only when requested. Background colour only tints sparse distant points; use coloured particles for sky/sunset, never a backdrop sheet. "
		"A new object noun by itself means build that recognizable complete subject, not just recolour the previous object. You may invent shapes using lathe profile, polygon outlines and curved tubes. lathe requires profile:[[height,radius],...] with 3..32 strictly increasing heights within -.5...5, radius0...5; first/last radius0 to close the body. size scales the profile. Optional curve points (local cm) offset the lathe centreline, allowing curved fruit, bottles, vases, vegetables, limbs. Compose distinctive body, cap, stem and leaves with separate IDs/colours. No predetermined noun-to-preset mapping exists. "
		"Orient each part for its actual 3D contact surface: polygon/star/leaf lie in localXY with thicknessZ, so rotation[0,0,0] makes a horizontal cap. The X90 rule is ONLY for front-facing silhouettes, never an automatic rule for caps. Use parent anchors or explicit contact points to connect stems/caps/leaves, including when a body is tilted. Preserve distinctive proportions and curvature rather than replacing every subject with a round sphere. "
		"particles:{count:int0..1024,size:.25..5,behavior:drift/flock/orbit/rain/snow/fire/fountain/fireworks/form,formation:volume/sphere/ring/heart/star/helix/galaxy/grid/wave/torus/cube/fish/bird/butterfly/flower/custom,"
		"shape:sphere/fish,colorMode:solid/alternate/rainbow/height/speed/leader,center:[x,y,z] (xy+-260,z45..320),wind:3numbers+-2,gravity:-2..2,turbulence:0..2,spread:20..250,formationStrength:0..4,cohesion/alignment/separation:0..2,groups:int1..6,"
		"formationNext:none or another formation,formationPeriod:2..60 seconds,travel:0..80 cm,formationRotation:[x,y,z] degrees+-180,points:2..128 normalized xyz points within+-1 for custom}. "
		"Flock + formation!=volume really swims in that silhouette. form gathers points; formationNext smoothly morphs between shapes. custom draws a closed point contour. leader makes particle0 accent at the fish eye; all others main color. "
		"world:{background/floor:#RRGGBB,ambient:.1..3,bloom:0..2,box:boolean,keyColor/fillColor:#RRGGBB,keyIntensity:.1..12,keyDirection:[pitch,yaw,roll] degrees,lights:[{position:[x,y,z],color:#RRGGBB,intensity:0..20000,radius:50..1200}] up to4 local lights}. "
		"camera:{autoFrame:true,yaw:-360..360,pitch:10..85,orbit:-30..30 DEGREES/SECOND (0=stopped; 8..15=slow continuous orbit around subject),zoom:.65..2.2,fill:.4.. .88,focus:[x,y,z]}. Camera-only requests preserve geometry and particle settings; use orbit for a moving camera, not object spin. autoFrame fits the animated subject bounds; use fill .72,zoom1. Disable box normally; no visible grid for object scenes. "
		"objects:[up to48 object groups, <=256 total copies]. REQUIRED object fields: id<=32chars,shape,color:#RRGGBB,position:[x,y,z],size:[width,depth,height]1..600 cm. "
		"Use sphere with nonuniform size for an ellipsoid object; ellipsoid is otherwise an SDF part type. Put storyboard ONLY at the scene root, beside objects, NEVER inside an object. "
		"Shapes: loft/lathe/sphere/box/cylinder/cone/torus/disk/star/heart/leaf/fish/flower/polygon/group/bird/bowl/tube/ribbon/water/rock/tree/elephant/giraffe/lion/butterfly/whale/petal/roof/gear/blossom. "
		"Prefer native assemblies when available: blossom includes stem,leaves,hub and TEN connected petals. blossom animation:bloom,amplitude55,speed1 opens/closes around fixed roots; size[190,190,210],position[0,0,105],color pink. Do NOT add a second stem or hub to blossom. gear is a toothed ring with a central hole, axial direction localZ; gear size[110,110,16],rotation[90,0,0],animation:spinLocal,speed1 or-1 turns about its own axle. Prefer gear+spinLocal for gears over separately calculated rotated teeth. roof is a closed gabled roof: ridge runs alongY, bottom is position.Z-size.Z/2, ridge is position.Z+size.Z/2; put its bottom on top of house walls without rotation. butterfly has thin patterned wings and antennae, whale has eyes/flukes/fins, petal is a curved thin volume; use these silhouettes instead of fat spheres for wings or petals. bird includes eyes,beak,body,wings; STORYBOARD (continuous skinned animation keyframes for actions: eating, waving, walking, opening): top-level storyboard:{hold:0,loop:true for cyclic actions,frames:[2..8 {move:.6..3 seconds to interpolate to this pose,hold:0,set:[{id:existing object id,...object fields,patch:[{i:sdf part index,a:[..],b:[..],r:[..],rot:[..],color:..}]}]}]}. Every frame overrides the BASE scene, not the previous frame. Base is pose 0; a loop smoothly returns home. The same particles follow the moving skin without rebuilding or pausing. Keep object ids, primitive types, part order and counts unchanged; animate joints with sdf patch endpoints and rotations. Use native motions for spin, bob, sway and flutter, combined with keyframes for articulated actions. Legacy hold values slow the movement rather than freezing it. SDF SCULPTING (PREFERRED for animals, people, creatures, fruit, food, vehicles, furniture and any organic or smoothly connected subject): ONE object shape:sdf with parts:[up to40 {type:ellipsoid|capsule|box|torus,a:[x,y,z] centre or capsule start (LOCAL cm from the object position),b:[x,y,z] capsule end,r:[rx,ry,rz] ellipsoid radii/box half size, capsule [r1 at a,r2 at b] (tapered limbs, necks, tails, fingers), torus [major,minor],rot:[x,y,z] deg,k:smooth blend radius cm (0 sharp..60 very soft; 4..15 typical; bigger where flesh flows: neck, shoulders, haunches),cut:true carves (eye sockets, mouth, nostrils, hollows),smooth:true no fur/leaf relief (eyes, nose, claws, hooves, teeth),color:#RRGGBB per part (markings follow the part that dominates; blend radius softens colour seams)}]. All parts fuse into ONE continuous skin, so build anatomy like a sculptor: overlapping masses for ribcage, belly, hips, shoulder blades; tapered capsules for upper and lower limb segments meeting at knees/elbows; a paw/hoof ellipsoid; a head of several ellipsoids (cranium, cheeks, muzzle) plus cut eye sockets with smooth eyeballs; ears as flattened ellipsoids. Use 18..40 parts for a hero subject; size is then only a bounding hint, position is the sdf origin; detail:fur etc applies to the whole skin except smooth parts. Example quadruped torso: {type:ellipsoid,a:[0,0,0],r:[70,38,40],k:12},{type:capsule,a:[45,-22,-5],b:[50,-24,-55],r:[16,10],k:10}. Prefer this over loft/lathe/sphere assemblies. REAL MODELS: shape:model with model:<id> places a real artist-made 3D model converted to particles; its colours, leaves, bark and proportions come from the model, so color is ignored and the model keeps its true aspect inside size (size is a bounding box, give its real proportions). Native animation/motions and keyframe transforms move the model as a whole; its internal artist-made geometry remains rigid. Available ids: {MODELS}. Use a listed model whenever the request matches it (any ordinary tree/木/大きな木 -> island_tree_01), and compose everything else from primitives. Otherwise, tree is a PROCEDURAL BRANCHING tree (tapering trunk, recursive limbs and twigs, hundreds of leaf clumps with bark relief); prefer it for any broadleaf tree, set color to the foliage colour, size[200..300,200..300,260..340] for a hero tree; conifers/palms/bonsai should be composed from lathe/tube parts with detail:foliage; animals include FOUR legs,eyes,ears,tail and characteristic features. bowl is HOLLOW; water is a wave surface with specular highlights. "
		"Objects fit x+-300,y+-200,z0..350; ground z0; viewer NEGATIVE Y, screen RIGHT is NEGATIVE X; zUP. Centered shapes occupy position+-size/2. Bird/butterfly wing span is size.Y: use bird size[75,130,38],butterfly size[55,110,22] so wings are not narrow sticks. bird/fish/whale are already upright and face+X. rotation[0,0,0] gives a readable SIDE silhouette; NEVER apply the polygon X=90 rule to these bodies, which turns them edge-on. Use small Z angles for oblique views. Other animals can rotate Z=-90 to face viewer. "
		"Optional object: parent:'' or another id (relative position/rotation inherited; parent size is baked, not inherited),pivot:[0,0,0] LOCAL hinge cm,rotation:[x,y,z]deg+-360,roughness:.08..1,metallic:0..1,emission:0..8 ONLY actual lights,focus:true (false for terrain/background/water),"
		"copies:1..32,layout:ring/line/grid/spiral/scatter,spacing:0..240,wobble:0.. .35,reaction:none/tilt/shake/both,animation:none/spin/bob/orbit/pulse/wave/sway/path/fly/swim/flow/flutter/stream/spinLocal/bloom,speed:-3..3,amplitude:0..150. "
		"fly/swim translate AND orient along an undulating flight trajectory; bird and butterfly fly/flutter ALSO flap wings. flutter flaps in place. Legacy path uses path:[[absolute x,y,z],...]2..16 smooth loop waypoints. "
		"group has no geometry; use as animated parent of eyes,limbs,petals,mechanisms etc. Children move with it. To rotate around a hinge specify pivot at the LOCAL joint, not world origin. "
		"Prefer NAMED ANCHORS for connected joints: object anchor:center/x-/x+/y-/y+/z-/z+ (default center). With a non-center anchor, position is the JOINT location, and that face stays fixed while rotating. "
		"parentAnchor has the same face names; it adds that point of the parent to the child's position. Example petal: parent:stem,parentAnchor:z+,position:[0,0,0],anchor:x-,size:[90,40,5],rotation Z=A,rotate axis[-sinA,cosA,0]. This attaches every petal root to the stem top. Do not also offset position by half its size. "
		"polygon: outline:[[x,y],...]3..32 non-intersecting ordered boundary points within+-.5,extruded alongZ. XY leaf/star/heart/flower face viewer with rotation:[90,0,0]. "
		"torus radius controls its ring thickness in cm (default3). tube/ribbon REQUIRE curve:[[x,y,z],...]2..24 LOCAL centimetre control points, radius:.3..40 cm,closed:false (true makes a smooth closed loop). Their size is ignored for curve geometry. Use tube for noodles,ropes,branches,rails,fluid streams; ribbon for fabric,paths,paper. "
		"motions:[up to6 independent channels per object]: {type:translate/rotate/scale/orbit/path/flow/bend/ripple/stream,axis:[x,y,z] each+-1 nonzero,amplitude:-360..360,period:.25..60 seconds,phase:-16..16 cycles,wave:sine/triangle/saw,space:parent/local,closed:true,orient:true,path:[[local offset x,y,z],...]2..24}. "
		"Channels ADD and inherit parent motion. Rotation/translation/orbit axes default to PARENT coordinates; space:local uses the object rest orientation. For a wheel already rotated X=90 use rotate axis[0,0,1],space:local to turn around its own axle instead of tipping it. translate amplitude cm,rotate degrees,scale fractional .1 etc,orbit cm around axis. sine oscillates,triangle pingpongs,saw advances0..1; rotate360+saw makes continuous revolution. "
		"path adds smooth curve offsets to position (closed true loops;false travels back/forth),orient aligns+X to tangent. flow ONLY tube/ribbon: a strand travels from curve source toward endpoint and is continuously consumed; period controls speed. For POURING/WATERFALL/PIPES/LIGHT TRANSPORT use stream instead: it keeps the entire curve connected and moves highlights along it. flow is for finite material being consumed (noodles), not a continuous fluid. "
		"Tube/ribbon can bind to moving parts with startObject/endObject:another id,startAnchor/endAnchor:center or face name,startOffset/endOffset:[local cm]. Endpoints follow those objects automatically. For noodles endObject:mouth,endAnchor:center; for a pour startObject:spout,endObject:cup,endAnchor:z+,endOffset:[0,0,-5]. curve control points still describe the approximate local route. "
		"bend rotates flexible geometry increasingly away from pivot (plants,wings,fabric);ripple displaces vertices along axis with traveling waves (water,cloth). "
		"IMPORTANT: A noun is not an action. A flying bird MUST have fly or animated parent path AND wing flutter. Slurping MUST have multiple tube curves with flow leading from bowl TO mouth; mouth and face need eyes and a connected endpoint. "
		"For collective figures like Swimmy: flock,formation fish,strength3,count300+,colorMode leader,visible pale fish on dark blue,accent red; no disconnected fixed red fish. "
		"Build other actions from hierarchy + hinges + paths + deformation: opening petals use rotating leaf children; walking uses rotating leg children on moving parent; conveyor uses path; fountains use particles; eating/absorbing uses flowing tubes. "
		"Prefer blossom+bloom for a natural opening flower. For individually authored radial flowers use petal shapes, anchor:x-,parent:stem,parentAnchor:z+,position:[0,0,0],rotation Z=A. The rotate channel axis[-sin(A),cos(A),0],amplitude55,period6,phase0 is shared by all petals. Rotate only petals around tangential horizontal hinges; never rotate the stem or hub. "
		"If a scene shows a person, include neck,torso/hair and facial features; do not leave an isolated floating head. Keep noodle endpoints inside the visible mouth. "
		"POLISH: recognizable silhouette and features, connected parts, large subject, crisp contrasted point colours, selective density, subtle warm/cool shading and modest bloom .2...7. No solid bases, physical shadows, metal reflections or background plates exist. Set focus:false on decorative tank frames as well as large water/context surfaces so the requested animal fills the view. "
		"Do NOT make all objects emissive or white,do not flood the screen with glow/trails. Set focus:false ONLY on broad terrain/background/water. Trees that define a forest, houses in a village, or a whole scene requested to be visible MUST stay focus:true. Keep camera zoom1 with autoFrame for a complete subject; excessive zoom crops the head or flower. Use count0 when particles are irrelevant. "
		"Two independent schools use particles behavior:flock,formation:volume,shape:fish,groups:2,count>=200; do not substitute fixed rows of copied fish. For a large collective fish use formation:fish. "
		"When all lights are off but a readable silhouette is requested, use a slightly lighter blue background and dark objects so their outline remains visible. For sculpture lighting with no current objects, CREATE a sculpture to illuminate rather than returning empty objects. Before responding verify requested VERBS are visibly animated, every parent exists, every animal has facial detail/legs, all tubes connect, color contrast and framing are legible. "
		"pointCloud.style:color (default) or crystal. crystal renders the scene as a 3D laser-engraved crystal: fine cool-white dots stippled by the lighting with faint inner volume. Use crystal when the user mentions crystal/glass engraving/クリスタル/ガラス/レーザー彫刻/水晶 or asks for an engraved look; with crystal set background #010309..#04070F, bloom .3...6, keyColor near white, keyIntensity 4..7 from an oblique side so light/shadow stippling reveals form. Keep the style on edits. No transparent glass block renderer exists: never enclose the subject in a dense particle block. Exact readable text is not supported; do not claim hand-built strokes spell Japanese correctly. No arbitrary code,downloaded assets,sound,new mechanics or real-world actions. No fluid solver,automatic collision avoidance or dynamic maze pathfinding: explain when these are preplanned visual animations. For unsupported actions compose the closest visible animated approximation and honestly say the limitation in note. Current scene:")).Replace(TEXT("{MODELS}"),*ModelCatalog()).Replace(TEXT("131072"),*FString::FromInt(Budget)).Replace(TEXT("{BUDGET}"),*FString::FromInt(Budget)) + Current.ToJson() + TEXT("QUALITY PRIORITIES for the scene above: Follow the user-requested silhouette, exact count, colour landmarks and visible action before decorative detail. Part-count recommendations are not quotas: use only useful parts. Native bird + fly/flutter is preferred when wing flapping is requested; a rigid SDF bird does not flap automatically. For SDF colour landmarks, the coloured surface must protrude beyond the neighbouring skin; a buried calyx, eye or marking disappears. Use low blend radius at crisp different-colour boundaries, or separate attached geometry with a stable shared parent. Do not bury requested caps, stems, eyes or mouths inside another volume. For articulated actions, plan at least two visibly distinct poses and preserve the contact constraints in EVERY pose: both hands must keep gripping the held item while the item approaches the mouth. Animate the limb endpoints and hands together with the item using matching storyboard frames, not just the prop alone. Keep a seated pose seated throughout, with feet in front of the haunches. For compact actions use a 3..6 second readable loop with observable travel and a natural return. Before emitting JSON mentally check the starting, middle and returning poses: no gaps at joints, no detached hands or props, no penetration of the face. Do not add unsupported fields. Keep the camera fixed unless motion of the camera was requested; requested subject rotation should be applied to the whole subject including attached details. For an ordinary slow turntable presentation, keep the subject upright and rotate its shared root around world Z (axis:[0,0,1], wave:saw, amplitude:360, period:20..40 seconds). A small constant resting tilt is enough to reveal asymmetry. Do not tumble the subject end over end or use a diagonal multi-axis rotation unless the user asks for tumbling. Choose a low oblique fixed camera that keeps the defining silhouette readable throughout the turn.");
}

bool FLightBoxStyle::Pose(int32 Index,FLightBoxStyle& Out,FString* Why) const
{
	TSharedPtr<FJsonObject> J;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ToJson()),J)||!J.IsValid())return false;
	J->RemoveField(TEXT("storyboard"));
	if(Index>0){
		if(!Story.IsValidIndex(Index-1))return false;
		TArray<TSharedPtr<FJsonValue>> Set;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Story[Index-1].Set),Set))return false;
		TArray<TSharedPtr<FJsonValue>> Nodes=J->GetArrayField(TEXT("objects"));
		for(const auto& V:Set){
			const auto Change=V->AsObject();FString Id;Change->TryGetStringField(TEXT("id"),Id);
			const int32 At=Nodes.IndexOfByPredicate([&](const TSharedPtr<FJsonValue>& X){FString Y;return X->AsObject()->TryGetStringField(TEXT("id"),Y)&&Y==Id;});
			if(At==INDEX_NONE){if(Why)*Why=TEXT("unknown id ")+Id;return false;}
			const auto Target=Nodes[At]->AsObject();
			for(const auto& Field:Change->Values){
				if(Field.Key==TEXT("id"))continue;
				if(Field.Key==TEXT("patch")){
					// Move individual sdf parts without restating the whole sculpture.
					const TArray<TSharedPtr<FJsonValue>>* Patches;const TArray<TSharedPtr<FJsonValue>>* PartsArr;
					if(!Field.Value->TryGetArray(Patches)||!Target->TryGetArrayField(TEXT("parts"),PartsArr)){if(Why)*Why=TEXT("patch needs an sdf object with parts");return false;}
					TArray<TSharedPtr<FJsonValue>> Parts=*PartsArr;
					for(const auto& P:*Patches){
						if(P->Type!=EJson::Object){if(Why)*Why=TEXT("patch entries are objects");return false;}
						double I;if(!P->AsObject()->TryGetNumberField(TEXT("i"),I)||I<0||int32(I)>=Parts.Num()){if(Why)*Why=TEXT("patch i must index an existing part");return false;}
						auto Part=MakeShared<FJsonObject>(*Parts[int32(I)]->AsObject());for(const auto& PF:P->AsObject()->Values)if(PF.Key!=TEXT("i"))Part->SetField(PF.Key,PF.Value);
						Parts[int32(I)]=MakeShared<FJsonValueObject>(Part);
					}
					Target->SetArrayField(TEXT("parts"),Parts);
				}else Target->SetField(Field.Key,Field.Value);
			}
		}
		J->SetArrayField(TEXT("objects"),Nodes);
	}
	FString Text,Error;FJsonSerializer::Serialize(J.ToSharedRef(),TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
	if(!Parse(Text,Out,Error,Why))return false;
	if(Index>0)Out.AssembleSeconds=Story[Index-1].Move;
	return true;
}

int32 FLightBoxStyle::StoryTargetAt(float Time,float& Move) const
{
	Move=AssembleSeconds;if(Story.IsEmpty())return 0;
	float Cycle=StoryHold;for(const auto& F:Story)Cycle+=F.Move+F.Hold;
	if(bStoryLoop){Cycle+=Story[0].Move;Time=FMath::Fmod(Time,Cycle);}
	float Start=StoryHold;if(Time<Start)return 0;
	for(int32 I=0;I<Story.Num();++I){const float End=Start+Story[I].Move+Story[I].Hold;if(Time<End){Move=Story[I].Move;return I+1;}Start=End;}
	if(!bStoryLoop){Move=Story.Last().Move;return Story.Num();}
	Move=Story[0].Move;return 0; // flying home to the base pose before the cycle repeats
}

bool FLightBoxStyle::CompleteGenerated(const FString& Words,const FLightBoxStyle& Before,FLightBoxStyle& Next,FString& Diagnostic)
{
    auto Has=[&](std::initializer_list<const TCHAR*> Keys){for(auto K:Keys)if(Words.Contains(K,ESearchCase::IgnoreCase))return true;return false;};
    const bool IsEdit=Has({TEXT("そのまま"),TEXT("変えず"),TEXT("だけ"),TEXT("追加"),TEXT("増や"),TEXT("減ら"),TEXT("半分"),TEXT("二倍"),TEXT("only"),TEXT("keep"),TEXT("add ")});
    const bool ParticleRequest=Has({TEXT("粒"),TEXT("群"),TEXT("雪"),TEXT("雨"),TEXT("煙"),TEXT("蛍"),TEXT("花火"),TEXT("火花"),TEXT("炎"),TEXT("噴水"),TEXT("星"),TEXT("ホタル"),TEXT("吹雪"),TEXT("花びら"),TEXT("particle"),TEXT("swarm"),TEXT("flock"),TEXT("snow"),TEXT("rain"),TEXT("fire"),TEXT("stars")});
    const bool School=Has({TEXT("魚"),TEXT("イワシ"),TEXT("いわし"),TEXT("fish"),TEXT("sardine")})&&Has({TEXT("群"),TEXT("school")});
    const bool Silhouette=Has({TEXT("形"),TEXT("かたち"),TEXT("スイミー"),TEXT("ハート"),TEXT("輪"),TEXT("シルエット"),TEXT("shape"),TEXT("silhouette"),TEXT("formation"),TEXT("heart"),TEXT("ring")});
    // Multiple ordinary schools need separate moving leaders. A silhouette uses a single global target.
    if(!IsEdit&&School&&!Silhouette&&Next.Groups>1&&Next.Behavior==TEXT("flock")){
        Next.Formation=TEXT("volume");Next.FormationNext=TEXT("none");
    }
    if(!IsEdit&&!ParticleRequest&&Before.Objects.IsEmpty()&&!Next.Objects.IsEmpty()&&Next.Count==Before.Count) {
        auto ParticleJson=[](const FLightBoxStyle& S){TSharedPtr<FJsonObject> J;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S.ToJson()),J);FString Out;FJsonSerializer::Serialize(J->GetObjectField(TEXT("particles")).ToSharedRef(),TJsonWriterFactory<>::Create(&Out));return Out;};
        // An exact copy of the old decorative field is an inherited default, not a new particle design.
        if(ParticleJson(Before)==ParticleJson(Next)){Next.Count=0;Next.bShowBox=false;}
    }
    FLightBoxStyle Applied=Before;bool Scoped=true;
    if(Has({TEXT("背景だけ"),TEXT("背景のみ"),TEXT("background only"),TEXT("only background")})) Applied.Background=Next.Background;
    else if(Has({TEXT("照明だけ"),TEXT("照明のみ"),TEXT("lighting only")})) {
        Applied.Ambient=Next.Ambient;Applied.Bloom=Next.Bloom;Applied.Glow=Next.Glow;Applied.KeyColor=Next.KeyColor;Applied.FillColor=Next.FillColor;Applied.KeyIntensity=Next.KeyIntensity;Applied.KeyDirection=Next.KeyDirection;Applied.Lights=Next.Lights;
        for(auto& O:Applied.Objects)if(const auto* N=Next.Objects.FindByPredicate([&](const auto& X){return X.Id==O.Id;}))O.Emission=N->Emission;
    } else if(Has({TEXT("カメラだけ"),TEXT("カメラのみ"),TEXT("camera only")})) {
        Applied.bAutoFrame=Next.bAutoFrame;Applied.CameraFocus=Next.CameraFocus;Applied.CameraYaw=Next.CameraYaw;Applied.CameraOrbit=Next.CameraOrbit;Applied.CameraPitch=Next.CameraPitch;Applied.CameraZoom=Next.CameraZoom;Applied.CameraFill=Next.CameraFill;
    } else if(Has({TEXT("枠だけ"),TEXT("枠のみ")})) Applied.bShowBox=Next.bShowBox;
    else if(Has({TEXT("粒の数だけ"),TEXT("粒数だけ")})) {Applied.PointCount=Next.PointCount;Applied.Count=Next.Count;}
    else if(Has({TEXT("速さだけ"),TEXT("速度だけ"),TEXT("動きだけをゆっくり")})) {
        Applied.Motion=Next.Motion;Applied.AssembleSeconds=Next.AssembleSeconds;
        for(auto& O:Applied.Objects)if(const auto* N=Next.Objects.FindByPredicate([&](const auto& X){return X.Id==O.Id;})){O.Speed=N->Speed;for(int32 M=0;M<FMath::Min(O.Motions.Num(),N->Motions.Num());++M)if(O.Motions[M].Type==N->Motions[M].Type)O.Motions[M].Period=N->Motions[M].Period;}
    } else Scoped=false;
    if(Scoped){Applied.Title=Next.Title;Applied.Note=Next.Note;Next=MoveTemp(Applied);}
    // Sensor sensitivity is a standing rule: a response that does not name it keeps the current one.
    if(!Next.bSensorsSpecified)Next.Sensors=Before.Sensors;
    // A request only about how the scene reacts must not rebuild the subject.
    const bool SensorWords=Has({TEXT("反応"),TEXT("センサ"),TEXT("感度"),TEXT("敏感"),TEXT("鈍感"),TEXT("react"),TEXT("sensor")});
    if(SensorWords&&Next.bSensorsSpecified&&!Has({TEXT("色"),TEXT("形"),TEXT("作って"),TEXT("描いて"),TEXT("追加")})){
        FLightBoxStyle Kept=Before;Kept.Sensors=Next.Sensors;Kept.Note=Next.Note;Next=MoveTemp(Kept);
    }
    if(Has({TEXT("文字"),TEXT("日本語")})&&Has({TEXT("読め"),TEXT("表示")}))Next.Note=TEXT("正確な文字描画は未対応です。線の立体による近似表現です");
    if(Has({TEXT("物理実験"),TEXT("流体")})&&Has({TEXT("水"),TEXT("油")}))Next.Note=TEXT("流体の物理計算ではなく、設定した経路と形によるアニメーションです");

    return true;
}

bool FLightBoxStyle::ValidateIntent(const FString& Words,const FLightBoxStyle& Scene,FString& Diagnostic)
{
    if(Scene.Objects.ContainsByPredicate([](const auto& O){return O.bAttached&&!O.Parent.IsEmpty();})){
        const auto World=FLightBoxMotionSystem::Evaluate(Scene,0);TArray<FBox> Bounds;
        for(int32 I=0;I<Scene.Objects.Num();++I){FBox Box(ForceInit);for(const auto& P:FLightBoxMesh::Build(Scene.Objects[I]).Vertices)Box+=World[I].TransformPosition(P);Bounds.Add(Box);}
        for(int32 I=0;I<Scene.Objects.Num();++I){const auto& O=Scene.Objects[I];if(!O.bAttached||O.Parent.IsEmpty()||O.Shape==TEXT("group"))continue;
            const int32 Parent=Scene.Objects.IndexOfByPredicate([&](const auto& P){return P.Id==O.Parent;});
            if(Parent==INDEX_NONE||Scene.Objects[Parent].Shape==TEXT("group"))continue;
            if(Bounds[I].IsValid&&Bounds[Parent].IsValid&&!Bounds[I].Intersect(Bounds[Parent].ExpandBy(4))){
                Diagnostic=TEXT("Required attached part '")+O.Id+TEXT("' floats away from parent '")+O.Parent+TEXT("'. Child position is LOCAL to parent, not world. Subtract the parent's world offset; connect their mathematical surfaces. Do not remove attached:true just to evade contact validation.");return false;
            }
        }
    }
	auto Any=[&](TFunction<bool(const FLightBoxObject&)> Test){return Scene.Objects.ContainsByPredicate(Test);};
	if(Words.Contains(TEXT("彫刻"))&&Scene.Objects.IsEmpty()){Diagnostic=TEXT("The request needs a visible sculpture. There is no current sculpture to relight. Create a recognizable object assembly, count0, warm keyColor and cool fillColor. Returning the unchanged empty particle scene is insufficient.");return false;}
	const bool BirdRequest=(Words.Contains(TEXT("かもめ"))||Words.Contains(TEXT("カモメ"))||Words.Contains(TEXT("鳥"))||Words.Contains(TEXT("蝶"))||Words.Contains(TEXT("bird"),ESearchCase::IgnoreCase));
	if(BirdRequest&&(Words.Contains(TEXT("飛"))||Words.Contains(TEXT("fly"),ESearchCase::IgnoreCase))&&!Words.Contains(TEXT("飛ばない"))) {
		const bool Travels=Any([](const auto& O){return O.Animation==TEXT("fly")||O.Animation==TEXT("path")||O.Motions.ContainsByPredicate([](const auto& M){return M.Type==TEXT("path")||M.Type==TEXT("translate")||M.Type==TEXT("orbit");});});
		const bool Wings=Any([](const auto& O){return ((O.Shape==TEXT("bird")||O.Shape==TEXT("butterfly"))&&(O.Animation==TEXT("fly")||O.Animation==TEXT("flutter")))||O.Motions.ContainsByPredicate([](const auto& M){return M.Type==TEXT("rotate")||M.Type==TEXT("bend");});});
		if(!Travels||!Wings){Diagnostic=TEXT("Requested flying: stationary silhouettes are insufficient. Include actual path/fly movement AND fluttering wings (bird shape animation fly or child hinged wings).");return false;}
	}
	if(Words.Contains(TEXT("すす"))||Words.Contains(TEXT("啜"))||Words.Contains(TEXT("slurp"),ESearchCase::IgnoreCase)) {
		if(!Any([](const auto& O){return (O.Shape==TEXT("tube")||O.Shape==TEXT("ribbon"))&&(O.Animation==TEXT("flow")||O.Motions.ContainsByPredicate([](const auto& M){return M.Type==TEXT("flow");}));})) {
			Diagnostic=TEXT("Requested slurping: add tube curves from bowl to mouth with flow motion. Static noodles/steam do not satisfy ingestion.");return false;
		}
	}
	if(Scene.Objects.IsEmpty()&&(Words.Contains(TEXT("スイミー"))||(Words.Contains(TEXT("群れ"))&&(Words.Contains(TEXT("形"))||Words.Contains(TEXT("かたち")))))) {
		if(Scene.Count<32||Scene.Formation==TEXT("volume")||(Scene.Behavior!=TEXT("flock")&&Scene.Behavior!=TEXT("form"))) {Diagnostic=TEXT("Requested a collective silhouette: use particles behavior flock/form, non-volume formation (fish/custom etc), count>=32. A separate fixed fish is insufficient.");return false;}
	}
	if(Words.Contains(TEXT("群れ"))&&(Words.Contains(TEXT("泳"))||Words.Contains(TEXT("swim"),ESearchCase::IgnoreCase))) {
		if(Scene.Count<32||(Scene.Behavior!=TEXT("flock")&&Scene.Behavior!=TEXT("form"))) {Diagnostic=TEXT("Swimming schools require actual particles behavior flock/form, count>=32. Fixed copied fish and unrelated drift particles are insufficient.");return false;}
	}
	Diagnostic.Reset();return true;
}

FLinearColor FLightBoxStyle::ParticleColor(int32 Index, const FVector& Position, float Speed, float Time) const
{
	if (ColorMode == TEXT("rainbow")) { return FLinearColor::MakeFromHSV8(static_cast<uint8>(FMath::Fmod(Index * 251.f / FMath::Max(1, Count) + Time * 9, 255)), 190, 255); }
	if (ColorMode == TEXT("height")) { return FMath::Lerp(Color, Accent, FMath::Clamp((Position.Z - 45) / 290, 0., 1.)); }
	if (ColorMode == TEXT("speed")) { return FMath::Lerp(Color, Accent, FMath::Clamp(Speed / 220, 0.f, 1.f)); }
	if(ColorMode==TEXT("leader")) return Index==0?Accent:Color;
	return ColorMode == TEXT("solid") || Index % 4 != 0 ? Color : Accent;
}
