#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Race/MarbleWallResponseComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "PhysicsEngine/BodyInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaseMapWallBindingsTest, "MarbleRace.Walls.ChaseMapBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChaseMapWallBindingsTest::RunTest(const FString& Parameters)
{
	UWorld* Map = LoadObject<UWorld>(nullptr, TEXT("/Game/Maps/MainContent.MainContent"));
	if (!TestNotNull(TEXT("Actual race map loads"), Map)) return false;
	AActor* Track = nullptr;
	for (AActor* Actor : Map->PersistentLevel->Actors)
	{
		if (Actor && Actor->GetClass()->GetName() == TEXT("关卡-追逐斜坡_C"))
		{
			Track = Actor;
			break;
		}
	}
	if (!TestNotNull(TEXT("Placed chasing slope exists"), Track)) return false;
	auto* Response = Track->FindComponentByClass<UMarbleWallResponseComponent>();
	if (!TestNotNull(TEXT("Placed track has its response component"), Response)) return false;
	TestTrue(TEXT("Placed component is enabled"), Response->bEnabled);
	TestEqual(TEXT("Placed instance has two wall bindings, not an empty override"), Response->WallComponentNames.Num(), 2);
	TestTrue(TEXT("Both actual wall names are configured"),
		Response->WallComponentNames.Contains(TEXT("墙")) && Response->WallComponentNames.Contains(TEXT("墙1")));
	TArray<UChildActorComponent*> Children;
	Track->GetComponents(Children);
	int32 MatchingWalls = 0;
	for (UChildActorComponent* Child : Children)
		if (Child && Response->WallComponentNames.Contains(Child->GetFName()) && Child->GetChildActorClass()) ++MatchingWalls;
	TestEqual(TEXT("Saved bindings resolve to both placed wall components"), MatchingWalls, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleWallResponseTest, "MarbleRace.Walls.LocalBounceAndSpin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleWallResponseTest::RunTest(const FString& Parameters)
{
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	AActor* Track = World->SpawnActor<AActor>();
	USceneComponent* Root = NewObject<USceneComponent>(Track);
	Track->AddInstanceComponent(Root);
	Track->SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Static);
	Root->RegisterComponent();
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	TArray<UStaticMeshComponent*> Walls;
	for (const FName Name : {FName(TEXT("LeftWall")), FName(TEXT("RightWall")), FName(TEXT("OtherWall"))})
	{
		UChildActorComponent* Child = NewObject<UChildActorComponent>(Track, Name);
		Track->AddInstanceComponent(Child);
		Child->SetMobility(EComponentMobility::Static);
		Child->SetupAttachment(Root);
		Child->SetChildActorClass(AStaticMeshActor::StaticClass());
		Child->RegisterComponent();
		UStaticMeshComponent* Wall = Cast<AStaticMeshActor>(Child->GetChildActor())->GetStaticMeshComponent();
		Wall->SetMobility(EComponentMobility::Movable);
		Wall->SetStaticMesh(Cube);
		Wall->SetNotifyRigidBodyCollision(false);
		Wall->SetWorldLocation(FVector(Walls.Num() == 0 ? -500. : 500., 0., 0.));
		Walls.Add(Wall);
	}
	auto* Response = NewObject<UMarbleWallResponseComponent>(Track);
	Track->AddInstanceComponent(Response);
	Response->WallComponentNames = {TEXT("LeftWall"), TEXT("RightWall")};
	Response->RegisterComponent();
	UClass* MarbleClass = LoadClass<AActor>(nullptr, TEXT("/Game/角色/弹珠.弹珠_C"));
	AActor* Marble = MarbleClass ? World->SpawnActor<AActor>(MarbleClass) : nullptr;
	UPrimitiveComponent* Body = Marble ? Cast<UPrimitiveComponent>(Marble->GetRootComponent()) : nullptr;
	if (TestNotNull(TEXT("Real marble body exists"), Body))
	{
		Marble->SetActorScale3D(FVector(.33f));
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		World->GetWorldSettings()->NotifyBeginPlay();
		Body->SetSimulatePhysics(true);
		Body->SetEnableGravity(false);
		const auto* OriginalMaterial = Body->BodyInstance.GetSimplePhysicalMaterial();
		TestTrue(TEXT("Both selected walls receive hit notifications"),
			Walls[0]->BodyInstance.bNotifyRigidBodyCollision && Walls[1]->BodyInstance.bNotifyRigidBodyCollision);
		TestEqual(TEXT("Runtime binding count confirms two monitored walls"), Response->BoundWallCount, 2);
		TestFalse(TEXT("Other walls stay untouched"), Walls[2]->BodyInstance.bNotifyRigidBodyCollision);
		Body->SetWorldLocation(FVector(-375., 0., 0.), false, nullptr, ETeleportType::TeleportPhysics);
		Body->SetPhysicsLinearVelocity(FVector(-1000., 0., 0.));
		Body->SetPhysicsAngularVelocityInRadians(FVector(0., 20., 0.));
		Response->TickComponent(.03f, LEVELTICK_All, nullptr);
		World->Tick(LEVELTICK_All, .03f);
		AddInfo(FString::Printf(TEXT("Real collision velocity %s, rotation %s"),
			*Body->GetPhysicsLinearVelocity().ToString(), *Body->GetPhysicsAngularVelocityInRadians().ToString()));
		TestTrue(TEXT("A real physics wall hit invokes the limited bounce"),
			Body->GetPhysicsLinearVelocity().X > 0. && Body->GetPhysicsLinearVelocity().X <= 400.01);
		TestTrue(TEXT("A real physics wall hit removes spin"), Body->GetPhysicsAngularVelocityInRadians().IsNearlyZero(.01));
		TestTrue(TEXT("Actual physics hit increments the response count"), Response->BounceCount > 0);
		World->Tick(LEVELTICK_All, .13f);
		Body->SetWorldLocation(FVector::ZeroVector, false, nullptr, ETeleportType::TeleportPhysics);
		auto HitWall = [&](int32 Index, FVector Velocity, FVector Normal)
		{
			Body->SetPhysicsLinearVelocity(Velocity);
			Body->SetPhysicsAngularVelocityInRadians(FVector(0., 20., 0.));
			Response->TickComponent(.01f, LEVELTICK_All, nullptr);
			// Simulate a solver that already cancelled the incoming horizontal speed.
			Body->SetPhysicsLinearVelocity(FVector(0., Velocity.Y, Velocity.Z));
			FHitResult Hit;
			Hit.ImpactNormal = Normal;
			Walls[Index]->OnComponentHit.Broadcast(Walls[Index], Marble, Body, FVector::ZeroVector, Hit);
		};
		HitWall(0, FVector(-1000., 0., -200.), FVector(1., 0., 0.));
		TestTrue(TEXT("Left wall produces a gentle inward bounce and reduces sliding"),
			Body->GetPhysicsLinearVelocity().Equals(FVector(200., 0., -50.), .01));
		TestTrue(TEXT("Residual rotation is removed immediately"), Body->GetPhysicsAngularVelocityInRadians().IsNearlyZero(.01));
		const FVector FirstBounce = Body->GetPhysicsLinearVelocity();
		FHitResult Repeat;
		Repeat.ImpactNormal = FVector(1., 0., 0.);
		Walls[0]->OnComponentHit.Broadcast(Walls[0], Marble, Body, FVector::ZeroVector, Repeat);
		TestTrue(TEXT("Duplicate hit events do not repeatedly slow sliding"), Body->GetPhysicsLinearVelocity().Equals(FirstBounce, .01));
		World->Tick(LEVELTICK_All, .13f);
		HitWall(1, FVector(10000., 0., -200.), FVector(-1., 0., 0.));
		TestTrue(TEXT("Right wall rebounds left with a capped speed"),
			Body->GetPhysicsLinearVelocity().Equals(FVector(-400., 0., -50.), .01));
		TestTrue(TEXT("The marble keeps its physical material"), Body->BodyInstance.GetSimplePhysicalMaterial() == OriginalMaterial);
		Response->bEnabled = false;
		HitWall(0, FVector(-1000., 0., -200.), FVector(1., 0., 0.));
		TestTrue(TEXT("Disabled response leaves solver velocity and spin alone"),
			Body->GetPhysicsLinearVelocity().Equals(FVector(0., 0., -200.), .01) &&
			Body->GetPhysicsAngularVelocityInRadians().Equals(FVector(0., 20., 0.), .01));
		Response->EndPlay(EEndPlayReason::Destroyed);
		TestFalse(TEXT("Ending the effect restores the original wall hit setting"), Walls[0]->BodyInstance.bNotifyRigidBodyCollision);
	}
	World->EndPlay(EEndPlayReason::Destroyed);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}

#endif
