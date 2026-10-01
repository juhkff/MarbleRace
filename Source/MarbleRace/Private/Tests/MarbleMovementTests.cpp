#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Camera/CameraActor.h"
#include "Race/MarbleMovementLibrary.h"
#include "Components/ChildActorComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleMovementEndpointTest,
	"MarbleRace.Movement.EndpointCoordinateFrames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleMovementEndpointTest::RunTest(const FString& Parameters)
{
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);

	AActor* Level = World->SpawnActor<AActor>();
	USceneComponent* LevelRoot = NewObject<USceneComponent>(Level);
	Level->AddInstanceComponent(LevelRoot);
	Level->SetRootComponent(LevelRoot);
	LevelRoot->RegisterComponent();
	LevelRoot->SetWorldLocation(FVector(0, 0, 4932.42));

	UChildActorComponent* ChildComponent = NewObject<UChildActorComponent>(Level);
	Level->AddInstanceComponent(ChildComponent);
	ChildComponent->SetupAttachment(LevelRoot);
	ChildComponent->SetRelativeLocation(FVector(-1088.834, 0, -1952.342));
	// A different class avoids Unreal's recursive-child guard for AActor owners.
	ChildComponent->SetChildActorClass(ACameraActor::StaticClass());
	ChildComponent->RegisterComponent();
	AActor* Child = ChildComponent->GetChildActor();
	if (TestNotNull(TEXT("ChildActorComponent creates moving actor"), Child))
	{
		const FVector Start(-1139.1901, 0, -1952.342021);
		const FVector End(1418, 0, -1952.342021);
		TestTrue(TEXT("Child endpoint uses level origin, not child actor location"),
			UMarbleMovementLibrary::ResolveMovementEndpoint(Child, Start).Equals(
				FVector(-1139.1901, 0, 2980.077979), .001));
		TestTrue(TEXT("Translated level preserves requested rightward displacement"),
			(UMarbleMovementLibrary::ResolveMovementEndpoint(Child, End) -
			 UMarbleMovementLibrary::ResolveMovementEndpoint(Child, Start)).Equals(
				FVector(2557.1901, 0, 0), .001));

		const FTransform LevelTransform(FRotator(0, 45, 0), FVector(100, 200, 300), FVector(2, 3, 4));
		LevelRoot->SetWorldTransform(LevelTransform);
		TestTrue(TEXT("Child endpoint respects parent rotation and scale"),
			UMarbleMovementLibrary::ResolveMovementEndpoint(Child, End).Equals(
				LevelTransform.TransformPosition(End), .001));
		ChildComponent->SetRelativeLocation(FVector(900, 800, 700));
		TestTrue(TEXT("Changing child placement does not double-offset endpoints"),
			UMarbleMovementLibrary::ResolveMovementEndpoint(Child, End).Equals(
				LevelTransform.TransformPosition(End), .001));
	}

	AActor* Standalone = World->SpawnActor<AActor>();
	USceneComponent* StandaloneRoot = NewObject<USceneComponent>(Standalone);
	Standalone->AddInstanceComponent(StandaloneRoot);
	Standalone->SetRootComponent(StandaloneRoot);
	StandaloneRoot->RegisterComponent();
	Standalone->SetActorTransform(FTransform(FRotator(0, 90, 0), FVector(11, 22, 33), FVector(2)));
	const FVector Offset(100, 0, -20);
	TestTrue(TEXT("Standalone retains legacy world-axis offset, ignoring its rotation/scale"),
		UMarbleMovementLibrary::ResolveMovementEndpoint(Standalone, Offset).Equals(FVector(111, 22, 13), .001));
	Standalone->AttachToComponent(LevelRoot, FAttachmentTransformRules::KeepWorldTransform);
	TestTrue(TEXT("Direct attachment uses same parent frame as relocation"),
		UMarbleMovementLibrary::ResolveMovementEndpoint(Standalone, Offset).Equals(
			LevelRoot->GetComponentTransform().TransformPosition(Offset), .001));
	TestTrue(TEXT("Null actor safely returns endpoint"),
		UMarbleMovementLibrary::ResolveMovementEndpoint(nullptr, Offset).Equals(Offset));

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}

#endif
