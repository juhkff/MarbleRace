#include "Race/RelocationManagerActor.h"

#include "Components/SceneComponent.h"
#include "RelocationManagerComponent.h"

ARelocationManagerActor::ARelocationManagerActor()
{
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("重定位根节点"));
	RootComponent = SceneRoot;
}

FVector ARelocationManagerActor::RollRespawnLocation() const
{
	FVector Location = RespawnLocation;
	if (!bEnableRandomRespawn)
	{
		return Location;
	}

	// 逐轴独立抽样；范围为 0（或负数）的轴保持原值。
	const auto OffsetAxis = [](const double Base, const double Range) -> double
	{
		if (Range <= 0.0)
		{
			return Base;
		}
		// FMath::FRand() 落在 [0, 1)，映射成 [-Range, +Range)。
		const double Unit = static_cast<double>(FMath::FRand());
		return Base + (Unit * 2.0 - 1.0) * Range;
	};

	Location.X = OffsetAxis(Location.X, RespawnRandomRange.X);
	Location.Y = OffsetAxis(Location.Y, RespawnRandomRange.Y);
	Location.Z = OffsetAxis(Location.Z, RespawnRandomRange.Z);
	return Location;
}

URelocationManagerComponent* ARelocationManagerActor::GetRelocationComponent() const
{
	return FindComponentByClass<URelocationManagerComponent>();
}
