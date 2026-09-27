#pragma once

#include "CoreMinimal.h"
#include "RaceRosterTypes.generated.h"

class USoundBase;

/**
 * 名单里的一颗小球：叫什么、长什么样、领跑时播哪首歌。
 * 存在存档里，角色设置页才能在运行时增删改。打包后的数据资产做不到这一点。
 */
USTRUCT(BlueprintType, meta=(DisplayName="角色条目"))
struct FRaceCharacterEntry
{
	GENERATED_BODY()

	/** 显示在小球旁边的名字，字体支持的语言都可以写。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="角色", meta=(DisplayName="姓名"))
	FString DisplayName = TEXT("小球");

	/** 身份颜色：圆盘、姓名和尾迹都用它。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="角色", meta=(DisplayName="颜色"))
	FLinearColor Color = FLinearColor(0.9f, 0.3f, 0.3f, 1.0f);

	/**
	 * 头像的原始图片字节。放在存档里，不依赖原来的文件还在原路径。
	 * 设成只读，是为了让反射系统承认这个分类被用到了。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="角色", meta=(DisplayName="头像数据"))
	TArray<uint8> PortraitPngData;

	/** 图片来自哪里。界面上会显示，再次导入时也会用。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="角色", meta=(DisplayName="头像来源"))
	FString PortraitSourcePath;

	/** 这颗球领跑时播放的主题曲。软引用，开赛前加载。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="音乐", meta=(DisplayName="主题曲"))
	TSoftObjectPtr<USoundBase> ThemeMusic;

	/** 关掉后仍留在名单里，但下一场不参加。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="角色", meta=(DisplayName="参赛"))
	bool bEnabled = true;

	/** 有可用的头像字节时为真。 */
	bool HasPortrait() const { return PortraitPngData.Num() > 0; }
};
