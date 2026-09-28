#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "JumpPadImpulseComponent.generated.h"

class UPrimitiveComponent;

/** 在实体跳板被弹珠撞击时，向弹珠的物理组件施加一次冲量。 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="跳板弹射"))
class MARBLERACE_API UJumpPadImpulseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJumpPadImpulseComponent();

	/** 世界空间方向；无需单位向量，会自动归一化。侧视场景中 Z 正方向为向上。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="跳板弹射", meta=(DisplayName="弹射方向"))
	FVector LaunchDirection = FVector::UpVector;

	/** 给弹珠增加的速度，单位 cm/s，不受弹珠质量影响。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="跳板弹射",
		meta=(DisplayName="弹射力度", ClampMin="0", UIMin="0", ForceUnits="cm/s"))
	float LaunchStrength = 1000.f;

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
