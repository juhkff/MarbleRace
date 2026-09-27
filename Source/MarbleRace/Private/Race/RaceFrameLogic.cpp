#include "Race/RaceFrameLogic.h"

#include "Components/SplineComponent.h"

bool HasAuthoredRaceSpline(const USplineComponent* Spline)
{
	if (!Spline || Spline->GetNumberOfSplinePoints() < 2 || Spline->GetSplineLength() <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const USplineComponent* DefaultSpline = GetDefault<USplineComponent>();
	if (Spline->GetNumberOfSplinePoints() != DefaultSpline->GetNumberOfSplinePoints()
		|| Spline->IsClosedLoop() != DefaultSpline->IsClosedLoop()
		|| !Spline->GetRelativeTransform().Equals(FTransform::Identity))
	{
		return true;
	}

	// 和引擎类默认对象比，不假设某一版引擎的
	// 默认点和切线。序列化后仍没改过的样条也能认出来。
	for (int32 Index = 0; Index < Spline->GetNumberOfSplinePoints(); ++Index)
	{
		if (!Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local).Equals(
			DefaultSpline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local))
			|| !Spline->GetArriveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local).Equals(
				DefaultSpline->GetArriveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local))
			|| !Spline->GetLeaveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local).Equals(
				DefaultSpline->GetLeaveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local))
			|| Spline->GetSplinePointType(Index) != DefaultSpline->GetSplinePointType(Index))
		{
			return true;
		}
	}
	return false;
}
