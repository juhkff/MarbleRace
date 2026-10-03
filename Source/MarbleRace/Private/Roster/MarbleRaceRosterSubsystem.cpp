#include "Roster/MarbleRaceRosterSubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Roster/RaceRosterSave.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "Race/MarbleThemePlayback.h"

namespace
{
	/** 彼此不同的平涂颜色，浅色赛道上任意两颗球都能分清。 */
	const FLinearColor GRacePalette[] =
	{
		FLinearColor(0.91f, 0.30f, 0.24f), FLinearColor(0.20f, 0.44f, 0.86f),
		FLinearColor(0.36f, 0.76f, 0.32f), FLinearColor(0.95f, 0.74f, 0.20f),
		FLinearColor(0.62f, 0.33f, 0.83f), FLinearColor(0.16f, 0.72f, 0.74f),
		FLinearColor(0.94f, 0.47f, 0.71f), FLinearColor(0.55f, 0.58f, 0.62f),
		FLinearColor(0.42f, 0.30f, 0.22f), FLinearColor(0.72f, 0.86f, 0.24f),
		FLinearColor(0.25f, 0.25f, 0.32f), FLinearColor(0.98f, 0.58f, 0.22f)
	};

	constexpr int32 GRacePaletteSize = UE_ARRAY_COUNT(GRacePalette);

}

FLinearColor UMarbleRaceRosterSubsystem::GetPaletteColor(int32 PaletteIndex)
{
	const int32 SafeIndex = GRacePaletteSize > 0 ? ((PaletteIndex % GRacePaletteSize) + GRacePaletteSize) % GRacePaletteSize : 0;
	return GRacePalette[SafeIndex];
}

void UMarbleRaceRosterSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!LoadRoster())
	{
		ResetToDefaults();
		SaveRoster();
	}
}

int32 UMarbleRaceRosterSubsystem::GetEnabledCount() const
{
	int32 Count = 0;
	for (const FRaceCharacterEntry& Entry : Entries)
	{
		if (Entry.bEnabled)
		{
			++Count;
		}
	}
	return Count;
}

int32 UMarbleRaceRosterSubsystem::GetEnabledEntryIndex(int32 SlotIndex) const
{
	if (SlotIndex < 0)
	{
		return INDEX_NONE;
	}

	int32 Seen = 0;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		if (!Entries[Index].bEnabled)
		{
			continue;
		}
		if (Seen == SlotIndex)
		{
			return Index;
		}
		++Seen;
	}
	return INDEX_NONE;
}

int32 UMarbleRaceRosterSubsystem::AddEntry()
{
	FRaceCharacterEntry Entry;
	Entry.Color = GetPaletteColor(NextPaletteIndex);
	++NextPaletteIndex;
	Entry.DisplayName = FString::Printf(TEXT("小球 %d"), Entries.Num() + 1);

	const int32 NewIndex = Entries.Add(Entry);
	SaveRoster();
	return NewIndex;
}

bool UMarbleRaceRosterSubsystem::RemoveEntry(int32 Index)
{
	if (!Entries.IsValidIndex(Index))
	{
		return false;
	}

	Entries.RemoveAt(Index);
	if (PortraitCache.FindRef(Index))
	{
		PortraitCache.Remove(Index);
	}

	// 缓存按下标存，被删条目后面的每一项都往前移了一格。
	TMap<int32, TObjectPtr<UTexture2D>> Rebuilt;
	for (int32 OldIndex = Index + 1; OldIndex <= Entries.Num(); ++OldIndex)
	{
		if (const TObjectPtr<UTexture2D>* Found = PortraitCache.Find(OldIndex))
		{
			Rebuilt.Add(OldIndex - 1, *Found);
		}
	}
	PortraitCache = MoveTemp(Rebuilt);

	SaveRoster();
	return true;
}

