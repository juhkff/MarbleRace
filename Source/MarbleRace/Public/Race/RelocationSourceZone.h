#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMeshActor.h"
#include "RelocationSourceZone.generated.h"

class ARelocationManagerActor;
class UPrimitiveComponent;

/** 传送/陷阱的入口；把球交给同区域的管理器（显式指定，或自动找同级子 Actor）。 */
UCLASS(Blueprintable, meta=(DisplayName="重定位入口"))
class MARBLERACE_API ARelocationSourceZone : public AStaticMeshActor
{
	GENERATED_BODY()

public:
	ARelocationSourceZone();

	/**
	 * 同区域的管理器。留空时会在 BeginPlay 顺着父 Actor 自动找同级的重定位管理器子 Actor，
	 * 所以被放进别的蓝图当子 Actor 时不必手填；关卡里单独摆放的区域仍需在此指定。
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="重定位", meta=(DisplayName="重定位Manager"))
	TObjectPtr<ARelocationManagerActor> RelocationManager;

	/** 比较管理器使用 n % 出生点数量；1 起编号，0 用于默认入口到第一个位置。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位", meta=(DisplayName="陷阱编号", ClampMin="0"))
	int32 TrapNumber = 0;

	/** 弹珠进入区域时修改该球的恢复力；默认关闭，不改变原有传送行为。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="过线设置",
		meta=(DisplayName="开启修改恢复力"))
	bool bEnableRestitutionChange = false;

	/**
	 * 恢复力系数（0～1）。仅修改触发弹珠的运行时材质，不修改共用材质资产。
	 * 离开区域和重定位后仍保留，直到经过另一个开启该设置的区域再次修改。
	 * 最终碰撞效果仍取决于双方材质的合并规则和碰撞物体的运动。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="过线设置",
		meta=(DisplayName="过线后恢复力", EditCondition="bEnableRestitutionChange", ClampMin="0", ClampMax="1"))
	float RestitutionAfterCrossing = 0.f;

	/** 按当前区域设置修改正在模拟物理的弹珠；关闭功能或对象无效时不做任何修改。 */
	UFUNCTION(BlueprintCallable, Category="过线设置", meta=(DisplayName="应用过线恢复力"))
	bool ApplyCrossingRestitution(AActor* Marble, UPrimitiveComponent* Body);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UClass> MarbleClass;

	/** 未显式指定时，顺着父 Actor 找同级的重定位管理器子 Actor。 */
	ARelocationManagerActor* ResolveRelocationManager() const;

	UFUNCTION()
	void HandleEntrance(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	                    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	                    bool bFromSweep, const FHitResult& SweepResult);
};
