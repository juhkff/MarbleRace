#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JumpPadImpulseComponent.generated.h"

class UPrimitiveComponent;

/** 在实体跳板被弹珠撞击时，按指定方向和速度覆盖弹珠的线速度。 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="跳板弹射"))
class MARBLERACE_API UJumpPadImpulseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJumpPadImpulseComponent();

	/** 世界空间方向；无需单位向量，会自动归一化。侧视场景中 Z 正方向为向上。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="跳板弹射", meta=(DisplayName="弹射方向"))
	FVector LaunchDirection = FVector::UpVector;

	/** 碰到跳板后的目标速度，单位 cm/s；覆盖原有线速度，不受弹珠质量影响。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="跳板弹射",
		meta=(DisplayName="弹射力度", ClampMin="0", UIMin="0", ForceUnits="cm/s"))
	float LaunchStrength = 1000.f;

	/** 每次触发在 X-Z 平面内随机向左或向右偏转的最大角度；0 表示方向固定。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="跳板弹射|随机",
		meta=(DisplayName="方向随机角度", ClampMin="0", ClampMax="180", UIMax="45", ForceUnits="deg"))
	float RandomAngleDegrees = 10.f;

	/** 每次触发给目标速度随机加减的最大幅度（cm/s）；0 表示力度固定。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="跳板弹射|随机",
		meta=(DisplayName="力度随机幅度", ClampMin="0", ForceUnits="cm/s"))
	float RandomStrengthRange = 100.f;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY()
	TSubclassOf<AActor> MarbleClass;

	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> PadCollision;

	TMap<TWeakObjectPtr<UPrimitiveComponent>, double> LastLaunchTimes;

	UFUNCTION()
	void HandlePadHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	                  UPrimitiveComponent* OtherComp, FVector NormalImpulse,
	                  const FHitResult& Hit);
};
