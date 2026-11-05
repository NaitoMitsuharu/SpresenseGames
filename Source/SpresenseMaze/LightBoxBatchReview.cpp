#include "LightBoxBatchReview.h"
#include "LightBoxGame.h"
#include "LightBoxDirector.h"
#include "LightBoxGeometry.h"
#include "LightBoxMotion.h"
#include "Camera/CameraComponent.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

namespace {
using FJson=TSharedPtr<FJsonObject>;
bool Read(const FString& P,FJson& J) {FString S;return FFileHelper::LoadFileToString(S,*P)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S),J)&&J.IsValid();}
void Write(const FString& P,const TSharedRef<FJsonObject>& J) {FString S;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&S));FFileHelper::SaveStringToFile(S,*(P+TEXT(".tmp")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);IFileManager::Get().Move(*P,*(P+TEXT(".tmp")),true);}
bool Flow(const FLightBoxObject& O){return O.Animation==TEXT("flow")||O.Animation==TEXT("stream")||O.Motions.ContainsByPredicate([](const auto& M){return M.Type==TEXT("flow")||M.Type==TEXT("stream");});}
bool Has(const FString& S,std::initializer_list<const TCHAR*> Names){for(auto N:Names) if(S.Contains(N,ESearchCase::IgnoreCase)) return true;return false;}
TSharedRef<FJsonObject> Measure(const FLightBoxStyle& S,const FLightBoxStyle& Base,const TArray<TSharedPtr<FJsonValue>>& Checks)
{
    auto J=MakeShared<FJsonObject>();int32 Vertices=0,Dynamic=0,Parents=0,Hinges=0,Paths=0,Flows=0,Birds=0,Water=0,Animals=0,Copies=0;
    float FlowChange=0;
    float RoughMin=1,RoughMax=0,MetalMax=0,MotionMax=0,FrameMax=0,MinZ=10000,MaxGap=0;
    TArray<FLightBoxMesh> Meshes;TArray<int32> Mouths,Strands;
    for(int32 I=0;I<S.Objects.Num();++I) {
        const auto& O=S.Objects[I];Meshes.Add(FLightBoxMesh::Build(O,0));Vertices+=Meshes.Last().Vertices.Num();if(O.IsDeforming()) Dynamic+=Meshes.Last().Vertices.Num();
        if(Flow(O)){const auto Other=FLightBoxMesh::Build(O,.37f);for(int32 K=0;K<FMath::Min(Other.Vertices.Num(),Meshes.Last().Vertices.Num());++K){FlowChange=FMath::Max(FlowChange,float(FVector::Dist(Other.Vertices[K],Meshes.Last().Vertices[K])));FlowChange=FMath::Max(FlowChange,FMath::Abs(Other.Colors[K].R-Meshes.Last().Colors[K].R));}}
        Parents+=!O.Parent.IsEmpty();Copies+=O.Copies;Water+=O.Shape==TEXT("water");Animals+=O.Shape==TEXT("elephant")||O.Shape==TEXT("giraffe")||O.Shape==TEXT("lion");
        Hinges+=O.Shape==TEXT("blossom")&&O.Animation==TEXT("bloom");Paths+=O.Animation==TEXT("stream");
        Birds+=(O.Shape==TEXT("bird")||O.Shape==TEXT("butterfly"))&&(O.Animation==TEXT("fly")||O.Animation==TEXT("flutter"));Paths+=O.Animation==TEXT("path")||O.Animation==TEXT("orbit");
        if(Flow(O)){++Flows;if(!Has(O.Id,{TEXT("steam"),TEXT("smoke"),TEXT("湯気"),TEXT("煙")})) Strands.Add(I);}
        if(Has(O.Id,{TEXT("mouth"),TEXT("口")})) Mouths.Add(I);
        RoughMin=FMath::Min(RoughMin,O.Roughness);RoughMax=FMath::Max(RoughMax,O.Roughness);MetalMax=FMath::Max(MetalMax,O.Metallic);
        for(const auto& M:O.Motions){Hinges+=M.Type==TEXT("rotate");Paths+=M.Type==TEXT("path")||M.Type==TEXT("orbit")||M.Type==TEXT("translate");}
    }
    const auto Start=FLightBoxMotionSystem::Evaluate(S,0);
    const auto Cam=FLightBoxMotionSystem::Camera(S,2340.f/1080);const double TanH=FMath::Tan(FMath::DegreesToRadians(29.)),TanV=TanH/(2340./1080);
    bool Finite=true;FBox2D Screen(ForceInit);
    for(float T:{0.f,.5f,1.5f,3.f,6.f,10.f}) {
        const auto World=FLightBoxMotionSystem::Evaluate(S,T);const auto Resolved=FLightBoxMotionSystem::ResolveBindings(S,World);
        for(int32 I=0;I<S.Objects.Num();++I) {
            const auto& O=S.Objects[I];const auto Mesh=O.IsDeforming()?FLightBoxMesh::Build(Resolved[I],T):Meshes[I];
            MotionMax=FMath::Max(MotionMax,float(FVector::Dist(Start[I].GetLocation(),World[I].GetLocation())));
            for(int32 K=0;K<Mesh.Vertices.Num();K+=FMath::Max(1,Mesh.Vertices.Num()/160)) {
                const FVector V=World[I].TransformPosition(Mesh.Vertices[K]);Finite&=!V.ContainsNaN();
                if(!O.bFocus) continue;
                MotionMax=FMath::Max(MotionMax,float(FVector::Dist(V,Start[I].TransformPosition(Meshes[I].Vertices[K]))));
                MinZ=FMath::Min(MinZ,float(V.Z));const auto P=Cam.InverseTransformPosition(V);
                const FVector2D Q(P.Y/FMath::Max(1.,P.X)/TanH,P.Z/FMath::Max(1.,P.X)/TanV);Screen+=Q;
                FrameMax=FMath::Max(FrameMax,float(FMath::Max(FMath::Abs(Q.X),FMath::Abs(Q.Y))));
            }
        }
        if(Mouths.Num()) for(int32 I:Strands) if(S.Objects[I].Curve.Num()) {
            const FVector End=World[I].TransformPosition(Resolved[I].Curve.Last());double Gap=1e9;
            for(int32 M:Mouths){FBox Box(ForceInit);for(auto V:Meshes[M].Vertices)Box+=World[M].TransformPosition(V);if(Box.IsValid)Gap=FMath::Min(Gap,FMath::Sqrt(Box.ComputeSquaredDistanceToPoint(End)));}
            MaxGap=FMath::Max(MaxGap,float(Gap));
        }
    }
    FLightBoxSimulation ParticleSimulation;ParticleSimulation.Reset(S); TArray<FVector> InitialParticles;for(const auto& P:ParticleSimulation.Motes)InitialParticles.Add(P.Position);float ParticleMotion=0;
    for(int32 Step=0;Step<360;++Step)ParticleSimulation.Step(1.f/60,FVector2D::ZeroVector,0,S);
    for(int32 I=0;I<InitialParticles.Num();++I)ParticleMotion=FMath::Max(ParticleMotion,float(FVector::Dist(InitialParticles[I],ParticleSimulation.Motes[I].Position)));
    auto Metrics=MakeShared<FJsonObject>();Metrics->SetNumberField(TEXT("particleMotionCm"),ParticleMotion);Metrics->SetNumberField(TEXT("objects"),S.Objects.Num());Metrics->SetNumberField(TEXT("copies"),Copies);Metrics->SetNumberField(TEXT("vertices"),Vertices);Metrics->SetNumberField(TEXT("dynamicVertices"),Dynamic);
    Metrics->SetNumberField(TEXT("parents"),Parents);Metrics->SetNumberField(TEXT("hinges"),Hinges);Metrics->SetNumberField(TEXT("paths"),Paths);Metrics->SetNumberField(TEXT("flows"),Flows);Metrics->SetNumberField(TEXT("flowSurfaceChange"),FlowChange);
    Metrics->SetNumberField(TEXT("motionCm"),MotionMax);Metrics->SetNumberField(TEXT("frameExtent"),FrameMax);Metrics->SetNumberField(TEXT("minimumZ"),MinZ);Metrics->SetNumberField(TEXT("mouthGapCm"),MaxGap);
    Metrics->SetBoolField(TEXT("finite"),Finite);Metrics->SetNumberField(TEXT("particles"),S.Count);Metrics->SetNumberField(TEXT("screenArea"),Screen.bIsValid?Screen.GetSize().X*Screen.GetSize().Y/4:0);
    J->SetObjectField(TEXT("metrics"),Metrics);
    TArray<TSharedPtr<FJsonValue>> Findings;int32 Passed=0;
    FLightBoxStyle OA,OB;OA.Objects=S.Objects;OB.Objects=Base.Objects;const bool SameObjects=OA.ToJson()==OB.ToJson();
    for(const auto& V:Checks) {
        const FString C=V->AsString();bool Good=true;
        if(C==TEXT("flying"))Good=(Birds>0||(Parents>0&&Hinges>0&&Paths>0))&&MotionMax>2;
        else if(C==TEXT("moving"))Good=MotionMax>2||ParticleMotion>2;
        else if(C==TEXT("flow"))Good=Flows>0&&FlowChange>.001f;
        else if(C==TEXT("mouth"))Good=Flows>0&&Mouths.Num()>0&&MaxGap<12;
        else if(C==TEXT("flock_shape"))Good=S.Count>=32&&S.Formation!=TEXT("volume")&&(S.Behavior==TEXT("flock")||S.Behavior==TEXT("form"));
        else if(C==TEXT("formation"))Good=S.Count>0&&S.Formation!=TEXT("volume");
        else if(C==TEXT("custom_formation"))Good=S.Count>0&&S.Formation==TEXT("custom")&&S.FormationPoints.Num()>=5;
        else if(C==TEXT("morph"))Good=S.Count>0&&S.FormationNext!=TEXT("none")&&S.FormationNext!=S.Formation;
        else if(C==TEXT("flock"))Good=S.Count>0&&S.Behavior==TEXT("flock");
        else if(C==TEXT("hinge"))Good=Hinges>0&&MotionMax>2;
        else if(C==TEXT("articulated"))Good=Parents>0&&MotionMax>2;
        else if(C==TEXT("path"))Good=Paths>0&&MotionMax>2;
        else if(C==TEXT("objects"))Good=S.Objects.Num()>0;
        else if(C==TEXT("particles"))Good=S.Count>0;
        else if(C==TEXT("water"))Good=Water>0;
        else if(C==TEXT("animals"))Good=Animals>0;
        else if(C==TEXT("deform"))Good=(Dynamic>0||Hinges>0||S.Objects.ContainsByPredicate([](const auto& O){return O.Animation==TEXT("sway");}))&&MotionMax>2;
        else if(C==TEXT("snow")||C==TEXT("rain")||C==TEXT("fire")||C==TEXT("fountain")||C==TEXT("fireworks"))Good=S.Count>0&&S.Behavior==C;
        else if(C==TEXT("shake"))Good=S.Shake==TEXT("scatter");
        else if(C==TEXT("grounded"))Good=FMath::Abs(MinZ)<8;
        else if(C==TEXT("framing"))Good=FrameMax<.93f;
        else if(C==TEXT("materials"))Good=MetalMax>.2f||RoughMax-RoughMin>.15f;
        else if(C==TEXT("lights"))Good=S.Lights.Num()>0||S.Objects.ContainsByPredicate([](const auto& O){return O.Emission>.2;});
        else if(C==TEXT("contrast"))Good=true; // Visual review required; do not pretend a scalar proves legibility.
        else if(C==TEXT("honest_limit"))Good=!S.Note.IsEmpty();
        else if(C==TEXT("count1024"))Good=S.Count==1024;
        else if(C==TEXT("count0"))Good=S.Count==0;
        else if(C==TEXT("copies32"))Good=S.Objects.ContainsByPredicate([](const auto& O){return O.Copies==32;});
        else if(C==TEXT("preserve_objects"))Good=SameObjects;
        else if(C==TEXT("half_count"))Good=S.Count==Base.Count/2;
        else if(C==TEXT("append"))Good=S.Objects.Num()>Base.Objects.Num();
        else if(C==TEXT("preserve_count"))Good=S.Count==Base.Count;
        else if(C==TEXT("hide_box"))Good=!S.bShowBox;
        else if(C==TEXT("camera_change"))Good=S.CameraYaw!=Base.CameraYaw||S.CameraPitch!=Base.CameraPitch||S.CameraZoom!=Base.CameraZoom;
        else if(C==TEXT("slower"))Good=S.Motion<Base.Motion;
        else if(C==TEXT("double_speed")) {
            Good=FMath::IsNearlyEqual(S.Motion,Base.Motion*2,.02f);
            for(int32 I=0;I<FMath::Min(S.Objects.Num(),Base.Objects.Num());++I) {
                Good|=FMath::IsNearlyEqual(S.Objects[I].Speed,Base.Objects[I].Speed*2,.02f);
                for(int32 M=0;M<FMath::Min(S.Objects[I].Motions.Num(),Base.Objects[I].Motions.Num());++M)Good|=FMath::IsNearlyEqual(S.Objects[I].Motions[M].Period,Base.Objects[I].Motions[M].Period*.5,.02f);
            }
        }
        else if(C==TEXT("preserve_motion")) {
            Good=S.Motion==Base.Motion&&S.Count==Base.Count&&S.Objects.Num()==Base.Objects.Num();
            for(float T:{0.f,1.f,3.f}){const auto A=FLightBoxMotionSystem::Evaluate(S,T),B=FLightBoxMotionSystem::Evaluate(Base,T);for(int32 I=0;I<FMath::Min(A.Num(),B.Num());++I)Good&=A[I].Equals(B[I],.01);}
        }
        auto F=MakeShared<FJsonObject>();F->SetStringField(TEXT("check"),C);F->SetBoolField(TEXT("pass"),Good);F->SetBoolField(TEXT("manual"),C==TEXT("contrast"));Findings.Add(MakeShared<FJsonValueObject>(F));Passed+=Good&&C!=TEXT("contrast");
    }
    J->SetStringField(TEXT("auditVersion"),TEXT("3.1"));J->SetArrayField(TEXT("checks"),Findings);J->SetNumberField(TEXT("checksPassed"),Passed);J->SetNumberField(TEXT("checksTotal"),Checks.Num()-Checks.FilterByPredicate([](const auto& X){return X->AsString()==TEXT("contrast");}).Num());
    return J;
}
}

