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
	return Location;
}

URelocationManagerComponent* ARelocationManagerActor::GetRelocationComponent() const
{
	return FindComponentByClass<URelocationManagerComponent>();
}
