#include "Race/RelocationManagerActor.h"

#include "Components/ChildActorComponent.h"
#include "Components/SceneComponent.h"
#include "RelocationManagerComponent.h"

ARelocationManagerActor::ARelocationManagerActor()
{
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("重定位根节点"));
	RootComponent = SceneRoot;
}

FVector ARelocationManagerActor::RollRespawnLocation() const
{
	return RollRespawnLocationAt(RespawnLocation);
}

bool ARelocationManagerActor::TryRollRespawnLocation(AActor* Marble, FVector& OutPosition) const
{
	OutPosition = RollRespawnLocation();
	return true;
}

void ARelocationManagerActor::CommitMarbleRespawn(AActor* Marble)
{
}

FVector ARelocationManagerActor::RollRespawnLocationAt(const FVector& BaseLocation) const
{
	// Match the frame in which the owning level Blueprint's component positions are edited.
	// A ChildActorComponent's own transform is the Manager placement, not the level origin.
	const auto ToWorldLocation = [this](const FVector& BlueprintLocation) -> FVector
	{
		const USceneComponent* ReferenceComponent = GetRespawnCoordinateFrame();
		return IsValid(ReferenceComponent)
			? ReferenceComponent->GetComponentTransform().TransformPosition(BlueprintLocation)
			: BlueprintLocation;
	};

	FVector Location = BaseLocation;
	if (!bEnableRandomRespawn)
	{
		return ToWorldLocation(Location);
	}

	// 小于这个值（厘米）的偏移在画面上和 0 没有区别，一律当成 0 重抽。
	constexpr double MinOffset = 1e-4;

	// 逐轴独立抽样；范围为 0（或负数）的轴保持原值。
	const auto OffsetAxis = [MinOffset](const double Base, const double Range) -> double
	{
		if (Range <= 0.0)
		{
			return Base;
		}

		// FMath::FRand() 落在 [0, 1)，映射成 [-Range, +Range)；
		// 偏移量不允许落在 0 上（落到 0 就等于没有随机），抽到就重抽。
		for (int32 Attempt = 0; Attempt < 8; ++Attempt)
		{
			const double Unit = FMath::FRand();
			if (const double Offset = (Unit * 2.0 - 1.0) * Range; FMath::Abs(Offset) > MinOffset)
			{
				return Base + Offset;
			}
		}

		// 极端情况下连续抽到 0：强制推到范围的一侧，保证仍然偏离重生点。
		const double Sign = FMath::FRand() < 0.5f ? -1.0 : 1.0;
		return Base + Sign * Range;
	};

	Location.X = OffsetAxis(Location.X, RespawnRandomRange.X);
	Location.Y = OffsetAxis(Location.Y, RespawnRandomRange.Y);
	Location.Z = OffsetAxis(Location.Z, RespawnRandomRange.Z);
	// Random ranges use the same Blueprint axes as the configured respawn position.
	return ToWorldLocation(Location);
}

const USceneComponent* ARelocationManagerActor::GetRespawnCoordinateFrame() const
{
	const UChildActorComponent* OwningChildComponent = GetParentComponent();
	return OwningChildComponent
		? OwningChildComponent->GetAttachParent()
		: (GetRootComponent() ? GetRootComponent()->GetAttachParent() : nullptr);
}

FVector ARelocationManagerActor::RollRespawnImpulse() const
{
	if (!bEnableRespawnImpulse)
	{
		return FVector::ZeroVector;
	}

	const auto Sample = [](const double A, const double B) -> double
	{
		if (!FMath::IsFinite(A) || !FMath::IsFinite(B))
		{
			return 0.0;
		}
		return FMath::FRandRange(FMath::Min(A, B), FMath::Max(A, B));
	};

	const FVector& Minimum = RespawnImpulseDirectionRange.Minimum;
	const FVector& Maximum = RespawnImpulseDirectionRange.Maximum;
	FVector Direction(Sample(Minimum.X, Maximum.X), Sample(Minimum.Y, Maximum.Y),
	                  Sample(Minimum.Z, Maximum.Z));
	Direction = Direction.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	if (const USceneComponent* ReferenceComponent = GetRespawnCoordinateFrame(); IsValid(ReferenceComponent))
	{
		// Rotation only: level translation/scale must not change the sampled impulse magnitude.
		Direction = ReferenceComponent->GetComponentTransform().TransformVectorNoScale(Direction).GetSafeNormal();
	}

	const double A = FMath::Max(0.f, RespawnImpulseMagnitudeRange.Minimum);
	const double B = FMath::Max(0.f, RespawnImpulseMagnitudeRange.Maximum);
	return Direction * Sample(A, B);
}

URelocationManagerComponent* ARelocationManagerActor::GetRelocationComponent() const
{
	return FindComponentByClass<URelocationManagerComponent>();
}
