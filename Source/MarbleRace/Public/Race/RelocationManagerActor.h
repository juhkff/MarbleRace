#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RelocationManagerActor.generated.h"

class USceneComponent;
class URelocationManagerComponent;

/** 一个区域共用的重定位管理器；由各传送/陷阱区域显式引用。 */
UCLASS(Blueprintable, meta=(DisplayName="重定位管理器"))
class MARBLERACE_API ARelocationManagerActor : public AActor
{
	GENERATED_BODY()

public:
	ARelocationManagerActor();

	/** 弹珠重新出现的世界坐标；管理器自身的放置位置不是出口。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="重定位", meta=(DisplayName="重生位置"))
	FVector RespawnLocation = FVector::ZeroVector;

	/** 使用用户在“重定位Manager”蓝图里添加的组件，不另造第二份队列。 */
	URelocationManagerComponent* GetRelocationComponent() const;

private:
	UPROPERTY(VisibleAnywhere, Category="重定位")
	TObjectPtr<USceneComponent> SceneRoot;
};
