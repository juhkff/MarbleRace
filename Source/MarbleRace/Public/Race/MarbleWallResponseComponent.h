#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MarbleWallResponseComponent.generated.h"

class UPrimitiveComponent;

/** Only selected child walls receive a short bounce and remove residual marble spin. */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="弹珠碰墙反弹"))
class MARBLERACE_API UMarbleWallResponseComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UMarbleWallResponseComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="碰墙", meta=(DisplayName="墙组件名称"))
	TArray<FName> WallComponentNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="碰墙", meta=(DisplayName="启用碰墙反弹"))
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="碰墙", meta=(DisplayName="反弹比例", ClampMin="0", ClampMax="1"))
	float BounceRatio = .2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="碰墙", meta=(DisplayName="最小反弹速度", ClampMin="0", ForceUnits="cm/s"))
	float MinimumBounceSpeed = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="碰墙", meta=(DisplayName="最大反弹速度", ClampMin="0", ForceUnits="cm/s"))
	float MaximumBounceSpeed = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="碰墙", meta=(DisplayName="沿墙速度保留比例", ClampMin="0", ClampMax="1"))
	float TangentialVelocityRetention = .25f;

	UPROPERTY(VisibleInstanceOnly, Transient, Category="碰墙|调试", meta=(DisplayName="已绑定墙壁数量"))
	int32 BoundWallCount = 0;

	UPROPERTY(VisibleInstanceOnly, Transient, Category="碰墙|调试", meta=(DisplayName="已触发反弹次数"))
	int32 BounceCount = 0;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY()
	TSubclassOf<AActor> MarbleClass;

	TMap<TWeakObjectPtr<UPrimitiveComponent>, bool> Walls;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, FVector> IncomingVelocities;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, double> LastBounceTimes;

	UFUNCTION()
	void HandleWallHit(UPrimitiveComponent* Wall, AActor* OtherActor, UPrimitiveComponent* Body,
		FVector NormalImpulse, const FHitResult& Hit);
};
