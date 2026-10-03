#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Race/MarbleRaceLevelGameMode.h"
#include "Race/MarbleRacePresentation.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleFinishSlowMotionTest, "MarbleRace.Presentation.FinishSlowMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleFinishSlowMotionTest::RunTest(const FString& Parameters)
{
	for (const double DistanceAtEntry : {10.0, 400.0, 1800.0})
	{
		double Distance = DistanceAtEntry;
		double Elapsed = 0.0;
		constexpr double Speed = 4000.0;
		constexpr double Frame = 1.0 / 60.0;
		while (Distance > 0.0 && Elapsed < 3.0)
		{
			const float Scale = MarbleRace::FinishTimeScale(Distance, Speed, MarbleRace::FinishApproachSeconds - Elapsed);
			Distance -= Speed * Scale * Frame;
			Elapsed += Frame;
		}
		TestTrue(TEXT("Even a close follower's approach is stretched to roughly 2.4 real seconds"), Elapsed >= 2.35 && Elapsed <= 2.5);
	}
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	AMarbleRaceLevelGameMode* Mode = World->SpawnActor<AMarbleRaceLevelGameMode>();
	AActor* Marble = World->SpawnActor<AActor>();
	auto* Root = NewObject<USceneComponent>(Marble);
	Marble->SetRootComponent(Root);
	Root->RegisterComponent();
	Root->ComponentVelocity = FVector(0.f, 0.f, -1000.f);
	Marble->SetActorLocation(FVector(0.f, 0.f, 400.f));
	Mode->bHasFinishLine = true;
	Mode->FinishLineZ = 0.f;
	Mode->UpdateFinishSlowMotion(Marble, 0.0);
	TestEqual(TEXT("Disabled effect leaves world speed unchanged"), World->GetWorldSettings()->TimeDilation, 1.f);
	Mode->bFinishSlowMotionEnabled = true;
	Mode->UpdateFinishSlowMotion(Marble, 0.0);
	TestTrue(TEXT("Approaching marble slows the whole world"), World->GetWorldSettings()->TimeDilation < 0.2f);
	TestTrue(TEXT("BGM uses UI audio"), Mode->ThemePlayerA->bIsUISound && Mode->ThemePlayerB->bIsUISound);
	TestEqual(TEXT("BGM pitch stays normal during slow motion"), Mode->ThemePlayerA->PitchMultiplier, 1.f);
	const FMarbleThemePlayback Playback{0, 0.0, 0.0, 20.0};
	TestEqual(TEXT("Music progress advances by real seconds despite world slowdown"), Playback.PositionAt(2.4), 2.4);
	Mode->UpdateFinishSlowMotion(Marble, 2.41);
	TestTrue(TEXT("Estimated deadline does not accelerate a marble before it crosses"), World->GetWorldSettings()->TimeDilation < 0.2f);
	Marble->SetActorLocation(FVector(0.f, 0.f, -1.f));
	Mode->UpdateFinishSlowMotion(Marble, 2.42);
	TestEqual(TEXT("Crossing restores normal world speed"), World->GetWorldSettings()->TimeDilation, 1.f);
	AActor* Follower = World->SpawnActor<AActor>();
	auto* FollowerRoot = NewObject<USceneComponent>(Follower);
	Follower->SetRootComponent(FollowerRoot);
	FollowerRoot->RegisterComponent();
	FollowerRoot->ComponentVelocity = FVector(0.f, 0.f, -1000.f);
	Follower->SetActorLocation(FVector(0.f, 0.f, 10.f));
	Mode->UpdateFinishSlowMotion(Follower, 2.42);
	TestTrue(TEXT("Close next leader receives its own slow approach"), World->GetWorldSettings()->TimeDilation < 0.01f);
	Mode->UpdateFinishSlowMotion(nullptr, 2.5);
	TestEqual(TEXT("No leader restores world speed"), World->GetWorldSettings()->TimeDilation, 1.f);
	Mode->SlowMotionTriggeredMarbles.Reset();
	World->GetWorldSettings()->SetTimeDilation(0.8f);
	Mode->UpdateFinishSlowMotion(Follower, 3.0);
	Mode->EndPlay(EEndPlayReason::EndPlayInEditor);
	TestEqual(TEXT("Leaving race restores the previous world speed"), World->GetWorldSettings()->TimeDilation, 0.8f);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleCountdownZoomTest, "MarbleRace.Presentation.CountdownZoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleCountdownZoomTest::RunTest(const FString& Parameters)
{
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	AMarbleRaceLevelGameMode* Mode = World->SpawnActor<AMarbleRaceLevelGameMode>();
	Mode->FollowCamera = World->SpawnActor<ACameraActor>();
	UCameraComponent* Camera = Mode->FollowCamera->GetCameraComponent();
	Camera->SetProjectionMode(ECameraProjectionMode::Orthographic);
	Camera->SetOrthoWidth(2500.f);
	Mode->CameraOrthoWidth = 2500.f;
	Mode->StartCountdownZoom(0.0);
	TestEqual(TEXT("Disabled transition retains original framing"), Camera->OrthoWidth, 2500.f);
	Mode->bCountdownZoomEnabled = true;
	Mode->StartCountdownZoom(0.0);
	TestEqual(TEXT("Countdown starts with a closer orthographic view"), Camera->OrthoWidth, 1500.f);
	Mode->UpdateCountdownZoom(0.9);
	TestEqual(TEXT("Halfway smoothly zooms out"), Camera->OrthoWidth, 2000.f);
	Mode->UpdateCountdownZoom(1.81);
	TestEqual(TEXT("Normal view is restored before countdown ends"), Camera->OrthoWidth, 2500.f);
	TestFalse(TEXT("Completed transition stops updating the camera"), Mode->bCountdownZoomActive);
	Mode->StartCountdownZoom(3.0);
	Mode->EndPlay(EEndPlayReason::EndPlayInEditor);
	TestEqual(TEXT("Exiting during transition restores normal framing"), Camera->OrthoWidth, 2500.f);
	Camera->SetProjectionMode(ECameraProjectionMode::Perspective);
	Mode->StartCountdownZoom(4.0);
	TestTrue(TEXT("Perspective cameras start with a narrower FOV"), FMath::IsNearlyEqual(Camera->FieldOfView, 54.f, 0.001f));
	Mode->UpdateCountdownZoom(5.81);
	TestEqual(TEXT("Perspective cameras restore normal FOV"), Camera->FieldOfView, 90.f);
	Mode->CountdownSeconds = 1;
	Mode->StartCountdownZoom(6.0);
	TestTrue(TEXT("Transition also completes before a shortened countdown"), Mode->CountdownZoomDuration < 1.0);
	Mode->RestoreCameraZoom();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}
#endif
