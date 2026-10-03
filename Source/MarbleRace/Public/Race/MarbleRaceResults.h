#pragma once

#include "CoreMinimal.h"

/** Snapshot the finish so results survive actor destruction and roster edits. */
struct FMarbleRaceFinishResult
{
	int32 Rank = 0;
	int32 RosterIndex = INDEX_NONE;
	FString CharacterName;
	FString ThemeTitle;
	double ElapsedSeconds = 0.0;
};

namespace MarbleRace
{
	inline constexpr double FinishNotificationSeconds = 2.0;
	inline constexpr int32 ResultsPerPage = 10;
	inline constexpr double ResultsPageSeconds = 5.0;
	inline int32 ResultsPageCount(int32 Count) { return FMath::Max(1, FMath::DivideAndRoundUp(Count, ResultsPerPage)); }
	inline int32 ResultsPageAt(double Elapsed, int32 Count)
	{
		return FMath::Clamp(FMath::FloorToInt(FMath::Max(0., Elapsed) / ResultsPageSeconds), 0, ResultsPageCount(Count) - 1);
	}
	inline bool ResultsDisplayFinished(double Elapsed, int32 Count)
	{
		return Elapsed >= ResultsPageCount(Count) * ResultsPageSeconds;
	}

	inline FString FormatFinishTime(double Seconds)
	{
		if (!FMath::IsFinite(Seconds)) Seconds = 0.0;
		const int64 Hundredths = FMath::RoundToInt64(FMath::Max(0.0, Seconds) * 100.0);
		return FString::Printf(TEXT("%02lld:%02lld.%02lld"), Hundredths / 6000,
			(Hundredths / 100) % 60, Hundredths % 100);
	}
}
