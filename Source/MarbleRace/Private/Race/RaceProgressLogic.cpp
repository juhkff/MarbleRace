#include "Race/RaceProgressLogic.h"

FRaceCrossing EvaluateGateCrossing(const FVector& PreviousWorld, const FVector& CurrentWorld, const FRaceGate& Gate)
{
	FRaceCrossing Result;

	const FVector Previous = Gate.Transform.InverseTransformPosition(PreviousWorld);
	const FVector Current = Gate.Transform.InverseTransformPosition(CurrentWorld);

	// 必须按文档里的方向穿过；已经站在门后面不算，
	// 往上穿也不算通过检查点。
	if (!(Previous.Z > 0.0f && Current.Z <= 0.0f))
	{
		return Result;
	}

	const float Denominator = Previous.Z - Current.Z;
	const float Alpha = Denominator > UE_KINDA_SMALL_NUMBER ? FMath::Clamp(Previous.Z / Denominator, 0.0f, 1.0f) : 0.0f;
	const FVector Hit = FMath::Lerp(Previous, Current, Alpha);

	// 开口从盒子的 X=0、Y=0 起算，半尺寸限制门的左右范围。
	if (FMath::Abs(Hit.X) > Gate.HalfExtent.X || FMath::Abs(Hit.Y) > Gate.HalfExtent.Y)
	{
		return Result;
	}

	Result.bCrossed = true;
	Result.Alpha = Alpha;
	return Result;
}

float EvaluateLinearSegmentProgress(const FVector& SegmentStart, const FVector& SegmentEnd, const FVector& Position)
{
	const FVector Segment = SegmentEnd - SegmentStart;
	const float LengthSquared = Segment.SizeSquared();
	if (LengthSquared <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	return FMath::Clamp(FVector::DotProduct(Position - SegmentStart, Segment) / LengthSquared, 0.0f, 1.0f);
}

float EvaluateVerticalProgress(float StartZ, float FinishZ, float CurrentZ)
{
	const float Span = StartZ - FinishZ;
	if (FMath::Abs(Span) <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	return FMath::Clamp((StartZ - CurrentZ) / Span, 0.0f, 1.0f);
}

bool IsRankedAhead(const FRaceRankState& A, const FRaceRankState& B)
{
	if (A.RacerId == B.RacerId)
	{
		return false;
	}

	// 正式成绩始终排在临时进度前面。
	if (A.bFinished != B.bFinished)
	{
		return A.bFinished;
	}
	if (A.bFinished)
	{
		if (!FMath::IsNearlyEqual(A.FinishTime, B.FinishTime, 0.0001f))
		{
			return A.FinishTime < B.FinishTime;
		}
		return A.RacerId < B.RacerId;
	}

	if (A.LastCheckpointIndex != B.LastCheckpointIndex)
	{
		return A.LastCheckpointIndex > B.LastCheckpointIndex;
	}
	if (!FMath::IsNearlyEqual(A.SegmentProgress, B.SegmentProgress, 0.0001f))
	{
		return A.SegmentProgress > B.SegmentProgress;
	}
	return A.RacerId < B.RacerId;
}

void SortRaceOrder(TArray<FRaceRankState>& States)
{
	States.Sort([](const FRaceRankState& A, const FRaceRankState& B)
	{
		return IsRankedAhead(A, B);
	});
}

void FRaceLeaderDebounce::Reset()
{
	CommittedId = INDEX_NONE;
	CandidateId = INDEX_NONE;
	CandidateSeconds = 0.0f;
}

void FRaceLeaderDebounce::Reset(int32 InCommittedId)
{
	CommittedId = InCommittedId;
	CandidateId = InCommittedId;
	CandidateSeconds = 0.0f;
}

bool FRaceLeaderDebounce::Update(int32 InCandidateId, bool bMeetsMargin, float DeltaSeconds, float RequiredSeconds, int32& OutCommittedId)
{
	OutCommittedId = CommittedId;

	if (InCandidateId == INDEX_NONE)
	{
		// 这一帧进度暂时不明（门区含糊，或传感器暂停）。先保持
		// 当前目标，不因为单独一帧不清楚就把音乐切掉。
		CandidateId = INDEX_NONE;
		CandidateSeconds = 0.0f;
		return false;
	}

	if (InCandidateId == CommittedId)
	{
		CandidateId = InCandidateId;
		CandidateSeconds = 0.0f;
		return false;
	}

	if (InCandidateId != CandidateId)
	{
		CandidateId = InCandidateId;
		CandidateSeconds = 0.0f;
	}

	CandidateSeconds += FMath::Max(0.0f, DeltaSeconds);

	const bool bTimeReady = CandidateSeconds >= FMath::Max(0.0f, RequiredSeconds);
	if (bTimeReady && bMeetsMargin)
	{
		CommittedId = InCandidateId;
		OutCommittedId = CommittedId;
		return true;
	}

	return false;
}
