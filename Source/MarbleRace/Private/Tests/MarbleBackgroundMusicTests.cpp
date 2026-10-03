#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Race/MarbleRaceLevelGameMode.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Sound/SoundWave.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleBackgroundMusicTest, "MarbleRace.Music.BackgroundTimeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleBackgroundMusicTest::RunTest(const FString& Parameters)
{
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* Mode = World->SpawnActor<AMarbleRaceLevelGameMode>();
	auto* SongA = NewObject<USoundWave>();
	auto* SongB = NewObject<USoundWave>();
	SongA->Duration = 10.f;
	SongB->Duration = 20.f;
	SongA->bLooping = SongB->bLooping = true;
	const auto Check = [this](const TCHAR* Label, double Actual, double Expected)
	{
		TestTrue(Label, FMath::IsNearlyEqual(Actual, Expected, .0001));
	};
	Mode->bBackgroundMusic = true;
	Mode->RemoveDrums();
	Check(TEXT("Countdown completion starts the undilated music clock"),
		Mode->MusicTimelineStartedAt, World->GetRealTimeSeconds());
	Mode->bFollowLeader = false;
	Mode->MusicTimelineStartedAt = 100.0;
	Mode->SwitchThemeMusic(SongA, 0, 100.0, 6.0);
	Check(TEXT("First audible song starts at zero despite a configured offset"), Mode->ThemePlaybackA.StartedFrom, 0.0);
	Mode->SwitchThemeMusic(SongB, 1, 104.0, 11.0);
	Check(TEXT("A never-leading song has already advanced silently"), Mode->ThemePlaybackB.StartedFrom, 4.0);
	Check(TEXT("Only incoming channel remains active after switching"), Mode->ThemePlaybackA.StopsAt, 104.0);
	Mode->SwitchThemeMusic(SongA, 0, 107.0, 6.0);
	Check(TEXT("Inactive time advances the previously leading song"), Mode->ThemePlaybackA.StartedFrom, 7.0);
	Mode->SwitchThemeMusic(nullptr, INDEX_NONE, 108.0);
	Mode->SwitchThemeMusic(SongB, 1, 126.0, 11.0);
	Check(TEXT("No-leader gaps and full loops still advance every song"), Mode->ThemePlaybackB.StartedFrom, 6.0);
	Mode->SwitchThemeMusic(SongA, 0, 130.0, 6.0);
	Check(TEXT("A complete silent loop restarts at zero, ignoring the offset"), Mode->ThemePlaybackA.StartedFrom, 0.0);
	Mode->LockMusicSwitching();
	Mode->UpdateLeaderMusic(nullptr);
	TestTrue(TEXT("Lock retains the audible channel"), Mode->ThemePlaybackA.StopsAt == TNumericLimits<double>::Max());
	Mode->UnlockMusicSwitching();
	Mode->SwitchThemeMusic(SongB, 1, 137.0, 11.0);
	Check(TEXT("Silent songs also advance while music switching is locked"), Mode->ThemePlaybackB.StartedFrom, 17.0);
	TestTrue(TEXT("Background mode does not depend on per-leader resume history"), Mode->ThemeResumePositions.IsEmpty());
	// Finishing uses the currently audible song's synchronized position, not the initial offset.
	AActor* Marble = World->SpawnActor<AActor>();
	Mode->Marbles.Add(Marble);
	Mode->FinishedMarbles.Add(Marble);
	Mode->bHasFinishLine = true;
	Mode->bFinishBGM = true;
	Mode->UpdateRaceCompletion(138.0);
	Check(TEXT("Post-race playback ends at this song's next loop boundary"), Mode->MusicFinishAt, 140.0);
	Mode->UpdateRaceCompletion(139.9);
	TestFalse(TEXT("Post-race playback continues until its boundary"), Mode->bPostRaceMusicFinished);
	Mode->UpdateRaceCompletion(140.0);
	TestTrue(TEXT("Post-race playback stops without another loop"), Mode->bPostRaceMusicFinished);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}
#endif