void FLightBoxBatchReview::Tick(ALightBoxPawn& P)
{
#if !UE_BUILD_SHIPPING
    if(LastWork==0)LastWork=FPlatformTime::Seconds();
    if(Tag.IsEmpty()) {
        TArray<FString> Files;IFileManager::Get().FindFiles(Files,*(Directory/TEXT("*.request.json")),true,false);Files.Sort();
        for(const auto& File:Files) {
            if(Processed.Contains(File))continue;Processed.Add(File);LastWork=FPlatformTime::Seconds();
            FJson Request;if(!Read(Directory/File,Request))continue;
            FString Id,Completion,Words;Request->TryGetStringField(TEXT("id"),Id);Request->TryGetStringField(TEXT("completion"),Completion);Request->TryGetStringField(TEXT("words"),Words);
            FString Json,Error,Why;bool Accepted=FFileHelper::LoadFileToString(Json,*Completion)&&FLightBoxStyle::ParseCompletion(Json,Scene,Error,&Why);
            FLightBoxStyle Base;FString BasePath,BaseJson;Request->TryGetStringField(TEXT("base"),BasePath);if(FFileHelper::LoadFileToString(BaseJson,*BasePath))FLightBoxStyle::Parse(BaseJson,Base,Error);
            if(Accepted)Accepted=FLightBoxStyle::CompleteGenerated(Words,Base,Scene,Why)&&FLightBoxStyle::ValidateIntent(Words,Scene,Why);
            TSharedRef<FJsonObject> Audit=MakeShared<FJsonObject>();const TArray<TSharedPtr<FJsonValue>>* Checks=nullptr;
            if(Accepted&&Request->TryGetArrayField(TEXT("checks"),Checks))Audit=Measure(Scene,Base,*Checks);
            Audit->SetStringField(TEXT("id"),Id);Audit->SetStringField(TEXT("words"),Words);Audit->SetBoolField(TEXT("accepted"),Accepted);Audit->SetStringField(TEXT("diagnostic"),Accepted?TEXT(""):Why);
            if(Accepted)FFileHelper::SaveStringToFile(Scene.ToJson(),*(Directory/(Id+TEXT(".scene.json"))),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            Write(Directory/(Id+TEXT(".audit.json")),Audit);
            if(!Accepted)continue;
            const FString Final=Directory/(Id+TEXT("_t2.png"));if(IFileManager::Get().FileExists(*Final))continue;
            Tag=Id;Shot=0;WaitFrames=0;bWaiting=false;
            P.GetGameInstance()->GetSubsystem<ULightBoxDirector>()->ApplyScene(Scene);P.ApplyScene(Scene);P.Simulation.Reset(Scene);P.Elapsed=0;P.GeometryClock=1;P.AnimateScene(0,FVector2D::ZeroVector,Scene);P.RenderParticles(Scene);AppliedAt=FPlatformTime::Seconds();return;
        }
        if(IFileManager::Get().FileExists(*(Directory/TEXT("generation.done.json")))&&FPlatformTime::Seconds()-LastWork>3) {
            auto Done=MakeShared<FJsonObject>();Done->SetNumberField(TEXT("processed"),Processed.Num());Write(Directory/TEXT("render.done.json"),Done);FPlatformMisc::RequestExit(false);
        }
        return;
    }
    if(FPlatformTime::Seconds()-AppliedAt<.3)return;
    if(bWaiting) {
        if(!IFileManager::Get().FileExists(*ShotPath))return;
        bWaiting=false;++Shot;WaitFrames=0;
        if(Shot>=3){Tag.Reset();return;}
    }
    const float Times[]={.2f,2.5f,6.f};const float Target=Times[Shot];
    while(P.Elapsed<Target-.0001f){const float Dt=FMath::Min(1.f/60,Target-P.Elapsed);P.Simulation.Step(Dt,FVector2D::ZeroVector,0,Scene);P.Elapsed+=Dt;}
    P.GeometryClock=1;P.AnimateScene(0,FVector2D::ZeroVector,Scene);P.RenderParticles(Scene);
    if(++WaitFrames<5)return;
    ShotPath=Directory/(Tag+FString::Printf(TEXT("_t%d.png"),Shot));
    FScreenshotRequest::RequestScreenshot(ShotPath,true,false);bWaiting=true;
#endif
}