bool UMarbleRaceRosterSubsystem::MoveEntry(const int32 Index, const int32 Delta)
{
	const int32 Target = Index + Delta;
	if (!Entries.IsValidIndex(Index) || !Entries.IsValidIndex(Target))
	{
		return false;
	}

	Entries.Swap(Index, Target);

	// 缩略图和条目一起交换，列表才继续显示对应的脸。
	TObjectPtr<UTexture2D> MovedEntry = PortraitCache.FindRef(Index);
	TObjectPtr<UTexture2D> OtherEntry = PortraitCache.FindRef(Target);
	PortraitCache.Remove(Index);
	PortraitCache.Remove(Target);
	if (MovedEntry)
	{
		PortraitCache.Add(Target, MovedEntry);
	}
	if (OtherEntry)
	{
		PortraitCache.Add(Index, OtherEntry);
	}

	SaveRoster();
	return true;
}

bool UMarbleRaceRosterSubsystem::SetDisplayName(const int32 Index, const FString& NewName)
{
	if (!Entries.IsValidIndex(Index))
	{
		return false;
	}
	Entries[Index].DisplayName = NewName.IsEmpty() ? TEXT("小球") : NewName;
	SaveRoster();
	return true;
}

bool UMarbleRaceRosterSubsystem::SetColor(int32 Index, const FLinearColor& NewColor)
{
	if (!Entries.IsValidIndex(Index))
	{
		return false;
	}
	Entries[Index].Color = NewColor;
	Entries[Index].Color.A = 1.f;
	SaveRoster();
	return true;
}

bool UMarbleRaceRosterSubsystem::SetEnabled(int32 Index, bool bInEnabled)
{
	if (!Entries.IsValidIndex(Index))
	{
		return false;
	}
	Entries[Index].bEnabled = bInEnabled;
	SaveRoster();
	return true;
}

bool UMarbleRaceRosterSubsystem::SetThemeMusic(int32 Index, USoundBase* Music)
{
	if (!Entries.IsValidIndex(Index))
	{
		return false;
	}
	Entries[Index].ThemeMusic = Music;
	Entries[Index].ThemeTitle.Reset();
	Entries[Index].ThemeStartTimeSeconds = 0.f;
	SaveRoster();
	return true;
}

bool UMarbleRaceRosterSubsystem::SetThemeMusicPath(int32 Index, const FSoftObjectPath& MusicPath)
{
	if (!Entries.IsValidIndex(Index))
	{
		return false;
	}
	Entries[Index].ThemeMusic = TSoftObjectPtr<USoundBase>(MusicPath);
	Entries[Index].ThemeTitle.Reset();
	Entries[Index].ThemeStartTimeSeconds = 0.f;
	SaveRoster();
	return true;
}

float UMarbleRaceRosterSubsystem::GetThemeDuration(int32 Index) const
{
	if (!Entries.IsValidIndex(Index)) return 0.f;
	USoundBase* Music = Entries[Index].ThemeMusic.LoadSynchronous();
	const USoundWave* Wave = Cast<USoundWave>(Music);
	const float Duration = Wave ? Wave->Duration : Music ? Music->GetDuration() : 0.f;
	return FMath::IsFinite(Duration) && Duration > 0.f ? Duration : 0.f;
}

bool UMarbleRaceRosterSubsystem::SetThemeStartTime(int32 Index, float Seconds)
{
	if (!Entries.IsValidIndex(Index) || !FMath::IsFinite(Seconds)) return false;
	Entries[Index].ThemeStartTimeSeconds = FMarbleThemePlayback::ClampStartTime(Seconds, GetThemeDuration(Index));
	return SaveRoster();
}

