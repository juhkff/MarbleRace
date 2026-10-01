#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Race/SimpleMoveComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

namespace
{
	struct FSimpleMoveFixture
	{
		UWorld* World;
		USceneComponent* Target;
		USimpleMoveComponent* Move;
		const FVector Start = FVector(100, 20, 300);

		FSimpleMoveFixture()
		{
			const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
				ERHIFeatureLevel::SM5, &Values);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			AActor* Owner = World->SpawnActor<AActor>();
			Target = NewObject<USceneComponent>(Owner);
			Owner->AddInstanceComponent(Target);
			Owner->SetRootComponent(Target);
			Target->RegisterComponent();
			Target->SetRelativeLocation(Start);
			Move = NewObject<USimpleMoveComponent>(Owner);
			Owner->AddInstanceComponent(Move);
			Move->SetupAttachment(Target);
			Move->RegisterComponent();
			Move->MoveDistance = 100;
			Move->OutboundSpeed = 100;
			Move->OutboundSpeedMax = 100;
			Move->ReturnSpeed = 50;
			Move->ReturnSpeedMax = 50;
			Move->CycleInterval = 1;
		}
		void Tick(float Seconds) { Move->TickComponent(Seconds, LEVELTICK_All, nullptr); }
		~FSimpleMoveFixture()
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimpleMoveReciprocalTest, "MarbleRace.SimpleMove.ReciprocalAndOvershoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimpleMoveReciprocalTest::RunTest(const FString& Parameters)
{
	FSimpleMoveFixture F;
	F.Move->MoveDirection = FVector(3, 0, 4);
	const FVector Direction(.6, 0, .8);
	F.Tick(.5f);
	TestTrue(TEXT("Normalized direction and outbound speed"), F.Target->GetRelativeLocation().Equals(F.Start + Direction * 50));
	F.Tick(.5f);
	TestTrue(TEXT("Exact turnaround reaches endpoint"), F.Target->GetRelativeLocation().Equals(F.Start + Direction * 100));
	F.Tick(1.f);
	TestTrue(TEXT("Endpoint interval delays the return"), F.Target->GetRelativeLocation().Equals(F.Start + Direction * 100));
	F.Tick(1.f);
	TestTrue(TEXT("Independent return speed"), F.Target->GetRelativeLocation().Equals(F.Start + Direction * 50));
	F.Tick(1.5f);
	TestTrue(TEXT("Interval holds at start"), F.Target->GetRelativeLocation().Equals(F.Start));
	F.Tick(10.75f);
	TestTrue(TEXT("Large frame preserves remaining cycle time"), F.Target->GetRelativeLocation().Equals(F.Start + Direction * 25));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimpleMoveOneWayTest, "MarbleRace.SimpleMove.OneWayAndInvalidSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimpleMoveOneWayTest::RunTest(const FString& Parameters)
{
	FSimpleMoveFixture F;
	F.Move->bReciprocate = false;
	F.Move->ReturnSpeed = 0;
	F.Tick(1.5f);
	TestTrue(TEXT("One-way waits at endpoint and ignores return speed"), F.Target->GetRelativeLocation().Equals(F.Start + FVector(100, 0, 0)));
	F.Tick(.5f);
	TestTrue(TEXT("Next cycle resets to start"), F.Target->GetRelativeLocation().Equals(F.Start));
	F.Tick(4.25f);
	TestTrue(TEXT("One-way overshoot"), F.Target->GetRelativeLocation().Equals(F.Start + FVector(25, 0, 0)));
	F.Move->OutboundSpeed = 0;
	F.Tick(10.f);
	TestTrue(TEXT("Zero speed does not divide by zero or advance"), F.Target->GetRelativeLocation().Equals(F.Start + FVector(25, 0, 0)));
	F.Move->OutboundSpeed = 100;
	F.Move->MoveDirection = FVector::ZeroVector;
	F.Tick(.1f);
	TestTrue(TEXT("Zero direction safely restores start"), F.Target->GetRelativeLocation().Equals(F.Start));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimpleMoveAttachmentTest, "MarbleRace.SimpleMove.AttachmentCoordinates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimpleMoveAttachmentTest::RunTest(const FString& Parameters)
{
	FSimpleMoveFixture F;
	AActor* Host = F.World->SpawnActor<AActor>();
	USceneComponent* Frame = NewObject<USceneComponent>(Host);
	Host->AddInstanceComponent(Frame);
	Host->SetRootComponent(Frame);
	Frame->RegisterComponent();
	Frame->SetWorldLocation(FVector(1000, 2000, 3000));
	Frame->SetWorldRotation(FRotator(0, 90, 0));
	Frame->SetWorldScale3D(FVector(2, 3, 4));
	F.Target->AttachToComponent(Frame, FAttachmentTransformRules::KeepRelativeTransform);
	F.Target->SetRelativeRotation(FRotator(10, 20, 30));
	F.Target->SetRelativeScale3D(FVector(5, 6, 7));
	const FVector StartWorld = F.Target->GetComponentLocation();
	const FRotator Rotation = F.Target->GetRelativeRotation();
	F.Tick(.5f);
	TestTrue(TEXT("Direction follows rotated host, distance ignores scaling"),
		F.Target->GetComponentLocation().Equals(StartWorld + FVector(0, 50, 0), .001));
	TestTrue(TEXT("Rotation is retained"), F.Target->GetRelativeRotation().Equals(Rotation));
	TestTrue(TEXT("Scale is retained"), F.Target->GetRelativeScale3D().Equals(FVector(5, 6, 7)));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimpleMoveStartupTest, "MarbleRace.SimpleMove.StartDelayOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimpleMoveStartupTest::RunTest(const FString& Parameters)
{
	for (const bool bReciprocate : {false, true})
	{
		FSimpleMoveFixture F;
		F.Move->bReciprocate = bReciprocate;
		F.Move->StartDelay = 2;
		F.Move->BeginPlay();
		const float Delay = F.Move->ActualStartDelay;
		TestTrue(TEXT("Sample is within configured bounds"), Delay >= 0.f && Delay <= 2.f);
		F.Tick(Delay * .5f);
		TestTrue(TEXT("No movement during startup delay"), F.Target->GetRelativeLocation().Equals(F.Start));
		F.Tick(Delay * .5f);
		TestTrue(TEXT("Delay boundary keeps the initial position"), F.Target->GetRelativeLocation().Equals(F.Start));
		F.Tick(.25f);
		TestTrue(TEXT("Movement begins after startup delay"), F.Target->GetRelativeLocation().Equals(F.Start + FVector(25, 0, 0)));
		F.Move->StartDelay = 100;
		TestEqual(TEXT("Startup sample remains unchanged"), F.Move->ActualStartDelay, Delay);
		F.Tick(bReciprocate ? 5.f : 2.f);
		TestTrue(TEXT("Following cycles and runtime edits do not repeat startup delay"),
			F.Target->GetRelativeLocation().Equals(F.Start + FVector(25, 0, 0)));
	}
	{
		FSimpleMoveFixture F;
		F.Move->StartDelay = 2;
		F.Move->BeginPlay();
		F.Tick(F.Move->ActualStartDelay + .5f);
		TestTrue(TEXT("Frame crossing delay preserves remaining movement time"),
			F.Target->GetRelativeLocation().Equals(F.Start + FVector(50, 0, 0), .001));
	}
	{
		FSimpleMoveFixture F;
		F.Move->StartDelay = -1;
		F.Tick(.5f);
		TestTrue(TEXT("Negative delay acts as zero"), F.Target->GetRelativeLocation().Equals(F.Start + FVector(50, 0, 0)));
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimpleMoveRandomSpeedTest, "MarbleRace.SimpleMove.RandomSpeedRanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimpleMoveRandomSpeedTest::RunTest(const FString& Parameters)
{
	FSimpleMoveFixture F;
	F.Move->OutboundSpeed = 80;
	F.Move->OutboundSpeedMax = 120;
	F.Move->ReturnSpeed = 60;
	F.Move->ReturnSpeedMax = 40;
	F.Tick(.1f);
	const float Outbound = F.Move->ActualOutboundSpeed;
	const float Return = F.Move->ActualReturnSpeed;
	TestTrue(TEXT("Outbound sample stays within range"), Outbound >= 80 && Outbound <= 120);
	TestTrue(TEXT("Reversed return bounds are normalized"), Return >= 40 && Return <= 60);
	F.Tick(.1f);
	TestEqual(TEXT("Outbound sample is constant during a cycle"), F.Move->ActualOutboundSpeed, Outbound);
	TestEqual(TEXT("Return sample is constant during a cycle"), F.Move->ActualReturnSpeed, Return);
	TestTrue(TEXT("Motion uses sampled speed"), F.Target->GetRelativeLocation().Equals(F.Start + FVector(Outbound * .2, 0, 0), .001));

	// Force a disjoint range next cycle so this test does not rely on chance.
	F.Move->OutboundSpeed = F.Move->OutboundSpeedMax = 300;
	F.Move->ReturnSpeed = F.Move->ReturnSpeedMax = 150;
	const double Duration = 100.0 / Outbound + 100.0 / Return + 2.0;
	F.Tick(static_cast<float>(Duration - .2 + .01));
	TestEqual(TEXT("Next cycle samples updated outbound range"), F.Move->ActualOutboundSpeed, 300.f);
	TestEqual(TEXT("Next cycle samples updated return range"), F.Move->ActualReturnSpeed, 150.f);
	TestTrue(TEXT("Cycle boundary preserves frame remainder"), F.Target->GetRelativeLocation().Equals(F.Start + FVector(3, 0, 0), .002));
	{
		FSimpleMoveFixture G;
		G.Move->bReciprocate = false;
		G.Move->OutboundSpeed = 70;
		G.Move->OutboundSpeedMax = 90;
		G.Move->ReturnSpeed = G.Move->ReturnSpeedMax = 0;
		G.Tick(.25f);
		TestEqual(TEXT("One-way mode does not sample return speed"), G.Move->ActualReturnSpeed, 0.f);
		TestTrue(TEXT("One-way motion uses outbound range"), G.Target->GetRelativeLocation().Equals(
			G.Start + FVector(G.Move->ActualOutboundSpeed * .25, 0, 0), .001));
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimpleMoveMovingCollisionTest, "MarbleRace.SimpleMove.KinematicPush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSimpleMoveMovingCollisionTest::RunTest(const FString& Parameters)
{
	FSimpleMoveFixture F;
	AActor* BlockActor = F.World->SpawnActor<AActor>();
	UBoxComponent* Block = NewObject<UBoxComponent>(BlockActor);
	BlockActor->AddInstanceComponent(Block);
	BlockActor->SetRootComponent(Block);
	Block->SetMobility(EComponentMobility::Movable);
	Block->SetBoxExtent(FVector(50, 100, 100));
	Block->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Block->SetCollisionResponseToAllChannels(ECR_Block);
	Block->RegisterComponent();
	TestFalse(TEXT("Enhanced detection is opt-in"), Block->BodyInstance.IsUsingMACD());
	F.Move->AttachToComponent(Block, FAttachmentTransformRules::KeepRelativeTransform);
	F.Move->bEnhancedMovingCollision = true;
	F.Move->bReciprocate = false;
	F.Move->MoveDistance = 200;
	F.Move->OutboundSpeed = F.Move->OutboundSpeedMax = 400;
	F.Move->CycleInterval = 10;

	AActor* BallActor = F.World->SpawnActor<AActor>();
	USphereComponent* Ball = NewObject<USphereComponent>(BallActor);
	BallActor->AddInstanceComponent(Ball);
	BallActor->SetRootComponent(Ball);
	Ball->SetSphereRadius(20);
	Ball->SetWorldLocation(FVector(100, 0, 0));
	Ball->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Ball->SetCollisionObjectType(ECC_PhysicsBody);
	Ball->SetCollisionResponseToAllChannels(ECR_Block);
	Ball->SetEnableGravity(false);
	Ball->BodyInstance.SetUseCCD(true);
	Ball->RegisterComponent();
	Ball->SetSimulatePhysics(true);
	F.World->InitializeActorsForPlay(FURL());
	F.World->BeginPlay();
	for (int32 Step = 0; Step < 120; ++Step)
	{
		// This transient world does not register the fixture's component tick with a level.
		F.Tick(1.f / 60.f);
		F.World->SetupPhysicsTickFunctions(1.f / 60.f);
		F.World->StartPhysicsSim();
		F.World->GetPhysicsScene()->WaitPhysScenes();
		F.World->FinishPhysicsSim();
	}
	TestTrue(TEXT("MACD reaches the actual moving body"), Block->BodyInstance.IsUsingMACD());
	TestTrue(TEXT("CCD reaches the actual moving body"), Block->BodyInstance.bUseCCD != 0);
	TestFalse(TEXT("Block stays kinematic"), Block->IsSimulatingPhysics());
	TestTrue(TEXT("Ball cannot stall prescribed motion"), Block->GetComponentLocation().Equals(FVector(200, 0, 0), .01));
	TestTrue(FString::Printf(TEXT("Physical contact pushes the ball beyond the block (ball X=%.3f)"),
		Ball->GetComponentLocation().X), Ball->GetComponentLocation().X >= 269);
	return true;
}
#endif
