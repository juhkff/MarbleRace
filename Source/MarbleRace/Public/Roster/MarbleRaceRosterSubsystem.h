#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Roster/RaceRosterTypes.h"
#include "MarbleRaceRosterSubsystem.generated.h"

class UTexture2D;
class USoundBase;

/**
 * 掌管整场会话的小球名单，并把它存进一个存档槽。
 *
 * 故意用游戏实例子系统：名单必须从主菜单活到比赛关卡，
 * 它也是唯一一份不依附于单场比赛的角色数据。
 */
UCLASS(meta=(DisplayName="角色名单"))
class MARBLERACE_API UMarbleRaceRosterSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 加载名单。第一次会创建默认小球。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 存档槽名称，也是 Saved/SaveGames 下的文件名。 */
	static const TCHAR* GetSaveSlotName() { return TEXT("MarbleRaceRoster"); }

	/** C++ 访问器。TArray 引用不能作为蓝图函数的返回值。 */
	const TArray<FRaceCharacterEntry>& GetEntries() const { return Entries; }

	UFUNCTION(BlueprintPure, Category="比赛|名单", meta=(DisplayName="条目数量"))
	int32 GetEntryCount() const { return Entries.Num(); }

	/** 下一场真正会上场的小球数量。 */
	UFUNCTION(BlueprintPure, Category="比赛|名单", meta=(DisplayName="参赛数量"))
	int32 GetEnabledCount() const;

	/** 某个比赛槽位对应的已启用条目下标。没有则为 INDEX_NONE。 */
	int32 GetEnabledEntryIndex(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category="比赛|名单", meta=(DisplayName="下标是否有效"))
	bool IsValidIndex(int32 Index) const { return Entries.IsValidIndex(Index); }

	/** 追加一颗球，名字自动生成，颜色取调色板的下一种。 */
	int32 AddEntry();

	bool RemoveEntry(int32 Index);
	bool MoveEntry(int32 Index, int32 Delta);

	bool SetDisplayName(int32 Index, const FString& NewName);
	bool SetColor(int32 Index, const FLinearColor& NewColor);
	bool SetEnabled(int32 Index, bool bInEnabled);
	bool SetThemeMusic(int32 Index, USoundBase* Music);
	bool SetThemeMusicPath(int32 Index, const FSoftObjectPath& MusicPath);

	/**
	 * 从磁盘导入图片，把字节存进条目，并刷新缓存的缩略图。
	 * 文件不存在或读不了时返回 false，并写入 OutError。
	 */
	bool SetPortraitFromFile(int32 Index, const FString& SourcePath, FString& OutError);

	/** 去掉头像，小球退回带姓名首字的色盘。 */
	bool ClearPortrait(int32 Index);

	/** 给名单界面用的缩略图，第一次用到才创建。没有头像时为空。 */
	UTexture2D* GetPortraitTexture(int32 Index);

	/** 工程里已导入的声音，给主题曲选择器用。第一次之后会缓存。 */
	void RefreshAvailableThemes();
	const TArray<TSoftObjectPtr<USoundBase>>& GetAvailableThemes() const { return AvailableThemes; }

	/** 某条目主题曲的可读名称。没设置时是占位文字。 */
	FString GetThemeDisplayName(int32 Index) const;

	/** Saved/Portraits 里的图片，作为一键选用的头像来源。 */
	void RefreshPortraitFolder();
	const TArray<FString>& GetPortraitFolderFiles() const { return PortraitFolderFiles; }
	static FString GetPortraitFolderPath();

	bool SaveRoster();
	bool LoadRoster();

	/** 恢复内置的默认小球。没有存档时使用。 */
	void ResetToDefaults();

private:
	UPROPERTY(Transient)
	TArray<FRaceCharacterEntry> Entries;

	/** 由条目字节生成的缩略图。字节才是权威数据，所以缩略图不存档。 */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UTexture2D>> PortraitCache;

	UPROPERTY(Transient)
	TArray<TSoftObjectPtr<USoundBase>> AvailableThemes;

	UPROPERTY(Transient)
	TArray<FString> PortraitFolderFiles;

	/** 下一颗新建小球要用的颜色序号。 */
	int32 NextPaletteIndex = 0;

	static FLinearColor GetPaletteColor(int32 PaletteIndex);
	UTexture2D* BuildPortraitTexture(const TArray<uint8>& Bytes);
};