bool UMarbleRaceRosterSubsystem::SetPortraitFromFile(int32 Index, const FString& SourcePath, FString& OutError)
{
	if (!Entries.IsValidIndex(Index))
	{
		OutError = TEXT("无效的小球索引");
		return false;
	}

	TArray<uint8> FileBytes;
	if (!FFileHelper::LoadFileToArray(FileBytes, *SourcePath))
	{
		OutError = FString::Printf(TEXT("读取失败：%s"), *SourcePath);
		return false;
	}
	if (FileBytes.Num() == 0)
	{
		OutError = TEXT("文件为空");
		return false;
	}

	// 存之前先校验：坏文件否则要到比赛时才静默失败。
	UTexture2D* Preview = FImageUtils::ImportBufferAsTexture2D(FileBytes);
	if (!Preview)
	{
		OutError = TEXT("不是可识别的图片格式（支持 png/jpg/bmp）");
		return false;
	}

	Entries[Index].PortraitPngData = MoveTemp(FileBytes);
	Entries[Index].PortraitSourcePath = SourcePath;
	PortraitCache.Add(Index, Preview);
	SaveRoster();
	OutError.Reset();
	return true;
}

bool UMarbleRaceRosterSubsystem::ClearPortrait(int32 Index)
{
	if (!Entries.IsValidIndex(Index))
	{
		return false;
	}
	Entries[Index].PortraitPngData.Reset();
	Entries[Index].PortraitSourcePath.Reset();
	Entries[Index].BuiltInPortrait.Reset();
	PortraitCache.Remove(Index);
	SaveRoster();
	return true;
}

UTexture2D* UMarbleRaceRosterSubsystem::BuildPortraitTexture(const TArray<uint8>& Bytes)
{
	if (Bytes.Num() == 0)
	{
		return nullptr;
	}
	return FImageUtils::ImportBufferAsTexture2D(Bytes);
}

UTexture2D* UMarbleRaceRosterSubsystem::GetPortraitTexture(int32 Index)
{
	if (!Entries.IsValidIndex(Index))
	{
		return nullptr;
	}

	if (TObjectPtr<UTexture2D>* Cached = PortraitCache.Find(Index))
	{
		if (*Cached)
		{
			return *Cached;
		}
		PortraitCache.Remove(Index);
	}

	if (!Entries[Index].HasPortrait())
	{
		return nullptr;
	}

	UTexture2D* Built = Entries[Index].PortraitPngData.IsEmpty()
		? Entries[Index].BuiltInPortrait.LoadSynchronous()
		: BuildPortraitTexture(Entries[Index].PortraitPngData);
	if (Built)
	{
		PortraitCache.Add(Index, Built);
	}
	return Built;
}

void UMarbleRaceRosterSubsystem::RefreshAvailableThemes()
{
	AvailableThemes.Reset();
	ThemeLabels.Reset();
	FString Json;
	TSharedPtr<FJsonObject> Manifest;
	const TArray<TSharedPtr<FJsonValue>>* Characters = nullptr;
	for (const TCHAR* RelativePath : {TEXT("Roster/default_roster.json"), TEXT("GGST/roster.json"), TEXT("Roster/local_roster.json")})
	{
		if (FFileHelper::LoadFileToString(Json, *(FPaths::ProjectConfigDir() / RelativePath)) &&
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Manifest) && Manifest.IsValid() &&
			Manifest->TryGetArrayField(TEXT("characters"), Characters))
		{
			for (const auto& Value : *Characters)
			{
				const TSharedPtr<FJsonObject> Character = Value->AsObject();
				FString Path, Name, Title;
				if (Character.IsValid() && Character->TryGetStringField(TEXT("music_asset"), Path) &&
					Character->TryGetStringField(TEXT("name"), Name) && Character->TryGetStringField(TEXT("theme_title"), Title))
				{
					ThemeLabels.Add(FSoftObjectPath(Path), Name + TEXT(" · ") + Title);
				}
			}
		}
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(USoundBase::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(FName(TEXT("/Game")));

	TArray<FAssetData> Found;
	AssetRegistry.GetAssets(Filter, Found);

	Found.Sort([this](const FAssetData& A, const FAssetData& B)
	{
		const bool bBuiltInA = ThemeLabels.Contains(A.ToSoftObjectPath());
		const bool bBuiltInB = ThemeLabels.Contains(B.ToSoftObjectPath());
		if (bBuiltInA != bBuiltInB) return bBuiltInA;
		return A.AssetName.LexicalLess(B.AssetName);
	});

	for (const FAssetData& Asset : Found)
	{
		AvailableThemes.Add(TSoftObjectPtr<USoundBase>(Asset.ToSoftObjectPath()));
	}

	UE_LOG(LogTemp, Log, TEXT("角色名单：发现 %d 个可用音频资源"), AvailableThemes.Num());
}

FString UMarbleRaceRosterSubsystem::GetThemeDisplayName(int32 Index) const
{
	if (!Entries.IsValidIndex(Index))
	{
		return TEXT("-");
	}

	const TSoftObjectPtr<USoundBase>& Music = Entries[Index].ThemeMusic;
	if (Music.IsNull())
	{
		return TEXT("（未设置）");
	}
	if (!Entries[Index].ThemeTitle.IsEmpty())
	{
		return Entries[Index].ThemeTitle;
	}
	return GetThemeLabel(Music.ToSoftObjectPath());
}

FString UMarbleRaceRosterSubsystem::GetThemeLabel(const FSoftObjectPath& Path) const
{
	if (const FString* Label = ThemeLabels.Find(Path)) return *Label;
	return Path.GetAssetName();
}

FString UMarbleRaceRosterSubsystem::GetPortraitFolderPath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Portraits"));
}

