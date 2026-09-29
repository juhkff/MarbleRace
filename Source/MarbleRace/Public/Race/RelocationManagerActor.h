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

	/**
	 * 弹珠重新出现的世界坐标；管理器自身的放置位置不是出口。
	 * 值是纯数据，所以可以在蓝图默认值里填；管理器被摆到别处时这里不会自动跟随。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位", meta=(DisplayName="重生位置"))
	FVector RespawnLocation = FVector::ZeroVector;

	/**
	 * 重生后是否打开弹珠的世界重力，并把它当作从静止开始下落。
	 * 重生点落在重力场里时可以关掉，改由重力场的方向和大小推动弹珠。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位", meta=(DisplayName="重生后启用重力"))
	bool bEnableGravityAfterRespawn = true;

	/**
	 * 开启后，每次尝试放出弹珠时都会围绕“重生位置”重新抽一个落点，
	 * 让同一出口的连续重生不会叠在一处。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位|随机",
		meta=(DisplayName="开启随机范围"))
	bool bEnableRandomRespawn = false;

	/**
	 * 每个轴的最大波动量（cm）；实际偏移在各轴的 ±该值 内均匀取值。
	 * 填 0 的轴保持重生位置不变 —— 侧视赛道把 Y 留成 0 即可。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位|随机",
		meta=(DisplayName="随机范围", EditCondition="bEnableRandomRespawn", ClampMin="0", ForceUnits="cm"))
	FVector RespawnRandomRange = FVector::ZeroVector;

	/**
	 * 抽一次实际落点：关闭随机时就是“重生位置”；
	 * 开启后在每个轴的 ±随机范围 内独立均匀取样（范围填 0 的轴不动）。
	 */
	UFUNCTION(BlueprintPure, Category="重定位", meta=(DisplayName="抽取重生位置"))
	FVector RollRespawnLocation() const;

	/** 使用用户在“重定位Manager”蓝图里添加的组件，不另造第二份队列。 */
	URelocationManagerComponent* GetRelocationComponent() const;

private:
	UPROPERTY(VisibleAnywhere, Category="重定位")
	TObjectPtr<USceneComponent> SceneRoot;
};
