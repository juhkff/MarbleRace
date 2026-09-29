#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMeshActor.h"
#include "RelocationSourceZone.generated.h"

class ARelocationManagerActor;
class UPrimitiveComponent;

/** 传送/陷阱的入口；只负责把球交给显式指定的同一个管理器。 */
UCLASS(Blueprintable, meta=(DisplayName="重定位入口"))
class MARBLERACE_API ARelocationSourceZone : public AStaticMeshActor
{
	GENERATED_BODY()

public:
	ARelocationSourceZone();

	/** 必须在每个传送和陷阱区域实例上指定同区域的管理器。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="重定位", meta=(DisplayName="重定位Manager"))
	TObjectPtr<ARelocationManagerActor> RelocationManager;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleEntrance(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	                    UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	                    bool bFromSweep, const FHitResult& SweepResult);
};
