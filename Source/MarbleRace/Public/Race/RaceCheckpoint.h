#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Race/RaceProgressLogic.h"
#include "RaceCheckpoint.generated.h"

class UBoxComponent;
class USceneComponent;
class USplineComponent;

/**
 * 路线上的一道有顺序的门。
 *
 * 门平面是 Actor 的局部 Z=0。小球必须从局部 +Z 一侧走到局部 -Z 一侧。
 * 默认旋转下，这就是向下穿过。把 Actor 放在小球该经过的位置，路线转弯时再旋转它。
 */
UCLASS(meta=(DisplayName="检查点"))
class MARBLERACE_API ARaceCheckpoint : public AActor
{
	GENERATED_BODY()

public:
	ARaceCheckpoint();

	/** 路线顺序，数字小的在前。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|检查点", meta=(DisplayName="顺序"))
	int32 Order = 0;

	/** 整条路线只能有一个终点，而且它的顺序必须最大。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|检查点", meta=(DisplayName="是终点"))
	bool bIsFinish = false;

	/** 开口在局部 X/Y 上的半尺寸。要比真实通道宽。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="比赛|检查点", meta=(DisplayName="开口半尺寸"))
	FVector2D HalfExtent = FVector2D(500.0f, 100.0f);

	/**
	 * 可选。把样条点从上一道门编辑到这一道门，弯道里的进度会更准。
	 * 少于两个点时，导演改用两道门之间的直线。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="比赛|检查点", meta=(DisplayName="路线样条"))
	TObjectPtr<USplineComponent> RouteSpline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="比赛|检查点", meta=(DisplayName="门的外观"))
	TObjectPtr<UBoxComponent> GateVisual;

	FRaceGate MakeGate() const;
	bool HasUsableSpline() const;
	/** 沿手摆样条的进度，0 到 1。不能用时返回负数。 */
	float EvaluateSplineProgress(const FVector& WorldLocation) const;

	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPROPERTY(VisibleAnywhere, Category="比赛|检查点", meta=(DisplayName="根组件"))
	TObjectPtr<USceneComponent> SceneRoot;

	void SyncGateVisual();
};
