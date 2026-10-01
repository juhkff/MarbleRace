#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "OscillatingRotationComponent.generated.h"

/** 以余弦曲线旋转挂接父组件，角度相对其开始时的朝向。 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="震荡旋转"))
class MARBLERACE_API UOscillatingRotationComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UOscillatingRotationComponent();
	/** 启动角度，超出左右极限时夹到极限；默认先向右摆动。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="震荡旋转", meta=(DisplayName="初始旋转值", Units="deg"))
	float InitialAngle = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="震荡旋转", meta=(DisplayName="左极限值", Units="deg"))
	float LeftLimit = -45.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="震荡旋转", meta=(DisplayName="右极限值", Units="deg"))
	float RightLimit = 45.f;
	/** 中点处的最大角速度；0 暂停。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="震荡旋转", meta=(DisplayName="旋转速度", ClampMin="0", Units="deg/s"))
	float RotationSpeed = 90.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="震荡旋转", meta=(DisplayName="转轴"))
	TEnumAsByte<EAxis::Type> SpinAxis = EAxis::Z;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	UFUNCTION(BlueprintPure, Category="震荡旋转", meta=(DisplayName="获取当前震荡角度"))
	double GetCurrentAngle() const;
private:
	void Capture(USceneComponent* Parent);
	void Apply(USceneComponent* Parent) const;
	TWeakObjectPtr<USceneComponent> RotationTarget;
	FQuat BaseRotation = FQuat::Identity;
	double Phase = 0.0;
};