void UMarbleRaceRosterSubsystem::RefreshPortraitFolder()
{
	PortraitFolderFiles.Reset();

	const FString Folder = GetPortraitFolderPath();
	IFileManager::Get().MakeDirectory(*Folder, true);

	TArray<FString> Found;
	IFileManager::Get().FindFiles(Found, *(Folder / TEXT("*.png")), true, false);
	PortraitFolderFiles.Append(Found);
	Found.Reset();
	IFileManager::Get().FindFiles(Found, *(Folder / TEXT("*.jpg")), true, false);
	PortraitFolderFiles.Append(Found);
	Found.Reset();
	IFileManager::Get().FindFiles(Found, *(Folder / TEXT("*.jpeg")), true, false);
	PortraitFolderFiles.Append(Found);
	Found.Reset();
	IFileManager::Get().FindFiles(Found, *(Folder / TEXT("*.bmp")), true, false);
	PortraitFolderFiles.Append(Found);

	PortraitFolderFiles.Sort([](const FString& A, const FString& B)
	{
		// 字符串没有 LexicalLess（那个在名字类型上）；用小于号排名称。
		return A < B;
	});
}

bool UMarbleRaceRosterSubsystem::SaveRoster()
{
	URaceRosterSave* Save = Cast<URaceRosterSave>(
		UGameplayStatics::CreateSaveGameObject(URaceRosterSave::StaticClass()));
	if (!Save)
	{
		return false;
	}

	Save->Version = 2;
	Save->Entries = Entries;
	const bool bSaved = UGameplayStatics::SaveGameToSlot(Save, GetSaveSlotName(), 0);
	if (!bSaved)
	{
		UE_LOG(LogTemp, Warning, TEXT("角色名单保存失败：%s"), GetSaveSlotName());
	}
	return bSaved;
}

bool UMarbleRaceRosterSubsystem::LoadRoster()
{
	if (!UGameplayStatics::DoesSaveGameExist(GetSaveSlotName(), 0))
	{
		return false;
	}

	URaceRosterSave* Save = Cast<URaceRosterSave>(
		UGameplayStatics::LoadGameFromSlot(GetSaveSlotName(), 0));
	if (Save && Save->Version == 1 && FPaths::FileExists(FPaths::ProjectConfigDir() / TEXT("GGST/roster.json")))
	{
		// 用户要求替换当前旧名单。先保留完整旧存档，再创建 GGST 名单。
		if (!UGameplayStatics::SaveGameToSlot(Save, TEXT("MarbleRaceRoster_PreGGST"), 0))
		{
			UE_LOG(LogTemp, Error, TEXT("旧名单备份失败，保留旧名单"));
			Entries = Save->Entries;
			NextPaletteIndex = Entries.Num();
			return true;
		}
		return false;
	}
	if (!Save || (Save->Version != 1 && Save->Version != 2))
	{
		UE_LOG(LogTemp, Warning, TEXT("角色名单存档存在但无法使用，将重建默认名单"));
		return false;
	}

	Entries = Save->Entries;
	for (FRaceCharacterEntry& Entry : Entries)
	{
		Entry.Color.A = 1.f;
		if (!FMath::IsFinite(Entry.ThemeStartTimeSeconds) || Entry.ThemeStartTimeSeconds < 0.f)
			Entry.ThemeStartTimeSeconds = 0.f;
	}
	// 调色板序号一直往前走，新小球不会偶然用回同一种颜色。
	NextPaletteIndex = Entries.Num();
	UE_LOG(LogTemp, Log, TEXT("角色名单已载入：%d 个小球"), Entries.Num());
	return true;
}

