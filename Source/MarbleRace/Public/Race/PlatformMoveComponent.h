#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PlatformMoveComponent.generated.h"

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="平板移动"))
class MARBLERACE_API UPlatformMoveComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPlatformMoveComponent();

	UPROPERTY(EditAnywhere, Category="平板", meta=(DisplayName="移动轴"))
	TEnumAsByte<EAxis::Type> MoveAxis = EAxis::X;

	/** 从摆放点到另一端的距离，到头后原路返回。 */
	UPROPERTY(EditAnywhere, Category="平板", meta=(DisplayName="移动距离", ClampMin="0", ForceUnits="cm"))
	float Distance = 400.f;

	/** 去和回算一次。 */
	UPROPERTY(EditAnywhere, Category="平板", meta=(DisplayName="周期", ClampMin="0.1", ForceUnits="s"))
	float Period = 4.f;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	FVector Origin = FVector::ZeroVector;
	float Elapsed = 0.f;
};
