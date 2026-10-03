#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Race/MarbleRaceLevelGameMode.h"
#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleMusicLockTest, "MarbleRace.Music.LockAndImmediateUnlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleMusicLockTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	AMarbleRaceLevelGameMode* Mode = World->SpawnActor<AMarbleRaceLevelGameMode>();
	const auto SpawnMarble = [World](float Z)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		auto* Root = NewObject<USceneComponent>(Actor);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(FVector(0.f, 0.f, Z));
		return Actor;
	};
	AActor* A = SpawnMarble(0.f);
	AActor* B = SpawnMarble(200.f);
	Mode->Marbles = {A, B};
	Mode->MarbleRosterIndices.Add(A, 0);
	Mode->MarbleRosterIndices.Add(B, 1);
	Mode->bFollowLeader = true;
	Mode->CurrentLeader = A;
	Mode->UpdateLeaderMusic(A);
	const bool OriginalChannel = Mode->bUseThemePlayerA;
	Mode->LockMusicSwitching();
	B->SetActorLocation(FVector(0.f, 0.f, -200.f));
	Mode->UpdateLeaderMusic(B);
	Mode->UpdateLeaderMusic(nullptr);
	TestTrue(TEXT("Locked state remains enabled"), Mode->IsMusicSwitchingLocked());
	TestEqual(TEXT("Leader changes and no-leader gaps retain original music"), Mode->MusicLeader.Get(), A);
	TestEqual(TEXT("Lock never restarts or changes audio channel"), Mode->bUseThemePlayerA, OriginalChannel);
	TestTrue(TEXT("Locked song remains active"),
		(Mode->bUseThemePlayerA ? Mode->ThemePlaybackB : Mode->ThemePlaybackA).StopsAt == TNumericLimits<double>::Max());
	Mode->UnlockMusicSwitching();
	TestFalse(TEXT("Unlock clears the lock"), Mode->IsMusicSwitchingLocked());
	TestEqual(TEXT("Unlock checks and switches immediately without a subsequent tick"), Mode->MusicLeader.Get(), B);
	TestNotEqual(TEXT("New leader uses the other channel"), Mode->bUseThemePlayerA, OriginalChannel);
	const bool UnlockedChannel = Mode->bUseThemePlayerA;
	Mode->UnlockMusicSwitching();
	TestEqual(TEXT("Unlock with unchanged leader keeps uninterrupted playback"), Mode->bUseThemePlayerA, UnlockedChannel);
	Mode->LockMusicSwitching();
	Mode->Marbles.Reset();
	Mode->UnlockMusicSwitching();
	TestTrue(TEXT("Unlock with no remaining leader stops current playback"),
		(Mode->bUseThemePlayerA ? Mode->ThemePlaybackB : Mode->ThemePlaybackA).StopsAt != TNumericLimits<double>::Max());
	Instance->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}
#endif
