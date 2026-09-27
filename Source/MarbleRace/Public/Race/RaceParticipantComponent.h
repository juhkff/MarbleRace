#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RaceParticipantComponent.generated.h"

class URaceCharacterProfile;
class UPrimitiveComponent;

/**
 * 一颗小球的身份和比赛状态。
 *
 * 这个组件不决定名次，也不播放音乐。导演读写这里的状态，
 * 每名选手只有这一处权威数据。
 */
UCLASS(ClassGroup=(MarbleRace), meta=(BlueprintSpawnableComponent, DisplayName="参赛者"))
class MARBLERACE_API URaceParticipantComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URaceParticipantComponent();

	/** 角色身份：头像、姓名、颜色和主题曲都从这里来。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|角色", meta=(DisplayName="角色资料"))
	TObjectPtr<URaceCharacterProfile> CharacterProfile;

	/** 带标签的小球会自动报名，除非把这个关掉。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛", meta=(DisplayName="自动报名"))
	bool bAutoRegister = true;

	// ---- 给界面读取的状态 ----------------------------------------------------

	UFUNCTION(BlueprintPure, Category="比赛", meta=(DisplayName="选手编号"))
	int32 GetRacerId() const { return RacerId; }

	UFUNCTION(BlueprintPure, Category="比赛", meta=(DisplayName="已报名"))
	bool IsRegistered() const { return bRegistered; }

	UFUNCTION(BlueprintPure, Category="比赛", meta=(DisplayName="已完赛"))
	bool IsFinished() const { return bFinished; }

	UFUNCTION(BlueprintPure, Category="比赛", meta=(DisplayName="名次"))
	int32 GetFinishRank() const { return FinishRank; }

	UFUNCTION(BlueprintPure, Category="比赛", meta=(DisplayName="完赛用时"))
	float GetFinishTime() const { return FinishTime; }

	UFUNCTION(BlueprintPure, Category="比赛", meta=(DisplayName="已出界"))
	bool IsOutOfBounds() const { return bOutOfBounds; }

	UFUNCTION(BlueprintPure, Category="比赛", meta=(DisplayName="角色资料"))
	URaceCharacterProfile* GetCharacterProfile() const { return CharacterProfile; }

	FText GetDisplayName() const;
	FLinearColor GetDisplayColor() const;
	/** 姓名的第一个字。没有头像时画在圆盘上。 */
	FString GetInitialText() const;
	UTexture2D* GetPortrait() const;

	/** 已通过的最高检查点序号。一个都没过时为 -1。 */
	int32 GetLastCheckpointIndex() const { return NextCheckpointIndex - 1; }
	int32 GetNextCheckpointIndex() const { return NextCheckpointIndex; }
	float GetSegmentProgress() const { return SegmentProgress; }
	FVector GetPreviousLocation() const { return PreviousLocation; }
	const FTransform& GetStartTransform() const { return StartTransform; }

	/** 用来冻结、传送和判断边界的物理体，可能为空。 */
	UPrimitiveComponent* GetPhysicsComponent();
	/** 世界空间半径。界面用它让头像圆盘和真实小球一样大。 */
	float GetWorldRadius();

	// ---- 给导演调用的接口 -------------------------------------------------

	void SetRacerId(int32 InRacerId);
	/** 关卡里没填角色资料时，由导演补上。 */
	void AssignProfileIfMissing(URaceCharacterProfile* InProfile);
	void BeginRaceRegistration(const FTransform& InStartTransform);
	void EndRaceRegistration();
	/** 把当前变换记成重新开始时的位置。 */
	void CaptureStartTransform();
	/** 清掉进度和成绩，但保留已记录的起点。 */
	void ResetRuntimeState();
	/** 把小球放回起点，并清掉速度。 */
	void TeleportToStart();
	void SetPreviousLocation(const FVector& InLocation);
	void SetNextCheckpointIndex(int32 InIndex);
	void SetSegmentProgress(float InProgress);
	void SetOutOfBounds(bool bInOutOfBounds);
	void RecordFinish(float InFinishTime, int32 InFinishRank);

private:
	UPROPERTY(Transient)
	int32 RacerId = INDEX_NONE;

	UPROPERTY(Transient)
	bool bRegistered = false;

	UPROPERTY(Transient)
	bool bFinished = false;

	UPROPERTY(Transient)
	int32 FinishRank = INDEX_NONE;

	UPROPERTY(Transient)
	float FinishTime = 0.0f;

	UPROPERTY(Transient)
	bool bOutOfBounds = false;

	UPROPERTY(Transient)
	int32 NextCheckpointIndex = 0;

	UPROPERTY(Transient)
	float SegmentProgress = 0.0f;

	UPROPERTY(Transient)
	FVector PreviousLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	FTransform StartTransform = FTransform::Identity;

	UPROPERTY(Transient)
	TWeakObjectPtr<UPrimitiveComponent> CachedPhysicsComponent;
};
