#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Race/MarbleCameraFollow.h"
#include "Race/MarbleRaceLevelGameMode.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
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
