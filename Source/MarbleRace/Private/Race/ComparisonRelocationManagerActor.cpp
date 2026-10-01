#include "Race/ComparisonRelocationManagerActor.h"

AComparisonRelocationManagerActor::AComparisonRelocationManagerActor()
{
	bUsesRespawnLocationArray = true;
}

void AComparisonRelocationManagerActor::SetMarbleRespawnSource(AActor* Marble, int32 SourceNumber)
{
	for (auto It = SourceNumbers.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid()) It.RemoveCurrent();
	}
	if (IsValid(Marble)) SourceNumbers.Add(Marble, FMath::Max(0, SourceNumber));
}

bool AComparisonRelocationManagerActor::TryRollRespawnLocation(AActor* Marble, FVector& OutPosition) const
{
	if (!IsValid(Marble) || RespawnLocations.IsEmpty()) return false;
	const int32 Number = SourceNumbers.FindRef(TWeakObjectPtr<AActor>(Marble));
	OutPosition = RollRespawnLocationAt(RespawnLocations[Number % RespawnLocations.Num()]);
	return true;
}

void AComparisonRelocationManagerActor::CommitMarbleRespawn(AActor* Marble)
{
	SourceNumbers.Remove(TWeakObjectPtr<AActor>(Marble));
}

void AComparisonRelocationManagerActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SourceNumbers.Empty();
	Super::EndPlay(EndPlayReason);
}
