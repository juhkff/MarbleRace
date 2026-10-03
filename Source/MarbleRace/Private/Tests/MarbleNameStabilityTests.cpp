#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/MarbleNameStability.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleNameStabilityTest, "MarbleRace.UI.StableNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleNameStabilityTest::RunTest(const FString& Parameters)
{
	FVector2D Smoothed(100.0, 100.0);
	double LargestDeviation = 0.0;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Smoothed = MarbleRace::StabilizeNamePosition(Smoothed, FVector2D(100.0, 100.0 + (Frame % 2 ? -2.0 : 2.0)), 1.f / 60.f);
		LargestDeviation = FMath::Max(LargestDeviation, FMath::Abs(Smoothed.Y - 100.0));
	}
	TestTrue(TEXT("Alternating two-pixel vibration is reduced below one pixel"), LargestDeviation < 1.0);
	const FVector2D Teleport(500.0, 600.0);
	TestEqual(TEXT("Teleport snaps instead of leaving names behind"), MarbleRace::StabilizeNamePosition(Smoothed, Teleport, 1.f / 60.f), Teleport);
	TestEqual(TEXT("Recovery from a long frame snaps to current marble"), MarbleRace::StabilizeNamePosition(Smoothed, FVector2D(110.0, 110.0), 0.3f), FVector2D(110.0, 110.0));
	TestEqual(TEXT("Zero elapsed time does not move label"), MarbleRace::StabilizeNamePosition(Smoothed, FVector2D(110.0, 110.0), 0.f), Smoothed);
	for (const int32 Rate : {30, 60, 120})
	{
		FVector2D Position(0.0, 0.0);
		for (int32 Frame = 0; Frame < Rate; ++Frame) Position = MarbleRace::StabilizeNamePosition(Position, FVector2D(24.0, 0.0), 1.f / Rate);
		TestTrue(TEXT("At common frame rates, label catches up without overshoot"), Position.X > 23.99 && Position.X <= 24.0);
	}
	return true;
}
#endif
