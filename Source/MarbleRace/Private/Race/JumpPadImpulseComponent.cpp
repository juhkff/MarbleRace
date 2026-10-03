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

	// 按弹珠记录，不同碰撞体、不同接触点不能绕过同一颗球的冷却。
	const double Now = GetWorld()->GetTimeSeconds();
	const TWeakObjectPtr<AActor> Key(OtherActor);
	if (const double* LastLaunch = LastLaunchTimes.Find(Key);
		LastLaunch && Now - *LastLaunch < FMath::Max(0.f, LaunchCooldownSeconds))
	{
		return;
	}
	LastLaunchTimes.Add(Key, Now);

	// 只绕世界 Y 轴偏转：侧视关卡的随机方向留在 X-Z 平面，不随机推向镜头。
	const float Angle = FMath::FRandRange(-FMath::Abs(RandomAngleDegrees), FMath::Abs(RandomAngleDegrees));
	const FVector RandomDirection = Direction.RotateAngleAxis(Angle, FVector::YAxisVector);
	const float RandomOffset = FMath::FRandRange(-FMath::Abs(RandomStrengthRange), FMath::Abs(RandomStrengthRange));
	const float Speed = FMath::Max(0.f, LaunchStrength + RandomOffset);

	// 每次命中重新抽样，直接覆盖原有线速度，而不是和原速度叠加。
	OtherComp->SetPhysicsLinearVelocity(RandomDirection * Speed, false);
}
