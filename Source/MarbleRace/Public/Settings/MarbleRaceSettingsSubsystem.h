#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MarbleRaceSettingsSubsystem.generated.h"

/** Preferences shared by the menu and race, stored separately from the roster. */
UCLASS()
class MARBLERACE_API UMarbleRaceSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	bool AreMarbleNamesStable() const { return bStableMarbleNames; }
	void SetStableMarbleNames(bool bEnabled);
	bool IsFinishSlowMotionEnabled() const { return bFinishSlowMotion; }
	void SetFinishSlowMotion(bool bEnabled);
	bool IsCountdownZoomEnabled() const { return bCountdownZoom; }
	void SetCountdownZoom(bool bEnabled);
	bool ShouldFinishBGM() const { return bFinishBGM; }
	void SetFinishBGM(bool bEnabled);
	bool IsBackgroundMusicEnabled() const { return bBackgroundMusic; }
	void SetBackgroundMusic(bool bEnabled);
	bool ShouldRecordRace() const { return bRecordRace; }
	void SetRecordRace(bool bEnabled);
	bool IsMusicNormalizationEnabled() const { return bNormalizeMusic; }
	void SetMusicNormalization(bool bEnabled);
	float GetThemeVolumeGain(const FSoftObjectPath& Music) const;
private:
	bool bStableMarbleNames = false;
	bool bFinishSlowMotion = true;
	bool bCountdownZoom = true;
	bool bFinishBGM = false;
	bool bBackgroundMusic = false;
	bool bRecordRace = false;
	bool bNormalizeMusic = true;
	TMap<FSoftObjectPath, float> MusicGains;
};
