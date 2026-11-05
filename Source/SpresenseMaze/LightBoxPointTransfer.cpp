#include "LightBoxPointData.h"
#include "LightBoxSimulation.h"

namespace
{
    uint32 Spread(uint32 X){X&=1023;X=(X|(X<<16))&0x030000FF;X=(X|(X<<8))&0x0300F00F;X=(X|(X<<4))&0x030C30C3;return (X|(X<<2))&0x09249249;}
    uint32 SpatialKey(const FVector3f& P){auto Q=[](float V){return uint32(FMath::Clamp(FMath::FloorToInt((V+1024)*.5f),0,1023));};return Spread(Q(P.X))|(Spread(Q(P.Y))<<1)|(Spread(Q(P.Z))<<2);}
    bool SameMotion(const FLightBoxObject& O,const FLightBoxStyle& Before,const FLightBoxStyle& After)
    {
        const FLightBoxObject* Next=&O;
        for(int32 Depth=0;Depth<9&&Next;++Depth){
            const auto* Old=Before.Objects.FindByPredicate([&](const auto& X){return X.Id==Next->Id;});
            if(!Old||FLightBoxStyle::ObjectMotionKey(*Old)!=FLightBoxStyle::ObjectMotionKey(*Next))return false;
            if(Next->Parent.IsEmpty())return true;
            const FString Parent=Next->Parent;Next=After.Objects.FindByPredicate([&](const auto& X){return X.Id==Parent;});
        }return false;
    }
}

FLightBoxPointTransfer LightBoxPoints::Transfer(const TArray<FLightBoxPoint>& Current,TArray<FLightBoxPoint> Next,const FLightBoxStyle& Before,const FLightBoxStyle& After)
{
    FLightBoxPointTransfer R;R.To=MoveTemp(Next);R.From.SetNum(R.To.Num());R.SourceIndices.Init(INDEX_NONE,R.To.Num());
    TBitArray<> Used(false,Current.Num()),Assigned(false,R.To.Num());
    TMap<FName,TArray<int32>> ByOwner;
    for(int32 I=0;I<Current.Num();++I)if(!Current[I].Owner.IsNone())ByOwner.FindOrAdd(Current[I].Owner).Add(I);
    TSet<FName> Stable;
    for(const auto& O:After.Objects)if(SameMotion(O,Before,After))Stable.Add(FName(*O.Id));
    Stable.Add(TEXT("__sky"));
    if(FLightBoxSimulation::Signature(Before)==FLightBoxSimulation::Signature(After))Stable.Add(TEXT("__field"));
    // Local sample IDs do not depend on a part's order or on another part's quota.
    for(int32 I=0;I<R.To.Num();++I){auto& P=R.To[I];if(!Stable.Contains(P.Owner))continue;
        const auto* Old=ByOwner.Find(P.Owner);if(!Old||!Old->IsValidIndex(P.Sample))continue;
        const int32 J=(*Old)[P.Sample];if(Used[J]||Current[J].Sample!=P.Sample)continue;
        R.From[I]=Current[J];R.From[I].bFollow=false;R.SourceIndices[I]=J;P.bFollow=true;Used[J]=true;Assigned[I]=true;++R.Retained;
    }
    struct FEntry{uint32 Key;int32 Index;};TArray<FEntry> Departing,Arriving;
    for(int32 I=0;I<Current.Num();++I)if(!Used[I])Departing.Add({SpatialKey(Current[I].Position),I});
    for(int32 I=0;I<R.To.Num();++I)if(!Assigned[I])Arriving.Add({SpatialKey(R.To[I].Position),I});
    auto Less=[](const FEntry& A,const FEntry& B){return A.Key==B.Key?A.Index<B.Index:A.Key<B.Key;};
    Departing.Sort(Less);Arriving.Sort(Less);
    // Locality-preserving one-to-one matching: O(n log n), never n squared at a million points.
    const int32 Pairs=FMath::Min(Departing.Num(),Arriving.Num());
    for(int32 K=0;K<Pairs;++K){const int32 J=Departing[K].Index,I=Arriving[K].Index;R.From[I]=Current[J];R.From[I].bFollow=false;R.SourceIndices[I]=J;Used[J]=true;Assigned[I]=true;++R.Traveling;}
    for(int32 I=0;I<R.To.Num();++I)if(!Assigned[I]){R.From[I]=Scatter(I,R.To[I],FVector::ZeroVector);++R.Traveling;}
    // Excess particles fade at their own current positions; no shared gathering point.
    for(int32 J=0;J<Current.Num();++J)if(!Used[J]){auto A=Current[J];A.bFollow=false;auto B=A;B.Radius=0;B.Owner=NAME_None;B.Vertices=FIntVector(-1,-1,-1);R.From.Add(A);R.To.Add(B);R.SourceIndices.Add(J);}
    return R;
}
