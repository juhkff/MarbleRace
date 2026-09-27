#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RaceMechanism.generated.h"

class UMaterialInstanceDynamic;
class UPhysicalMaterial;
class UStaticMeshComponent;

/** 赛道零件怎么动。反弹台本身不动，只把小球向上弹起。 */
UENUM(BlueprintType, meta=(DisplayName="机关运动方式"))
enum class ERaceMechanismMode : uint8
{
	/** 青色台面：把向下的撞击换成有上限的向上速度。 */
	Bounce UMETA(DisplayName="反弹"),
	/** 来回滑动、用碰撞把小球推开的杆。 */
	Oscillate UMETA(DisplayName="往复"),
	/** 在赛道平面里旋转、把小球甩出去的拨板。 */
	Spin UMETA(DisplayName="旋转")
};

/**
 * 赛道上的一块运动或反弹零件，用引擎方块拼成，不需要单独的网格资源。
 * 往复杆和拨板是很重的运动学刚体：按脚本运动，同时仍会碰撞，
 * 小球不会从一段动画里穿过去。
 */
UCLASS(meta=(DisplayName="赛道机关"))
class MARBLERACE_API ARaceMechanism : public AActor
{
	GENERATED_BODY()

public:
	ARaceMechanism();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|机关", meta=(DisplayName="运动方式"))
	ERaceMechanismMode Mode = ERaceMechanismMode::Bounce;

	/** 方块的完整尺寸。引擎方块的边长是 100。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|机关", meta=(DisplayName="尺寸"))
	FVector Size = FVector(200.0f, 80.0f, 16.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|机关", meta=(DisplayName="颜色"))
	FLinearColor Color = FLinearColor(0.15f, 0.95f, 1.0f);

	/** 小球落到反弹台上时获得的向上速度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|机关", meta=(DisplayName="反弹速度", ClampMin="0"))
	float BounceSpeed = 850.0f;

	/** 速度上限，避免一次次叠加把球弹出赛道。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|机关", meta=(DisplayName="最高带出速度", ClampMin="0"))
	float MaxCarrySpeed = 1100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|机关", meta=(DisplayName="反弹冷却", ClampMin="0.05"))
	float BounceCooldownSeconds = 0.2f;

	/** 往复运动的世界轴向。勾选为左右，不勾为上下。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|机关", meta=(DisplayName="沿左右往复"))
	bool bOscillateAlongX = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|机关", meta=(DisplayName="往复幅度", ClampMin="0"))
	float OscillateAmplitude = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|机关", meta=(DisplayName="往复周期", ClampMin="0.2"))
	float OscillatePeriod = 2.6f;

	/** 错开正弦波，让相邻的杆不要同步。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|机关", meta=(DisplayName="相位"))
	float PhaseSeconds = 0.0f;

	/** 绕深度轴（世界 Y）的转速，单位度/秒，扫过 XZ 比赛平面。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|机关", meta=(DisplayName="转速"))
	float SpinDegreesPerSecond = 150.0f;

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> SurfaceMaterial;

	FVector MotionOrigin = FVector::ZeroVector;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, float> BounceCooldowns;

	void RebuildBody();
	void ConfigureMotion();
	UMaterialInterface* ResolveColorMaterial() const;

	UFUNCTION()
	void HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		FVector NormalImpulse, const FHitResult& Hit);
};
