#pragma once
#include "CoreMinimal.h"

namespace MarbleRace
{
	inline constexpr float FinishApproachDistance = 2000.f;
	inline constexpr double FinishApproachSeconds = 2.4;
	inline constexpr double CountdownZoomSeconds = 1.8;

	inline float FinishTimeScale(const double Distance, const double DownwardSpeed, const double RemainingRealSeconds)
	{
		if (Distance <= 0.0 || DownwardSpeed <= 1.0 || RemainingRealSeconds <= 0.0) return 1.f;
		return static_cast<float>(FMath::Clamp(Distance / DownwardSpeed / RemainingRealSeconds, 0.0001, 1.0));
	}

	inline float CountdownZoomScale(const double ElapsedSeconds, const double Duration)
	{
		const double T = FMath::Clamp(ElapsedSeconds / FMath::Max(Duration, 0.001), 0.0, 1.0);
		const double Ease = T * T * (3.0 - 2.0 * T);
		return static_cast<float>(FMath::Lerp(0.6, 1.0, Ease));
	}
}
