#include "Misc/AutomationTest.h"
#include "Race/RaceProgressLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaceGateCrossingTest, "MarbleRace.Progress.GateCrossing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRaceGateCrossingTest::RunTest(const FString& Parameters)
{
	FRaceGate Gate;
	Gate.Transform = FTransform::Identity;
	Gate.HalfExtent = FVector2D(100.0f, 50.0f);

	const FRaceCrossing Downward = EvaluateGateCrossing(FVector(0.0f, 0.0f, 50.0f), FVector(0.0f, 0.0f, -50.0f), Gate);
	TestTrue(TEXT("从局部 +Z 到 -Z 应判定通过"), Downward.bCrossed);
	TestEqual(TEXT("交点插值应为中点"), Downward.Alpha, 0.5f, 0.001f);

	const FRaceCrossing Upward = EvaluateGateCrossing(FVector(0.0f, 0.0f, -50.0f), FVector(0.0f, 0.0f, 50.0f), Gate);
	TestFalse(TEXT("反向穿越不算通过"), Upward.bCrossed);

	const FRaceCrossing OutsideX = EvaluateGateCrossing(FVector(200.0f, 0.0f, 50.0f), FVector(200.0f, 0.0f, -50.0f), Gate);
	TestFalse(TEXT("超出横向范围不算通过"), OutsideX.bCrossed);

	const FRaceCrossing OutsideY = EvaluateGateCrossing(FVector(0.0f, 300.0f, 50.0f), FVector(0.0f, 300.0f, -50.0f), Gate);
	TestFalse(TEXT("超出纵向范围不算通过"), OutsideY.bCrossed);

	const FRaceCrossing AlreadyBelow = EvaluateGateCrossing(FVector(0.0f, 0.0f, -10.0f), FVector(0.0f, 0.0f, -60.0f), Gate);
	TestFalse(TEXT("已在平面下方不会重复触发"), AlreadyBelow.bCrossed);

	const FRaceCrossing Fast = EvaluateGateCrossing(FVector(0.0f, 0.0f, 5000.0f), FVector(0.0f, 0.0f, -5000.0f), Gate);
	TestTrue(TEXT("高速大步长也应捕获穿越"), Fast.bCrossed);
	TestEqual(TEXT("高速穿越插值"), Fast.Alpha, 0.5f, 0.001f);

	// 门要在自己的局部空间里判定，不能用世界高度。
	FRaceGate Offset;
	Offset.Transform = FTransform(FVector(500.0f, 0.0f, 200.0f));
	Offset.HalfExtent = FVector2D(100.0f, 100.0f);
	TestTrue(TEXT("带位移的门按局部空间判定"),
		EvaluateGateCrossing(FVector(500.0f, 0.0f, 250.0f), FVector(500.0f, 0.0f, 150.0f), Offset).bCrossed);
	TestFalse(TEXT("门范围以外的位置不算通过"),
		EvaluateGateCrossing(FVector(0.0f, 0.0f, 250.0f), FVector(0.0f, 0.0f, 150.0f), Offset).bCrossed);

	FRaceGate RotatedGate;
	RotatedGate.Transform = FTransform(FRotator(0.0f, 0.0f, 90.0f));
	RotatedGate.HalfExtent = FVector2D(100.0f, 100.0f);
	const FVector LocalZAxis = RotatedGate.Transform.GetUnitAxis(EAxis::Z);
	const FVector RotatedStart = RotatedGate.Transform.GetLocation() + LocalZAxis * 50.0f;
	const FVector RotatedEnd = RotatedGate.Transform.GetLocation() - LocalZAxis * 50.0f;
	TestTrue(TEXT("旋转后的门沿其局部 Z 轴判定"),
		EvaluateGateCrossing(RotatedStart, RotatedEnd, RotatedGate).bCrossed);
	TestFalse(TEXT("旋转后的门不接受反向穿越"),
		EvaluateGateCrossing(RotatedEnd, RotatedStart, RotatedGate).bCrossed);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaceProgressEvaluationTest, "MarbleRace.Progress.Evaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRaceProgressEvaluationTest::RunTest(const FString& Parameters)
{
	const FVector SegmentStart(0.0f, 0.0f, 0.0f);
	const FVector SegmentEnd(0.0f, 0.0f, 100.0f);

	TestEqual(TEXT("段起点进度为 0"),
		EvaluateLinearSegmentProgress(SegmentStart, SegmentEnd, FVector(0.0f, 0.0f, 0.0f)), 0.0f, 0.001f);
	TestEqual(TEXT("段中点进度为 0.5"),
		EvaluateLinearSegmentProgress(SegmentStart, SegmentEnd, FVector(0.0f, 0.0f, 50.0f)), 0.5f, 0.001f);
	TestEqual(TEXT("超过终点被夹取为 1"),
		EvaluateLinearSegmentProgress(SegmentStart, SegmentEnd, FVector(0.0f, 0.0f, 900.0f)), 1.0f, 0.001f);
	TestEqual(TEXT("退回起点之前被夹取为 0"),
		EvaluateLinearSegmentProgress(SegmentStart, SegmentEnd, FVector(0.0f, 0.0f, -900.0f)), 0.0f, 0.001f);
	TestEqual(TEXT("退化线段返回 0"),
		EvaluateLinearSegmentProgress(SegmentStart, SegmentStart, FVector(0.0f, 0.0f, 10.0f)), 0.0f, 0.001f);

	TestEqual(TEXT("高度进度起点为 0"), EvaluateVerticalProgress(1000.0f, -200.0f, 1000.0f), 0.0f, 0.001f);
	TestEqual(TEXT("高度进度中点为 0.5"), EvaluateVerticalProgress(1000.0f, -200.0f, 400.0f), 0.5f, 0.001f);
	TestEqual(TEXT("高度进度终点为 1"), EvaluateVerticalProgress(1000.0f, -200.0f, -200.0f), 1.0f, 0.001f);
	TestEqual(TEXT("反弹回上方时不小于 0"), EvaluateVerticalProgress(1000.0f, -200.0f, 3000.0f), 0.0f, 0.001f);
	TestEqual(TEXT("越过终点后夹取为 1"), EvaluateVerticalProgress(1000.0f, -200.0f, -5000.0f), 1.0f, 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaceRankingTest, "MarbleRace.Progress.Ranking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRaceRankingTest::RunTest(const FString& Parameters)
{
	FRaceRankState First;
	First.RacerId = 1;
	First.LastCheckpointIndex = 1;
	First.SegmentProgress = 0.2f;

	FRaceRankState Second;
	Second.RacerId = 2;
	Second.LastCheckpointIndex = 1;
	Second.SegmentProgress = 0.8f;
	TestTrue(TEXT("同一段内进度更高者在前"), IsRankedAhead(Second, First));

	FRaceRankState FurtherAlong;
	FurtherAlong.RacerId = 3;
	FurtherAlong.LastCheckpointIndex = 2;
	FurtherAlong.SegmentProgress = 0.01f;
	TestTrue(TEXT("通过检查点更多者在前，即使段内进度很低"), IsRankedAhead(FurtherAlong, Second));

	FRaceRankState Finisher;
	Finisher.RacerId = 4;
	Finisher.bFinished = true;
	Finisher.FinishTime = 100.0f;
	TestTrue(TEXT("已完赛者优先于未完赛者"), IsRankedAhead(Finisher, FurtherAlong));

	FRaceRankState FasterFinisher;
	FasterFinisher.RacerId = 5;
	FasterFinisher.bFinished = true;
	FasterFinisher.FinishTime = 99.0f;
	TestTrue(TEXT("完赛用时更短者在前"), IsRankedAhead(FasterFinisher, Finisher));
	TestFalse(TEXT("完赛用时更长者在后"), IsRankedAhead(Finisher, FasterFinisher));

	FRaceRankState SameTimeDifferentId;
	SameTimeDifferentId.RacerId = 0;
	SameTimeDifferentId.bFinished = true;
	SameTimeDifferentId.FinishTime = 99.0f;
	TestTrue(TEXT("用时相同时按参赛者编号稳定排序"), IsRankedAhead(SameTimeDifferentId, FasterFinisher));
	TestFalse(TEXT("同一参赛者不会领先自己"), IsRankedAhead(FasterFinisher, FasterFinisher));

	TArray<FRaceRankState> Order;
	Order.Add(First);
	Order.Add(FasterFinisher);
	Order.Add(FurtherAlong);
	Order.Add(Second);
	SortRaceOrder(Order);

	TestEqual(TEXT("排序第一名是完赛最快者"), Order[0].RacerId, 5);
	TestEqual(TEXT("排序第二名是通过检查点更多者"), Order[1].RacerId, 3);
	TestEqual(TEXT("排序第三名是同段内进度更高者"), Order[2].RacerId, 2);
	TestEqual(TEXT("排序最后是进度最低者"), Order[3].RacerId, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaceLeaderDebounceTest, "MarbleRace.Progress.LeaderDebounce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRaceLeaderDebounceTest::RunTest(const FString& Parameters)
{
	constexpr float Required = 0.5f;
	FRaceLeaderDebounce Debounce;
	int32 Committed = INDEX_NONE;

	TestFalse(TEXT("0.1 秒不足以确认"), Debounce.Update(5, true, 0.1f, Required, Committed));
	TestFalse(TEXT("0.2 秒不足以确认"), Debounce.Update(5, true, 0.1f, Required, Committed));
	TestFalse(TEXT("0.3 秒不足以确认"), Debounce.Update(5, true, 0.1f, Required, Committed));
	TestFalse(TEXT("0.4 秒不足以确认"), Debounce.Update(5, true, 0.1f, Required, Committed));
	TestTrue(TEXT("达到 0.5 秒后确认切换"), Debounce.Update(5, true, 0.1f, Required, Committed));
	TestEqual(TEXT("确认的目标"), Committed, 5);

	TestFalse(TEXT("同一目标不会重复切换"), Debounce.Update(5, true, 0.3f, Required, Committed));

	TestFalse(TEXT("新候选需要重新计时"), Debounce.Update(7, true, 0.3f, Required, Committed));
	TestFalse(TEXT("新候选 0.4 秒仍不足"), Debounce.Update(7, true, 0.1f, Required, Committed));
	TestTrue(TEXT("新候选满 0.5 秒后切换"), Debounce.Update(7, true, 0.2f, Required, Committed));
	TestEqual(TEXT("切换后的目标"), Committed, 7);

	Debounce.Update(9, false, 1.0f, Required, Committed);
	TestEqual(TEXT("领先优势不足时不切换"), Committed, 7);

	TestTrue(TEXT("优势足够后切换"), Debounce.Update(9, true, 0.1f, Required, Committed));
	TestEqual(TEXT("优势足够后的目标"), Committed, 9);

	TestFalse(TEXT("候选未知时不切换"), Debounce.Update(INDEX_NONE, true, 5.0f, Required, Committed));
	TestEqual(TEXT("候选未知时保留当前目标"), Committed, 9);

	Debounce.Reset();
	TestEqual(TEXT("重置后没有已确认目标"), Debounce.GetCommittedId(), INDEX_NONE);
	TestTrue(TEXT("重置后需要重新计时"), Debounce.Update(3, true, 0.6f, Required, Committed));
	TestEqual(TEXT("重置后可确认新目标"), Committed, 3);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
