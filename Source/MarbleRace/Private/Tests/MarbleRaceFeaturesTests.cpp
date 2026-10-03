#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Race/MarbleRaceLevelGameMode.h"
#include "Race/MarbleDrumSpawn.h"
#include "Race/RoundRobinRelocationManagerActor.h"
#include "RelocationManagerComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Sound/SoundWave.h"
#include "Settings/MarbleRaceSettingsSubsystem.h"
#include "Engine/GameInstance.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQueuedMarbleLeaderTest, "MarbleRace.Camera.QueuedNextRespawnPosition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FQueuedMarbleLeaderTest::RunTest(const FString& Parameters)
{
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* Mode = World->SpawnActor<AMarbleRaceLevelGameMode>();
	auto* Manager = World->SpawnActor<ARoundRobinRelocationManagerActor>();
	Manager->SetActorLocation(FVector(0., 0., 8000.));
	Manager->RespawnLocations = {FVector(0., 0., 500.), FVector(0., 0., 2000.)};
	Manager->bEnableRandomRespawn = false;
	auto* Queue = NewObject<URelocationManagerComponent>(Manager);
	Manager->AddInstanceComponent(Queue);
	Queue->RegisterComponent();
	Queue->BeginPlay();
	UClass* Class = LoadClass<AActor>(nullptr, TEXT("/Game/角色/弹珠.弹珠_C"));
	AActor* Queued = World->SpawnActor<AActor>(Class, FVector(0., 0., -1000.), FRotator::ZeroRotator);
	AActor* Visible = World->SpawnActor<AActor>(Class, FVector(0., 0., 1000.), FRotator::ZeroRotator);
	auto* Body = Cast<UPrimitiveComponent>(Queued->GetRootComponent());
	Body->SetSimulatePhysics(true);
	Queue->EnqueueMarble(Queued, Body);
	Mode->Marbles = {Queued, Visible};
	Mode->bHasFinishLine = true;
	Mode->FinishLineZ = 0.f;
	Mode->MarkFinishedMarbles();
	TestFalse(TEXT("Queued ball below the finish is not marked finished"), Mode->HasFinished(Queued));
	TestEqual(TEXT("Ranking uses next respawn, not hidden actor or manager position"), Mode->GetRacePosition(Queued).Z, 500.0);
	TestEqual(TEXT("Queued marble can lead a visible marble"), Mode->FindLeadingMarble(), Queued);
	const FVector Planned = Mode->GetRacePosition(Queued);
	for (int32 Index = 0; Index < 30; ++Index) Mode->FindLeadingMarble();
	TestTrue(TEXT("Leader queries do not reroll or advance the queue"), Mode->GetRacePosition(Queued).Equals(Planned));
	Queue->MinimumReleaseInterval = 0.f;
	Queue->TickComponent(0.f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Actual release uses the position used by ranking"), Queued->GetActorLocation().Equals(Planned, .01));
	Queue->EnqueueMarble(Queued, Body);
	TestEqual(TEXT("Next queued entry uses the ball's advanced round-robin index"), Mode->GetRacePosition(Queued).Z, 2000.0);
	Mode->CurrentLeader.Reset();
	TestEqual(TEXT("A lower visible marble can overtake a queued marble"), Mode->FindLeadingMarble(), Visible);
	Queue->EndPlay(EEndPlayReason::Destroyed);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	World->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleDrumRandomTest, "MarbleRace.Spawn.RandomDiskAndSpacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMarbleDrumRandomTest::RunTest(const FString& Parameters)
{
	FRandomStream A(123), B(456);
	const auto First = MarbleRace::SampleDrumOffsets(34, 1000.f, 190.f, A);
	const auto Second = MarbleRace::SampleDrumOffsets(34, 1000.f, 190.f, B);
	TestEqual(TEXT("All 34 characters have a random location"), First.Num(), 34);
	for (int32 Index = 0; Index < First.Num(); ++Index)
	{
		TestTrue(TEXT("Spawn stays inside the drum and on the side-view plane"), First[Index].Size() <= 1000.01 && First[Index].Y == 0.);
		TestFalse(TEXT("New seed changes each character position"), First[Index].Equals(Second[Index], .01));
		for (int32 Other = Index + 1; Other < First.Num(); ++Other)
			TestTrue(TEXT("Marbles do not overlap at spawn"), FVector::Distance(First[Index], First[Other]) >= 189.99);
	}
	FRandomStream Uniform(789);
	const auto Single = MarbleRace::SampleDrumOffsets(1, 1000.f, 0.f, Uniform);
	TestFalse(TEXT("Even a single marble spawns randomly, not always at the center"), Single[0].IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleRaceCompletionTest, "MarbleRace.Music.FinishLastTrackOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMarbleRaceCompletionTest::RunTest(const FString& Parameters)
{
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* Mode = World->SpawnActor<AMarbleRaceLevelGameMode>();
	AActor* Last = World->SpawnActor<AActor>();
	Mode->Marbles = {Last};
	Mode->bHasFinishLine = true;
	Mode->bFinishBGM = true;
	auto* Song = NewObject<USoundWave>();
	Song->Duration = 10.f; Song->bLooping = true;
	Mode->SwitchThemeMusic(Song, 0, 0.0, 3.0);
	TestFalse(TEXT("No-leader gaps before everyone finishes are not the end of the race"), Mode->UpdateRaceCompletion(2.0));
	Mode->FinishedMarbles.Add(Last);
	TestTrue(TEXT("Final crossing completes the race"), Mode->UpdateRaceCompletion(2.0));
	TestEqual(TEXT("Continuation finishes the current loop without restarting"), Mode->MusicFinishAt, 7.0);
	TestFalse(TEXT("Music is retained after final crossing"), Mode->bPostRaceMusicFinished);
	Mode->UnlockMusicSwitching();
	TestEqual(TEXT("Unlock cannot cut or restart the final song"), Mode->MusicFinishAt, 7.0);
	Mode->UpdateRaceCompletion(6.99);
	TestFalse(TEXT("Song continues up to its end"), Mode->bPostRaceMusicFinished);
	Mode->UpdateRaceCompletion(7.0);
	TestTrue(TEXT("Song stops at its end and does not loop again"), Mode->bPostRaceMusicFinished);
	auto* Disabled = World->SpawnActor<AMarbleRaceLevelGameMode>();
	Disabled->Marbles = {Last}; Disabled->FinishedMarbles = {Last}; Disabled->bHasFinishLine = true;
	Disabled->SwitchThemeMusic(Song, 0, 0.0);
	Disabled->LockMusicSwitching();
	Disabled->UpdateRaceCompletion(2.0);
	TestTrue(TEXT("Disabled option stops at final crossing, including while locked"), Disabled->bPostRaceMusicFinished);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleLoudnessTest, "MarbleRace.Music.FullTrackLoudnessCalibration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMarbleLoudnessTest::RunTest(const FString& Parameters)
{
	if (!FPaths::FileExists(FPaths::ProjectConfigDir() / TEXT("GGST/music_levels.json")))
	{
		AddInfo(TEXT("Optional GGST loudness data is not installed; skipping pack calibration verification."));
		return true;
	}
	FString Json;
	TestTrue(TEXT("Calibration file is present"), FFileHelper::LoadFileToString(Json, *(FPaths::ProjectConfigDir() / TEXT("GGST/music_levels.json"))));
	TSharedPtr<FJsonObject> Levels;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Levels)) return false;
	UGameInstance* Instance = NewObject<UGameInstance>();
	const FString TestIni = FPaths::ProjectSavedDir() / (TEXT("Automation/Loudness-") + FGuid::NewGuid().ToString() + TEXT(".ini"));
	TGuardValue<FString> IniGuard(GGameUserSettingsIni, TestIni);
	GConfig->Add(TestIni, FConfigFile());
	auto* Settings = NewObject<UMarbleRaceSettingsSubsystem>(Instance);
	FObjectSubsystemCollection<UGameInstanceSubsystem> Collection;
	Settings->Initialize(Collection);
	const auto& Tracks = Levels->GetArrayField(TEXT("tracks"));
	TestEqual(TEXT("All 34 tracks are calibrated"), Tracks.Num(), 34);
	for (const auto& Value : Tracks)
	{
		const auto Track = Value->AsObject();
		const double GainDb = 20.0 * FMath::LogX(10.0, Settings->GetThemeVolumeGain(FSoftObjectPath(Track->GetStringField(TEXT("music_asset")))));
		TestTrue(TEXT("All integrated playback loudness matches the same target"),
			FMath::IsNearlyEqual(Track->GetNumberField(TEXT("integrated_lufs")) + GainDb, Levels->GetNumberField(TEXT("target_lufs")), .001));
		TestTrue(TEXT("Calibrated true peak stays below the clipping limit"), Track->GetNumberField(TEXT("true_peak_dbtp")) + GainDb <= -.999);
	}
	GConfig->UnloadFile(TestIni);
	return true;
}
#endif
