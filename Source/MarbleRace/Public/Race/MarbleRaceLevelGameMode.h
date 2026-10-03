#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Camera/CameraActor.h"
#include "TimerManager.h"
#include "Race/MarbleThemePlayback.h"
#include "Race/MarbleRaceResults.h"
#include "MarbleRaceLevelGameMode.generated.h"

class UAudioComponent;
class UMaterialInterface;
class USoundBase;
class IInputProcessor;

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

	const TMap<TObjectPtr<AActor>, int32>& GetMarbleRosterIndices() const { return MarbleRosterIndices; }

	UFUNCTION(BlueprintCallable, Category="音乐")
	void LockMusicSwitching();

	UFUNCTION(BlueprintCallable, Category="音乐")
	void UnlockMusicSwitching();

	bool IsMusicSwitchingLocked() const { return bMusicSwitchingLocked; }
	int32 GetCountdownRemaining() const { return CountdownDisplay; }
	bool IsRaceComplete() const { return bRaceComplete; }
	const TArray<FMarbleRaceFinishResult>& GetFinishResults() const { return FinishResults; }
	const FMarbleRaceFinishResult* GetFinishNotification(double Now) const;
	int32 GetResultsPage(double Now) const { return MarbleRace::ResultsPageAt(Now - ResultsStartedAt, FinishResults.Num()); }

	UFUNCTION(BlueprintCallable, Category="比赛")
	void ReturnToMainMenu();
	UFUNCTION(BlueprintCallable, Category="比赛")
	void AbortRaceAndReturnToMainMenu();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditDefaultsOnly, Category="开局", meta=(DisplayName="弹珠类"))
	TSubclassOf<AActor> MarbleClass;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> CharacterMaterial;

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

	/** After a finish, camera velocity equals the distance to the new leader times this rate. */
	UPROPERTY(EditDefaultsOnly, Category="镜头", meta=(DisplayName="过线镜头追赶系数", ClampMin="0.1"))
	float CameraFinishCatchUpRate = 12.f;

	/** 弹珠横向偏离赛道中心线超过这个距离（cm）就认为已经掉出赛道，不再算作“第一名”。0 表示不判定。 */
	UPROPERTY(EditAnywhere, Category="镜头", meta=(DisplayName="掉出赛道判定距离", ClampMin="0.0", ForceUnits="cm"))
	float MaxLeaderLateralOffset = 4000.f;

	/** 弹珠向下速度超过这个值（cm/s）就认为在赛道外自由落体，不再算作“第一名”。0 表示不判定。 */
	UPROPERTY(EditAnywhere, Category="镜头", meta=(DisplayName="掉出赛道判定下落速度", ClampMin="0.0", ForceUnits="cm/s"))
	float MaxLeaderFallSpeed = 9000.f;