void UMarbleRaceRosterSubsystem::ResetToDefaults()
{
	for (const TCHAR* RelativePath : {TEXT("Roster/local_roster.json"), TEXT("GGST/roster.json"), TEXT("Roster/default_roster.json")})
	{
		if (LoadRosterFromManifest(FPaths::ProjectConfigDir() / RelativePath)) return;
	}

	// A missing or malformed theme pack must not leave the game with zero marbles.
	Entries.Reset();
	PortraitCache.Reset();
	for (int32 Index = 0; Index < GRacePaletteSize; ++Index)
	{
		FRaceCharacterEntry Entry;
		Entry.DisplayName = FString::Printf(TEXT("小球 %d"), Index + 1);
		Entry.Color = GetPaletteColor(Index);
		Entries.Add(Entry);
	}
	NextPaletteIndex = Entries.Num();
}

bool UMarbleRaceRosterSubsystem::LoadRosterFromManifest(const FString& ManifestPath)
{
	FString Json;
	TSharedPtr<FJsonObject> Manifest;
	if (!FFileHelper::LoadFileToString(Json, *ManifestPath) ||
		!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Manifest) || !Manifest.IsValid())
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Characters = nullptr;
	if (!Manifest->TryGetArrayField(TEXT("characters"), Characters))
	{
		return false;
	}
	TArray<FRaceCharacterEntry> LoadedEntries;
	for (const TSharedPtr<FJsonValue>& Value : *Characters)
	{
		const TSharedPtr<FJsonObject>* Character = nullptr;
		if (!Value->TryGetObject(Character) || !Character || !Character->IsValid())
		{
			continue;
		}
		FString Id, Name, Color, Portrait, Music, Theme;
		if (!(*Character)->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
		{
			continue;
		}
		(*Character)->TryGetStringField(TEXT("id"), Id);
		(*Character)->TryGetStringField(TEXT("color"), Color);
		(*Character)->TryGetStringField(TEXT("portrait_asset"), Portrait);
		(*Character)->TryGetStringField(TEXT("music_asset"), Music);
		(*Character)->TryGetStringField(TEXT("theme_title"), Theme);
		FRaceCharacterEntry Entry;
		Entry.CharacterId = Id;
		Entry.DisplayName = Name;
		Entry.Color = Color.IsEmpty() ? GetPaletteColor(LoadedEntries.Num()) : FLinearColor::FromSRGBColor(FColor::FromHex(Color));
		Entry.Color.A = 1.f;
		Entry.BuiltInPortrait = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(Portrait));
		Entry.PortraitSourcePath = Portrait;
		Entry.ThemeMusic = TSoftObjectPtr<USoundBase>(FSoftObjectPath(Music));
		Entry.ThemeTitle = Theme;
		(*Character)->TryGetBoolField(TEXT("enabled"), Entry.bEnabled);
		LoadedEntries.Add(Entry);
	}

	if (LoadedEntries.IsEmpty()) return false;
	Entries = MoveTemp(LoadedEntries);
	PortraitCache.Reset();
	NextPaletteIndex = Entries.Num();
	UE_LOG(LogTemp, Log, TEXT("已创建 %d 个默认角色弹珠：%s"), Entries.Num(), *ManifestPath);
	return true;
}
