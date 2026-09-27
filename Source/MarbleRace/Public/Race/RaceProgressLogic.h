#pragma once

#include "CoreMinimal.h"

/**
 * 不依赖引擎的比赛进度规则。
 *
 * 故意不碰 UObject 和世界，这样名次和过门规则可以用普通输入做单元测试。
 * 正式比赛里只有导演会调用这些函数。
 */

/** 一道门。局部 Z=0 是门平面，从局部 +Z 走向局部 -Z 才算穿过。 */
struct FRaceGate
{
	FTransform Transform = FTransform::Identity;
	FVector2D HalfExtent = FVector2D(500.0f, 100.0f);
};

struct FRaceCrossing
{
	bool bCrossed = false;
	/** 上一位置（0）和当前位置（1）之间的插值。 */
	float Alpha = 0.0f;
};

/** 只用于临时排序的归一化进度，不是正式成绩。 */
struct FRaceRankState
{
	int32 RacerId = INDEX_NONE;
	bool bFinished = false;
	float FinishTime = 0.0f;
	/** 这名选手合法通过的最高检查点序号。一个都没过时为 -1。 */
	int32 LastCheckpointIndex = INDEX_NONE;
	/** 当前这一段里的进度，0 到 1。被弹回去之后可以再变小。 */
	float SegmentProgress = 0.0f;
};

FRaceCrossing EvaluateGateCrossing(const FVector& PreviousWorld, const FVector& CurrentWorld, const FRaceGate& Gate);

float EvaluateLinearSegmentProgress(const FVector& SegmentStart, const FVector& SegmentEnd, const FVector& Position);

float EvaluateVerticalProgress(float StartZ, float FinishZ, float CurrentZ);

/** 按文档里的顺序判断 A 是否排在 B 前面，编号相同则用稳定的编号决胜。 */
bool IsRankedAhead(const FRaceRankState& A, const FRaceRankState& B);

/** 位置最靠前的排在最前面。 */
void SortRaceOrder(TArray<FRaceRankState>& States);

/**
 * 避免每次碰撞反弹都触发一次领跑变化。
 * 正式成绩仍由调用方记录。这里只决定主题音乐什么时候跟着换人。
 */
class FRaceLeaderDebounce
{
public:
	void Reset();
	/** 把已经在播的目标设为当前确认对象，例如冠军庆祝结束之后。 */
	void Reset(int32 InCommittedId);

	/**
	 * @param CandidateId     仍未完赛、位置最靠前的选手。未知时为 INDEX_NONE。
	 * @param bMeetsMargin    候选人是否已经明显领先当前确认的目标。
	 * @param RequiredSeconds 候选人必须保持领先这么久。
	 * @param OutCommittedId  更新后的确认目标。
	 * @return 只在确认目标发生变化的那一帧返回 true。
	 */
	bool Update(int32 CandidateId, bool bMeetsMargin, float DeltaSeconds, float RequiredSeconds, int32& OutCommittedId);

	int32 GetCommittedId() const { return CommittedId; }
	int32 GetCandidateId() const { return CandidateId; }
	float GetCandidateSeconds() const { return CandidateSeconds; }

private:
	int32 CommittedId = INDEX_NONE;
	int32 CandidateId = INDEX_NONE;
	float CandidateSeconds = 0.0f;
};
