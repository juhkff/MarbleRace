#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RaceMusicDirectorComponent.generated.h"

class UAudioComponent;
class USoundBase;

/** 只负责本地播放音乐，不读取、也不修改比赛的权威状态。 */
UCLASS(ClassGroup=(MarbleRace), meta=(BlueprintSpawnableComponent, DisplayName="主题音乐"))
class MARBLERACE_API URaceMusicDirectorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URaceMusicDirectorComponent();

	/** 使用已经加载好的资源。空资源会改播默认曲；默认曲也没有就静音。 */
	UFUNCTION(BlueprintCallable, Category="比赛|音乐", meta=(DisplayName="请求主题曲"))
	void RequestTheme(USoundBase* Sound, float StartOffset = 0.0f, float Gain = 1.0f, bool bForce = false);

	/** 停掉两路声音，并清掉请求、计时和暂停状态。 */
	UFUNCTION(BlueprintCallable, Category="比赛|音乐", meta=(DisplayName="重置音乐"))
	void ResetMusic();

	/** 冻住播放和计时。暂停期间的新请求仍然以最后一次为准。 */
	UFUNCTION(BlueprintCallable, Category="比赛|音乐", meta=(DisplayName="暂停音乐"))
	void SetMusicPaused(bool bPaused);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|音乐", meta=(DisplayName="淡化秒数", ClampMin="0.0"))
	float CrossfadeSeconds = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|音乐", meta=(DisplayName="最短播放秒数", ClampMin="0.0"))
	float MinimumPlaySeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|音乐", meta=(DisplayName="默认音乐"))
	TObjectPtr<USoundBase> DefaultMusic = nullptr;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Channels[2];

	// 即使调用方放开了自己的引用，也要留住排队中的资源。
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> PendingSound = nullptr;

	float PendingOffset = 0.0f;
	float PendingGain = 1.0f;
	bool bHasPending = false;
	bool bPendingForce = false;
	bool bMusicPaused = false;
	bool bStoppingChannel = false;
	bool bEndingPlay = false;
	bool bRestartCurrent = false;
	int32 CurrentChannel = INDEX_NONE;
	float PlayedSeconds = 0.0f;
	float Offsets[2] = {0.0f, 0.0f};
	float Gains[2] = {0.0f, 0.0f};
	float FadeFrom[2] = {0.0f, 0.0f};
	float FadeTo[2] = {0.0f, 0.0f};
	float FadeElapsed = 0.0f;
	float FadeDuration = 0.0f;
	bool bFading = false;

	bool EnsureChannels();
	void ApplyPending();
	void StopChannel(int32 Index);
	void BeginFade(float TargetGain);
	void AdvanceFade(float DeltaTime);
	void OnChannelFinished(int32 Index);

	UFUNCTION()
	void OnFirstChannelFinished();

	UFUNCTION()
	void OnSecondChannelFinished();
};
