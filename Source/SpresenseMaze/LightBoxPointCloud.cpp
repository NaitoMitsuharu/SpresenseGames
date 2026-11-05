#include "LightBoxPointCloud.h"
#include "Async/Async.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/Float16Color.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

ULightBoxPointCloud::ULightBoxPointCloud()
{
    SetCollisionEnabled(ECollisionEnabled::NoCollision);SetCastShadow(false);SetMobility(EComponentMobility::Movable);
    SetCanEverAffectNavigation(false);NumCustomDataFloats=1;BoundsScale=50;SetVisibility(false);
}
void ULightBoxPointCloud::Assemble(const FLightBoxStyle& Style)
{
    const uint32 Request=++Serial;bPreparing=true;
    TWeakObjectPtr<ULightBoxPointCloud> Weak(this);
    Async(EAsyncExecution::ThreadPool,[Weak,Style,Request](){
        auto Points=LightBoxPoints::Sample(Style);
        AsyncTask(ENamedThreads::GameThread,[Weak,Style,Request,Points=MoveTemp(Points)]()mutable{
            if(Weak.IsValid()&&Weak->Serial==Request)Weak->Commit(MoveTemp(Points),Style,Request);
        });
    });
}
void ULightBoxPointCloud::Commit(TArray<FLightBoxPoint>&& Points,const FLightBoxStyle& Style,uint32 Request)
{
    if(Request!=Serial||Points.IsEmpty())return;
    if(!PointMaterial){
        auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/LightBox/M_PointSculpture.M_PointSculpture"));
        if(!Material){bPreparing=false;return;}
        PointMaterial=UMaterialInstanceDynamic::Create(Material,this);SetMaterial(0,PointMaterial);
        SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane")));
    }
    const float OldPhase=Progress();const FLightBoxStyle Previous=Settings;
    const FVector OldCenter=FMath::Lerp(CenterFrom,CenterTo,OldPhase);
    FBox Box(ForceInit);for(const auto& P:Points)Box+=FVector(P.Position);
    const FVector NextCenter=Box.GetCenter();
    const int32 Total=FMath::Max(Points.Num(),Goals.Num());
    TArray<FLightBoxPoint> From;From.SetNumUninitialized(Total);
    for(int32 I=0;I<Total;++I){
        if(I<Goals.Num()){
            From[I]=Goals[I];From[I].Position=LightBoxPoints::Position(Sources[I],Goals[I],I,OldPhase,Previous.AssembleArc,Previous.AssembleTwist,Previous.AssembleStagger,Burst);
            const float T=LightBoxPoints::Ease(I,OldPhase,Previous.AssembleStagger);
            From[I].Color=FMath::Lerp(Sources[I].Color,Goals[I].Color,T);From[I].Radius=FMath::Lerp(Sources[I].Radius,Goals[I].Radius,T);
        }else From[I]=LightBoxPoints::Scatter(I,Points[I],NextCenter);
    }
    if(Points.Num()<Total){const int32 Active=Points.Num();Points.SetNum(Total);for(int32 I=Active;I<Total;++I){Points[I]=LightBoxPoints::Scatter(I,From[I],NextCenter);Points[I].Radius=0;}}
    Sources=MoveTemp(From);Goals=MoveTemp(Points);Settings=Style;CenterFrom=OldCenter;CenterTo=NextCenter;Age=0;Burst=0;bTrimmed=false;
    ResizePoints(Total);
    const int32 Width=FMath::RoundUpToPowerOfTwo(FMath::CeilToInt(FMath::Sqrt(float(Total))));
    DataTextures={Texture(Sources,Width,false),Texture(Goals,Width,false),Texture(Sources,Width,true),Texture(Goals,Width,true)};
    const TCHAR* Names[]={TEXT("FromTex"),TEXT("ToTex"),TEXT("FromColor"),TEXT("ToColor")};
    for(int32 I=0;I<4;++I)PointMaterial->SetTextureParameterValue(Names[I],DataTextures[I]);
    PointMaterial->SetScalarParameterValue(TEXT("DataWidth"),Width);
    PointMaterial->SetVectorParameterValue(TEXT("Path"),FLinearColor(Style.AssembleArc,Style.AssembleTwist,Style.AssembleStagger,0));
    PointMaterial->SetScalarParameterValue(TEXT("Phase"),0);bPreparing=false;SetVisibility(true);
}
void ULightBoxPointCloud::ResizePoints(int32 Count)
{
    if(GetInstanceCount()==Count)return;
    ClearInstances();TArray<FTransform> Transforms;Transforms.Init(FTransform::Identity,Count);AddInstances(Transforms,false,false,false);
    for(int32 I=0;I<Count;++I)PerInstanceSMCustomData[I]=float(I);
    MarkRenderStateDirty();
}
UTexture2D* ULightBoxPointCloud::Texture(const TArray<FLightBoxPoint>& Points,int32 Width,bool Color)
{
    auto* T=UTexture2D::CreateTransient(Width,Width,Color?PF_B8G8R8A8:PF_FloatRGBA);
    T->SRGB=false;T->NeverStream=true;T->Filter=TF_Nearest;T->AddressX=TA_Clamp;T->AddressY=TA_Clamp;
    auto& Mip=T->GetPlatformData()->Mips[0];void* Data=Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memzero(Data,Width*Width*(Color?sizeof(FColor):sizeof(FFloat16Color)));
    for(int32 I=0;I<Points.Num();++I){const auto& P=Points[I];
        if(Color)static_cast<FColor*>(Data)[I]=P.Color.ToFColor(false);
        else static_cast<FFloat16Color*>(Data)[I]=FFloat16Color(FLinearColor(P.Position.X,P.Position.Y,P.Position.Z,P.Radius));
    }
    Mip.BulkData.Unlock();T->UpdateResource();return T;
}
void ULightBoxPointCloud::Advance(float Dt,const FVector2D& Tilt,const FVector& Right,const FVector& Up)
{
    if(!PointMaterial)return;
    Age+=Dt;OrientationAge+=Dt;Burst=FMath::Max(0.f,Burst-Dt*.65f);
#if !UE_BUILD_SHIPPING
    const double Now=FPlatformTime::Seconds();const float MeasuredDt=LastReviewTick>0?float(Now-LastReviewTick):Dt;LastReviewTick=Now;
    float ReviewPhase;if(FParse::Value(FCommandLine::Get(),TEXT("PointCloudPhase="),ReviewPhase))Age=FMath::Clamp(ReviewPhase,0.f,1.f)*Settings.AssembleSeconds;
    FString Report;if(!bReviewWritten&&!bPreparing&&Age>=Settings.AssembleSeconds&&FParse::Value(FCommandLine::Get(),TEXT("PointCloudReport="),Report)){
        ReviewElapsed+=MeasuredDt;ReviewFrames.Add(MeasuredDt*1000);
        if(ReviewElapsed>=8){
            bReviewWritten=true;ReviewFrames.Sort();auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("points"),Settings.PointCount);J->SetNumberField(TEXT("instances"),GetInstanceCount());
            J->SetNumberField(TEXT("fps"),ReviewFrames.Num()/ReviewElapsed);J->SetNumberField(TEXT("frameMsP95"),ReviewFrames[FMath::Clamp(FMath::FloorToInt(ReviewFrames.Num()*.95),0,ReviewFrames.Num()-1)]);
            J->SetNumberField(TEXT("frameMsMax"),ReviewFrames.Last());J->SetNumberField(TEXT("seconds"),ReviewElapsed);J->SetBoolField(TEXT("gpuVertexAnimation"),true);
            FString Json;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Json));IFileManager::Get().MakeDirectory(*FPaths::GetPath(Report),true);FFileHelper::SaveStringToFile(Json,*Report);
        }
    }
