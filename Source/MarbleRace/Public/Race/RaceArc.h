#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RaceArc.generated.h"

class UPhysicalMaterial;
class UStaticMeshComponent;

/**
 * 可放置的圆弧壁。侧视平面是 XZ，绕深度轴（局部 Y）转动。
 * 缺口跟着圆弧一起转，弹珠从缺口离开。缺口会被限制到比弹珠更宽。
 * 转速为 0 时是固定圆底，不会自己转。
 */
UCLASS(meta=(DisplayName="旋转圆弧"))
class MARBLERACE_API ARaceArc : public AActor
{
	GENERATED_BODY()

public:
	ARaceArc();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 圆弧中心线到圆心的距离，单位厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|圆弧", meta=(DisplayName="半径", ClampMin="90"))
	float Radius = 220.0f;

	/** 实体部分扫过的角度。剩下的就是缺口，默认朝下。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|圆弧", meta=(DisplayName="实体张角", ClampMin="30", ClampMax="330"))
	float SweepDegrees = 280.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|圆弧", meta=(DisplayName="壁厚", ClampMin="8"))
	float Thickness = 24.0f;

	/** 沿深度（Y）的厚度，要盖住赛道。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|圆弧", meta=(DisplayName="深度", ClampMin="40"))
	float Depth = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|圆弧", meta=(DisplayName="分段数", ClampMin="4", ClampMax="32"))
	int32 SegmentCount = 14;

	/** 绕局部 Y 的转速，单位度/秒。正值把朝下的缺口转向一侧。0 表示固定。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|圆弧", meta=(DisplayName="转速"))
	float SpinDegreesPerSecond = 28.0f;

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Segments;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> SurfaceMaterial;

	void RebuildSegments();
	void ConfigureMotion();
	void ClampShape();
	float SegmentAngleRadians(int32 Index) const;
	bool ShouldSpin() const;
};