private:
	friend class FMarbleCameraEligibilityTest;
	friend class FMarbleCameraOffCourseTest;
	friend class FMarbleFinishCameraCatchUpTest;
	friend class FMarbleMusicResumeTest;
	friend class FMarbleBackgroundMusicTest;
	friend class FMarbleMusicPreviewTest;
	friend class FMarbleMusicLockTest;
	friend class FMarbleRaceCompletionTest;
	friend class FQueuedMarbleLeaderTest;
	friend class FMarbleFinishSlowMotionTest;
	friend class FMarbleCountdownZoomTest;
	friend class FMarbleRaceResultsTest;
	friend class FMarbleRaceFlowTest;

	void CacheRaceAnchors();
	void SpawnMarblesInDrum();
	void ShowCountdown(int32 SecondsRemaining);
	void HideCountdown();
	void AdvanceCountdown();
	void RemoveDrums();
	void MarkFinishedMarbles(double Now = -1.0);
	bool GetQueuedPosition(const AActor* Marble, FVector& OutPosition) const;
	FVector GetRacePosition(const AActor* Marble) const;
	bool UpdateRaceCompletion(double Now);
	bool HasFinished(const AActor* Marble) const;
	bool IsEligibleMarble(const AActor* Marble) const;
	bool IsOnCourse(const AActor* Marble) const;
	bool IsLeaderCandidate(const AActor* Marble) const;
	AActor* FindLeadingMarble() const;
	void FollowLeader(const AActor* Leader, float DeltaSeconds);
	void UpdateLeaderMusic(AActor* Leader);
	void SwitchThemeMusic(USoundBase* Music, int32 RosterIndex, double Now, double InitialStartTime = 0.0);
	void UpdateFinishSlowMotion(AActor* Leader, double Now);
	void RestoreWorldSpeed();
	void StartCountdownZoom(double Now);
	void UpdateCountdownZoom(double Now);
	void RestoreCameraZoom();

	UPROPERTY()
	TArray<TObjectPtr<AActor>> Drums;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> Marbles;

	UPROPERTY()
	TMap<TObjectPtr<AActor>, int32> MarbleRosterIndices;

	UPROPERTY()
	TObjectPtr<UAudioComponent> ThemePlayerA;

	UPROPERTY()
	TObjectPtr<UAudioComponent> ThemePlayerB;

	TWeakObjectPtr<AActor> MusicLeader;
	bool bUseThemePlayerA = true;
	bool bMusicSwitchingLocked = false;
	bool bFinishBGM = false;
	bool bBackgroundMusic = false;
	/** Silent tracks advance on this shared, undilated race clock. */
	double MusicTimelineStartedAt = 0.0;
	bool bRaceComplete = false;
	bool bRaceAborted = false;
	bool bReturningToMainMenu = false;
	double ResultsStartedAt = 0.0;
	TSharedPtr<IInputProcessor> EscapeProcessor;
	bool bPostRaceMusicFinished = false;
	double MusicFinishAt = TNumericLimits<double>::Max();
	int32 CountdownDisplay = 0;
	FMarbleThemePlayback ThemePlaybackA;
	FMarbleThemePlayback ThemePlaybackB;
	/** Playback positions belong to individual roster entries for this race. */
	TMap<int32, double> ThemeResumePositions;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> FinishedMarbles;

	TArray<FMarbleRaceFinishResult> FinishResults;
	/** Real-time samples allow ordering multiple crossings within the same frame. */
	struct FFinishPositionSample
	{
		double Z = 0.0;
		double SampledAt = 0.0;
	};
	TMap<TWeakObjectPtr<AActor>, FFinishPositionSample> PreviousFinishPositions;
	double RaceStartedAt = 0.0;
	double LastFinishNotificationAt = -TNumericLimits<double>::Max();

	UPROPERTY()
	TObjectPtr<ACameraActor> FollowCamera;

	TWeakObjectPtr<AActor> CurrentLeader;
	bool bFinishCameraCatchUpActive = false;

	int32 SecondsLeft = 0;
	FTimerHandle CountdownTimer;
	float CameraLockX = 0.f;
	float CameraSideY = 0.f;
	float CameraOrthoWidth = 2500.f;
	float CameraFieldOfView = 90.f;
	bool bFinishSlowMotionEnabled = false;
	bool bCountdownZoomEnabled = false;
	bool bFinishTimeDilationApplied = false;
	float OriginalTimeDilation = 1.f;
	TWeakObjectPtr<AActor> SlowMotionLeader;
	TSet<TWeakObjectPtr<AActor>> SlowMotionTriggeredMarbles;
	double SlowMotionStartedAt = 0.0;
	double CountdownZoomStartedAt = 0.0;
	double CountdownZoomDuration = 1.8;
	bool bCountdownZoomActive = false;
	/** 赛道中心线（滚筒所在的 X/Y），用来判断弹珠有没有横向掉出赛道。 */
	FVector2D CourseCentre = FVector2D::ZeroVector;
	float FinishLineZ = 0.f;
	bool bHasFinishLine = false;
	bool bFollowLeader = false;

};
