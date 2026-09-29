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

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 未显式指定时，顺着父 Actor 找同级的重定位管理器子 Actor。 */
	ARelocationManagerActor* ResolveRelocationManager() const;

	UFUNCTION()
	void HandleEntrance(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	                    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	                    bool bFromSweep, const FHitResult& SweepResult);
};
