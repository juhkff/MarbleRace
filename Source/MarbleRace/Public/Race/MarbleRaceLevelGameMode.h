#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Camera/CameraActor.h"
#include "TimerManager.h"
#include "Widgets/Text/STextBlock.h"
#include "MarbleRaceLevelGameMode.generated.h"

/**
 * 比赛关卡的游戏模式。
 * 不画主菜单。开局把画面切到关卡里已有的摄像机上，
 * 按参赛名单在滚筒里生成弹珠，倒计时结束后滚筒消失。
 * 开赛后镜头跟着当前第一名，过线后改跟下一名。
 */
UCLASS(meta=(DisplayName="比赛关卡模式"))
class MARBLERACE_API AMarbleRaceLevelGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMarbleRaceLevelGameMode();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category="开局", meta=(DisplayName="弹珠类"))
	TSubclassOf<AActor> MarbleClass;

	/** 圆盘原始直径为 400 cm；缩小后才能在滚筒内并排生成参赛弹珠。 */
	UPROPERTY(EditDefaultsOnly, Category="开局", meta=(DisplayName="生成弹珠缩放", ClampMin="0.05"))
	float MarbleSpawnScale = 0.45f;

	UPROPERTY(EditDefaultsOnly, Category="开局", meta=(DisplayName="滚筒类"))
	TSubclassOf<AActor> DrumClass;

	UPROPERTY(EditDefaultsOnly, Category="开局", meta=(DisplayName="终点类"))
	TSubclassOf<AActor> FinishClass;

	/** 显示 3、2、1 的总时长，到点后滚筒消失。 */
	UPROPERTY(EditDefaultsOnly, Category="开局", meta=(DisplayName="倒计时", ClampMin="1", ForceUnits="s"))
	int32 CountdownSeconds = 3;

	/** 镜头追上第一名的快慢。越大跟得越紧。 */
	UPROPERTY(EditDefaultsOnly, Category="镜头", meta=(DisplayName="跟随速度", ClampMin="0.1"))
	float CameraFollowSpeed = 6.f;

	/** Maximum camera travel per second, in orthographic viewport widths. */
	UPROPERTY(EditDefaultsOnly, Category="镜头", meta=(DisplayName="最大镜头移动速度（视野宽度/秒）", ClampMin="0.1"))
	float CameraMaxViewportWidthsPerSecond = 2.f;

	/** 弹珠横向偏离赛道中心线超过这个距离（cm）就认为已经掉出赛道，不再算作“第一名”。0 表示不判定。 */
	UPROPERTY(EditAnywhere, Category="镜头", meta=(DisplayName="掉出赛道判定距离", ClampMin="0.0", ForceUnits="cm"))
	float MaxLeaderLateralOffset = 4000.f;

	/** 弹珠向下速度超过这个值（cm/s）就认为在赛道外自由落体，不再算作“第一名”。0 表示不判定。 */
	UPROPERTY(EditAnywhere, Category="镜头", meta=(DisplayName="掉出赛道判定下落速度", ClampMin="0.0", ForceUnits="cm/s"))
	float MaxLeaderFallSpeed = 9000.f;

private:
	friend class FMarbleCameraEligibilityTest;
	friend class FMarbleCameraOffCourseTest;

	void CacheRaceAnchors();
	void SpawnMarblesInDrum();
	void ShowCountdown(int32 SecondsRemaining);
	void HideCountdown();
	void AdvanceCountdown();
	void RemoveDrums();
	void MarkFinishedMarbles();
	bool HasFinished(const AActor* Marble) const;
	bool IsEligibleMarble(const AActor* Marble) const;
	bool IsOnCourse(const AActor* Marble) const;
	bool IsLeaderCandidate(const AActor* Marble) const;
	AActor* FindLeadingMarble() const;
	void FollowLeader(const AActor* Leader, float DeltaSeconds) const;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> Drums;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> Marbles;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> FinishedMarbles;

	UPROPERTY()
	TObjectPtr<ACameraActor> FollowCamera;

	TWeakObjectPtr<AActor> CurrentLeader;

	int32 SecondsLeft = 0;
	FTimerHandle CountdownTimer;
	float CameraLockX = 0.f;
	float CameraSideY = 0.f;
	float CameraOrthoWidth = 2500.f;
	/** 赛道中心线（滚筒所在的 X/Y），用来判断弹珠有没有横向掉出赛道。 */
	FVector2D CourseCentre = FVector2D::ZeroVector;
	float FinishLineZ = 0.f;
	bool bHasFinishLine = false;
	bool bFollowLeader = false;

	TSharedPtr<SWidget> CountdownWidget;
	TSharedPtr<STextBlock> CountdownLabel;
};
