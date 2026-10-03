#pragma once

#include "CoreMinimal.h"

/** One channel's playback interval, frozen when its character loses the lead. */
struct FMarbleThemePlayback
{
	int32 RosterIndex = INDEX_NONE;
	double StartedAt = 0.0;
	double StartedFrom = 0.0;
	double Duration = 0.0;
	double StopsAt = TNumericLimits<double>::Max();

	static double ClampStartTime(const double Seconds, const double TrackDuration)
	{
		if (!FMath::IsFinite(Seconds) || !FMath::IsFinite(TrackDuration) || TrackDuration <= 0.0) return 0.0;
		return FMath::Clamp(Seconds, 0.0, FMath::Max(0.0, TrackDuration - 0.01));
	}

	double PositionAt(const double Now) const
	{
		if (Duration <= 0.0)
		{
			return 0.0;
		}
		const double Elapsed = FMath::Max(0.0, FMath::Min(Now, StopsAt) - StartedAt);
		return FMath::Fmod(StartedFrom + Elapsed, Duration);
	}
};
