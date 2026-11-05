#include "LightBoxPointCloud.h"
#include "LightBoxSimulation.h"
#include "RHI.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Components/MeshComponent.h"
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
    const uint32 Request=++Serial;bPreparing=true;bFramePending=false;
    TWeakObjectPtr<ULightBoxPointCloud> Weak(this);
    Async(EAsyncExecution::ThreadPool,[Weak,Style,Request](){
        auto Prepared=MakeShared<FLightBoxPointLayout,ESPMode::ThreadSafe>(LightBoxPoints::Build(Style));
        AsyncTask(ENamedThreads::GameThread,[Weak,Style,Request,Prepared]()mutable{
            if(Weak.IsValid()&&Weak->Serial==Request)Weak->Commit(Prepared,Style,Request);
        });
    });
}
void ULightBoxPointCloud::Commit(TSharedPtr<FLightBoxPointLayout,ESPMode::ThreadSafe> Prepared,const FLightBoxStyle& Style,uint32 Request)
{
    if(Request!=Serial||Prepared->Points.IsEmpty())return;
    auto Points=MoveTemp(Prepared->Points);const int32 ActiveCount=Points.Num();
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
            From[I]=Goals[I];auto Destination=Goals[I];Destination.Position=CurrentDestination(I);From[I].Position=LightBoxPoints::Position(Sources[I],Destination,I,OldPhase,Previous.AssembleArc,Previous.AssembleTwist,Previous.AssembleStagger,Burst);
            const float T=LightBoxPoints::Ease(I,OldPhase,Previous.AssembleStagger);
            From[I].Color=FMath::Lerp(Sources[I].Color,Goals[I].Color,T);From[I].Radius=FMath::Lerp(Sources[I].Radius,Goals[I].Radius,T);
        }else From[I]=LightBoxPoints::Scatter(I,Points[I],NextCenter);
    }
    if(Points.Num()<Total){const int32 Active=Points.Num();Points.SetNum(Total);for(int32 I=Active;I<Total;++I){Points[I]=LightBoxPoints::Scatter(I,From[I],NextCenter);Points[I].Radius=0;Points[I].Vertices=FIntVector(-1,-1,-1);}}
    Sources=MoveTemp(From);Goals=MoveTemp(Points);Layout=Prepared;Settings=Style;Settings.PointCount=ActiveCount;CenterFrom=OldCenter;CenterTo=NextCenter;Age=0;Burst=0;bTrimmed=false;
    ResizePoints(Total);
    const int32 Width=FMath::RoundUpToPowerOfTwo(FMath::CeilToInt(FMath::Sqrt(float(Total))));
    DataTextures={Texture(Sources,Width,false),Texture(Goals,Width,false),Texture(Sources,Width,true),Texture(Goals,Width,true)};
    const TCHAR* Names[]={TEXT("FromTex"),TEXT("ToTex"),TEXT("FromColor"),TEXT("ToColor")};
    for(int32 I=0;I<4;++I)PointMaterial->SetTextureParameterValue(Names[I],DataTextures[I]);
    PointMaterial->SetScalarParameterValue(TEXT("DataWidth"),Width);
    PointMaterial->SetVectorParameterValue(TEXT("Path"),FLinearColor(Style.AssembleArc,Style.AssembleTwist,Style.AssembleStagger,0));
    PointMaterial->SetScalarParameterValue(TEXT("Phase"),0);
    MotionTextures={BindingsTexture(false,Width),BindingsTexture(true,Width)};
    PointMaterial->SetTextureParameterValue(TEXT("LinksTex"),MotionTextures[0]);PointMaterial->SetTextureParameterValue(TEXT("WeightsTex"),MotionTextures[1]);
    FrameFrom=Layout->InitialVertices;FrameTo=FrameFrom;FrameClock=0;FrameAge=1;bFramePending=false;
    const int32 VertexWidth=FMath::RoundUpToPowerOfTwo(FMath::Max(1,FMath::CeilToInt(FMath::Sqrt(float(FrameTo.Num())))));
    MotionTextures.Add(VertexTexture(FrameFrom,VertexWidth));MotionTextures.Add(VertexTexture(FrameTo,VertexWidth));
    PointMaterial->SetTextureParameterValue(TEXT("VertexFrom"),MotionTextures[2]);PointMaterial->SetTextureParameterValue(TEXT("VertexTo"),MotionTextures[3]);
    PointMaterial->SetScalarParameterValue(TEXT("VertexWidth"),VertexWidth);PointMaterial->SetScalarParameterValue(TEXT("Animated"),Layout->bAnimated?1:0);PointMaterial->SetScalarParameterValue(TEXT("FrameMix"),1);
    bPreparing=false;SetVisibility(true);
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
UTexture2D* ULightBoxPointCloud::BindingsTexture(bool Weights,int32 Width)
{
    auto* T=UTexture2D::CreateTransient(Width,Width,Weights?PF_FloatRGBA:PF_A32B32G32R32F);T->SRGB=false;T->NeverStream=true;T->Filter=TF_Nearest;
    auto& Mip=T->GetPlatformData()->Mips[0];void* Data=Mip.BulkData.Lock(LOCK_READ_WRITE);FMemory::Memzero(Data,Width*Width*(Weights?sizeof(FFloat16Color):sizeof(FVector4f)));
    for(int32 I=0;I<Goals.Num();++I){const auto& P=Goals[I];if(Weights)static_cast<FFloat16Color*>(Data)[I]=FFloat16Color(FLinearColor(P.Weights.X,P.Weights.Y,0,0));else static_cast<FVector4f*>(Data)[I]=FVector4f(P.Vertices.X,P.Vertices.Y,P.Vertices.Z,0);}
    Mip.BulkData.Unlock();T->UpdateResource();return T;
}
UTexture2D* ULightBoxPointCloud::VertexTexture(const TArray<FVector4f>& Vertices,int32 Width)
{
    auto* T=UTexture2D::CreateTransient(Width,Width,PF_FloatRGBA);T->SRGB=false;T->NeverStream=true;T->Filter=TF_Nearest;
    auto& Mip=T->GetPlatformData()->Mips[0];auto* Data=static_cast<FFloat16Color*>(Mip.BulkData.Lock(LOCK_READ_WRITE));FMemory::Memzero(Data,Width*Width*sizeof(FFloat16Color));
    for(int32 I=0;I<Vertices.Num();++I){const auto& V=Vertices[I];Data[I]=FFloat16Color(FLinearColor(V.X,V.Y,V.Z,V.W));}
    Mip.BulkData.Unlock();T->UpdateResource();return T;
}
void ULightBoxPointCloud::UploadVertices(UTexture2D* T,const TArray<FVector4f>& Vertices)
{
    const int32 W=T->GetSizeX();auto* Bytes=new uint8[W*W*sizeof(FFloat16Color)]();auto* Data=reinterpret_cast<FFloat16Color*>(Bytes);
    for(int32 I=0;I<Vertices.Num();++I){const auto& V=Vertices[I];Data[I]=FFloat16Color(FLinearColor(V.X,V.Y,V.Z,V.W));}
    auto* Region=new FUpdateTextureRegion2D(0,0,0,0,W,W);
    T->UpdateTextureRegions(0,1,Region,W*sizeof(FFloat16Color),sizeof(FFloat16Color),Bytes,[](uint8* P,const FUpdateTextureRegion2D* R){delete[] P;delete R;});
}
FVector3f ULightBoxPointCloud::CurrentDestination(int32 I) const
{
    return LightBoxPoints::Destination(Goals[I],FrameFrom,FrameTo,FMath::Clamp(FrameAge*30,0.f,1.f));
}
void ULightBoxPointCloud::RefreshFrame(const FVector2D& Tilt,const FLightBoxSimulation* Field)
{
    if(bFramePending||FrameClock<1.f/30)return;FrameClock=0;bFramePending=true;
    TArray<FVector4f> Motes;if(Field)for(const auto& P:Field->Motes)Motes.Add(FVector4f(P.Position.X,P.Position.Y,P.Position.Z,.65f+.35f*FMath::Sin(Age*2+P.Phase)));
    const uint32 Request=Serial;auto Graph=Layout;auto Scene=Settings;const float Time=Age,Scatter=Burst;
    TWeakObjectPtr<ULightBoxPointCloud> Weak(this);
    Async(EAsyncExecution::ThreadPool,[Weak,Graph,Scene,Request,Time,Tilt,Scatter,Motes=MoveTemp(Motes)](){
        auto Vertices=LightBoxPoints::Frame(*Graph,Scene,Time,Tilt,Scatter,Motes);
        AsyncTask(ENamedThreads::GameThread,[Weak,Request,Vertices=MoveTemp(Vertices)]()mutable{
            if(!Weak.IsValid()||Weak->Serial!=Request)return;auto* C=Weak.Get();C->bFramePending=false;
            if(Vertices.Num()!=C->FrameTo.Num())return;
            const float Mix=FMath::Clamp(C->FrameAge*30,0.f,1.f);for(int32 I=0;I<C->FrameFrom.Num();++I)C->FrameFrom[I]=FMath::Lerp(C->FrameFrom[I],C->FrameTo[I],Mix);
            C->FrameTo=MoveTemp(Vertices);C->FrameAge=C->bFixedReviewTime?1:0;C->UploadVertices(C->MotionTextures[2],C->FrameFrom);C->UploadVertices(C->MotionTextures[3],C->FrameTo);
        });
    });
}
void ULightBoxPointCloud::Advance(float Dt,const FVector2D& Tilt,const FVector& Right,const FVector& Up,const FLightBoxSimulation* Field)
{
    if(!PointMaterial)return;
    Age+=Dt;OrientationAge+=Dt;FrameClock+=Dt;FrameAge+=Dt;Burst=FMath::Max(0.f,Burst-Dt*.65f);
    if(!bPreparing&&Layout.IsValid()&&Layout->bAnimated)RefreshFrame(Tilt,Field);
    PointMaterial->SetScalarParameterValue(TEXT("FrameMix"),FMath::Clamp(FrameAge*30,0.f,1.f));
#if !UE_BUILD_SHIPPING
    const double Now=FPlatformTime::Seconds();const float MeasuredDt=LastReviewTick>0?float(Now-LastReviewTick):Dt;LastReviewTick=Now;
    float ReviewPhase;if(FParse::Value(FCommandLine::Get(),TEXT("PointCloudPhase="),ReviewPhase))Age=FMath::Clamp(ReviewPhase,0.f,1.f)*Settings.AssembleSeconds;
    FString Report;if(!bReviewWritten&&!bPreparing&&Age>=Settings.AssembleSeconds&&FParse::Value(FCommandLine::Get(),TEXT("PointCloudReport="),Report)){
        ReviewElapsed+=MeasuredDt;ReviewFrames.Add(MeasuredDt*1000);
        if(ReviewElapsed>=8){
            bReviewWritten=true;ReviewFrames.Sort();auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("points"),Settings.PointCount);J->SetNumberField(TEXT("instances"),GetInstanceCount());
            J->SetNumberField(TEXT("fps"),ReviewFrames.Num()/ReviewElapsed);J->SetNumberField(TEXT("frameMsP95"),ReviewFrames[FMath::Clamp(FMath::FloorToInt(ReviewFrames.Num()*.95),0,ReviewFrames.Num()-1)]);
            J->SetNumberField(TEXT("frameMsMax"),ReviewFrames.Last());J->SetNumberField(TEXT("seconds"),ReviewElapsed);J->SetBoolField(TEXT("gpuVertexAnimation"),true);
            TArray<TSharedPtr<FJsonValue>> OtherPrimitives;int32 Solids=0;for(TActorIterator<AActor> A(GetWorld());A;++A)if(!A->IsHidden()){TArray<UPrimitiveComponent*> Components;A->GetComponents(Components);for(auto* C:Components)if(C->IsRegistered()&&C->IsVisible()&&!C->bHiddenInGame&&!Cast<ULightBoxPointCloud>(C)){if(Cast<UMeshComponent>(C))++Solids;OtherPrimitives.Add(MakeShared<FJsonValueString>(C->GetFullName()));}}
            J->SetArrayField(TEXT("otherPrimitives"),OtherPrimitives);J->SetBoolField(TEXT("particleOnly"),true);J->SetNumberField(TEXT("solidSceneComponents"),Solids);J->SetBoolField(TEXT("animatedTarget"),Layout.IsValid()&&Layout->bAnimated);J->SetNumberField(TEXT("controlVertices"),FrameTo.Num());
            float Travel=0;if(Layout.IsValid())for(int32 I=0;I<FrameTo.Num();++I)Travel=FMath::Max(Travel,FVector3f::Distance(FVector3f(FrameTo[I]),FVector3f(Layout->InitialVertices[I])));J->SetNumberField(TEXT("controlTravel"),Travel);

            FString Json;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Json));IFileManager::Get().MakeDirectory(*FPaths::GetPath(Report),true);FFileHelper::SaveStringToFile(Json,*Report);
        }
    }
