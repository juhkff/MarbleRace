#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Race/RaceProgressLogic.h"
#include "MarbleRaceDirector.generated.h"

class ARaceCheckpoint;
class ARaceStartDrum;
class UMaterialInterface;
class URaceCharacterProfile;
class URaceMusicDirectorComponent;
class URaceParticipantComponent;
class UTexture2D;

UENUM(BlueprintType, meta=(DisplayName="比赛状态"))
enum class ERaceState : uint8
{
	Ready UMETA(DisplayName="准备"),
	Countdown UMETA(DisplayName="倒计时"),
	Racing UMETA(DisplayName="比赛中"),
	Results UMETA(DisplayName="结果")
};

USTRUCT(BlueprintType, meta=(DisplayName="比赛成绩"))
struct FRaceResultEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="比赛", meta=(DisplayName="选手编号"))
	int32 RacerId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category="比赛", meta=(DisplayName="姓名"))
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category="比赛", meta=(DisplayName="名次"))
	int32 Rank = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category="比赛", meta=(DisplayName="完赛用时"))
	float FinishTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="比赛", meta=(DisplayName="头像"))
	TObjectPtr<UTexture2D> Portrait = nullptr;

	UPROPERTY(BlueprintReadOnly, Category="比赛", meta=(DisplayName="颜色"))
	FLinearColor Color = FLinearColor::White;
};

/** 一名已注册的小球，以及导演自己用的记账数据。 */
USTRUCT()
struct FRacerRuntime
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<URaceParticipantComponent> Participant = nullptr;

	bool bPhysicsEnabledByDirector = false;
	bool bLoggedOutOfBounds = false;

	/** 倒计时期间把这颗球按在旋转环上时使用的角度偏移。 */
	float DrumPhaseDegrees = 0.0f;
};

/**
 * 掌管整场比赛：报名、倒计时、进度、名次、领跑事件和主题音乐目标。
 * 界面只从这里读取；胜负不由别的对象决定。
 */
UCLASS(meta=(DisplayName="比赛导演"))
class MARBLERACE_API AMarbleRaceDirector : public AActor
{
	GENERATED_BODY()

public:
	AMarbleRaceDirector();
	virtual void Tick(float DeltaSeconds) override;

	// ---- 准备 ---------------------------------------------------------------

