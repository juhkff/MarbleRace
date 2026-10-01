#pragma once

#include "CoreMinimal.h"
#include "Race/RelocationManagerActor.h"
#include "ComparisonRelocationManagerActor.generated.h"

/** 按本次触发的陷阱编号选择出生点，不使用弹珠的历史轮询进度。 */
UCLASS(Blueprintable, meta=(DisplayName="比较重定位管理器"))
class MARBLERACE_API AComparisonRelocationManagerActor : public ARelocationManagerActor
{
	GENERATED_BODY()
public:
	AComparisonRelocationManagerActor();
	/** 第 n 个陷阱选择下标 n % 数组长度；0 为默认入口，空数组等待。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位", meta=(DisplayName="比较重生位置", MakeEditWidget=true))
	TArray<FVector> RespawnLocations;
	virtual void SetMarbleRespawnSource(AActor* Marble, int32 SourceNumber) override;
	virtual bool TryRollRespawnLocation(AActor* Marble, FVector& OutPosition) const override;
	virtual void CommitMarbleRespawn(AActor* Marble) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	TMap<TWeakObjectPtr<AActor>, int32> SourceNumbers;
};
