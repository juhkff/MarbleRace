#include "RelocationManagerComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/OverlapResult.h"
#include "Race/RelocationManagerActor.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	float GetMarbleRadius(const UPrimitiveComponent* Body)
	{
		// 在未旋转的局部轴上计算当前缩放的尺寸，避免旋转后的世界 AABB 膨胀。
		const FTransform ScaleTransform(FQuat::Identity, FVector::ZeroVector, Body->GetComponentScale());
		const float VisualRadius = Body->CalcBounds(ScaleTransform).BoxExtent.GetMax();
		const UBodySetup* Setup = Body->GetPhysicsBodySetup();
		if (!Setup || Setup->AggGeom.GetElementCount() == 0)
		{
			return VisualRadius;
		}
		return FMath::Max(VisualRadius, Setup->AggGeom.CalcAABB(ScaleTransform).GetExtent().GetMax());
	}

	bool FindPlacementOverlaps(const UWorld* World, const FPendingMarbleRelocation& Entry,
		const FVector& Position, TArray<FOverlapResult>& Overlaps)
	{
		FComponentQueryParams Params(SCENE_QUERY_STAT(RelocationPlacement));
		Params.AddIgnoredActor(Entry.Marble);
		return Entry.Body->ComponentOverlapMulti(Overlaps, World, Position,
			Entry.Body->GetComponentQuat(), Entry.Body->GetCollisionObjectType(), Params);
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

void URelocationManagerComponent::EnqueueMarble(AActor* Marble, UPrimitiveComponent* Body, int32 SourceNumber)
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

	if (auto* Manager = Cast<ARelocationManagerActor>(GetOwner()))
	{
		Manager->SetMarbleRespawnSource(Marble, SourceNumber);
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
	Entry.bAlwaysCreatedPhysicsState = Body->bAlwaysCreatePhysicsState;
	if (auto* Manager = Cast<ARelocationManagerActor>(GetOwner()))
		Entry.bHasPlannedRespawnPosition = Manager->TryRollRespawnLocation(Marble, Entry.PlannedRespawnPosition);

	// 暂停原对象而不是销毁它，保留赛程中的弹珠身份和材质。
	// 暂停期间仍保留真实碰撞几何用于落点查询，但自身不参与场景碰撞。
	Body->bAlwaysCreatePhysicsState = true;
	Body->SetSimulatePhysics(false);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marble->SetActorHiddenInGame(true);
	PendingMarbles.Add(Entry);
	UE_LOG(LogTemp, Log, TEXT("重定位管理器 %s：弹珠 %s 入队，等待 %d 颗"),
	       *GetOwner()->GetName(), *Marble->GetName(), PendingMarbles.Num());
}

bool URelocationManagerComponent::GetQueuedMarblePosition(const AActor* Marble, FVector& OutPosition) const
{
	for (const FPendingMarbleRelocation& Entry : PendingMarbles)
		if (Entry.Marble == Marble && Entry.bHasPlannedRespawnPosition)
		{
			OutPosition = Entry.PlannedRespawnPosition;
			return true;
		}
	return false;
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
	if (!IsValid(Entry.Body) || !Entry.Body->GetBodyInstance()->IsValidBodyInstance())
	{
		return false;
	}
	TArray<FOverlapResult> Overlaps;
	return !FindPlacementOverlaps(World, Entry, Position, Overlaps);
}

void URelocationManagerComponent::LogRespawnBlocked(const FPendingMarbleRelocation& Entry, const FVector& Position) const
{
	const UWorld* World = GetWorld();
	if (!World || !IsValid(Entry.Marble))
	{
		return;
	}

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
			const float Needed = Entry.Radius + GetMarbleRadius(OtherBody) + FMath::Max(0.f, MarbleClearance);
			const float Distance = FVector::Dist(Position, OtherBody->GetComponentLocation());
			if (Distance < Needed)
			{
				UE_LOG(LogTemp, Warning,
				       TEXT("重定位管理器 %s：出口 %s 被弹珠 %s 占住（距离 %.1f，需要 %.1f），队列等待中"),
				       *GetOwner()->GetName(), *Position.ToString(), *Other->GetName(), Distance, Needed);
				return;
			}
		}
	}

	TArray<FOverlapResult> Overlaps;
	if (IsValid(Entry.Body) && FindPlacementOverlaps(World, Entry, Position, Overlaps))
	{
		for (const FOverlapResult& Hit : Overlaps)
		{
			if (Hit.bBlockingHit)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("重定位管理器 %s：出口 %s 被 %s 的组件 %s 挡住（真实碰撞形状，缩放 %s），队列等待中"),
					*GetOwner()->GetName(), *Position.ToString(), *GetNameSafe(Hit.GetActor()),
					*GetNameSafe(Hit.GetComponent()), *Entry.Body->GetComponentScale().ToString());
				return;
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("重定位管理器 %s：出口 %s 判定为不干净但未找到原因（球半径 %.1f）"),
	       *GetOwner()->GetName(), *Position.ToString(), Entry.Radius);
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
	Entry.Body->bAlwaysCreatePhysicsState = Entry.bAlwaysCreatedPhysicsState;
	Entry.Body->SetSimulatePhysics(Entry.bWasSimulatingPhysics);
	if (Position)
	{
		// 成功重生：线速度和角速度都从零开始。
		// 重生点落在重力场里时，可以把管理器上的开关关掉，交给重力场施力。
		const ARelocationManagerActor* Manager = Cast<ARelocationManagerActor>(GetOwner());
		const bool bEnableGravity = Manager ? Manager->bEnableGravityAfterRespawn : true;
		Entry.Body->SetEnableGravity(bEnableGravity);
		if (Entry.bWasSimulatingPhysics)
		{
			Entry.Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Entry.Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
			if (Manager && Manager->bEnableRespawnImpulse)
			{
				// Sample once, only after successful respawn and physics/velocity restoration.
				const FVector Impulse = Manager->RollRespawnImpulse();
				if (!Impulse.IsNearlyZero())
				{
					Entry.Body->AddImpulse(Impulse, NAME_None, false);
				}
			}
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

	ARelocationManagerActor* Manager = Cast<ARelocationManagerActor>(GetOwner());
	if (!Manager)
	{
		return;
	}
	if (GetWorld()->GetTimeSeconds() - LastReleaseTime < FMath::Max(0.f, MinimumReleaseInterval)) return;
	// 按入队顺序检查各球自己的出口，阻塞的出口不妨碍其他出口。
	for (int32 ReleaseIndex = 0; ReleaseIndex < PendingMarbles.Num(); ++ReleaseIndex)
	{
	FPendingMarbleRelocation& First = PendingMarbles[ReleaseIndex];
	if (!IsValid(First.Marble) || !IsValid(First.Body)) continue;
	if (!First.bHasPlannedRespawnPosition)
	{
		First.bHasPlannedRespawnPosition = Manager->TryRollRespawnLocation(First.Marble, First.PlannedRespawnPosition);
		if (!First.bHasPlannedRespawnPosition) continue;
	}
	const FVector Position = First.PlannedRespawnPosition;
	const bool bClear = IsRespawnClear(First, Position);
	if (!bClear)
	{
		// 队列卡住时给出原因：被别的球占住，还是被静态物体挡住。
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now - LastBlockedLogTime > 1.0)
		{
			LastBlockedLogTime = Now;
			LogRespawnBlocked(First, Position);
		}
	}
	if (!bClear)
	{
		// Retain the existing ability to escape an obstructed random spawn area.
		// Only a release attempt can change the plan; ranking queries are read-only.
		if (Manager->bEnableRandomRespawn)
			First.bHasPlannedRespawnPosition = Manager->TryRollRespawnLocation(First.Marble, First.PlannedRespawnPosition);
		continue;
	}

	// 放出最早能够重生的球，仅在成功后推进该球的轮询索引。
	const FPendingMarbleRelocation Releasing = First;
	PendingMarbles.RemoveAt(ReleaseIndex);
	RestoreMarble(Releasing, &Position);
	Manager->CommitMarbleRespawn(Releasing.Marble);
	LastReleaseTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogTemp, Log, TEXT("重定位管理器 %s：弹珠 %s 放出于 %s，剩余 %d 颗"),
	       *GetOwner()->GetName(), IsValid(Releasing.Marble) ? *Releasing.Marble->GetName() : TEXT("无效"),
	       *Position.ToString(), PendingMarbles.Num());
	// 继续检查其他出口；刚放出的球会占据当前出口，阻止同出口重复放出。
	--ReleaseIndex;
	}
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