	/** 带这个标签的小球会在开赛时自己报名。标签文字已写进关卡，不要改默认值。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|准备", meta=(DisplayName="选手标签"))
	FName RacerTag = FName(TEXT("RaceMarble"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|准备", meta=(DisplayName="自动报名"))
	bool bAutoRegisterTaggedRacers = true;

	/** 按报名顺序发给选手的角色资料。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|准备", meta=(DisplayName="自动分配角色"))
	TArray<TObjectPtr<URaceCharacterProfile>> AutoAssignProfiles;

	/** 没有配置角色资料时，生成标得很明确的占位身份。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|准备", meta=(DisplayName="生成占位角色"))
	bool bCreatePlaceholderProfiles = true;

	/** 仅用于运行时验证反超和切歌的额外小球。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|准备", meta=(DisplayName="生成测试选手"))
	bool bSpawnTestRacers = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|准备", meta=(DisplayName="测试选手数量", ClampMin="1", ClampMax="8"))
	int32 TestRacerCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|准备", meta=(DisplayName="测试选手间距"))
	float TestRacerSpacingX = 65.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|准备", meta=(DisplayName="倒计时秒数"))
	float CountdownSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|准备", meta=(DisplayName="自动开始倒计时"))
	bool bAutoStartCountdown = true;

	/** 给已报名的小球打开连续碰撞检测，避免高速穿模。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|准备", meta=(DisplayName="连续碰撞检测"))
	bool bEnsureContinuousCollisionOnRacers = true;

	// ---- 名单 --------------------------------------------------------------

	/**
	 * 用游戏内名单（角色设置页里编辑的）作为小球身份，
	 * 而不只靠关卡里的占位和自动分配角色。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|名单", meta=(DisplayName="使用角色名单"))
	bool bUseRoster = true;

	/** 按关卡里的模板补足小球，直到名单里的人都上场。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|名单", meta=(DisplayName="按名单生成小球"))
	bool bSpawnRosterMarbles = true;

	// ---- 开局滚筒 ----------------------------------------------------------

	/** 把所有小球放进旋转的环里，使出发位置每次不同。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|开局", meta=(DisplayName="使用开局滚筒"))
	bool bUseStartDrum = true;

	/** 留空时使用默认的开局滚筒类。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|开局", meta=(DisplayName="滚筒类"))
	TSubclassOf<ARaceStartDrum> StartDrumClass;

	/** 关卡里没有滚筒时自动生成一个。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|开局", meta=(DisplayName="缺少时生成滚筒"))
	bool bSpawnDrumIfMissing = true;

	/**
	 * 生成滚筒时，相对模板小球起点的额外偏移。
	 * 滚筒半径会再加在上面，使环的底部落在原来的出发高度。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|开局", meta=(DisplayName="滚筒偏移"))
	FVector StartDrumOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|开局", meta=(DisplayName="滚筒转速"))
	float DrumSpinSpeedDegrees = 140.0f;

	/** 微调每颗球的起始角度，避免两场比赛排成同一圈。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|开局", meta=(DisplayName="随机起始角度"))
	bool bRandomizeDrumPlacement = true;

	/** 覆盖生成滚筒的材质；留空则用滚筒自己的默认材质。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|开局", meta=(DisplayName="滚筒材质"))
	TObjectPtr<UMaterialInterface> StartDrumMaterial;

	// ---- 进度 ------------------------------------------------------------

	/**
	 * 没有检查点时按高度计算进度。只适用于大体向下的赛道，
	 * 界面会标明这是回退模式。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|进度", meta=(DisplayName="无检查点时按高度"))
	bool bUseVerticalFallbackWhenNoCheckpoints = true;

	/** 高度回退模式的终点高度，必须高于落地几何体。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|进度", meta=(DisplayName="回退终点高度"))
	float FallbackFinishZ = -300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|进度", meta=(DisplayName="出界高度"))
	float OutOfBoundsZ = -3000.0f;

	// ---- 音乐 ---------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|音乐", meta=(DisplayName="领跑确认秒数"))
	float LeaderConfirmSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|音乐", meta=(DisplayName="领跑进度差"))
	float LeaderProgressMargin = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|音乐", meta=(DisplayName="冠军庆祝秒数"))
	float ChampionCelebrationSeconds = 5.0f;

	/** 庆祝结束后仍播放冠军主题，直到比赛结束。切歌会更慢。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|音乐", meta=(DisplayName="庆祝后保持冠军曲"))
	bool bKeepChampionThemeAfterCelebration = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|音乐", meta=(DisplayName="绘制头像时隐藏网格"))
	bool bHideRacerMeshesWhenPortraitsDrawn = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="比赛|音乐", meta=(DisplayName="音乐组件"))
	TObjectPtr<URaceMusicDirectorComponent> MusicDirector;

	// ---- 查询 -------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category="比赛", meta=(DisplayName="比赛状态"))
	ERaceState GetRaceState() const { return State; }

	UFUNCTION(BlueprintPure, Category="比赛", meta=(DisplayName="剩余倒计时"))
	float GetCountdownRemaining() const { return CountdownRemaining; }

	UFUNCTION(BlueprintPure, Category="比赛", meta=(DisplayName="比赛用时"))
	float GetRaceClock() const { return RaceClock; }

	UFUNCTION(BlueprintPure, Category="比赛", meta=(DisplayName="是否暂停"))
	bool IsRacePaused() const { return bRacePaused; }

	/** 仍在赛道上、位置最靠前的选手。还没人完赛时，这就是领跑者。 */
	int32 GetLeaderRacerId() const;

	/** 当前请求其主题曲的选手，和镜头目标不是同一个概念。 */
	int32 GetMusicTargetRacerId() const { return MusicTargetRacerId; }

	int32 GetChampionRacerId() const { return ChampionRacerId; }
	bool IsChampionCelebrating() const;
	bool IsUsingVerticalFallback() const { return bUsingVerticalFallback; }
	bool IsRouteUsable() const { return bRouteUsable; }
	FText GetRouteStatusText() const { return RouteStatusText; }
	FText GetMusicTargetDisplayName() const;

	const TArray<FRacerRuntime>& GetRacers() const { return Racers; }
	const TArray<FRaceResultEntry>& GetResults() const { return Results; }
	ARaceCheckpoint* GetCheckpointAt(int32 Index) const;

	/** 镜头应该看的那颗球，和音乐目标互不影响。 */
	AActor* GetCameraTargetActor() const;

	/** 正在使用的开局滚筒。从固定位置出发时为空。 */
	UFUNCTION(BlueprintPure, Category="比赛|开局", meta=(DisplayName="开局滚筒"))
	ARaceStartDrum* GetStartDrum() const { return StartDrum; }

	URaceParticipantComponent* FindRacerComponent(int32 RacerId) const;

	// ---- 控制 -------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category="比赛", meta=(DisplayName="报名选手"))
	void RegisterParticipant(URaceParticipantComponent* Participant);

	UFUNCTION(BlueprintCallable, Category="比赛", meta=(DisplayName="取消报名"))
	void UnregisterParticipant(URaceParticipantComponent* Participant);

