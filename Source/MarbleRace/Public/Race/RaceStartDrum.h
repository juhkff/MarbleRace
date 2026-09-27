#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RaceStartDrum.generated.h"

class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * 旋转的开局滚筒，用来打乱小球的出发位置。
 *
 * 形状是躺在比赛平面（XZ）里的圆环，环壁有一圈碰撞，内部有径向拨片。
 * 小球被锁在 XZ 平面上，碰撞开着时出不去。倒计时期间滚筒旋转，
 * 每颗球被带到不同的位置。松手时关掉碰撞，球带着当时的切线速度离开。
 *
 * 没有导入网格时，用引擎方块拼出整套结构。
 */
UCLASS(meta=(DisplayName="开局滚筒"))
class MARBLERACE_API ARaceStartDrum : public AActor
{
	GENERATED_BODY()

public:
	ARaceStartDrum();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 圆环内半径。小球放在这个圆里面。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|滚筒", meta=(DisplayName="半径", ClampMin="60", ClampMax="2000"))
	float Radius = 240.0f;

	/** 环壁的径向厚度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|滚筒", meta=(DisplayName="环壁厚度", ClampMin="2", ClampMax="200"))
	float BandThickness = 16.0f;

	/** 沿旋转轴的厚度，也就是侧视时滚筒的深度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|滚筒", meta=(DisplayName="深度", ClampMin="10", ClampMax="600"))
	float DrumDepth = 90.0f;

	/** 组成环壁的方块数量。越多越圆。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|滚筒", meta=(DisplayName="环壁分段", ClampMin="6", ClampMax="96"))
	int32 RimSegments = 18;

	/** 带动并搅动小球的径向拨片数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|滚筒", meta=(DisplayName="拨片数量", ClampMin="0", ClampMax="12"))
	int32 BladeCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|滚筒", meta=(DisplayName="拨片厚度", ClampMin="2", ClampMax="100"))
	float BladeThickness = 14.0f;

	/** 旋转时的角速度，单位是度/秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|滚筒", meta=(DisplayName="转速"))
	float SpinSpeedDegrees = 140.0f;

	/** 导演摆放时共用的随机角度范围。各槽之间仍保持安全间距。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|滚筒", meta=(DisplayName="摆放抖动", ClampMin="0", ClampMax="180"))
	float PlacementJitterDegrees = 25.0f;

	/** 松手后继续转，只为了好看。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|滚筒", meta=(DisplayName="松手后继续旋转"))
	bool bKeepSpinningAfterRelease = false;

	/** 套在环上的材质。留空则用网格里已经烘焙的材质。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|滚筒", meta=(DisplayName="滚筒材质"))
	TObjectPtr<UMaterialInterface> DrumMaterial;

	/**
	 * 一整圈的环网格（圆环加内部拨片）。用一个导入物体代替一圈方块，
	 * 这样环才是真正闭合的圆。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|滚筒", meta=(DisplayName="滚筒网格"))
	TObjectPtr<UStaticMesh> DrumMesh;

	// ---- 给导演调用的接口 -------------------------------------------------

	/** 开始（或重新开始）倒计时用的旋转。 */
	UFUNCTION(BlueprintCallable, Category="比赛|滚筒", meta=(DisplayName="开始旋转"))
	void BeginSpin(float SpeedDegrees);

	/** 停止旋转并关掉环的碰撞，让小球离开。 */
	UFUNCTION(BlueprintCallable, Category="比赛|滚筒", meta=(DisplayName="松开滚筒"))
	void ReleaseDrum();

	/** 恢复碰撞并清掉旋转，滚筒可以再次接住小球。 */
	UFUNCTION(BlueprintCallable, Category="比赛|滚筒", meta=(DisplayName="重置滚筒"))
	void ResetDrum();

	UFUNCTION(BlueprintPure, Category="比赛|滚筒", meta=(DisplayName="是否在旋转"))
	bool IsSpinning() const { return bSpinning; }

	/** 当前转子角度（度）。导演用它把小球按在环上。 */
	UFUNCTION(BlueprintPure, Category="比赛|滚筒", meta=(DisplayName="当前角度"))
	float GetSpinAngleDegrees() const { return CurrentSpinDegrees; }

	/** 按小球自己的半径，计算它在环内被按住的半径。 */
	UFUNCTION(BlueprintPure, Category="比赛|滚筒", meta=(DisplayName="按住半径"))
	float GetHoldingRadius(float RacerRadius) const;

	/**
	 * 把一颗球按均匀角度放进环内，避免开局互相重叠。
	 * Index 是槽位，Total 是参加的球数。
	 */
	void PlaceRacer(AActor* Racer, int32 Index, int32 Total, float RacerRadius, bool bRandomizeAngle);

	/** 按当前形状参数重建生成的部件。运行时改了尺寸后调用。 */
	UFUNCTION(BlueprintCallable, Category="比赛|滚筒", meta=(DisplayName="重建外形"))
	void RebuildGeometry();

private:
	UPROPERTY(VisibleAnywhere, Category="比赛|滚筒", meta=(DisplayName="根组件"))
	TObjectPtr<USceneComponent> Root;

	/** 所有跟着滚筒转的东西都挂在这下面。 */
	UPROPERTY(VisibleAnywhere, Category="比赛|滚筒", meta=(DisplayName="转子"))
	TObjectPtr<USceneComponent> Rotor;

	/** 环本身。通常情况下唯一的可见和碰撞物体。 */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> RingMesh;

	/** 没有环网格时才用的方块拼装。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Pieces;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;

	bool bSpinning = false;
	float CurrentSpinDegrees = 0.0f;
	/** 当前部件对应的形状签名，构造时用来跳过不必要的重建。 */
	int32 BuiltSignature = 0;

	UStaticMesh* ResolveCubeMesh();
	void SetPiecesCollisionEnabled(bool bEnabled);
	void AddPiece(const FTransform& LocalTransform, const FVector& Scale, bool bVisible);
	int32 ComputeShapeSignature() const;
	/** 拨片长度。保持在环壁以内，避免小球生成时嵌在拨片里。 */
	float GetBladeLength() const;
	/**
	 * 用方块拼出环。运动网格不能用逐多边形碰撞，凸包又会留缝，
	 * 所以这圈方块始终当作不可见碰撞体；没有环网格时才同时当作可见外形。
	 */
	void RebuildFallbackBlocks(float SafeRadius, float SafeThickness, float SafeDepth, bool bVisible);
};
