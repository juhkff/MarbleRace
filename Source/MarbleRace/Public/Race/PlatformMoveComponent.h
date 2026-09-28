#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Engine/EngineTypes.h"
#include "PlatformMoveComponent.generated.h"

class UPrimitiveComponent;
class AStaticMeshActor;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="平板移动"))
class MARBLERACE_API UPlatformMoveComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPlatformMoveComponent();

	UPROPERTY(EditAnywhere, Category="平板", meta=(DisplayName="移动轴"))
	TEnumAsByte<EAxis::Type> MoveAxis = EAxis::X;

	/** 沿移动轴正方向的路程。正方向分两段走完，再原路返回。只在蓝图默认值里改，关卡实例一律跟着走。 */
	UPROPERTY(EditDefaultsOnly, Category="平板", meta=(DisplayName="移动距离", ClampMin="0", ForceUnits="cm"))
	float Distance = 400.f;

	/** 沿正方向走完全程的时间。路程对半分成两段，时间也对半，每段都是先快后慢。只在蓝图默认值里改。 */
	UPROPERTY(EditDefaultsOnly, Category="平板", meta=(DisplayName="向右移动时间", ClampMin="0.1", ForceUnits="s"))
	float RightMoveTime = 2.f;

	/** 沿反方向匀速返回起点的时间。只在蓝图默认值里改。 */
	UPROPERTY(EditDefaultsOnly, Category="平板", meta=(DisplayName="向左移动时间", ClampMin="0.1", ForceUnits="s"))
	float LeftMoveTime = 2.f;

	/** 当侧墙与平板的间隙小于弹珠直径加此余量时，开始将夹点中的弹珠移开。 */
	UPROPERTY(EditAnywhere, Category="平板|防夹", meta=(DisplayName="夹球安全余量", ClampMin="0", ForceUnits="cm"))
	float PinchClearance = 30.f;

	/** 弹珠脱困期间沿世界 Z 轴匀速移动的速度（cm/s）。 */
	UPROPERTY(EditAnywhere, Category="平板|防夹", meta=(DisplayName="脱困移动速度", ClampMin="1", ForceUnits="cm/s"))
	float EscapeSpeed = 500.f;

	/** 每次脱困固定移动的时长；移动距离 = 脱困移动速度 × 脱困移动时间。 */
	UPROPERTY(EditAnywhere, Category="平板|防夹", meta=(DisplayName="脱困移动时间", ClampMin="0.01", ForceUnits="s"))
	float EscapeDuration = 0.5f;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FMarbleEscape
	{
		TWeakObjectPtr<UPrimitiveComponent> Body;
		FVector StartLocation = FVector::ZeroVector;
		FVector SavedVelocity = FVector::ZeroVector;
		float DirectionZ = 1.f;
		float ElapsedTime = 0.f;
		ECollisionEnabled::Type PreviousCollision = ECollisionEnabled::QueryAndPhysics;
		bool bWasSimulatingPhysics = false;
		bool bHadGravity = true;
	};

	void DetectSideWallPinch(UPrimitiveComponent& Platform, const FVector& WorldMove);
	void UpdateEscapingMarbles(float DeltaTime);
	void FinishEscape(const FMarbleEscape& Escape);
	void FindSideWalls();

	UPROPERTY()
	TSubclassOf<AActor> MarbleClass;

	TWeakObjectPtr<AStaticMeshActor> LeftWall;
	TWeakObjectPtr<AStaticMeshActor> RightWall;
	TArray<FMarbleEscape> ActiveEscapes;
	FVector Origin = FVector::ZeroVector;
	float Elapsed = 0.f;
};
