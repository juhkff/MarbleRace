#include "Race/JumpPadImpulseComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

UJumpPadImpulseComponent::UJumpPadImpulseComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	static ConstructorHelpers::FClassFinder<AActor> MarbleFinder(TEXT("/Game/角色/弹珠"));
	if (MarbleFinder.Succeeded())
	{
		MarbleClass = MarbleFinder.Class;
	}
}

void UJumpPadImpulseComponent::BeginPlay()
{
	Super::BeginPlay();

	PadCollision = GetOwner() ? Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent()) : nullptr;
	if (PadCollision)
	{
		// 实体碰撞保持 Block；仅启用物理碰撞通知，不修改跳板的碰撞响应。
		PadCollision->SetNotifyRigidBodyCollision(true);
		PadCollision->OnComponentHit.AddDynamic(this, &UJumpPadImpulseComponent::HandlePadHit);
	}
}

void UJumpPadImpulseComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PadCollision)
	{
		PadCollision->OnComponentHit.RemoveDynamic(this, &UJumpPadImpulseComponent::HandlePadHit);
		PadCollision = nullptr;
	}
	LastLaunchTimes.Empty();
	Super::EndPlay(EndPlayReason);
}

void UJumpPadImpulseComponent::HandlePadHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
                                            UPrimitiveComponent* OtherComp, FVector NormalImpulse,
                                            const FHitResult& Hit)
{
	if (!MarbleClass || !OtherActor || !OtherActor->IsA(MarbleClass) ||
	    !OtherComp || !OtherComp->IsSimulatingPhysics() || LaunchStrength <= 0.f)
	{
		return;
	}

	const FVector Direction = LaunchDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	// Chaos 在持续接触时可能一帧发出多次命中，短暂去重避免反复弹射。
	const double Now = GetWorld()->GetTimeSeconds();
	const TWeakObjectPtr<UPrimitiveComponent> Key(OtherComp);
	if (const double* LastLaunch = LastLaunchTimes.Find(Key); LastLaunch && Now - *LastLaunch < 0.15)
	{
		return;
	}
	LastLaunchTimes.Add(Key, Now);

	// bVelChange=true：力度表示速度增量，和弹珠的质量无关。
	OtherComp->AddImpulse(Direction * LaunchStrength, NAME_None, true);
}
