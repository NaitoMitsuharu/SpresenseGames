#include "LightBoxPointCloud.h"
#include "LightBoxSimulation.h"
#include "RHI.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Components/MeshComponent.h"
#include "Async/Async.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
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
    const float Phase=Progress(),Mix=FMath::Clamp(FrameAge*30,0.f,1.f),Time=AnimationAge,Scatter=Burst;
    const auto Previous=Settings;
    TWeakObjectPtr<ULightBoxPointCloud> Weak(this);
    Async(EAsyncExecution::ThreadPool,[Weak,Style,Request,Previous,Phase,Mix,Time,Scatter,OldFrom=Sources,OldTo=Goals,A=FrameFrom,B=FrameTo]()mutable{
        for(int32 I=0;I<OldTo.Num();++I){auto Target=OldTo[I];Target.Position=LightBoxPoints::Destination(Target,A,B,Mix);
            const float T=LightBoxPoints::Ease(I,Phase,Previous.AssembleStagger);
            OldTo[I].Position=LightBoxPoints::Position(OldFrom[I],Target,I,Phase,Previous.AssembleArc,Previous.AssembleTwist,Previous.AssembleStagger,Scatter);
            OldTo[I].Color=FMath::Lerp(OldFrom[I].Color,Target.Color,T);OldTo[I].Radius=FMath::Lerp(OldFrom[I].Radius,Target.Radius,T);OldTo[I].bFollow=false;
        }
        auto Prepared=MakeShared<FLightBoxPointLayout,ESPMode::ThreadSafe>(LightBoxPoints::Build(Style,Time));
        const int32 Active=Prepared->Points.Num();
        auto Transfer=MakeShared<FLightBoxPointTransfer,ESPMode::ThreadSafe>(LightBoxPoints::Transfer(OldTo,MoveTemp(Prepared->Points),Previous,Style));
        AsyncTask(ENamedThreads::GameThread,[Weak,Style,Request,Prepared,Transfer,Active]()mutable{
            if(Weak.IsValid()&&Weak->Serial==Request)Weak->Commit(Prepared,Transfer,Style,Request,Active);
        });
    });
}
void ULightBoxPointCloud::Commit(TSharedPtr<FLightBoxPointLayout,ESPMode::ThreadSafe> Prepared,TSharedPtr<FLightBoxPointTransfer,ESPMode::ThreadSafe> Transfer,const FLightBoxStyle& Style,uint32 Request,int32 ActiveCount)
{
    if(Request!=Serial)return;
    if(Transfer->To.IsEmpty()){bPreparing=false;return;}
    // Preparation runs while the old scene keeps moving. Rebase at commit, not at its earlier snapshot.
    const float OldPhase=Progress(),OldMix=FMath::Clamp(FrameAge*30,0.f,1.f);
    for(int32 I=0;I<Transfer->SourceIndices.Num();++I){const int32 J=Transfer->SourceIndices[I];if(!Goals.IsValidIndex(J))continue;
        auto Destination=Goals[J];Destination.Position=CurrentDestination(J);auto& Source=Transfer->From[I];
        Source.Position=LightBoxPoints::Position(Sources[J],Destination,J,OldPhase,Settings.AssembleArc,Settings.AssembleTwist,Settings.AssembleStagger,Burst);
        const float T=LightBoxPoints::Ease(J,OldPhase,Settings.AssembleStagger);Source.Color=FMath::Lerp(Sources[J].Color,Goals[J].Color,T);Source.Radius=FMath::Lerp(Sources[J].Radius,Goals[J].Radius,T);
        if(I>=ActiveCount)Transfer->To[I].Position=Source.Position;
    }
    if(Layout.IsValid()&&FLightBoxSimulation::Signature(Settings)==FLightBoxSimulation::Signature(Style)){
        for(int32 I=0;I<FMath::Min(Layout->FieldCount,Prepared->FieldCount);++I)Prepared->InitialVertices[Prepared->FieldOffset+I]=FMath::Lerp(FrameFrom[Layout->FieldOffset+I],FrameTo[Layout->FieldOffset+I],OldMix);
    }
    if(Prepared->bAnimated)Prepared->InitialVertices=LightBoxPoints::Frame(*Prepared,Style,AnimationAge,LastTilt,0,{},0);
    if(!PointMaterial){
        auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/LightBox/M_PointSculpture.M_PointSculpture"));
        if(!Material){bPreparing=false;return;}
        PointMaterial=UMaterialInstanceDynamic::Create(Material,this);SetMaterial(0,PointMaterial);
    }
    CenterFrom=FMath::Lerp(CenterFrom,CenterTo,Progress());ReachFrom=Reach();
    FBox Box(ForceInit);for(int32 I=0;I<ActiveCount;++I)Box+=FVector(Transfer->To[I].Position);CenterTo=Box.GetCenter();ReachTo=Box.IsValid?float(Box.GetExtent().GetMax()):100.f;
    Sources=MoveTemp(Transfer->From);Goals=MoveTemp(Transfer->To);Retained=Transfer->Retained;
    Layout=Prepared;Settings=Style;Settings.PointCount=ActiveCount;Age=0;Burst=0;bTrimmed=false;
    const int32 Total=Goals.Num();
    ResizePoints(Total);
    const int32 Width=FMath::RoundUpToPowerOfTwo(FMath::CeilToInt(FMath::Sqrt(float(Total))));
    DataTextures={Texture(Sources,Width,false),Texture(Goals,Width,false),Texture(Sources,Width,true),Texture(Goals,Width,true)};
    const TCHAR* Names[]={TEXT("FromTex"),TEXT("ToTex"),TEXT("FromColor"),TEXT("ToColor")};
    for(int32 I=0;I<4;++I)PointMaterial->SetTextureParameterValue(Names[I],DataTextures[I]);
    PointMaterial->SetScalarParameterValue(TEXT("DataWidth"),Width);
    OrderTexture=MakeOrderTexture(Total,Width);PointMaterial->SetTextureParameterValue(TEXT("OrderTex"),OrderTexture);
    PointMaterial->SetVectorParameterValue(TEXT("Path"),FLinearColor(Style.AssembleArc,Style.AssembleTwist,Style.AssembleStagger,0));
    PointMaterial->SetScalarParameterValue(TEXT("Phase"),0);
    PointMaterial->SetScalarParameterValue(TEXT("ColourPhase"),-1);StoryColourTextures.Reset();ColourFrom=ColourTo=INDEX_NONE;
    for(const auto& Colours:Layout->PoseColours)StoryColourTextures.Add(ColourTexture(Colours,Width));
    // Small cages already fit the GPU cache. Larger skins share one destination per grain
    // instead of repeating five dependent texture lookups for each of its corners.
    bCacheDestinations=Layout->bAnimated&&Layout->InitialVertices.Num()>2048&&Total<=262144;
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("LightBoxControlFetch")))bCacheDestinations=false;
#endif
    CacheBindings.Reset();CacheFrom.Reset();CacheTo.Reset();bSorted=bSortPending=false;SortClock=0;
    MotionTextures=bCacheDestinations?TArray<TObjectPtr<UTexture2D>>{DataTextures[0],DataTextures[0]}:TArray<TObjectPtr<UTexture2D>>{BindingsTexture(false,Width),BindingsTexture(true,Width)};
    PointMaterial->SetTextureParameterValue(TEXT("LinksTex"),MotionTextures[0]);PointMaterial->SetTextureParameterValue(TEXT("WeightsTex"),MotionTextures[1]);
    FrameFrom=Layout->InitialVertices;FrameTo=FrameFrom;FrameClock=0;FrameAge=1;bFramePending=false;
    const int32 VertexWidth=bCacheDestinations?Width:FMath::RoundUpToPowerOfTwo(FMath::Max(1,FMath::CeilToInt(FMath::Sqrt(float(FrameTo.Num())))));
    if(bCacheDestinations){CacheBindings=MakeShared<const TArray<FLightBoxPointBinding>,ESPMode::ThreadSafe>(LightBoxPoints::Bindings(Goals));CacheFrom=LightBoxPoints::Bake(*CacheBindings,FrameFrom);CacheTo=CacheFrom;}
    auto* VertexAtlas=VertexTexture(bCacheDestinations?CacheFrom:FrameFrom,VertexWidth);MotionTextures.Add(VertexAtlas);MotionTextures.Add(VertexAtlas);
    PointMaterial->SetTextureParameterValue(TEXT("VertexFrom"),MotionTextures[2]);PointMaterial->SetTextureParameterValue(TEXT("VertexTo"),MotionTextures[3]);
    PointMaterial->SetScalarParameterValue(TEXT("VertexWidth"),VertexWidth);PointMaterial->SetScalarParameterValue(TEXT("Animated"),bCacheDestinations?2:Layout->bAnimated?1:0);PointMaterial->SetScalarParameterValue(TEXT("FrameMix"),1);
    bPreparing=false;SetVisibility(true);
}
void ULightBoxPointCloud::ResizePoints(int32 Count)
{
    if(RenderedPoints==Count&&GetStaticMesh())return;
    ClearInstances();RenderedPoints=Count;if(Count<=0)return;
    // One instance contains all billboards. UV1 carries the exact grain ID, avoiding
    // per-grain GPU Scene transforms/culling while preserving all original grains.
    FMeshDescription Description;FStaticMeshAttributes Attributes(Description);Attributes.Register();
    Description.ReserveNewVertices(Count*3);Description.ReserveNewVertexInstances(Count*3);Description.ReserveNewEdges(Count*3);Description.ReserveNewTriangles(Count);Description.ReserveNewPolygons(Count);
    auto Positions=Attributes.GetVertexPositions();auto UV=Attributes.GetVertexInstanceUVs();UV.SetNumChannels(2);
    auto Normals=Attributes.GetVertexInstanceNormals();auto Tangents=Attributes.GetVertexInstanceTangents();auto Signs=Attributes.GetVertexInstanceBinormalSigns();
    const auto Group=Description.CreatePolygonGroup();Attributes.GetPolygonGroupMaterialSlotNames()[Group]=TEXT("PointSculpture");
    const FVector2f Corners[]={FVector2f(-.375f,0),FVector2f(1.375f,0),FVector2f(.5f,1.5f)};
    for(int32 I=0;I<Count;++I){FVertexInstanceID Triangle[3];
        for(int32 J=0;J<3;++J){const auto V=Description.CreateVertex();const auto VI=Description.CreateVertexInstance(V);Triangle[J]=VI;
            Positions[V]=FVector3f((Corners[J].X-.5f)*100,(Corners[J].Y-.5f)*100,0);UV.Set(VI,0,Corners[J]);UV.Set(VI,1,FVector2f(float(I),0));Normals[VI]=FVector3f(0,0,1);Tangents[VI]=FVector3f(1,0,0);Signs[VI]=1;}
        Description.CreateTriangle(Group,MakeArrayView(Triangle));}
    auto* Mesh=NewObject<UStaticMesh>(this,NAME_None,RF_Transient);Mesh->GetStaticMaterials().Add(FStaticMaterial(PointMaterial,TEXT("PointSculpture")));
    UStaticMesh::FBuildMeshDescriptionsParams Params;Params.bFastBuild=true;Params.bMarkPackageDirty=false;Params.bCommitMeshDescription=false;Params.PerLODOverrides.SetNum(1);Params.PerLODOverrides[0].bUseFullPrecisionUVs=true;
    Mesh->BuildFromMeshDescriptions({&Description},Params);SetStaticMesh(Mesh);
    AddInstance(FTransform::Identity,false);MarkRenderStateDirty();
}
UTexture2D* ULightBoxPointCloud::MakeOrderTexture(int32 Count,int32 Width)
{
    auto* T=UTexture2D::CreateTransient(Width,Width,PF_R32_FLOAT);T->SRGB=false;T->NeverStream=true;T->Filter=TF_Nearest;
    auto& Mip=T->GetPlatformData()->Mips[0];auto* Data=static_cast<float*>(Mip.BulkData.Lock(LOCK_READ_WRITE));FMemory::Memzero(Data,Width*Width*sizeof(float));
    for(int32 I=0;I<Count;++I)Data[I]=float(I);Mip.BulkData.Unlock();T->UpdateResource();return T;
}
void ULightBoxPointCloud::UploadOrder(const TArray<float>& Order)
{
    const int32 W=OrderTexture->GetSizeX();auto* Bytes=new uint8[W*W*sizeof(float)]();FMemory::Memcpy(Bytes,Order.GetData(),Order.Num()*sizeof(float));auto* Region=new FUpdateTextureRegion2D(0,0,0,0,W,W);
    OrderTexture->UpdateTextureRegions(0,1,Region,W*sizeof(float),sizeof(float),Bytes,[](uint8* P,const FUpdateTextureRegion2D* R){delete[] P;delete R;});
}
UTexture2D* ULightBoxPointCloud::Texture(const TArray<FLightBoxPoint>& Points,int32 Width,bool Color)
{
    auto* T=UTexture2D::CreateTransient(Width,Width,Color?PF_B8G8R8A8:PF_FloatRGBA);
    T->SRGB=false;T->NeverStream=true;T->Filter=TF_Nearest;T->AddressX=TA_Clamp;T->AddressY=TA_Clamp;
    auto& Mip=T->GetPlatformData()->Mips[0];void* Data=Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memzero(Data,Width*Width*(Color?sizeof(FColor):sizeof(FFloat16Color)));
    for(int32 I=0;I<Points.Num();++I){const auto& P=Points[I];
        if(Color)static_cast<FColor*>(Data)[I]=P.Color.ToFColor(false);
        else static_cast<FFloat16Color*>(Data)[I]=FFloat16Color(FLinearColor(P.Position.X,P.Position.Y,P.Position.Z,P.bFollow?-P.Radius:P.Radius));
    }
    Mip.BulkData.Unlock();T->UpdateResource();return T;
}
UTexture2D* ULightBoxPointCloud::ColourTexture(const TArray<FLinearColor>& Colours,int32 Width)
{
    auto* T=UTexture2D::CreateTransient(Width,Width,PF_B8G8R8A8);T->SRGB=false;T->NeverStream=true;T->Filter=TF_Nearest;T->AddressX=TA_Clamp;T->AddressY=TA_Clamp;
    auto& Mip=T->GetPlatformData()->Mips[0];auto* Data=static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));FMemory::Memzero(Data,Width*Width*sizeof(FColor));
    for(int32 I=0;I<Colours.Num();++I)Data[I]=Colours[I].ToFColor(false);
    Mip.BulkData.Unlock();T->UpdateResource();return T;
}
UTexture2D* ULightBoxPointCloud::BindingsTexture(bool Weights,int32 Width)
{
    auto* T=UTexture2D::CreateTransient(Width,Width,Weights?PF_FloatRGBA:PF_A32B32G32R32F);T->SRGB=false;T->NeverStream=true;T->Filter=TF_Nearest;
    auto& Mip=T->GetPlatformData()->Mips[0];void* Data=Mip.BulkData.Lock(LOCK_READ_WRITE);FMemory::Memzero(Data,Width*Width*(Weights?sizeof(FFloat16Color):sizeof(FVector4f)));
    for(int32 I=0;I<Goals.Num();++I){const auto& P=Goals[I];if(Weights)static_cast<FFloat16Color*>(Data)[I]=FFloat16Color((P.AffineFourth>=0?FLinearColor(P.LocalCoordinates.X,P.LocalCoordinates.Y,P.LocalCoordinates.Z,1):FLinearColor(P.Weights.X,P.Weights.Y,0,0)));else static_cast<FVector4f*>(Data)[I]=FVector4f(P.Vertices.X,P.Vertices.Y,P.Vertices.Z,P.AffineFourth);}
    Mip.BulkData.Unlock();T->UpdateResource();return T;
}
UTexture2D* ULightBoxPointCloud::VertexTexture(const TArray<FVector4f>& Vertices,int32 Width)
{
    // Adjacent rows hold From/To. One hardware-filtered sample performs temporal interpolation.
    auto* T=UTexture2D::CreateTransient(Width,Width*2,PF_FloatRGBA);T->SRGB=false;T->NeverStream=true;T->Filter=TF_Bilinear;T->AddressX=TA_Clamp;T->AddressY=TA_Clamp;
    auto& Mip=T->GetPlatformData()->Mips[0];auto* Data=static_cast<FFloat16Color*>(Mip.BulkData.Lock(LOCK_READ_WRITE));FMemory::Memzero(Data,Width*Width*2*sizeof(FFloat16Color));
    for(int32 I=0;I<Vertices.Num();++I){const auto& V=Vertices[I];const int32 At=(I/Width)*2*Width+I%Width;Data[At]=Data[At+Width]=FFloat16Color(FLinearColor(V.X,V.Y,V.Z,V.W));}
    Mip.BulkData.Unlock();T->UpdateResource();return T;
}
void ULightBoxPointCloud::UploadVertices(UTexture2D* T,const TArray<FVector4f>& From,const TArray<FVector4f>& To)
{
    const int32 W=T->GetSizeX(),H=T->GetSizeY();auto* Bytes=new uint8[W*H*sizeof(FFloat16Color)]();auto* Data=reinterpret_cast<FFloat16Color*>(Bytes);
    for(int32 I=0;I<To.Num();++I){const auto& A=From[I];const auto& B=To[I];const int32 At=(I/W)*2*W+I%W;
        Data[At]=FFloat16Color(FLinearColor(A.X,A.Y,A.Z,A.W));Data[At+W]=FFloat16Color(FLinearColor(B.X,B.Y,B.Z,B.W));}
    auto* Region=new FUpdateTextureRegion2D(0,0,0,0,W,H);
    T->UpdateTextureRegions(0,1,Region,W*sizeof(FFloat16Color),sizeof(FFloat16Color),Bytes,[](uint8* P,const FUpdateTextureRegion2D* R){delete[] P;delete R;});
}
FVector3f ULightBoxPointCloud::CurrentDestination(int32 I) const
{
    return LightBoxPoints::Destination(Goals[I],FrameFrom,FrameTo,FMath::Clamp(FrameAge*30,0.f,1.f));
}
void ULightBoxPointCloud::RefreshFrame(const FVector2D& Tilt,const FLightBoxSimulation* Field)
{
    if(bFramePending||FrameClock<1.f/30)return;FrameClock=FMath::Fmod(FrameClock,1.f/30);bFramePending=true;
    TArray<FVector4f> Motes;if(Field)for(const auto& P:Field->Motes)Motes.Add(FVector4f(P.Position.X,P.Position.Y,P.Position.Z,.65f+.35f*FMath::Sin(Age*2+P.Phase)));
    const uint32 Request=Serial;auto Graph=Layout;auto Bindings=CacheBindings;auto Scene=Settings;const float Time=AnimationAge,Scatter=Burst,StoryTime=FMath::Max(0.f,Age-FMath::Max(1.f,Settings.AssembleSeconds));
    TWeakObjectPtr<ULightBoxPointCloud> Weak(this);
    Async(EAsyncExecution::ThreadPool,[Weak,Graph,Bindings,Scene,Request,Time,Tilt,Scatter,StoryTime,Motes=MoveTemp(Motes)](){
        auto Vertices=LightBoxPoints::Frame(*Graph,Scene,Time,Tilt,Scatter,Motes,StoryTime);
        auto Cached=Bindings.IsValid()?LightBoxPoints::Bake(*Bindings,Vertices):TArray<FVector4f>();
        AsyncTask(ENamedThreads::GameThread,[Weak,Request,Vertices=MoveTemp(Vertices),Cached=MoveTemp(Cached)]()mutable{
            if(!Weak.IsValid()||Weak->Serial!=Request)return;auto* C=Weak.Get();C->bFramePending=false;
            if(Vertices.Num()!=C->FrameTo.Num())return;
            const float Mix=FMath::Clamp(C->FrameAge*30,0.f,1.f);for(int32 I=0;I<C->FrameFrom.Num();++I)C->FrameFrom[I]=FMath::Lerp(C->FrameFrom[I],C->FrameTo[I],Mix);
            C->FrameTo=MoveTemp(Vertices);
            if(C->bCacheDestinations){for(int32 I=0;I<C->CacheFrom.Num();++I)C->CacheFrom[I]=FMath::Lerp(C->CacheFrom[I],C->CacheTo[I],Mix);C->CacheTo=MoveTemp(Cached);}
            C->FrameAge=C->bFixedReviewTime?1:0;
            C->UploadVertices(C->MotionTextures[2],C->bCacheDestinations?C->CacheFrom:C->FrameFrom,C->bCacheDestinations?C->CacheTo:C->FrameTo);

        });
    });
}
void ULightBoxPointCloud::RefreshDrawOrder(const FVector& Right,const FVector& Up)
{
    if(bPreparing||bSortPending||Progress()<1||Goals.IsEmpty())return;
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("LightBoxUnsorted")))return;
#endif
    const FVector3f Forward(GetComponentTransform().InverseTransformVectorNoScale(FVector::CrossProduct(Right,Up)).GetSafeNormal());
    if(bSorted&&FVector3f::DotProduct(Forward,SortedForward)>.996f&&(!Layout->bAnimated||SortClock<.75f))return;
    TArray<FVector3f> Positions;Positions.SetNumUninitialized(Goals.Num());
    for(int32 I=0;I<Goals.Num();++I)Positions[I]=bCacheDestinations?(*CacheBindings)[I].Rest+FVector3f(CacheTo[I]):CurrentDestination(I);
    bSortPending=true;SortClock=0;const uint32 Request=Serial;TWeakObjectPtr<ULightBoxPointCloud> Weak(this);
    Async(EAsyncExecution::ThreadPool,[Weak,Request,Forward,Positions=MoveTemp(Positions)](){auto Order=LightBoxPoints::DepthOrder(Positions,Forward);
        AsyncTask(ENamedThreads::GameThread,[Weak,Request,Forward,Order=MoveTemp(Order)](){
            if(!Weak.IsValid()||Weak->Serial!=Request)return;auto* C=Weak.Get();C->bSortPending=false;
            if(Order.Num()!=C->PointTotal())return;
            // Update only submission IDs, retaining the proxy, geometry and every original grain.
            C->UploadOrder(Order);C->bSorted=true;C->SortedForward=Forward;
        });
    });
}
FVector ULightBoxPointCloud::WorldCentre() const
{
    return GetComponentTransform().TransformPosition(FMath::Lerp(CenterFrom,CenterTo,Progress()));
}
void ULightBoxPointCloud::AddWave(const FVector& WorldDir,float Strength,float Lead)
{
    if(Settings.Shake==TEXT("none")||Strength<=0)return;
    // A new front takes the older slot, so quick claps overlap instead of being dropped.
    const int32 Slot=WaveAge[0]>WaveAge[1]?0:1;
    WaveDir[Slot]=WorldDir.IsNearlyZero()?FVector::ZeroVector:GetComponentTransform().InverseTransformVectorNoScale(WorldDir).GetSafeNormal();
    WaveAge[Slot]=-Lead;WaveStrength[Slot]=FMath::Clamp(Strength,0.f,1.f)*(Settings.Shake==TEXT("gather")?-1.f:1.f);
}
float ULightBoxPointCloud::Breath() const
{
    if(Think<=.001f)return 0;
    const float Beat=.5f+.5f*FMath::Sin(Age*2.4f);
    return Think*(.03f+.05f*Beat*Beat);
}
void ULightBoxPointCloud::Advance(float Dt,const FVector2D& Tilt,const FVector& Right,const FVector& Up,const FLightBoxSimulation* Field)
{
    if(!PointMaterial)return;
    LastTilt=Tilt;SortClock+=Dt;Age+=Dt;AnimationAge+=Dt;OrientationAge+=Dt;FrameClock+=Dt;FrameAge+=Dt;Burst=FMath::Max(0.f,Burst-Dt*.65f);
    Think=FMath::FInterpTo(Think,bThinking?1.f:0.f,Dt,bThinking?1.2f:3.f);
    SensorClock=FMath::Fmod(SensorClock+Dt,1000.f);
    for(int32 K=0;K<2;++K)if(WaveAge[K]>-50){WaveAge[K]+=Dt;if(WaveAge[K]>3.5f){WaveAge[K]=-100;WaveStrength[K]=0;}}
    if(!bPreparing&&Layout.IsValid()&&Layout->bAnimated)RefreshFrame(Tilt,Field);
    PointMaterial->SetScalarParameterValue(TEXT("FrameMix"),FMath::Clamp(FrameAge*30,0.f,1.f));
    if(Progress()>=1&&!StoryColourTextures.IsEmpty()){
        const auto F=LightBoxAnimation::Interval(Settings,FMath::Max(0.f,Age-FMath::Max(1.f,Settings.AssembleSeconds)));
        if(ColourFrom!=F.From||ColourTo!=F.To){ColourFrom=F.From;ColourTo=F.To;PointMaterial->SetTextureParameterValue(TEXT("FromColor"),StoryColourTextures[F.From]);PointMaterial->SetTextureParameterValue(TEXT("ToColor"),StoryColourTextures[F.To]);}
        PointMaterial->SetScalarParameterValue(TEXT("ColourPhase"),F.Mix);
    }
#if !UE_BUILD_SHIPPING
    const double Now=FPlatformTime::Seconds();const float MeasuredDt=LastReviewTick>0?float(Now-LastReviewTick):Dt;LastReviewTick=Now;
    float ReviewPhase;if(FParse::Value(FCommandLine::Get(),TEXT("PointCloudPhase="),ReviewPhase))Age=FMath::Clamp(ReviewPhase,0.f,1.f)*Settings.AssembleSeconds;
    FString Report;if(!bReviewWritten&&!bPreparing&&Age>=Settings.AssembleSeconds&&FParse::Value(FCommandLine::Get(),TEXT("PointCloudReport="),Report)){
        ReviewElapsed+=MeasuredDt;ReviewFrames.Add(MeasuredDt*1000);
        if(ReviewElapsed>=8){
            bReviewWritten=true;ReviewFrames.Sort();auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("retainedPoints"),Retained);J->SetNumberField(TEXT("points"),Settings.PointCount);J->SetNumberField(TEXT("instances"),GetInstanceCount());J->SetNumberField(TEXT("billboards"),PointTotal());
            J->SetNumberField(TEXT("fps"),ReviewFrames.Num()/ReviewElapsed);J->SetNumberField(TEXT("frameMsP95"),ReviewFrames[FMath::Clamp(FMath::FloorToInt(ReviewFrames.Num()*.95),0,ReviewFrames.Num()-1)]);
            J->SetNumberField(TEXT("frameMsMax"),ReviewFrames.Last());J->SetNumberField(TEXT("seconds"),ReviewElapsed);J->SetBoolField(TEXT("gpuVertexAnimation"),true);
            TArray<TSharedPtr<FJsonValue>> OtherPrimitives;int32 Solids=0;for(TActorIterator<AActor> A(GetWorld());A;++A)if(!A->IsHidden()){TArray<UPrimitiveComponent*> Components;A->GetComponents(Components);for(auto* C:Components)if(C->IsRegistered()&&C->IsVisible()&&!C->bHiddenInGame&&!Cast<ULightBoxPointCloud>(C)){if(Cast<UMeshComponent>(C))++Solids;OtherPrimitives.Add(MakeShared<FJsonValueString>(C->GetFullName()));}}
            J->SetArrayField(TEXT("otherPrimitives"),OtherPrimitives);J->SetBoolField(TEXT("particleOnly"),true);J->SetNumberField(TEXT("solidSceneComponents"),Solids);J->SetBoolField(TEXT("animatedTarget"),Layout.IsValid()&&Layout->bAnimated);J->SetNumberField(TEXT("controlVertices"),FrameTo.Num());J->SetBoolField(TEXT("cachedDestinations"),bCacheDestinations);J->SetBoolField(TEXT("depthOrdered"),bSorted);
            float Travel=0;if(Layout.IsValid())for(int32 I=0;I<FrameTo.Num();++I)Travel=FMath::Max(Travel,FVector3f::Distance(FVector3f(FrameTo[I]),FVector3f(Layout->InitialVertices[I])));J->SetNumberField(TEXT("controlTravel"),Travel);

            FString Json;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Json));IFileManager::Get().MakeDirectory(*FPaths::GetPath(Report),true);FFileHelper::SaveStringToFile(Json,*Report);
        }
    }
