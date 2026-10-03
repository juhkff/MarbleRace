#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Race/MarbleRaceLevelGameMode.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Sound/SoundWave.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleMusicResumeTest,
	"MarbleRace.Music.ResumeAndLoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleMusicResumeTest::RunTest(const FString& Parameters)
{
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	AMarbleRaceLevelGameMode* Mode = World->SpawnActor<AMarbleRaceLevelGameMode>();
	USoundWave* SongA = NewObject<USoundWave>();
	USoundWave* SongB = NewObject<USoundWave>();
	SongA->Duration = 10.f;
	SongB->Duration = 20.f;
	SongA->bLooping = SongB->bLooping = true;
	const auto Check = [this](const TCHAR* Label, const double Actual, const double Expected)
	{
		TestTrue(Label, FMath::IsNearlyEqual(Actual, Expected, 0.0001));
	};

	Mode->SwitchThemeMusic(SongA, 0, 0.0);
	Check(TEXT("First trigger starts at zero"), Mode->ThemePlaybackA.StartedFrom, 0.0);
	Mode->SwitchThemeMusic(SongB, 1, 2.0);
	Check(TEXT("Different character starts independently"), Mode->ThemePlaybackB.StartedFrom, 0.0);
	Check(TEXT("Switch cuts outgoing immediately"), Mode->ThemePlaybackA.StopsAt, 2.0);
	Mode->SwitchThemeMusic(SongA, 0, 100.0);
	Check(TEXT("Long inactive gap does not advance A"), Mode->ThemePlaybackA.StartedFrom, 2.0);
	Mode->SwitchThemeMusic(SongB, 1, 100.1);
	Check(TEXT("B resumes its own loop remainder"), Mode->ThemePlaybackB.StartedFrom, 18.0);
	Mode->SwitchThemeMusic(SongA, 0, 100.2);
	Check(TEXT("Rapid switch resumes at the interruption point"), Mode->ThemePlaybackA.StartedFrom, 2.1);
	Mode->SwitchThemeMusic(nullptr, INDEX_NONE, 101.0);
	Mode->SwitchThemeMusic(SongA, 0, 200.0);
	Check(TEXT("No-leader interval preserves song position"), Mode->ThemePlaybackB.StartedFrom, 2.9);
	Mode->SwitchThemeMusic(SongB, 1, 212.0);
	Mode->SwitchThemeMusic(SongA, 0, 300.0);
	Check(TEXT("Resumed song loops back and retains remainder"), Mode->ThemePlaybackB.StartedFrom, 4.9);
	const FMarbleThemePlayback Completed{0, 0.0, 2.0, 10.0};
	Check(TEXT("Exactly at track end returns to the beginning"), Completed.PositionAt(8.0), 0.0);

	Mode->ThemeResumePositions.Reset();
	Mode->ThemePlaybackA = {};
	Mode->ThemePlaybackB = {};
	Mode->bUseThemePlayerA = true;
	Mode->SwitchThemeMusic(SongA, 0, 0.0, 6.0);
	Check(TEXT("First trigger uses the configured starting point"), Mode->ThemePlaybackA.StartedFrom, 6.0);
	Mode->SwitchThemeMusic(SongB, 1, 2.0, 4.0);
	Check(TEXT("Each character has an independent initial offset"), Mode->ThemePlaybackB.StartedFrom, 4.0);
	Mode->SwitchThemeMusic(SongA, 0, 100.0, 6.0);
	Check(TEXT("Later trigger resumes instead of resetting to initial offset"), Mode->ThemePlaybackA.StartedFrom, 8.0);
	Mode->SwitchThemeMusic(SongB, 1, 102.0, 4.0);
	Mode->SwitchThemeMusic(SongA, 0, 200.0, 6.0);
	Check(TEXT("A completed track restarts at zero, not its initial offset"), Mode->ThemePlaybackA.StartedFrom, 0.0);
	Mode->SwitchThemeMusic(SongB, 1, 205.0, 4.0);
	Mode->SwitchThemeMusic(SongA, 0, 300.0, 6.0);
	Check(TEXT("Progress after looping also resumes normally"), Mode->ThemePlaybackA.StartedFrom, 5.0);

	Mode->ThemeResumePositions.Reset();
	Mode->ThemePlaybackA = {};
	Mode->ThemePlaybackB = {};
	Mode->bUseThemePlayerA = true;
	Mode->SwitchThemeMusic(SongA, 0, 400.0);
	Mode->SwitchThemeMusic(nullptr, INDEX_NONE, 402.0);
	Mode->SwitchThemeMusic(SongA, 0, 402.1);
	Check(TEXT("Same leader resumes after a short no-leader gap"), Mode->ThemePlaybackB.StartedFrom, 2.0);
	Mode->SwitchThemeMusic(SongB, 1, 403.0);
	Check(TEXT("Old channel cannot overwrite newer progress for the same song"), Mode->ThemeResumePositions.FindRef(0), 2.9);
	Mode->SwitchThemeMusic(SongA, 0, 500.0);
	Check(TEXT("Latest playback progress is retained"), Mode->ThemePlaybackB.StartedFrom, 2.9);
	AActor* RemovedLeader = World->SpawnActor<AActor>();
	Mode->MusicLeader = RemovedLeader;
	RemovedLeader->Destroy();
	Mode->UpdateLeaderMusic(nullptr);
	TestTrue(TEXT("Destroyed leader also stops music and freezes progress"),
		Mode->ThemePlaybackB.StopsAt != TNumericLimits<double>::Max());

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
