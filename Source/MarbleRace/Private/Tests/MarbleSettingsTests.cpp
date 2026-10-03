#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/GameInstance.h"
#include "Settings/MarbleRaceSettingsSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleSettingsTest, "MarbleRace.UI.SettingsPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleSettingsTest::RunTest(const FString& Parameters)
{
	const FString TestIni = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") /
		(TEXT("NameSettings-") + FGuid::NewGuid().ToString() + TEXT(".ini")));
	TGuardValue<FString> IniGuard(GGameUserSettingsIni, TestIni);
	GConfig->Add(TestIni, FConfigFile());
	UGameInstance* Instance = NewObject<UGameInstance>();
	FObjectSubsystemCollection<UGameInstanceSubsystem> Collection;
	auto* Settings = NewObject<UMarbleRaceSettingsSubsystem>(Instance);
	Settings->Initialize(Collection);
	TestFalse(TEXT("Missing preference defaults to original name display"), Settings->AreMarbleNamesStable());
	TestTrue(TEXT("Finish slow motion defaults on"), Settings->IsFinishSlowMotionEnabled());
	TestTrue(TEXT("Countdown zoom defaults on"), Settings->IsCountdownZoomEnabled());
	TestFalse(TEXT("Finishing the last BGM defaults off"), Settings->ShouldFinishBGM());
	TestFalse(TEXT("Background music defaults off"), Settings->IsBackgroundMusicEnabled());
	TestFalse(TEXT("Automatic recording defaults off"), Settings->ShouldRecordRace());
	TestTrue(TEXT("Loudness normalization defaults on"), Settings->IsMusicNormalizationEnabled());
	Settings->SetStableMarbleNames(true);
	Settings->SetFinishSlowMotion(false);
	Settings->SetCountdownZoom(false);
	Settings->SetFinishBGM(true);
	Settings->SetBackgroundMusic(true);
	Settings->SetRecordRace(true);
	Settings->SetMusicNormalization(false);
	FConfigFile Saved;
	Saved.Read(TestIni);
	bool bOnDisk = false;
	TestTrue(TEXT("Preference was saved to disk"), Saved.GetBool(TEXT("MarbleRace.OtherSettings"), TEXT("StableMarbleNames"), bOnDisk) && bOnDisk);
	TestTrue(TEXT("Background music is saved to disk"), Saved.GetBool(TEXT("MarbleRace.OtherSettings"), TEXT("BackgroundMusic"), bOnDisk) && bOnDisk);
	auto* Reloaded = NewObject<UMarbleRaceSettingsSubsystem>(Instance);
	Reloaded->Initialize(Collection);
	TestTrue(TEXT("New session restores enabled preference"), Reloaded->AreMarbleNamesStable());
	TestFalse(TEXT("New session restores disabled slow motion"), Reloaded->IsFinishSlowMotionEnabled());
	TestFalse(TEXT("New session restores disabled countdown zoom"), Reloaded->IsCountdownZoomEnabled());
	TestTrue(TEXT("Finishing BGM survives reload"), Reloaded->ShouldFinishBGM());
	TestTrue(TEXT("Background music survives reload"), Reloaded->IsBackgroundMusicEnabled());
	TestTrue(TEXT("Recording preference survives reload"), Reloaded->ShouldRecordRace());
	TestFalse(TEXT("Normalization preference survives reload"), Reloaded->IsMusicNormalizationEnabled());
	Reloaded->SetStableMarbleNames(false);
	Reloaded->SetFinishSlowMotion(true);
	Reloaded->SetCountdownZoom(true);
	Reloaded->SetBackgroundMusic(false);
	auto* Disabled = NewObject<UMarbleRaceSettingsSubsystem>(Instance);
	Disabled->Initialize(Collection);
	TestFalse(TEXT("Turning off restores original mode on next session"), Disabled->AreMarbleNamesStable());
	TestTrue(TEXT("Re-enabled slow motion survives reload"), Disabled->IsFinishSlowMotionEnabled());
	TestTrue(TEXT("Re-enabled countdown zoom survives reload"), Disabled->IsCountdownZoomEnabled());
	TestFalse(TEXT("Disabling background music survives reload"), Disabled->IsBackgroundMusicEnabled());
	GConfig->UnloadFile(TestIni);
	IFileManager::Get().Delete(*TestIni);
	return true;
}
#endif
