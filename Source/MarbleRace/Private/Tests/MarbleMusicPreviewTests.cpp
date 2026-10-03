#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Roster/MarbleRaceRosterSubsystem.h"
#include "Race/MarbleRaceLevelGameMode.h"
#include "UI/MarbleRaceMenuHUD.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleMusicPreviewTest, "MarbleRace.Music.PreviewAndStartTimePersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleMusicPreviewTest::RunTest(const FString& Parameters)
{
	// This test exercises real save/load without replacing the user's roster.
	const FString SavePath = FPaths::ProjectSavedDir() / TEXT("SaveGames/MarbleRaceRoster.sav");
	TArray<uint8> Original;
	const bool bHadSave = FFileHelper::LoadFileToArray(Original, *SavePath);
	ON_SCOPE_EXIT
	{
		if (bHadSave) FFileHelper::SaveArrayToFile(Original, *SavePath);
		else IFileManager::Get().Delete(*SavePath);
	};
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	auto* HUD = World->SpawnActor<AMarbleRaceMenuHUD>();
	auto* Roster = HUD->GetRoster();
	Roster->ResetToDefaults();
	TestEqual(TEXT("Default start time is zero"), Roster->GetEntries()[0].ThemeStartTimeSeconds, 0.f);
	TestTrue(TEXT("Setting first start time saves successfully"), Roster->SetThemeStartTime(0, 45.5f));
	TestTrue(TEXT("Another character saves independently"), Roster->SetThemeStartTime(1, 18.f));
	auto* Reloaded = NewObject<UMarbleRaceRosterSubsystem>(Instance);
	TestTrue(TEXT("Saved roster reloads"), Reloaded->LoadRoster());
	TestEqual(TEXT("Fractional offset survives save/load"), Reloaded->GetEntries()[0].ThemeStartTimeSeconds, 45.5f);
	TestEqual(TEXT("Other character retains its own offset"), Reloaded->GetEntries()[1].ThemeStartTimeSeconds, 18.f);
	TestEqual(TEXT("Unedited character remains at zero"), Reloaded->GetEntries()[2].ThemeStartTimeSeconds, 0.f);
	auto* Mode = World->SpawnActor<AMarbleRaceLevelGameMode>();
	AActor* Leader = World->SpawnActor<AActor>();
	Mode->MarbleRosterIndices.Add(Leader, 1);
	Mode->UpdateLeaderMusic(Leader);
	TestEqual(TEXT("Real leader check reads the saved character offset"), Mode->ThemePlaybackA.StartedFrom, 18.0);
	HUD->SelectedEntryIndex = 0;
	HUD->EnsureThemePreview();
	TestTrue(TEXT("Full track duration is available to the timeline"), HUD->PreviewPlayback.Duration > 180.0);
	TestTrue(TEXT("Preview sound is non-spatial UI audio"), HUD->PreviewPlayer->bIsUISound && !HUD->PreviewPlayer->bAllowSpatialization);
	HUD->SeekThemePreview(40.0, 100.0);
	TestEqual(TEXT("Paused seek stays at the selected position"), HUD->PreviewPlayback.PositionAt(150.0), 40.0);
	HUD->ToggleThemePreview(150.0);
	TestEqual(TEXT("Playback advances after resuming"), HUD->PreviewPlayback.PositionAt(155.0), 45.0);
	HUD->ToggleThemePreview(155.0);
	TestEqual(TEXT("Pause freezes the preview"), HUD->PreviewPlayback.PositionAt(200.0), 45.0);
	TestEqual(TEXT("Preview seeking does not edit the race starting point"), Roster->GetEntries()[0].ThemeStartTimeSeconds, 45.5f);
	HUD->SelectedEntryIndex = 1;
	HUD->EnsureThemePreview();
	TestFalse(TEXT("Selecting another character stops the old preview"), HUD->bPreviewPlaying);
	TestEqual(TEXT("Another character's preview starts independently"), HUD->PreviewPlayback.PositionAt(200.0), 0.0);
	HUD->ToggleThemePreview(200.0);
	HUD->StopThemePreview();
	TestFalse(TEXT("Leaving the music page stops playback"), HUD->bPreviewPlaying);
	TestNull(TEXT("Leaving the page releases the preview sound"), HUD->PreviewPlayer->Sound.Get());
	TestTrue(TEXT("Out-of-range start is accepted and clamped"), Roster->SetThemeStartTime(0, 99999.f));
	TestTrue(TEXT("Start time remains within the track"), Roster->GetEntries()[0].ThemeStartTimeSeconds < Roster->GetThemeDuration(0));
	Instance->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}
#endif
