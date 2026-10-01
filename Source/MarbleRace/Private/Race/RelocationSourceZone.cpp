#include "Race/RelocationSourceZone.h"

#include "Components/ChildActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Race/RelocationManagerActor.h"
#include "RelocationManagerComponent.h"
#include "MarbleRestitution.h"
#include "UObject/ConstructorHelpers.h"

ARelocationSourceZone::ARelocationSourceZone()
{
	PrimaryActorTick.bCanEverTick = false;
	static ConstructorHelpers::FClassFinder<AActor> MarbleFinder(TEXT("/Game/角色/弹珠"));
	if (MarbleFinder.Succeeded())
	{
		MarbleClass = MarbleFinder.Class;
	}
}

bool ARelocationSourceZone::ApplyCrossingRestitution(AActor* Marble, UPrimitiveComponent* Body)
{
	if (!bEnableRestitutionChange || !FMath::IsFinite(RestitutionAfterCrossing) ||
		!IsValid(Marble) || !MarbleClass || !Marble->IsA(MarbleClass) ||
		!IsValid(Body) || Body->GetOwner() != Marble || !Body->IsSimulatingPhysics())
	{
		return false;
	}

	if (!MarbleRace::SetBodyRestitution(Body, RestitutionAfterCrossing))
	{
		return false;
	}
	UE_LOG(LogTemp, Log, TEXT("过线区域 %s：弹珠 %s 恢复力设置为 %.3f"),
		*GetName(), *Marble->GetName(), FMath::Clamp(RestitutionAfterCrossing, 0.f, 1.f));
	return true;
}

ARelocationManagerActor* ARelocationSourceZone::ResolveRelocationManager() const
{
	if (IsValid(RelocationManager))
	{
		return RelocationManager;
	}

	// 区域常被别的蓝图当子 Actor 用，而蓝图默认值没法引用关卡里的管理器实例，
	// 所以这里顺着父 Actor 找同一蓝图内的重定位管理器子 Actor。
	AActor* Parent = GetAttachParentActor();
	if (!Parent)
	{
		Parent = GetOwner();
	}
	if (Parent)
	{
		TArray<UChildActorComponent*> ChildComponents;
		Parent->GetComponents<UChildActorComponent>(ChildComponents);
		for (const UChildActorComponent* Component : ChildComponents)
		{
			AActor* Child = Component ? Component->GetChildActor() : nullptr;
			if (Child && Child != this)
			{
				if (ARelocationManagerActor* Manager = Cast<ARelocationManagerActor>(Child))
				{
					return Manager;
				}
			}
		}
	}

	// 关卡里单独摆放的区域没有父 Actor 可查，退而用最近的管理器；一个区域一般只有一个。
	ARelocationManagerActor* Nearest = nullptr;
	float NearestDistanceSquared = TNumericLimits<float>::Max();
	for (TActorIterator<ARelocationManagerActor> It(GetWorld()); It; ++It)
	{
		ARelocationManagerActor* Manager = *It;
		if (!IsValid(Manager))
		{
			continue;
		}
		const float DistanceSquared = FVector::DistSquared(Manager->GetActorLocation(), GetActorLocation());
		if (DistanceSquared < NearestDistanceSquared)
		{
			NearestDistanceSquared = DistanceSquared;
			Nearest = Manager;
		}
	}
	if (Nearest)
	{
		UE_LOG(LogTemp, Log, TEXT("重定位入口 %s 未指定重定位Manager，自动使用最近的管理器 %s"),
		       *GetName(), *Nearest->GetName());
	}
	return Nearest;
}

void ARelocationSourceZone::BeginPlay()
{
	Super::BeginPlay();

	if (!IsValid(RelocationManager))
	{
		RelocationManager = ResolveRelocationManager();
	}
	if (!IsValid(RelocationManager))
	{
		UE_LOG(LogTemp, Error, TEXT("重定位入口 %s 未指定重定位Manager，弹珠不会被传送"), *GetName());
	}

	// 此刻没找到也照样绑定：父蓝图稍后再赋值时，入口仍然有效。
	if (UStaticMeshComponent* Mesh = GetStaticMeshComponent())
	{
		Mesh->OnComponentBeginOverlap.AddDynamic(this, &ARelocationSourceZone::HandleEntrance);
	}
}

void ARelocationSourceZone::HandleEntrance(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                           UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
                                           bool bFromSweep, const FHitResult& SweepResult)
{
	// Apply before enqueueing disables physics; the body's override survives relocation.
	ApplyCrossingRestitution(OtherActor, OtherComp);
	if (!IsValid(RelocationManager))
	{
		RelocationManager = ResolveRelocationManager();
	}
	if (IsValid(RelocationManager))
	{
		if (URelocationManagerComponent* Component = RelocationManager->GetRelocationComponent())
		{
			UE_LOG(LogTemp, Log, TEXT("重定位入口 %s：弹珠 %s 进入，交给管理器 %s"),
			       *GetName(), *GetNameSafe(OtherActor), *RelocationManager->GetName());
			Component->EnqueueMarble(OtherActor, OtherComp, TrapNumber);
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
