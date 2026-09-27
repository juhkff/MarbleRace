#pragma once

#include "CoreMinimal.h"
#include "Race/RaceProgressLogic.h"

class USplineComponent;

/** 延后的完赛事件：整帧收齐之后再提交名次。 */
struct FRacePendingFinish
{
	int32 RacerIndex = INDEX_NONE;
	int32 RacerId = INDEX_NONE;
	float FinishTime = 0.0f;
};

inline void SortPendingRaceFinishes(TArray<FRacePendingFinish>& Events)
{
	Events.Sort([](const FRacePendingFinish& A, const FRacePendingFinish& B)
	{
		// 不做近似相等：测到的领先再小也算领先。
		return A.FinishTime != B.FinishTime ? A.FinishTime < B.FinishTime : A.RacerId < B.RacerId;
	});
}

inline bool AcceptOrderedRaceCrossing(const FRaceCrossing& Crossing, float& LastAlpha)
{
	if (!Crossing.bCrossed || Crossing.Alpha < LastAlpha)
	{
		return false;
	}
	LastAlpha = Crossing.Alpha;
	return true;
}

/** 拒绝引擎没改过的两点占位样条，已摆好的几何不算。 */
bool HasAuthoredRaceSpline(const USplineComponent* Spline);
