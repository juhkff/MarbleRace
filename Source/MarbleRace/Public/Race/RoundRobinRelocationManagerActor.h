#pragma once

#include "CoreMinimal.h"
#include "Race/RelocationManagerActor.h"
#include "RoundRobinRelocationManagerActor.generated.h"

/** 每颗弹珠独立循环重生位置；不同弹珠不会共享或推进彼此的索引。 */
UCLASS(Blueprintable, meta=(DisplayName="轮询重定位管理器"))
class MARBLERACE_API ARoundRobinRelocationManagerActor : public ARelocationManagerActor
{
	GENERATED_BODY()

public:
	ARoundRobinRelocationManagerActor();

	/** 与普通管理器相同的坐标系。每颗弹珠从第一个位置开始，成功重生后循环推进。空数组暂停放出。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位", meta=(DisplayName="轮询重生位置", MakeEditWidget=true))
	TArray<FVector> RespawnLocations;

	virtual bool TryRollRespawnLocation(AActor* Marble, FVector& OutPosition) const override;
	virtual void CommitMarbleRespawn(AActor* Marble) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// 弱引用不延长弹珠生命周期；仅当前管理器实例持有运行期进度。
	TMap<TWeakObjectPtr<AActor>, int32> NextRespawnIndices;
};
