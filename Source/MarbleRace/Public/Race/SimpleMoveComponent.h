#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "SimpleMoveComponent.generated.h"

/** Attach to the component to move, just like PlatformMove. */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="简单移动"))
class MARBLERACE_API USimpleMoveComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USimpleMoveComponent();

	/** 对被移动的碰撞组件启用 CCD 和运动感知碰撞检测。保持运动学轨迹，不因弹珠阻挡而停止。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="简单移动|碰撞", meta=(DisplayName="增强移动碰撞"))
	bool bEnhancedMovingCollision = false;

	/** 方向使用被移动组件的父级坐标系；无父级时使用世界坐标。自动归一化，零向量不移动。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="简单移动", meta=(DisplayName="移动方向"))
	FVector MoveDirection = FVector::ForwardVector;

	/** 世界距离（厘米），不受网格自身或父级缩放影响。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="简单移动", meta=(DisplayName="移动距离", ClampMin="0", ForceUnits="cm"))
	float MoveDistance = 400.f;

	/** 开启：匀速去程、终点等待、匀速返程、起点等待。关闭：去程、终点等待、瞬间复位。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="简单移动", meta=(DisplayName="往复运动"))
	bool bReciprocate = true;

	/** 游戏开始时从 0 到此值随机选取一次启动延迟；不计入后续周期。保留属性名以兼容已有配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="简单移动", meta=(DisplayName="最大启动延迟", ClampMin="0", ForceUnits="s"))
	float StartDelay = 0.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="简单移动", meta=(DisplayName="实际启动延迟", ForceUnits="s"))
	float ActualStartDelay = 0.f;

	/** 往复运动时，在终点和起点各等待此时长；单程运动时，仅在终点等待后复位。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="简单移动", meta=(DisplayName="周期间隔", ClampMin="0", ForceUnits="s"))
	float CycleInterval = 0.f;

	/** 保留属性名以兼容已有蓝图配置；与上限共同构成随机范围。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="简单移动|速度", meta=(DisplayName="去程速度下限", ClampMin="0.1", ForceUnits="cm/s"))
	float OutboundSpeed = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="简单移动|速度", meta=(DisplayName="去程速度上限", ClampMin="0.1", ForceUnits="cm/s"))
	float OutboundSpeedMax = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="简单移动|速度", meta=(DisplayName="返程速度下限", ClampMin="0.1", ForceUnits="cm/s", EditCondition="bReciprocate", EditConditionHides))
	float ReturnSpeed = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="简单移动|速度", meta=(DisplayName="返程速度上限", ClampMin="0.1", ForceUnits="cm/s", EditCondition="bReciprocate", EditConditionHides))
	float ReturnSpeedMax = 200.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="简单移动|速度", meta=(DisplayName="实际去程速度", ForceUnits="cm/s"))
	float ActualOutboundSpeed = 0.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="简单移动|速度", meta=(DisplayName="实际返程速度", ForceUnits="cm/s", EditCondition="bReciprocate", EditConditionHides))
	float ActualReturnSpeed = 0.f;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	void CaptureStart(USceneComponent* Target);
	void InitializeStartup();
	void SampleCycleSpeeds();

	TWeakObjectPtr<USceneComponent> MovementTarget;
	FVector InitialRelativeLocation = FVector::ZeroVector;
	double CycleTime = 0.0;
	double StartupElapsed = 0.0;
	bool bStartupComplete = false;
	bool bStartupInitialized = false;
	bool bCycleSpeedsSampled = false;
};