#endif
    if(!bTrimmed&&Progress()>=1){bTrimmed=true;if(Goals.Num()>Settings.PointCount){Goals.SetNum(Settings.PointCount);Sources.SetNum(Settings.PointCount);ResizePoints(Settings.PointCount);}}
    PointMaterial->SetScalarParameterValue(TEXT("Phase"),Progress());
    PointMaterial->SetVectorParameterValue(TEXT("Path"),FLinearColor(Settings.AssembleArc,Settings.AssembleTwist,Settings.AssembleStagger,Burst));
    const FVector Center=FMath::Lerp(CenterFrom,CenterTo,Progress());PointMaterial->SetVectorParameterValue(TEXT("Center"),FLinearColor(Center.X,Center.Y,Center.Z,0));
    const FQuat R=FQuat(FVector::YAxisVector,Tilt.Y*Settings.Tilt*.25f)*FQuat(FVector::XAxisVector,Tilt.X*Settings.Tilt*.25f);
    auto VectorParameter=[&](const TCHAR* Name,const FVector& V){PointMaterial->SetVectorParameterValue(Name,FLinearColor(V.X,V.Y,V.Z,0));};
    VectorParameter(TEXT("AxisX"),R.RotateVector(FVector::XAxisVector));VectorParameter(TEXT("AxisY"),R.RotateVector(FVector::YAxisVector));VectorParameter(TEXT("AxisZ"),R.RotateVector(FVector::ZAxisVector));
    VectorParameter(TEXT("Right"),Right);VectorParameter(TEXT("Up"),Up);
    const auto Transform=GetComponentTransform();VectorParameter(TEXT("Origin"),Transform.GetLocation());
    VectorParameter(TEXT("ObjectX"),Transform.TransformVector(FVector::XAxisVector));VectorParameter(TEXT("ObjectY"),Transform.TransformVector(FVector::YAxisVector));VectorParameter(TEXT("ObjectZ"),Transform.TransformVector(FVector::ZAxisVector));
}
void ULightBoxPointCloud::HideCloud(){++Serial;bPreparing=false;SetVisibility(false);Sources.Reset();Goals.Reset();ClearInstances();DataTextures.Reset();MotionTextures.Reset();Layout.Reset();FrameFrom.Reset();FrameTo.Reset();}
void ULightBoxPointCloud::OnComponentDestroyed(bool Hierarchy){++Serial;Super::OnComponentDestroyed(Hierarchy);}
