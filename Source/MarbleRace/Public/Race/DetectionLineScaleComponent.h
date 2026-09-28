#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DetectionLineScaleComponent.generated.h"

/**
 * 挂在检测线上。由网格的碰撞重叠事件调用，把穿过的弹珠改成目标大小。
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="过线改大小"))
class MARBLERACE_API UDetectionLineScaleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDetectionLineScaleComponent();

	UPROPERTY(EditDefaultsOnly, Category="检测", meta=(DisplayName="弹珠类"))
	TSubclassOf<AActor> MarbleClass;

	/** 接到网格的「碰撞开始重叠」事件上。大小读检测线蓝图上的「目标缩放」。 */
	UFUNCTION(BlueprintCallable, Category="检测", meta=(DisplayName="改弹珠大小"))
	void ResizeMarble(AActor* OtherActor, const float Scale) const;
};