	UFUNCTION(BlueprintCallable, Category="比赛", meta=(DisplayName="重新开始"))
	void RestartRace();

	/** 冻住小球和音乐，但不暂停引擎，因此按键仍然有效。 */
	UFUNCTION(BlueprintCallable, Category="比赛", meta=(DisplayName="暂停比赛"))
	void SetRacePaused(bool bPaused);

	/** 界面开始画头像时调用，避免网格和头像叠在一起。 */
	UFUNCTION(BlueprintCallable, Category="比赛", meta=(DisplayName="启用外观覆盖"))
	void SetVisualOverrideEnabled(bool bEnabled);

	/** 测试用：通过正常的音乐组件播放某一名选手的主题曲。 */
	UFUNCTION(BlueprintCallable, Category="比赛", meta=(DisplayName="调试播放主题曲"))
	void DebugRequestThemeForRacer(int32 RacerId);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TArray<FRacerRuntime> Racers;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ARaceCheckpoint>> Route;

	UPROPERTY(Transient)
	TArray<FRaceResultEntry> Results;

	UPROPERTY(Transient)
	TArray<TObjectPtr<URaceCharacterProfile>> PlaceholderProfiles;

	/** 本场从名单生成的临时身份。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<URaceCharacterProfile>> RosterProfiles;

	UPROPERTY(Transient)
	TObjectPtr<ARaceStartDrum> StartDrum;

	ERaceState State = ERaceState::Ready;
	float RaceClock = 0.0f;
	float CountdownRemaining = 0.0f;
	float FallbackStartZ = 0.0f;
	int32 NextRacerId = 0;
	int32 NextFinishRank = 0;
	int32 LeaderRacerId = INDEX_NONE;
	int32 MusicTargetRacerId = INDEX_NONE;
	int32 ChampionRacerId = INDEX_NONE;
	float ChampionCelebrationEndTime = 0.0f;
	bool bRacePaused = false;
	bool bVisualOverrideEnabled = false;
	bool bUsingVerticalFallback = false;
	bool bRouteUsable = false;
	bool bChampionThemeLocked = false;
	bool bWasCelebrating = false;
	FText RouteStatusText;

	FRaceLeaderDebounce LeaderDebounce;

	void BuildRoute();
	void BuildPlaceholderProfiles();
	void AutoRegisterTaggedRacers();
	void SpawnTestRacers();
	/** 按关卡模板生成一颗球。生成失败时返回 false。 */
	bool SpawnRacerFromTemplate(int32 SpawnIndex);
	/** 把名单里的姓名、颜色、头像和主题曲套到已报名的小球上。 */
	void ApplyRosterIdentities();
	/** 不断生成小球，直到场上至少有 DesiredCount 颗。 */
	int32 EnsureRacerCount(int32 DesiredCount);
	/** 使用关卡里的滚筒，没有就在模板小球的起点生成一个。 */
	ARaceStartDrum* ResolveStartDrum();
	/** 把已报名的小球均匀放在滚筒内侧。 */
	void PlaceRacersInDrum();
	/**
	 * 倒计时期间把每颗球按在滚筒内的旋转圆周上。
	 * 球处于冻结状态，避免穿出环壁。松手时解冻，并带上滚筒当时的切线速度。
	 */
	void UpdateDrumHold(float DeltaSeconds);
	/** 关卡里没有手摆机关时，生成一套缩短赛道用的机关。已有机关则不动。 */
	void EnsureCourseMechanisms();
	/** 给赛道表面套上低摩擦材质，避免小球停在缓坡上。 */
	void ApplyCourseSurfacePhysics();
	/** 终点线只是图形。带标签的物件不能变成检查点上方的地板。 */
	void DisableFinishLineCollision();
	void PrepareRace();
	void StartRace();
	void EnterResults();
	void UpdateRacers(float DeltaSeconds);
	void UpdateMusic(float DeltaSeconds);
	void OnRacerFinished(FRacerRuntime& Racer, float FinishTime);
	void SetRacersFrozen(bool bFrozen);
	void ApplyMeshVisibility();
	void UpdateOutOfBounds(FRacerRuntime& Racer, const FVector& Location);
	float EvaluateSegmentProgress(const URaceParticipantComponent& Participant, const FVector& Location) const;
	URaceCharacterProfile* PickProfileForIndex(int32 Index) const;
	FRaceRankState MakeRankState(const FRacerRuntime& Racer) const;
	FRaceRankState MakeRankStateById(int32 RacerId) const;
	int32 FindFrontMostUnfinishedRacerId() const;
	void RequestThemeForRacer(int32 RacerId, bool bForce);
};
