#include "Race/RelocationSourceZone.h"

#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Race/RelocationManagerActor.h"
#include "RelocationManagerComponent.h"

ARelocationSourceZone::ARelocationSourceZone()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ARelocationSourceZone::BeginPlay()
{
	Super::BeginPlay();
	if (!IsValid(RelocationManager))
	{
		UE_LOG(LogTemp, Error, TEXT("重定位入口 %s 未指定重定位Manager，弹珠不会被传送"), *GetName());
		return;
	}
	if (UStaticMeshComponent* Mesh = GetStaticMeshComponent())
	{
		Mesh->OnComponentBeginOverlap.AddDynamic(this, &ARelocationSourceZone::HandleEntrance);
	}
}

void ARelocationSourceZone::HandleEntrance(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                           UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
                                           bool bFromSweep, const FHitResult& SweepResult)
{
	if (IsValid(RelocationManager))
	{
		if (URelocationManagerComponent* Component = RelocationManager->GetRelocationComponent())
		{
			Component->EnqueueMarble(OtherActor, OtherComp);
		}
	}
}

void ARelocationSourceZone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UStaticMeshComponent* Mesh = GetStaticMeshComponent())
	{
		Mesh->OnComponentBeginOverlap.RemoveDynamic(this, &ARelocationSourceZone::HandleEntrance);
	}
	Super::EndPlay(EndPlayReason);
}
