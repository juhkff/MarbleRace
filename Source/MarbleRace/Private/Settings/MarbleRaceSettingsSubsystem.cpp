#include "Settings/MarbleRaceSettingsSubsystem.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	const TCHAR* SettingsSection = TEXT("MarbleRace.OtherSettings");
	// Keep the existing section so previously saved name preferences still apply.
	void SavePreference(const TCHAR* Key, const bool bEnabled)
	{
		if (GConfig)
		{
			GConfig->SetBool(SettingsSection, Key, bEnabled, GGameUserSettingsIni);
			GConfig->Flush(false, GGameUserSettingsIni);
		}
	}
}

void UMarbleRaceSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (GConfig)
	{
		GConfig->GetBool(SettingsSection, TEXT("StableMarbleNames"), bStableMarbleNames, GGameUserSettingsIni);
		GConfig->GetBool(SettingsSection, TEXT("FinishSlowMotion"), bFinishSlowMotion, GGameUserSettingsIni);
		GConfig->GetBool(SettingsSection, TEXT("CountdownZoomTransition"), bCountdownZoom, GGameUserSettingsIni);
		GConfig->GetBool(SettingsSection, TEXT("FinishCurrentBGM"), bFinishBGM, GGameUserSettingsIni);
		GConfig->GetBool(SettingsSection, TEXT("BackgroundMusic"), bBackgroundMusic, GGameUserSettingsIni);
		GConfig->GetBool(SettingsSection, TEXT("RecordRace"), bRecordRace, GGameUserSettingsIni);
		GConfig->GetBool(SettingsSection, TEXT("NormalizeMusic"), bNormalizeMusic, GGameUserSettingsIni);
	}
	FString Json;
	TSharedPtr<FJsonObject> Levels;
	const TArray<TSharedPtr<FJsonValue>>* Tracks = nullptr;
	for (const TCHAR* RelativePath : {TEXT("Roster/music_levels.json"), TEXT("GGST/music_levels.json"), TEXT("Roster/local_music_levels.json")})
	{
		if (FFileHelper::LoadFileToString(Json, *(FPaths::ProjectConfigDir() / RelativePath)) &&
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Levels) && Levels.IsValid() &&
			Levels->TryGetArrayField(TEXT("tracks"), Tracks))
		{
			for (const auto& Track : *Tracks)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				FString Path;
				double Gain = 1.0;
				if (Track->TryGetObject(Object) && (*Object)->TryGetStringField(TEXT("music_asset"), Path) &&
					(*Object)->TryGetNumberField(TEXT("gain"), Gain) && FMath::IsFinite(Gain) && Gain > 0.0)
					MusicGains.Add(FSoftObjectPath(Path), Gain);
			}
		}
	}
}

void UMarbleRaceSettingsSubsystem::SetFinishBGM(const bool bEnabled)
{
	bFinishBGM = bEnabled;
	SavePreference(TEXT("FinishCurrentBGM"), bEnabled);
}

void UMarbleRaceSettingsSubsystem::SetBackgroundMusic(const bool bEnabled)
{
	bBackgroundMusic = bEnabled;
	SavePreference(TEXT("BackgroundMusic"), bEnabled);
}

void UMarbleRaceSettingsSubsystem::SetRecordRace(const bool bEnabled)
{
	bRecordRace = bEnabled;
	SavePreference(TEXT("RecordRace"), bEnabled);
}

void UMarbleRaceSettingsSubsystem::SetMusicNormalization(const bool bEnabled)
{
	bNormalizeMusic = bEnabled;
	SavePreference(TEXT("NormalizeMusic"), bEnabled);
}

float UMarbleRaceSettingsSubsystem::GetThemeVolumeGain(const FSoftObjectPath& Music) const
{
	const float* Gain = MusicGains.Find(Music);
	return bNormalizeMusic && Gain ? *Gain : 1.f;
}

void UMarbleRaceSettingsSubsystem::SetStableMarbleNames(const bool bEnabled)
{
	bStableMarbleNames = bEnabled;
	SavePreference(TEXT("StableMarbleNames"), bEnabled);
}

void UMarbleRaceSettingsSubsystem::SetFinishSlowMotion(const bool bEnabled)
{
	bFinishSlowMotion = bEnabled;
	SavePreference(TEXT("FinishSlowMotion"), bEnabled);
}

void UMarbleRaceSettingsSubsystem::SetCountdownZoom(const bool bEnabled)
{
	bCountdownZoom = bEnabled;
	SavePreference(TEXT("CountdownZoomTransition"), bEnabled);
}
