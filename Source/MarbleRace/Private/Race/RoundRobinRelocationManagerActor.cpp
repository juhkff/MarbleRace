#include "Race/RoundRobinRelocationManagerActor.h"

ARoundRobinRelocationManagerActor::ARoundRobinRelocationManagerActor()
{
	bUsesRespawnLocationArray = true;
}

bool ARoundRobinRelocationManagerActor::TryRollRespawnLocation(AActor* Marble, FVector& OutPosition) const
{
	if (!IsValid(Marble) || RespawnLocations.IsEmpty())
	{
		return false;
	}
	const int32 Index = NextRespawnIndices.FindRef(TWeakObjectPtr<AActor>(Marble)) % RespawnLocations.Num();
	OutPosition = RollRespawnLocationAt(RespawnLocations[Index]);
	return true;
}

void ARoundRobinRelocationManagerActor::CommitMarbleRespawn(AActor* Marble)
{
	for (auto It = NextRespawnIndices.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	if (IsValid(Marble) && !RespawnLocations.IsEmpty())
	{
		int32& Index = NextRespawnIndices.FindOrAdd(TWeakObjectPtr<AActor>(Marble));
		Index = (Index % RespawnLocations.Num() + 1) % RespawnLocations.Num();
	}
}

void ARoundRobinRelocationManagerActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	NextRespawnIndices.Empty();
	Super::EndPlay(EndPlayReason);
}
