#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Race/MarbleCameraFollow.h"
#include "Race/MarbleRaceLevelGameMode.h"
#include "Components/SceneComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleCameraFrameBoundTest,
	"MarbleRace.Camera.FrameBoundAndSmoothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleCameraFrameBoundTest::RunTest(const FString& Parameters)
{
	constexpr float MaxSpeed = 5000.f;
	for (const float DeltaSeconds : {1.f / 120.f, 1.f / 60.f, 1.f / 30.f, 0.25f})
	{
		for (const float TargetZ : {-20000.f, 20000.f})
		{
			const float Next = MarbleRace::AdvanceCameraZ(6008.f, TargetZ, DeltaSeconds, 6.f, MaxSpeed);
			TestTrue(TEXT("Large relocation/leader change obeys frame travel bound"),
				FMath::Abs(Next - 6008.f) <= MaxSpeed * DeltaSeconds + .001f);
			TestTrue(TEXT("Camera moves toward target without overshoot"),
				Next >= FMath::Min(6008.f, TargetZ) && Next <= FMath::Max(6008.f, TargetZ));
		}
	}
	const float DeltaSeconds = 1.f / 60.f;
	TestEqual(TEXT("Normal follow retains existing interpolation"),
		MarbleRace::AdvanceCameraZ(1000.f, 900.f, DeltaSeconds, 6.f, MaxSpeed),
		FMath::FInterpTo(1000.f, 900.f, DeltaSeconds, 6.f));
	TestEqual(TEXT("Zero delta holds position"), MarbleRace::AdvanceCameraZ(1000.f, 0.f, 0.f, 6.f, MaxSpeed), 1000.f);
	TestEqual(TEXT("Negative delta holds position"), MarbleRace::AdvanceCameraZ(1000.f, 0.f, -1.f, 6.f, MaxSpeed), 1000.f);
	TestEqual(TEXT("Zero maximum speed holds position"), MarbleRace::AdvanceCameraZ(1000.f, 0.f, 1.f, 6.f, 0.f), 1000.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleCameraEligibilityTest,
	"MarbleRace.Camera.QueuedFinishedAndNoLeader",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleCameraEligibilityTest::RunTest(const FString& Parameters)
{
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	AMarbleRaceLevelGameMode* Mode = World->SpawnActor<AMarbleRaceLevelGameMode>();
	const auto SpawnMarble = [World](const float Z)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(Actor);
		Actor->AddInstanceComponent(Root);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(FVector(0, 0, Z));
		return Actor;
	};
	AActor* Queued = SpawnMarble(-1000.f);
	AActor* Visible = SpawnMarble(1000.f);
	Queued->SetActorHiddenInGame(true);
	Mode->Marbles.Add(Queued);
	Mode->Marbles.Add(Visible);
	Mode->CurrentLeader = Queued;
	Mode->bHasFinishLine = true;
	Mode->FinishLineZ = 0.f;
	Mode->MarkFinishedMarbles();
	TestFalse(TEXT("Hidden queued marble below finish is not finished"), Mode->HasFinished(Queued));
	TestTrue(TEXT("Hidden sticky leader is replaced by visible marble"), Mode->FindLeadingMarble() == Visible);
	TestTrue(TEXT("Visible non-simulating anti-pinch marble remains eligible"), Mode->IsEligibleMarble(Visible));
	TestFalse(TEXT("Null marble is ineligible"), Mode->IsEligibleMarble(nullptr));

	Mode->FollowCamera = World->SpawnActor<ACameraActor>();
	Mode->FollowCamera->SetActorLocation(FVector(0, 0, 700.f));
	Mode->bFollowLeader = true;
	Visible->SetActorHiddenInGame(true);
	Mode->Tick(1.f / 60.f);
	TestTrue(TEXT("All queued: no leader"), Mode->FindLeadingMarble() == nullptr);
	TestEqual(TEXT("All queued: camera holds"), Mode->FollowCamera->GetActorLocation().Z, 700.0);
	TestFalse(TEXT("All queued: stale fallback cleared"), Mode->CurrentLeader.IsValid());

	Queued->SetActorHiddenInGame(false);
	Visible->SetActorHiddenInGame(false);
	Visible->SetActorLocation(FVector(0, 0, -500.f));
	Mode->CurrentLeader = Visible;
	Mode->Tick(1.f / 60.f);
	TestTrue(TEXT("Visible crossings are marked finished"), Mode->HasFinished(Queued) && Mode->HasFinished(Visible));
	TestTrue(TEXT("All finished: no leader"), Mode->FindLeadingMarble() == nullptr);
	TestEqual(TEXT("All finished: camera holds instead of following final ball"), Mode->FollowCamera->GetActorLocation().Z, 700.0);
	TestFalse(TEXT("All finished: stale fallback cleared"), Mode->CurrentLeader.IsValid());

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleDistanceCameraLawTest,
	"MarbleRace.Camera.DistanceLinearSpeedAndFrameRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleDistanceCameraLawTest::RunTest(const FString& Parameters)
{
	constexpr float Rate = 12.f, Delta = 1.f / 60.f;
	const float Near = MarbleRace::AdvanceDistanceCameraZ(0.f, 100.f, Delta, Rate);
	const float Far = MarbleRace::AdvanceDistanceCameraZ(0.f, 1000.f, Delta, Rate);
	TestTrue(TEXT("Ten times the distance produces ten times the movement speed"), FMath::IsNearlyEqual(Far, Near * 10.f, .001f));
	for (float Target : {-10000.f, 10000.f})
	{
		const float Next = MarbleRace::AdvanceDistanceCameraZ(0.f, Target, .25f, Rate);
		TestTrue(TEXT("Long frames remain continuous and never overshoot"), Next > FMath::Min(0.f, Target) && Next < FMath::Max(0.f, Target));
	}
	const float Expected = MarbleRace::AdvanceDistanceCameraZ(0.f, 10000.f, 1.f, Rate);
	for (int32 FPS : {30, 60, 120})
	{
		float Position = 0.f;
		for (int32 Frame = 0; Frame < FPS; ++Frame)
			Position = MarbleRace::AdvanceDistanceCameraZ(Position, 10000.f, 1.f / FPS, Rate);
		TestTrue(TEXT("Equal elapsed time yields equal progress at different frame rates"), FMath::IsNearlyEqual(Position, Expected, .02f));
	}
	TestEqual(TEXT("Zero frame time holds the camera"), MarbleRace::AdvanceDistanceCameraZ(100.f, 1000.f, 0.f, Rate), 100.f);
	TestEqual(TEXT("Zero rate holds the camera"), MarbleRace::AdvanceDistanceCameraZ(100.f, 1000.f, Delta, 0.f), 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleFinishCameraCatchUpTest,
	"MarbleRace.Camera.DistanceCatchUpAfterFinish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleFinishCameraCatchUpTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	auto* Mode = World->SpawnActor<AMarbleRaceLevelGameMode>();
	const auto SpawnMarble = [World](float Z)
	{
		AActor* Marble = World->SpawnActor<AActor>();
		auto* Root = NewObject<USceneComponent>(Marble);
		Marble->AddInstanceComponent(Root);
		Marble->SetRootComponent(Root);
		Root->RegisterComponent();
		Marble->SetActorLocation(FVector(0.f, 0.f, Z));
		return Marble;
	};
	AActor* A = SpawnMarble(100.f);
	AActor* B = SpawnMarble(4000.f);
	AActor* C = SpawnMarble(7000.f);
	AActor* D = SpawnMarble(9000.f);
	AActor* E = SpawnMarble(10000.f);
	Mode->Marbles = {A, B, C, D, E};
	Mode->CurrentLeader = A;
	Mode->bHasFinishLine = Mode->bFollowLeader = true;
	Mode->FinishLineZ = 0.f;
	Mode->FollowCamera = World->SpawnActor<ACameraActor>();
	Mode->CameraLockX = 32.f;
	Mode->CameraSideY = -1200.f;
	Mode->FollowCamera->SetActorLocation(FVector(32.f, -1200.f, 100.f));
	auto* Controller = World->SpawnActor<APlayerController>();
	World->AddController(Controller);
	if (!Controller->PlayerCameraManager) Controller->PlayerCameraManager = World->SpawnActor<APlayerCameraManager>();
	APlayerCameraManager* CameraManager = Controller->PlayerCameraManager;
	CameraManager->bUseClientSideCameraUpdates = false;
	CameraManager->InitializeFor(Controller);
	Controller->SetViewTarget(Mode->FollowCamera);
	CameraManager->UpdateCamera(0.f);
	CameraManager->bGameCameraCutThisFrame = false;
	constexpr float Delta = 1.f / 60.f;
	World->DeltaRealTimeSeconds = Delta;
	A->SetActorLocation(FVector(0.f, 0.f, -1.f));
	Mode->Tick(Delta * .1f);
	const FVector First = Mode->FollowCamera->GetActorLocation();
	TestTrue(TEXT("Crossing starts continuous movement instead of teleporting"), First.Z > 100.f && First.Z < 4000.f);
	TestTrue(TEXT("A distant successor exceeds the ordinary fixed speed cap"), First.Z - 100.f > 5000.f * Delta);
	TestTrue(TEXT("Finish slow motion does not slow down camera catch-up"), FMath::IsNearlyEqual(First.Z, MarbleRace::AdvanceDistanceCameraZ(100.f, 4000.f, Delta, 12.f), .001f));
	TestEqual(TEXT("Track X/Y framing is retained"), FVector2D(First.X, First.Y), FVector2D(32.f, -1200.f));
	TestEqual(TEXT("Next unfinished leader becomes the target"), Mode->CurrentLeader.Get(), B);
	TestEqual(TEXT("The rendered view cache tracks this frame's continuous movement"), CameraManager->GetCameraLocation(), First);
	TestFalse(TEXT("Movement is not flagged as a camera cut"), !!CameraManager->bGameCameraCutThisFrame);
	TestTrue(TEXT("Catch-up continues for subsequent frames"), Mode->bFinishCameraCatchUpActive);
	B->SetActorLocation(FVector(0.f, 0.f, -1.f));
	C->SetActorLocation(FVector(0.f, 0.f, -2.f));
	Mode->Tick(Delta);
	TestEqual(TEXT("Simultaneous crossings skip every finished marble"), Mode->CurrentLeader.Get(), D);
	TestTrue(TEXT("Retargeting stays continuous"), Mode->FollowCamera->GetActorLocation().Z > First.Z && Mode->FollowCamera->GetActorLocation().Z < 9000.f);
	for (int32 Frame = 0; Frame < 120 && Mode->bFinishCameraCatchUpActive; ++Frame) Mode->Tick(Delta);
	TestFalse(TEXT("Near the successor the camera returns to ordinary follow"), Mode->bFinishCameraCatchUpActive);
	TestTrue(TEXT("Camera catches up close to the successor"), FMath::Abs(Mode->FollowCamera->GetActorLocation().Z - 9000.f) <= 250.f);
	Mode->FollowCamera->SetActorLocation(FVector(32.f, -1200.f, 9000.f));
	D->SetActorLocation(FVector(0.f, 0.f, 8800.f));
	Mode->Tick(Delta);
	TestTrue(TEXT("Ordinary follow keeps its original smoothing"), Mode->FollowCamera->GetActorLocation().Z > 8800.f && Mode->FollowCamera->GetActorLocation().Z < 9000.f);
	E->SetActorLocation(FVector(0.f, 0.f, 8000.f));
	Mode->Tick(Delta);
	TestEqual(TEXT("Ordinary overtakes still select the new leader"), Mode->CurrentLeader.Get(), E);
	const double BeforeGap = Mode->FollowCamera->GetActorLocation().Z;
	TestTrue(TEXT("Ordinary overtakes keep their smooth transition"), BeforeGap > 8000.f);
	E->SetActorLocation(FVector(0.f, 0.f, -1.f));
	D->SetActorHiddenInGame(true);
	Mode->Tick(Delta);
	TestEqual(TEXT("Without a successor the camera holds"), Mode->FollowCamera->GetActorLocation().Z, BeforeGap);
	TestTrue(TEXT("Catch-up survives a gap in eligible leaders"), Mode->bFinishCameraCatchUpActive);
	D->SetActorHiddenInGame(false);
	Mode->Tick(Delta);
	const double BeforeLast = Mode->FollowCamera->GetActorLocation().Z;
	TestTrue(TEXT("An available successor is approached without a jump"), BeforeLast > 8800.f && BeforeLast < BeforeGap);
	D->SetActorLocation(FVector(0.f, 0.f, -1.f));
	Mode->Tick(Delta);
	TestEqual(TEXT("The final finish holds the camera"), Mode->FollowCamera->GetActorLocation().Z, BeforeLast);
	TestFalse(TEXT("Final finish clears the leader"), Mode->CurrentLeader.IsValid());
	Instance->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleCameraOffCourseTest,
	"MarbleRace.Camera.OffCourseMarbleIsNotLeader",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleCameraOffCourseTest::RunTest(const FString& Parameters)
{
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	AMarbleRaceLevelGameMode* Mode = World->SpawnActor<AMarbleRaceLevelGameMode>();
	const auto SpawnMarble = [World](const FVector Location)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(Actor);
		Actor->AddInstanceComponent(Root);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	};

	// 被滚筒甩出去的弹珠：横向已经远离赛道中心线，而且掉得很深，不能把镜头带下去。
	AActor* OnCourse = SpawnMarble(FVector(120.f, 0.f, -8000.f));
	AActor* ThrownFar = SpawnMarble(FVector(9000.f, 0.f, -30000.f));
	AActor* FallingFast = SpawnMarble(FVector(120.f, 0.f, -20000.f));
	Cast<USceneComponent>(FallingFast->GetRootComponent())->ComponentVelocity = FVector(0.f, 0.f, -40000.f);
	Mode->Marbles.Add(OnCourse);
	Mode->Marbles.Add(ThrownFar);
	Mode->Marbles.Add(FallingFast);
	Mode->CourseCentre = FVector2D::ZeroVector;

	TestTrue(TEXT("On-course marble is a leader candidate"), Mode->IsLeaderCandidate(OnCourse));
	TestFalse(TEXT("Marble thrown far sideways is not a leader candidate"), Mode->IsLeaderCandidate(ThrownFar));
	TestFalse(TEXT("Marble in free fall is not a leader candidate"), Mode->IsLeaderCandidate(FallingFast));
	TestTrue(TEXT("Off-course marbles stay eligible (they must still be marked finished)"), Mode->IsEligibleMarble(ThrownFar));
	TestTrue(TEXT("The camera keeps following the on-course marble"), Mode->FindLeadingMarble() == OnCourse);

	// 镜头中心线跟随滚筒，横向判定以赛道中心线为准。
	Mode->CourseCentre = FVector2D(9000.f, 0.f);
	TestTrue(TEXT("Lateral limit is measured from the course centre"), Mode->IsLeaderCandidate(ThrownFar));
	TestFalse(TEXT("The previous on-course marble is now off the lane"), Mode->IsLeaderCandidate(OnCourse));

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}

#endif
