#include "LightBoxModels.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

namespace
{
    // Poly Haven assets are CC0 (no attribution required); credited in Content/LightBox/Models/README.md.
    const LightBoxModels::FEntry Entries[]={
        {TEXT("island_tree_01"),TEXT("realistic mature broadleaf tree: thick gnarled trunk, spreading limbs, dense leafy crown; about 4.7m wide, 5m tall")},
    };
    FCriticalSection CacheLock;
    TMap<FString,TSharedPtr<const FLightBoxModelCloud,ESPMode::ThreadSafe>> Cache;
}

TConstArrayView<LightBoxModels::FEntry> LightBoxModels::Catalog(){return MakeArrayView(Entries);}

bool LightBoxModels::IsKnown(const FString& Id)
{
    for(const auto& E:Entries)if(Id==E.Id)return true;
    return false;
}

TSharedPtr<const FLightBoxModelCloud,ESPMode::ThreadSafe> LightBoxModels::Parse(const TArray<uint8>& Bytes)
{
    constexpr int32 Header=36,Record=14; // int16 xyz | u8 rgb | pad | s8 normal xyz | pad
    if(Bytes.Num()<Header||FMemory::Memcmp(Bytes.GetData(),"LBPT",4)!=0)return nullptr;
    uint32 Version,Count;float Bounds[6];
    FMemory::Memcpy(&Version,Bytes.GetData()+4,4);FMemory::Memcpy(&Count,Bytes.GetData()+8,4);FMemory::Memcpy(Bounds,Bytes.GetData()+12,24);
    if(Version!=1||Count==0||Count>4000000||int64(Bytes.Num())!=Header+int64(Count)*Record)return nullptr;
    for(float B:Bounds)if(!FMath::IsFinite(B))return nullptr;
    auto Cloud=MakeShared<FLightBoxModelCloud,ESPMode::ThreadSafe>();
    Cloud->Min=FVector3f(Bounds[0],Bounds[1],Bounds[2]);Cloud->Max=FVector3f(Bounds[3],Bounds[4],Bounds[5]);
    const FVector3f Centre=(Cloud->Min+Cloud->Max)*.5f,Half=(Cloud->Max-Cloud->Min)*.5f;
    Cloud->Positions.SetNumUninitialized(Count);Cloud->Normals.SetNumUninitialized(Count);Cloud->Colors.SetNumUninitialized(Count);
    for(uint32 I=0;I<Count;++I){
        const uint8* R=Bytes.GetData()+Header+I*Record;int16 Q[3];FMemory::Memcpy(Q,R,6);
        Cloud->Positions[I]=Centre+FVector3f(Q[0],Q[1],Q[2])/32767.f*Half;
        Cloud->Colors[I]=FColor(R[6],R[7],R[8],255);
        Cloud->Normals[I]=FVector3f(int8(R[10]),int8(R[11]),int8(R[12])).GetSafeNormal();
    }
    return Cloud;
}

float LightBoxModels::FitScale(const FLightBoxModelCloud& Cloud,const FVector& Size)
{
    const FVector3f E=Cloud.Extent();
    return FMath::Max(.0001f,FMath::Min3(float(Size.X)/E.X,float(Size.Y)/E.Y,float(Size.Z)/E.Z));
}

TSharedPtr<const FLightBoxModelCloud,ESPMode::ThreadSafe> LightBoxModels::Load(const FString& Id)
{
    if(!IsKnown(Id))return nullptr;
    {FScopeLock Lock(&CacheLock);if(const auto* Found=Cache.Find(Id))return *Found;}
    TArray<uint8> Bytes;TSharedPtr<const FLightBoxModelCloud,ESPMode::ThreadSafe> Cloud;
    if(FFileHelper::LoadFileToArray(Bytes,*(FPaths::ProjectContentDir()/TEXT("LightBox/Models")/(Id+TEXT(".lbpt"))),FILEREAD_Silent))Cloud=Parse(Bytes);
    FScopeLock Lock(&CacheLock);Cache.Add(Id,Cloud);return Cloud;
}