#endif
    if(!bTrimmed&&Progress()>=1){bTrimmed=true;if(Goals.Num()>Settings.PointCount){Goals.SetNum(Settings.PointCount);Sources.SetNum(Settings.PointCount);ResizePoints(Settings.PointCount);}}
    RefreshDrawOrder(Right,Up);
    PointMaterial->SetScalarParameterValue(TEXT("Phase"),Progress());
    // Tilt moves the grains, not the view: they slide downhill (more the higher they sit) and loosen a little.
    const float Lean=FMath::Min(1.f,float(Tilt.Size()));
    PointMaterial->SetVectorParameterValue(TEXT("Path"),FLinearColor(Settings.AssembleArc,Settings.AssembleTwist,Settings.AssembleStagger,Burst+.06f*Lean*Settings.Tilt+Breath()));
    const FVector Center=FMath::Lerp(CenterFrom,CenterTo,Progress());PointMaterial->SetVectorParameterValue(TEXT("Center"),FLinearColor(Center.X,Center.Y,Center.Z,0));
    const float Slide=Settings.Tilt*.42f;
    auto VectorParameter=[&](const TCHAR* Name,const FVector& V){PointMaterial->SetVectorParameterValue(Name,FLinearColor(V.X,V.Y,V.Z,0));};
    VectorParameter(TEXT("AxisX"),FVector::XAxisVector);VectorParameter(TEXT("AxisY"),FVector::YAxisVector);VectorParameter(TEXT("AxisZ"),FVector(Tilt.Y*Slide,-Tilt.X*Slide,1));
    VectorParameter(TEXT("Right"),Right);VectorParameter(TEXT("Up"),Up);
    {
        const FTransform& T=GetComponentTransform();
        auto Local=[&](const FVector& V){return V.IsNearlyZero()?FVector::ZeroVector:T.InverseTransformVectorNoScale(V).GetSafeNormal();};
        auto Vector4=[&](const TCHAR* Name,const FVector& V,float W){PointMaterial->SetVectorParameterValue(Name,FLinearColor(V.X,V.Y,V.Z,W));};
        Vector4(TEXT("Wave0"),WaveDir[0],WaveAge[0]);Vector4(TEXT("Wave1"),WaveDir[1],WaveAge[1]);
        PointMaterial->SetVectorParameterValue(TEXT("WaveForce"),FLinearColor(WaveStrength[0],WaveStrength[1],WaveSpeed(),Reach()));
        Vector4(TEXT("Wind"),Local(WindWorld),WindAmount);Vector4(TEXT("Pull"),Local(PullWorld),PullAmount);
        Vector4(TEXT("Heat"),T.InverseTransformPosition(HeatWorld),HeatAmount);
        PointMaterial->SetScalarParameterValue(TEXT("SensorClock"),SensorClock);
    }
    const auto Transform=GetComponentTransform();VectorParameter(TEXT("Origin"),Transform.GetLocation());
    VectorParameter(TEXT("ObjectX"),Transform.TransformVector(FVector::XAxisVector));VectorParameter(TEXT("ObjectY"),Transform.TransformVector(FVector::YAxisVector));VectorParameter(TEXT("ObjectZ"),Transform.TransformVector(FVector::ZAxisVector));
}
void ULightBoxPointCloud::HideCloud(){++Serial;bPreparing=false;SetVisibility(false);Sources.Reset();Goals.Reset();ClearInstances();RenderedPoints=0;OrderTexture=nullptr;DataTextures.Reset();MotionTextures.Reset();StoryColourTextures.Reset();Layout.Reset();FrameFrom.Reset();FrameTo.Reset();CacheFrom.Reset();CacheTo.Reset();CacheBindings.Reset();}
void ULightBoxPointCloud::OnComponentDestroyed(bool Hierarchy){++Serial;Super::OnComponentDestroyed(Hierarchy);}
