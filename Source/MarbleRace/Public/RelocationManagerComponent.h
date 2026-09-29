#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "RelocationManagerComponent.generated.h"

class UPrimitiveComponent;

/** 队列项保留弹珠原来的物理状态，仅用于取消排队/关卡结束时恢复。 */
USTRUCT(BlueprintType)
struct FPendingMarbleRelocation
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="重定位")
	TObjectPtr<AActor> Marble;

	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> Body;

	FVector PreviousLinearVelocity = FVector::ZeroVector;
	FVector PreviousAngularVelocity = FVector::ZeroVector;
	ECollisionEnabled::Type PreviousCollision = ECollisionEnabled::QueryAndPhysics;
	float Radius = 0.f;
	bool bWasSimulatingPhysics = false;
	bool bHadGravity = true;
	bool bWasHidden = false;
};

/** 一个管理器处理所有显式连接到它的传送和陷阱区域。 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="重定位队列"))
class MARBLERACE_API URelocationManagerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URelocationManagerComponent();

	/** 出口球与球之间的额外距离（cm），避免刚好擦边时下一帧发生碰撞。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位", meta=(DisplayName="球间安全间距", ClampMin="0", ForceUnits="cm"))
	float MarbleClearance = 20.f;

	/** 两颗球之间至少间隔这么久放出（秒）；不妨碍每帧检查出口是否空闲。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位", meta=(DisplayName="最短放出间隔", ClampMin="0", ForceUnits="s"))
	float MinimumReleaseInterval = 0.25f;

	/** 当前等待重生的球；顺序即入队顺序，仅由本管理器修改。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="重定位", meta=(DisplayName="待重定位弹珠"))
	TArray<FPendingMarbleRelocation> PendingMarbles;

	/** 进入传送/陷阱区域时先隐藏并暂停球，再按顺序交由管理器放出。 */
	UFUNCTION(BlueprintCallable, Category="重定位")
	void EnqueueMarble(AActor* Marble, UPrimitiveComponent* Body);

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool IsRespawnClear(const FPendingMarbleRelocation& Entry, const FVector& Position) const;

	/** 出口被占/被挡时，每秒最多打印一次原因，便于排查队列卡住。 */
	void LogRespawnBlocked(const FPendingMarbleRelocation& Entry, const FVector& Position) const;

	void RestoreMarble(const FPendingMarbleRelocation& Entry, const FVector* Position);

	UPROPERTY()
	TSubclassOf<AActor> MarbleClass;

	double LastReleaseTime = -1000.0;
	double LastBlockedLogTime = -1000.0;
};
