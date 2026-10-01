#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmoothBrakeZone.generated.h"

class UBoxComponent;
class UPrimitiveComponent;

/** 以连续反向力和力矩制动，不直接设置弹珠速度，也不改变其重力设置。 */
UCLASS(Blueprintable, meta=(DisplayName="平滑制动区域"))
class MARBLERACE_API ASmoothBrakeZone : public AActor
{
	GENERATED_BODY()

public:
	ASmoothBrakeZone();

	/** 调整盒子范围，覆盖中点前的一段轨道；也可作为关卡蓝图中的子 Actor。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="平滑制动", meta=(DisplayName="制动范围"))
	TObjectPtr<UBoxComponent> BrakeVolume;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="平滑制动", meta=(DisplayName="启用制动"))
	bool bEnableBraking = true;

	/** 低于此速度不制动，也不主动加速。重力持续作用时，实际稳定速度会略高于目标。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="平滑制动", meta=(DisplayName="目标小速度", ClampMin="0", ForceUnits="cm/s"))
	float TargetSpeed = 50.f;

	/** 超出目标的速度乘以此值，得到制动加速度。越大越快减速，效果不受弹珠质量影响。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="平滑制动", meta=(DisplayName="制动强度", ClampMin="0"))
	float BrakeStrength = 30.f;

	/** 制动加速度上限（cm/s²），避免高速球刚进入时制动过猛。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="平滑制动", meta=(DisplayName="最大制动加速度", ClampMin="0"))
	float MaximumBrakeAcceleration = 8000.f;

	/** 从盒子边界向内这段距离逐渐增强制动力；采用世界距离，不随区域缩放改变。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="平滑制动|过渡", meta=(DisplayName="边缘渐变距离", ClampMin="0", ForceUnits="cm"))
	float BoundaryFadeDistance = 100.f;

	/** 进入和离开区域时制动力随时间平滑过渡；离开后只保留短暂衰减的制动。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="平滑制动|过渡", meta=(DisplayName="制动力过渡时间", ClampMin="0", ForceUnits="s"))
	float BlendTime = .15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="平滑制动|旋转", meta=(DisplayName="同步制动旋转"))
	bool bBrakeRotation = true;

	/** 旋转目标按目标小速度/当前球半径计算，保留与小速度相匹配的滚动。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="平滑制动|旋转", meta=(DisplayName="旋转制动强度", EditCondition="bBrakeRotation", ClampMin="0"))
	float AngularBrakeStrength = 20.f;

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 返回反向加速度；不主动加速、不把速度推到目标以下，也不导致倒退。 */
	static FVector CalculateBrakeAcceleration(const FVector& Velocity, float DesiredSpeed,
		float Strength, float MaxAcceleration, float Weight, float DeltaSeconds);

	/** 渐变权重：边界和盒外为 0，深入盒内达到 1。 */
	float GetSpatialWeight(const FVector& WorldPosition) const;

private:
	UPROPERTY(Transient)
	TSubclassOf<AActor> MarbleClass;

	TMap<TWeakObjectPtr<UPrimitiveComponent>, float> BrakeWeights;
};
