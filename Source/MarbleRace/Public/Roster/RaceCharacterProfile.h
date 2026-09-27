#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RaceCharacterProfile.generated.h"

class UTexture2D;
class USoundBase;

/** 每个角色一份数据资产。头像、姓名和主题曲都从这里来。 */
UCLASS(BlueprintType, meta=(DisplayName="角色资料"))
class MARBLERACE_API URaceCharacterProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="角色", meta=(DisplayName="角色编号"))
	FName CharacterId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="角色", meta=(DisplayName="姓名"))
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="角色", meta=(DisplayName="头像"))
	TObjectPtr<UTexture2D> Portrait;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="角色", meta=(DisplayName="颜色"))
	FLinearColor Color = FLinearColor::White;

	// 开赛前就加载好，反超时不再临时加载。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="音乐", meta=(DisplayName="主题曲"))
	TObjectPtr<USoundBase> ThemeMusic;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="音乐", meta=(DisplayName="音乐起始偏移", ClampMin="0"))
	float MusicStartOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="音乐", meta=(DisplayName="音乐音量", ClampMin="0", ClampMax="2"))
	float MusicGain = 1.f;
};
