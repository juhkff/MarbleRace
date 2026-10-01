#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PeriodicRotationComponent.generated.h"

/** Rotate the attach parent around its local axis relative to its initial orientation. */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="周期旋转"))
class MARBLERACE_API UPeriodicRotationComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UPeriodicRotationComponent();

	/** 每周期的角度；负值反向。非往复模式到达终点后瞬间复位重播。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="周期旋转", meta=(DisplayName="旋转量", Units="deg"))
	float RotationAmount = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="周期旋转", meta=(DisplayName="是否往复"))
	bool bReciprocate = false;

	/** 度/秒；零暂停，运行时负值按零处理。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="周期旋转", meta=(DisplayName="旋转速度", ClampMin="0", Units="deg/s"))
	float RotationSpeed = 90.f;

	/** 与圆环旋转一样，转轴基于父组件的本地坐标系。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="周期旋转", meta=(DisplayName="转轴"))
	TEnumAsByte<EAxis::Type> SpinAxis = EAxis::Z;

	/** 绕指定支点旋转；关闭时保持旧行为，只改旋转、绕被转组件自身原点。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="周期旋转", meta=(DisplayName="绕支点旋转"))
	bool bRotateAroundPivot = false;

	/** 支点相对本组件原点的位置，用本组件的本地单位表示（组件缩放会自动作用其上）。
	 *  因为它是本地偏移，把资产摆到关卡或嵌进别的蓝图时它会自动跟着走。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="周期旋转",
		meta=(DisplayName="支点位置", EditCondition="bRotateAroundPivot", EditConditionHides))
	FVector PivotLocation = FVector::ZeroVector;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintPure, Category="周期旋转", meta=(DisplayName="获取当前周期旋转角度"))
	double GetCurrentCycleAngle() const;

private:
	void CaptureInitialRotation(USceneComponent* Parent);
	TWeakObjectPtr<USceneComponent> RotationTarget;
	FQuat InitialRelativeRotation = FQuat::Identity;
	FVector InitialRelativeLocation = FVector::ZeroVector;
	FVector InitialRelativeScale3D = FVector::OneVector;
	double PhaseDegrees = 0.0;
};
