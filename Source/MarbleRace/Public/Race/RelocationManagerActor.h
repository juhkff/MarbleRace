#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RelocationManagerActor.generated.h"

class USceneComponent;
class URelocationManagerComponent;

/** 每个局部轴独立抽样，抽取的向量会归一化为作用力方向。 */
USTRUCT(BlueprintType)
struct MARBLERACE_API FRespawnImpulseDirectionRange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位|作用力", meta=(DisplayName="最小方向"))
	FVector Minimum = FVector::UpVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位|作用力", meta=(DisplayName="最大方向"))
	FVector Maximum = FVector::UpVector;
};

/** 一次冲量大小的取值范围；物理冲量会考虑弹珠质量。 */
USTRUCT(BlueprintType)
struct MARBLERACE_API FRespawnImpulseMagnitudeRange
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位|作用力", meta=(DisplayName="最小值", ClampMin="0"))
	float Minimum = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位|作用力", meta=(DisplayName="最大值", ClampMin="0"))
	float Maximum = 1000.f;
};

/** 一个区域共用的重定位管理器；由各传送/陷阱区域显式引用。 */
UCLASS(Blueprintable, meta=(DisplayName="重定位管理器"))
class MARBLERACE_API ARelocationManagerActor : public AActor
{
	GENERATED_BODY()

public:
	ARelocationManagerActor();

	/**
	 * 弹珠重新出现的位置；作为关卡蓝图的子 Actor 时，填写管理器父组件坐标系中的蓝图位置。
	 * 放出时自动转换为世界坐标，随关卡的平移、旋转和缩放变化，不叠加管理器自身的摆放偏移。
	 * 单独摆放且未附加的管理器仍按世界坐标解释。已有数值不会被转换或覆盖。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位", meta=(DisplayName="重生位置", EditCondition="!bUsesRespawnLocationArray", EditConditionHides))
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
	 * 使用和重生位置相同的坐标系；作为关卡子 Actor 时，先在蓝图局部轴上抽样，再转换到世界。
	 * 填 0 的轴保持局部重生位置不变 —— 侧视赛道把 Y 留成 0 即可。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位|随机",
		meta=(DisplayName="随机范围", EditCondition="bEnableRandomRespawn", ClampMin="0", ForceUnits="cm"))
	FVector RespawnRandomRange = FVector::ZeroVector;

	/** 成功重生后施加一次冲量；默认关闭，等待出口和取消排队时不施加。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位|作用力", meta=(DisplayName="开启作用力"))
	bool bEnableRespawnImpulse = false;

	/**
	 * 每个轴在最小/最大方向之间独立随机取值后归一化；两端相同时为固定方向。
	 * 使用和重生位置相同的蓝图坐标轴，只转换方向、不受关卡缩放影响；零向量不产生冲量。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位|作用力",
		meta=(DisplayName="作用力方向范围", EditCondition="bEnableRespawnImpulse"))
	FRespawnImpulseDirectionRange RespawnImpulseDirectionRange;

	/**
	 * 每次成功重生独立随机抽取冲量大小（kg·cm/s），质量越大的球速度变化越小。
	 * 负数按 0 处理，最小/最大填反时自动交换。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="重定位|作用力",
		meta=(DisplayName="作用力大小范围", EditCondition="bEnableRespawnImpulse"))
	FRespawnImpulseMagnitudeRange RespawnImpulseMagnitudeRange;

	/**
	 * 返回一次实际落点的世界坐标：在配置坐标系中抽样，再按所属关卡/父组件的变换转换。
	 * 关闭随机时转换固定的“重生位置”；开启后在各局部轴的 ±随机范围 内独立均匀取样。
	 */
	UFUNCTION(BlueprintPure, Category="重定位", meta=(DisplayName="抽取重生位置"))
	FVector RollRespawnLocation() const;

	/** 队列按弹珠选择落点，成功放出后才提交本次重生。 */
	virtual bool TryRollRespawnLocation(AActor* Marble, FVector& OutPosition) const;
	virtual void CommitMarbleRespawn(AActor* Marble);
	/** 入队成功时记录触发入口编号，0 表示默认入口。 */
	virtual void SetMarbleRespawnSource(AActor* Marble, int32 SourceNumber) {}

	/** 抽取一次世界空间冲量；关闭作用力、零方向或零大小时返回零向量。 */
	UFUNCTION(BlueprintPure, Category="重定位|作用力", meta=(DisplayName="抽取重生作用力"))
	FVector RollRespawnImpulse() const;

	/** 使用用户在“重定位Manager”蓝图里添加的组件，不另造第二份队列。 */
	URelocationManagerComponent* GetRelocationComponent() const;

protected:
	/** 仅用于编辑器决定是否显示单点配置，由管理器类型决定，不保存到资产。 */
	UPROPERTY(Transient)
	bool bUsesRespawnLocationArray = false;

	FVector RollRespawnLocationAt(const FVector& BaseLocation) const;

private:
	/** 重生位置与冲量方向共用父组件坐标系，不使用 Manager 自身摆放偏移。 */
	const USceneComponent* GetRespawnCoordinateFrame() const;

	UPROPERTY(VisibleAnywhere, Category="重定位")
	TObjectPtr<USceneComponent> SceneRoot;
};
