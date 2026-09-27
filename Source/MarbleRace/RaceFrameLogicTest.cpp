#include "Misc/AutomationTest.h"
#include "Race/RaceFrameLogic.h"
#include "Components/SplineComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaceDeferredFinishTest, "MarbleRace.Progress.DeferredFinishOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRaceDeferredFinishTest::RunTest(const FString& Parameters)
{
	TArray<FRacePendingFinish> Events;
	Events.Add({0, 0, 10.9f});
	Events.Add({1, 1, 10.1f});
	SortPendingRaceFinishes(Events);
	TestEqual(TEXT("Later-enumerated earlier crossing wins"), Events[0].RacerId, 1);
	TestEqual(TEXT("Keep callback index attached to the event"), Events[0].RacerIndex, 1);

	Events.Reset();
	Events.Add({0, 8, 1.0f});
	Events.Add({1, 2, 1.0f});
	SortPendingRaceFinishes(Events);
	TestEqual(TEXT("Exact tie uses stable racer id"), Events[0].RacerId, 2);

	Events.Reset();
	Events.Add({0, 0, 1.00001f});
	Events.Add({1, 9, 1.0f});
	SortPendingRaceFinishes(Events);
	TestEqual(TEXT("A sub-tolerance time lead is not converted to an ID tie"), Events[0].RacerId, 9);
	Events.Swap(0, 1);
	SortPendingRaceFinishes(Events);
	TestEqual(TEXT("Enumeration order cannot change the champion"), Events[0].RacerId, 9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaceOrderedCrossingTest, "MarbleRace.Progress.OrderedFrameCrossings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRaceOrderedCrossingTest::RunTest(const FString& Parameters)
{
	FRaceGate LowGate;
	FRaceGate HighGate;
	HighGate.Transform.SetLocation(FVector(0.0f, 0.0f, 50.0f));
	const FVector Previous(0.0f, 0.0f, 100.0f);
	const FVector Current(0.0f, 0.0f, -100.0f);
	const FRaceCrossing Low = EvaluateGateCrossing(Previous, Current, LowGate);
	const FRaceCrossing High = EvaluateGateCrossing(Previous, Current, HighGate);

	float LastAlpha = 0.0f;
	TestTrue(TEXT("First legal checkpoint accepted"), AcceptOrderedRaceCrossing(Low, LastAlpha));
	TestFalse(TEXT("Cannot finish at an earlier crossing in the same step"), AcceptOrderedRaceCrossing(High, LastAlpha));
	TestEqual(TEXT("Rejected gate does not rewind the step"), LastAlpha, 0.5f);
	LastAlpha = 0.0f;
	TestTrue(TEXT("High gate first"), AcceptOrderedRaceCrossing(High, LastAlpha));
	TestTrue(TEXT("Later low gate can finish within the same frame"), AcceptOrderedRaceCrossing(Low, LastAlpha));
	TestFalse(TEXT("Non-crossing cannot advance"), AcceptOrderedRaceCrossing(FRaceCrossing(), LastAlpha));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaceDefaultSplineTest, "MarbleRace.Progress.DefaultSplineFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRaceDefaultSplineTest::RunTest(const FString& Parameters)
{
	USplineComponent* Spline = NewObject<USplineComponent>();
	TestFalse(TEXT("Engine's default horizontal two-point spline is not a route"), HasAuthoredRaceSpline(Spline));
	TestFalse(TEXT("Missing spline is safe"), HasAuthoredRaceSpline(nullptr));

	Spline->SetLocationAtSplinePoint(0, FVector(0.0f, 0.0f, 1000.0f), ESplineCoordinateSpace::Local, false);
	Spline->SetLocationAtSplinePoint(1, FVector::ZeroVector, ESplineCoordinateSpace::Local, true);
	TestTrue(TEXT("Authored downward route is preserved"), HasAuthoredRaceSpline(Spline));

	USplineComponent* TangentSpline = NewObject<USplineComponent>();
	TangentSpline->SetTangentAtSplinePoint(0, FVector(0.0f, 0.0f, 100.0f), ESplineCoordinateSpace::Local, true);
	TestTrue(TEXT("Custom tangents with default endpoints are preserved"), HasAuthoredRaceSpline(TangentSpline));

	USplineComponent* OffsetSpline = NewObject<USplineComponent>();
	OffsetSpline->SetRelativeLocation(FVector(0.0f, 0.0f, 500.0f));
	TestTrue(TEXT("Explicitly transformed spline is preserved"), HasAuthoredRaceSpline(OffsetSpline));
	Spline->ClearSplinePoints(true);
	TestFalse(TEXT("Cleared spline falls back"), HasAuthoredRaceSpline(Spline));
	return true;
}

#endif
