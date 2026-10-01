#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Race/SmoothBrakeZone.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmoothBrakeLawTest, "MarbleRace.Braking.LawAndFrameRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothBrakeLawTest::RunTest(const FString& Parameters)
{
	const FVector Velocity(600, 0, -800);
	const FVector Acceleration = ASmoothBrakeZone::CalculateBrakeAcceleration(Velocity, 50, 30, 8000, 1, .01f);
	TestTrue(TEXT("Force opposes motion without steering"), Acceleration.GetSafeNormal().Equals(-Velocity.GetSafeNormal()));
	TestTrue(TEXT("Acceleration is capped"), FMath::IsNearlyEqual(Acceleration.Size(), 8000., .001));
	TestTrue(TEXT("A slower marble is not accelerated or stopped"),
		ASmoothBrakeZone::CalculateBrakeAcceleration(FVector(20, 0, 0), 50, 30, 8000, 1, .01f).IsZero());
	TestTrue(TEXT("Stationary marble stays unaffected"),
		ASmoothBrakeZone::CalculateBrakeAcceleration(FVector::ZeroVector, 50, 30, 8000, 1, .01f).IsZero());
	TestTrue(TEXT("Zero region weight applies no force"),
		ASmoothBrakeZone::CalculateBrakeAcceleration(Velocity, 50, 30, 8000, 0, .01f).IsZero());
	const FVector LongFrame = Velocity + ASmoothBrakeZone::CalculateBrakeAcceleration(Velocity, 50, 1000, 100000, 1, 1) * 1;
	TestTrue(TEXT("A long frame cannot reverse or drop below the target"), LongFrame.Equals(Velocity.GetSafeNormal() * 50., .001));
	for (const int32 FPS : {30, 60, 120})
	{
		FVector Speed(1500, 0, 0);
		const float Dt = 1.f / FPS;
		for (int32 Step = 0; Step < FPS; ++Step)
		{
			Speed += ASmoothBrakeZone::CalculateBrakeAcceleration(Speed, 50, 30, 8000, 1, Dt) * Dt;
		}
		TestTrue(TEXT("Continuous braking approaches a small positive speed at each frame rate"), Speed.X >= 49.99 && Speed.X < 51);
	}
	// Gravity still acts: the field removes excess speed rather than freezing the marble.
	FVector Falling(0, 0, -1500);
	for (int32 Step = 0; Step < 240; ++Step)
	{
		Falling += (ASmoothBrakeZone::CalculateBrakeAcceleration(Falling, 50, 30, 8000, 1, 1.f / 120)
			+ FVector(0, 0, -980)) / 120;
	}
	TestTrue(TEXT("With gravity, a small downward speed remains"), Falling.Z < -50 && Falling.Z > -90);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmoothBrakeVolumeTest, "MarbleRace.Braking.VolumeAndPhysics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSmoothBrakeVolumeTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* Zone = World->SpawnActor<ASmoothBrakeZone>();
	Zone->BrakeVolume->SetBoxExtent(FVector(400, 150, 300));
	TestEqual(TEXT("Interior has full strength"), Zone->GetSpatialWeight(FVector::ZeroVector), 1.f);
	TestEqual(TEXT("Boundary is smoothly unforced"), Zone->GetSpatialWeight(FVector(400, 0, 0)), 0.f);
	TestEqual(TEXT("Outside is unforced"), Zone->GetSpatialWeight(FVector(500, 0, 0)), 0.f);
	TestTrue(TEXT("Halfway through the fade is half strength"), FMath::IsNearlyEqual(Zone->GetSpatialWeight(FVector(350, 0, 0)), .5f));
	Zone->SetActorTransform(FTransform(FRotator(0, 45, 0), FVector(1000, 2000, 3000), FVector(2, 2, 2)));
	TestTrue(TEXT("Transforms do not change the fade's world-space distance"),
		FMath::IsNearlyEqual(Zone->GetSpatialWeight(Zone->GetActorTransform().TransformPosition(FVector(375, 0, 0))), .5f));
	Zone->SetActorTransform(FTransform::Identity);
	Zone->BrakeVolume->SetBoxExtent(FVector(3000, 150, 3000));
	UClass* MarbleClass = LoadClass<AActor>(nullptr, TEXT("/Game/角色/弹珠.弹珠_C"));
	AActor* Marble = MarbleClass ? World->SpawnActor<AActor>(MarbleClass) : nullptr;
	UPrimitiveComponent* Body = Marble ? Cast<UPrimitiveComponent>(Marble->GetRootComponent()) : nullptr;
	if (TestNotNull(TEXT("Marble physics body exists"), Body))
	{
		Marble->SetActorScale3D(FVector(.33f));
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		World->GetWorldSettings()->NotifyBeginPlay();
		Body->SetEnableGravity(false);
		Body->SetPhysicsLinearVelocity(FVector(1000, 0, 0));
		Body->SetPhysicsAngularVelocityInRadians(FVector(0, 10, 0));
		Zone->BrakeVolume->UpdateOverlaps();
		TArray<UPrimitiveComponent*> Overlaps;
		Zone->BrakeVolume->GetOverlappingComponents(Overlaps);
		AddInfo(FString::Printf(TEXT("Brake overlap count %d, marble overlap %d, physics %d, actor begun %d"),
			Overlaps.Num(), int32(Body->GetGenerateOverlapEvents()), int32(Body->IsSimulatingPhysics()), int32(Zone->HasActorBegunPlay())));
		Zone->Tick(.01f);
		TestTrue(TEXT("Applying a force does not overwrite velocity immediately"), Body->GetPhysicsLinearVelocity().Equals(FVector(1000, 0, 0), .01));
		// One real physics step: repeated world ticks in the same engine frame do not
		// dispatch actor ticks again. Long-duration convergence is covered above.
		World->Tick(LEVELTICK_All, .01f);
		const FVector After = Body->GetPhysicsLinearVelocity();
		AddInfo(FString::Printf(TEXT("After physics: velocity %s, angular %s"), *After.ToString(), *Body->GetPhysicsAngularVelocityInRadians().ToString()));
		TestTrue(TEXT("Physics integration smoothly slows the actual marble without stopping it"), After.X > 50 && After.X < 990);
		TestTrue(TEXT("Actual rolling rotation is gradually reduced"), Body->GetPhysicsAngularVelocityInRadians().Size() < 10);
		Zone->bEnableBraking = false;
		Body->SetPhysicsLinearVelocity(FVector(500, 0, 0));
		Zone->Tick(.01f);
		TestTrue(TEXT("Disabling the zone does not change marble velocity"), Body->GetPhysicsLinearVelocity().Equals(FVector(500, 0, 0), .01));
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}

#endif
