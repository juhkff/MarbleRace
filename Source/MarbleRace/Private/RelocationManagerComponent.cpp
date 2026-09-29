#include "RelocationManagerComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Race/RelocationManagerActor.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	float GetMarbleRadius(const UPrimitiveComponent* Body)
	{
		const float VisualRadius = FMath::Max(Body->Bounds.BoxExtent.X, Body->Bounds.BoxExtent.Z);
		const FBodyInstance* Instance = Body->GetBodyInstance();
		if (!Instance || !Instance->IsValidBodyInstance())
		{
			return VisualRadius;
		}
		const FVector Extent = Instance->GetBodyBounds().GetExtent();
		return FMath::Max(VisualRadius, FMath::Max(Extent.X, Extent.Z));
	}
}

URelocationManagerComponent::URelocationManagerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;

	static ConstructorHelpers::FClassFinder<AActor> MarbleFinder(TEXT("/Game/角色/弹珠"));
	if (MarbleFinder.Succeeded())
	{
		MarbleClass = MarbleFinder.Class;
	}
}

void URelocationManagerComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!Cast<ARelocationManagerActor>(GetOwner()))
	{
		UE_LOG(LogTemp, Error, TEXT("重定位组件 %s 必须安装在重定位管理器 Actor 上"), *GetName());
		SetComponentTickEnabled(false);
	}
}

void URelocationManagerComponent::EnqueueMarble(AActor* Marble, UPrimitiveComponent* Body)
{
	if (!GetWorld() || !MarbleClass || !Marble || !Marble->IsA(MarbleClass) ||
	    !Body || !Body->IsSimulatingPhysics() ||
	    PendingMarbles.ContainsByPredicate([Marble](const FPendingMarbleRelocation& Entry)
	    {
		    return Entry.Marble == Marble;
	    }))
	{
		return;
	}

	FPendingMarbleRelocation Entry;
	Entry.Marble = Marble;
	Entry.Body = Body;
	Entry.Radius = GetMarbleRadius(Body);
	Entry.PreviousLinearVelocity = Body->GetPhysicsLinearVelocity();
	Entry.PreviousAngularVelocity = Body->GetPhysicsAngularVelocityInRadians();
	Entry.PreviousCollision = Body->GetCollisionEnabled();
	Entry.bWasSimulatingPhysics = true;
	Entry.bHadGravity = Body->IsGravityEnabled();
	Entry.bWasHidden = Marble->IsHidden();

	// 暂停原对象而不是销毁它，保留赛程中的弹珠身份和材质。
	Body->SetSimulatePhysics(false);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marble->SetActorHiddenInGame(true);
	PendingMarbles.Add(Entry);
	UE_LOG(LogTemp, Log, TEXT("重定位管理器 %s：弹珠 %s 入队，等待 %d 颗"),
	       *GetOwner()->GetName(), *Marble->GetName(), PendingMarbles.Num());
}

bool URelocationManagerComponent::IsRespawnClear(const FPendingMarbleRelocation& Entry, const FVector& Position) const
{
	const UWorld* World = GetWorld();
	if (!World || !IsValid(Entry.Marble) || Entry.Radius <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// 手工检查所有可见弹珠：防夹等逻辑暂时关闭碰撞的弹珠，也仍占据画面上的空间。
	if (MarbleClass)
	{
		for (TActorIterator<AActor> It(World, MarbleClass); It; ++It)
		{
			const AActor* Other = *It;
			if (Other == Entry.Marble || Other->IsHidden())
			{
				continue;
			}
			const UPrimitiveComponent* OtherBody = Cast<UPrimitiveComponent>(Other->GetRootComponent());
			if (!OtherBody)
			{
				continue;
			}

			const float Distance = Entry.Radius + GetMarbleRadius(OtherBody) + FMath::Max(0.f, MarbleClearance);
			if (FVector::DistSquared(Position, OtherBody->GetComponentLocation()) < FMath::Square(Distance))
			{
				return false;
			}
		}
	}

	// 另外检查静态墙、移动平台等非弹珠实体；忽略正在等待且已无碰撞的自身。
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RelocationPlacement));
	Params.AddIgnoredActor(Entry.Marble);
	return !World->OverlapBlockingTestByChannel(Position, FQuat::Identity, ECC_PhysicsBody,
	                                            FCollisionShape::MakeSphere(Entry.Radius + 2.f), Params);
}

void URelocationManagerComponent::RestoreMarble(const FPendingMarbleRelocation& Entry, const FVector* Position)
{
	if (!IsValid(Entry.Marble) || !IsValid(Entry.Body))
	{
		return;
	}

	if (Position)
	{
		Entry.Body->SetWorldLocation(*Position, false, nullptr, ETeleportType::TeleportPhysics);
	}
	Entry.Body->SetCollisionEnabled(Entry.PreviousCollision);
	Entry.Body->SetSimulatePhysics(Entry.bWasSimulatingPhysics);
	if (Position)
	{
		// 成功重生：停止该球的世界重力，线速度和角速度都从零开始。
		Entry.Body->SetEnableGravity(false);
		if (Entry.bWasSimulatingPhysics)
		{
			Entry.Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Entry.Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
		}
	}
	else
	{
		// 管理器销毁时不丢失仍在候队的球。
		Entry.Body->SetEnableGravity(Entry.bHadGravity);
		if (Entry.bWasSimulatingPhysics)
		{
			Entry.Body->SetPhysicsLinearVelocity(Entry.PreviousLinearVelocity);
			Entry.Body->SetPhysicsAngularVelocityInRadians(Entry.PreviousAngularVelocity);
		}
	}
	Entry.Marble->SetActorHiddenInGame(Entry.bWasHidden);
}

void URelocationManagerComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                               FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	while (!PendingMarbles.IsEmpty() && (!IsValid(PendingMarbles[0].Marble) || !IsValid(PendingMarbles[0].Body)))
	{
		PendingMarbles.RemoveAt(0);
	}
	if (PendingMarbles.IsEmpty())
	{
		return;
	}

	const ARelocationManagerActor* Manager = Cast<ARelocationManagerActor>(GetOwner());
	if (!Manager)
	{
		return;
	}
	const FVector Position = Manager->RespawnLocation;
	const FPendingMarbleRelocation& First = PendingMarbles[0];
	if (!IsRespawnClear(First, Position) ||
	    GetWorld()->GetTimeSeconds() - LastReleaseTime < FMath::Max(0.f, MinimumReleaseInterval))
	{
		return;
	}

	// 仅管理器放出队首球；传送/陷阱区域不再独立改动位置。
	const FPendingMarbleRelocation Releasing = First;
	PendingMarbles.RemoveAt(0);
	RestoreMarble(Releasing, &Position);
	LastReleaseTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogTemp, Log, TEXT("重定位管理器 %s：弹珠 %s 放出于 %s，剩余 %d 颗"),
	       *GetOwner()->GetName(), IsValid(Releasing.Marble) ? *Releasing.Marble->GetName() : TEXT("无效"),
	       *Position.ToString(), PendingMarbles.Num());
}

void URelocationManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const FPendingMarbleRelocation& Entry : PendingMarbles)
	{
		RestoreMarble(Entry, nullptr);
	}
	PendingMarbles.Empty();
	Super::EndPlay(EndPlayReason);
}
