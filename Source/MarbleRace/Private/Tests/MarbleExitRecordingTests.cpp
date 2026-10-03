#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/MarbleRaceMenuHUD.h"
#include "Race/MarbleRaceRecorderSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleExitRecordingTest, "MarbleRace.Recording.DeferredMenuExit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleExitRecordingTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	auto* HUD = World->SpawnActor<AMarbleRaceMenuHUD>();
	auto* Recorder = Instance->GetSubsystem<UMarbleRaceRecorderSubsystem>();
	TPromise<void> OlderRace, LatestRace;
	Recorder->Finalizations.Add(OlderRace.GetFuture());
	Recorder->Finalizations.Add(LatestRace.GetFuture());
	TestFalse(TEXT("No quit requested during normal menu use"), HUD->CanCompleteQuit());
	HUD->RequestQuit();
	TestTrue(TEXT("Exit request is remembered while encoding"), HUD->bQuitRequested);
	TestFalse(TEXT("Engine shutdown is not issued while encoding"), HUD->bQuitIssued);
	TestEqual(TEXT("All races still saving are counted"), Recorder->GetPendingSaveCount(), 2);
	HUD->Tick(0.016f);
	HUD->RequestQuit();
	TestFalse(TEXT("Repeated click and game tick return without blocking or quitting"), HUD->bQuitIssued);
	TestTrue(TEXT("Polling remains enabled even when rendering is suspended"), HUD->PrimaryActorTick.bCanEverTick);
	TestTrue(TEXT("Keyboard input is consumed during exit"), HUD->HandleKeyDown(EKeys::Escape));
	LatestRace.SetValue();
	TestEqual(TEXT("One finished save does not hide another pending race"), Recorder->GetPendingSaveCount(), 1);
	TestFalse(TEXT("Exit still waits for the earlier race"), HUD->CanCompleteQuit());
	OlderRace.SetValue();
	TestEqual(TEXT("All saves have completed"), Recorder->GetPendingSaveCount(), 0);
	TestTrue(TEXT("Shutdown can proceed immediately after all saves"), HUD->CanCompleteQuit());
	HUD->bQuitIssued = true; // Do not shut down the automation host.
	TestFalse(TEXT("Shutdown is dispatched at most once"), HUD->CanCompleteQuit());
	HUD->bQuitIssued = false;
	Recorder->Finalizations.Reset();
	TestTrue(TEXT("No recording means no exit delay"), HUD->CanCompleteQuit());
	Instance->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}
#endif