#endif
    if(!bTrimmed&&Progress()>=1){bTrimmed=true;if(Goals.Num()>Settings.PointCount){Goals.SetNum(Settings.PointCount);Sources.SetNum(Settings.PointCount);ResizePoints(Settings.PointCount);}}
    PointMaterial->SetScalarParameterValue(TEXT("Phase"),Progress());
    PointMaterial->SetVectorParameterValue(TEXT("Path"),FLinearColor(Settings.AssembleArc,Settings.AssembleTwist,Settings.AssembleStagger,Burst));
    const FVector Center=FMath::Lerp(CenterFrom,CenterTo,Progress());PointMaterial->SetVectorParameterValue(TEXT("Center"),FLinearColor(Center.X,Center.Y,Center.Z,0));
    const FQuat R=FQuat(FVector::UpVector,FMath::Sin(OrientationAge*.22f)*.18f)*FQuat(FVector::YAxisVector,Tilt.Y*Settings.Tilt*.25f)*FQuat(FVector::XAxisVector,Tilt.X*Settings.Tilt*.25f);
    auto VectorParameter=[&](const TCHAR* Name,const FVector& V){PointMaterial->SetVectorParameterValue(Name,FLinearColor(V.X,V.Y,V.Z,0));};
    VectorParameter(TEXT("AxisX"),R.RotateVector(FVector::XAxisVector));VectorParameter(TEXT("AxisY"),R.RotateVector(FVector::YAxisVector));VectorParameter(TEXT("AxisZ"),R.RotateVector(FVector::ZAxisVector));
    VectorParameter(TEXT("Right"),Right);VectorParameter(TEXT("Up"),Up);
}
void ULightBoxPointCloud::HideCloud(){++Serial;bPreparing=false;SetVisibility(false);Sources.Reset();Goals.Reset();ClearInstances();DataTextures.Reset();}
void ULightBoxPointCloud::OnComponentDestroyed(bool Hierarchy){++Serial;Super::OnComponentDestroyed(Hierarchy);}
