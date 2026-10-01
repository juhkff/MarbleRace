#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Race/RelocationManagerActor.h"
#include "Race/RoundRobinRelocationManagerActor.h"
#include "Race/ComparisonRelocationManagerActor.h"
#include "RelocationManagerComponent.h"
#include "Race/RelocationSourceZone.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Race/DetectionLineScaleComponent.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FRelocationTestWorld
	{
		UWorld* World = nullptr;

		FRelocationTestWorld()
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
				.CreateAISystem(false).ShouldSimulatePhysics(true);
			// CreateWorld already initializes the world; do not create WorldSettings twice.
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
				ERHIFeatureLevel::SM5, &Values);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		}

		~FRelocationTestWorld()
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCircularTrackCollisionTest,
	"MarbleRace.Track.DoubleCircle.BlocksScaledMarbleAndPreservesHole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCircularTrackCollisionTest::RunTest(const FString& Parameters)
{
	FRelocationTestWorld Fixture;
	UStaticMesh* Track = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/Mesh_DoubleCircle.Mesh_DoubleCircle"));
	if (!TestNotNull(TEXT("Circular track loads"), Track)) return false;
	AActor* Actor = Fixture.World->SpawnActor<AActor>();
	auto* Mesh = NewObject<UStaticMeshComponent>(Actor);
	Actor->AddInstanceComponent(Mesh);
	Actor->SetRootComponent(Mesh);
	Mesh->SetStaticMesh(Track);
	Mesh->SetMobility(EComponentMobility::Static);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->RegisterComponent();
	Mesh->CreatePhysicsState(false);
	AddInfo(FString::Printf(TEXT("Track collision mode %d; triangle data %d; valid body %d; bounds %s"),
		int32(Track->GetBodySetup()->CollisionTraceFlag), int32(Track->ContainsPhysicsTriMeshData(true)),
		int32(Mesh->BodyInstance.IsValidBodyInstance()), *Mesh->Bounds.ToString()));
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CircularTrackTest));
	Params.bTraceComplex = false; // Same simple collision path used by physics marbles.
	const bool bHit = Fixture.World->SweepSingleByChannel(Hit, FVector(-1000, 0, 300),
		FVector(100, 0, 300), FQuat::Identity, ECC_PhysicsBody, FCollisionShape::MakeSphere(66.f), Params);
	TestTrue(TEXT("A .33-sized marble is blocked by the actual circular rail"), bHit && Hit.GetComponent() == Mesh);
	TestFalse(TEXT("The open interior is not filled by a convex hull"),
		Fixture.World->OverlapBlockingTestByChannel(FVector(-500, 0, 500), FQuat::Identity,
			ECC_PhysicsBody, FCollisionShape::MakeSphere(66.f), Params));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelocationScaledShapeTest,
	"MarbleRace.Relocation.Placement.ScaledMarbleIgnoresInflatedBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRelocationScaledShapeTest::RunTest(const FString& Parameters)
{
	FRelocationTestWorld Fixture;
	UClass* MarbleClass = LoadClass<AActor>(nullptr, TEXT("/Game/角色/弹珠.弹珠_C"));
	if (!TestNotNull(TEXT("Marble class loads"), MarbleClass)) return false;
	AActor* Marble = Fixture.World->SpawnActor<AActor>(MarbleClass, FVector(-10000, 0, 10000), FRotator(0, 0, 90));
	UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Marble->GetRootComponent());
	if (!TestNotNull(TEXT("Marble body exists"), Body)) return false;
	Marble->SetActorScale3D(FVector(.33f));
	Body->SetSimulatePhysics(true);
	Body->SetWorldRotation(FRotator(45, 0, 90));
	auto* Manager = Fixture.World->SpawnActor<ARelocationManagerActor>();
	Manager->RespawnLocation = FVector(10000, 0, 10000);
	auto* Queue = NewObject<URelocationManagerComponent>(Manager);
	Manager->AddInstanceComponent(Queue);
	Queue->RegisterComponent();
	Queue->MinimumReleaseInterval = 0;
	AActor* Wall = Fixture.World->SpawnActor<AActor>();
	auto* Mesh = NewObject<UStaticMeshComponent>(Wall);
	Wall->AddInstanceComponent(Mesh);
	Wall->SetRootComponent(Mesh);
	Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetWorldScale3D(FVector(.02f, 5, 5));
	Mesh->SetWorldLocation(Manager->RespawnLocation + FVector(80, 0, 0));
	Mesh->RegisterComponent();
	const float OldRadius = FMath::Max(Body->Bounds.BoxExtent.X, Body->Bounds.BoxExtent.Z);
	const bool bOriginalPhysicsStatePolicy = Body->bAlwaysCreatePhysicsState;
	Queue->EnqueueMarble(Marble, Body);
	if (!TestEqual(TEXT("Scaled marble enters queue"), Queue->PendingMarbles.Num(), 1)) return false;
	TestTrue(TEXT("Old bounding-box radius falsely overlaps a nearby wall"),
		Fixture.World->OverlapBlockingTestByChannel(Manager->RespawnLocation, FQuat::Identity,
			ECC_PhysicsBody, FCollisionShape::MakeSphere(OldRadius + 2)));
	Queue->TickComponent(0, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Actual scaled marble fits and is released"), Queue->PendingMarbles.IsEmpty());
	TestTrue(TEXT("Scale remains .33 after teleport"), Body->GetComponentScale().Equals(FVector(.33f)));
	TestEqual(TEXT("Original physics-state policy restored"), bool(Body->bAlwaysCreatePhysicsState), bOriginalPhysicsStatePolicy);
	// A genuine obstacle through the destination still blocks the same scaled marble.
	Mesh->SetWorldLocation(Manager->RespawnLocation);
	Queue->EnqueueMarble(Marble, Body);
	Queue->TickComponent(0, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Real collision retains queued marble"), Queue->PendingMarbles.Num(), 1);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Queue->TickComponent(0, LEVELTICK_All, nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoundRobinRelocationTest,
	"MarbleRace.Relocation.RoundRobin.PerMarble",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRoundRobinRelocationTest::RunTest(const FString& Parameters)
{
	FRelocationTestWorld Fixture;
	auto* Manager = Fixture.World->SpawnActor<ARoundRobinRelocationManagerActor>();
	AActor* A = Fixture.World->SpawnActor<AActor>();
	AActor* B = Fixture.World->SpawnActor<AActor>();
	FVector Position;
	TestFalse(TEXT("Empty array waits without a fallback"), Manager->TryRollRespawnLocation(A, Position));
	Manager->RespawnLocations = {FVector(100, 0, 300), FVector(200, 0, 400), FVector(300, 0, 500)};
	auto Expect = [this, Manager, &Position](AActor* Marble, int32 Index)
	{
		TestTrue(TEXT("Valid marble has a position"), Manager->TryRollRespawnLocation(Marble, Position));
		TestTrue(TEXT("Position matches this marble's own index"), Position.Equals(Manager->RespawnLocations[Index]));
	};
	Expect(A, 0);
	Expect(A, 0); // Blocked/repeated attempts must not advance.
	Manager->CommitMarbleRespawn(A);
	Expect(A, 1);
	Expect(B, 0);
	Manager->CommitMarbleRespawn(B);
	Manager->CommitMarbleRespawn(A);
	Expect(A, 2);
	Expect(B, 1);
	Manager->CommitMarbleRespawn(A);
	Expect(A, 0);
	Manager->RespawnLocations.SetNum(1);
	Expect(B, 0);
	Manager->CommitMarbleRespawn(B);
	Expect(B, 0);
	TestFalse(TEXT("Invalid marble cannot be released"), Manager->TryRollRespawnLocation(nullptr, Position));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelocationImpulseSamplingTest,
	"MarbleRace.Relocation.Impulse.SamplingAndCoordinateFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRelocationImpulseSamplingTest::RunTest(const FString& Parameters)
{
	FRelocationTestWorld Fixture;
	ARelocationManagerActor* Manager = Fixture.World->SpawnActor<ARelocationManagerActor>();
	TestFalse(TEXT("Force is disabled by default"), Manager->bEnableRespawnImpulse);
	TestTrue(TEXT("Disabled force returns zero"), Manager->RollRespawnImpulse().IsZero());

	Manager->bEnableRespawnImpulse = true;
	Manager->RespawnImpulseDirectionRange.Minimum = FVector(5, 0, 0);
	Manager->RespawnImpulseDirectionRange.Maximum = FVector(5, 0, 0);
	Manager->RespawnImpulseMagnitudeRange.Minimum = 20;
	Manager->RespawnImpulseMagnitudeRange.Maximum = 20;
	TestTrue(TEXT("Direction is normalized independently of magnitude"),
		Manager->RollRespawnImpulse().Equals(FVector(20, 0, 0), 1.e-5));

	AActor* ReferenceActor = Fixture.World->SpawnActor<AActor>();
	USceneComponent* Reference = NewObject<USceneComponent>(ReferenceActor);
	ReferenceActor->AddInstanceComponent(Reference);
	ReferenceActor->SetRootComponent(Reference);
	Reference->RegisterComponent();
	Reference->SetWorldTransform(FTransform(FRotator(0, 90, 0), FVector(100, 200, 300), FVector(3, 2, 4)));
	Manager->AttachToComponent(Reference, FAttachmentTransformRules::KeepWorldTransform);
	TestTrue(TEXT("Local direction follows reference rotation, not translation or scale"),
		Manager->RollRespawnImpulse().Equals(FVector(0, 20, 0), 1.e-4));

	Manager->RespawnImpulseDirectionRange.Minimum = FVector(-1, -1, 2);
	Manager->RespawnImpulseDirectionRange.Maximum = FVector(1, 1, 4);
	Manager->RespawnImpulseMagnitudeRange.Minimum = 100;
	Manager->RespawnImpulseMagnitudeRange.Maximum = 50;
	for (int32 Index = 0; Index < 64; ++Index)
	{
		const FVector Impulse = Manager->RollRespawnImpulse();
		TestTrue(TEXT("Reversed magnitude bounds are sampled in range"),
			Impulse.Size() >= 50 - 1.e-4 && Impulse.Size() <= 100 + 1.e-4);
		const FVector LocalDirection = Reference->GetComponentTransform().InverseTransformVectorNoScale(Impulse).GetSafeNormal();
		TestTrue(TEXT("Sample remains in the configured upward cone"),
			LocalDirection.Z > 0 && FMath::Abs(LocalDirection.X / LocalDirection.Z) <= .5001
			&& FMath::Abs(LocalDirection.Y / LocalDirection.Z) <= .5001);
	}

	Manager->RespawnImpulseDirectionRange.Minimum = FVector::ZeroVector;
	Manager->RespawnImpulseDirectionRange.Maximum = FVector::ZeroVector;
	TestTrue(TEXT("Zero direction safely returns zero"), Manager->RollRespawnImpulse().IsZero());
	Manager->RespawnImpulseDirectionRange.Minimum = FVector::UpVector;
	Manager->RespawnImpulseDirectionRange.Maximum = FVector::UpVector;
	Manager->RespawnImpulseMagnitudeRange.Minimum = -10;
	Manager->RespawnImpulseMagnitudeRange.Maximum = -1;
	TestTrue(TEXT("Negative magnitude safely clamps to zero"), Manager->RollRespawnImpulse().IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelocationImpulseReleaseTest,
	"MarbleRace.Relocation.Impulse.AppliedOnceAfterSuccessfulRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRelocationImpulseReleaseTest::RunTest(const FString& Parameters)
{
	FRelocationTestWorld Fixture;
	ARelocationManagerActor* Manager = Fixture.World->SpawnActor<ARelocationManagerActor>();
	Manager->RespawnLocation = FVector(10000, 0, 10000);
	Manager->bEnableGravityAfterRespawn = false;
	URelocationManagerComponent* Queue = NewObject<URelocationManagerComponent>(Manager);
	Manager->AddInstanceComponent(Queue);
	Queue->RegisterComponent();
	Queue->MinimumReleaseInterval = 0;

	UClass* MarbleClass = LoadClass<AActor>(nullptr, TEXT("/Game/角色/弹珠.弹珠_C"));
	if (!TestNotNull(TEXT("Project marble class loads"), MarbleClass))
	{
		return false;
	}
	AActor* Marble = Fixture.World->SpawnActor<AActor>(MarbleClass, FVector(-10000, 0, 10000), FRotator::ZeroRotator);
	UPrimitiveComponent* Body = Marble ? Cast<UPrimitiveComponent>(Marble->GetRootComponent()) : nullptr;
	if (!TestNotNull(TEXT("Marble has a physics body"), Body))
	{
		return false;
	}
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetSimulatePhysics(true);
	Body->SetEnableGravity(false);
	Body->SetPhysicsLinearVelocity(FVector(20, 0, 30));

	Queue->EnqueueMarble(Marble, Body);
	TestEqual(TEXT("Marble enters queue"), Queue->PendingMarbles.Num(), 1);
	Queue->TickComponent(0, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Marble is released"), Queue->PendingMarbles.Num(), 0);
	TestTrue(TEXT("Default disabled force preserves zero respawn velocity"), Body->GetPhysicsLinearVelocity().IsNearlyZero());

	Manager->bEnableRespawnImpulse = true;
	Manager->RespawnImpulseDirectionRange.Minimum = FVector::UpVector;
	Manager->RespawnImpulseDirectionRange.Maximum = FVector::UpVector;
	Manager->RespawnImpulseMagnitudeRange.Minimum = 8000;
	Manager->RespawnImpulseMagnitudeRange.Maximum = 8000;
	Body->SetPhysicsLinearVelocity(FVector(20, 0, 30));
	Queue->EnqueueMarble(Marble, Body);
	TestTrue(TEXT("No impulse while waiting"), !Body->IsSimulatingPhysics() && Marble->IsHidden());
	Queue->TickComponent(0, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Enabled force still releases the marble"), Queue->PendingMarbles.Num(), 0);
	Fixture.World->Tick(LEVELTICK_All, 1.f / 60.f);
	const FVector Velocity = Body->GetPhysicsLinearVelocity();
	TestTrue(TEXT("One upward impulse applied after velocity reset"),
		Velocity.Z > 0 && FMath::Abs(Velocity.X) < .01 && FMath::Abs(Velocity.Y) < .01);
	Queue->TickComponent(0, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Empty queue cannot reapply the impulse"), Body->GetPhysicsLinearVelocity().Equals(Velocity, .01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRelocationRestitutionTest,
	"MarbleRace.Relocation.Restitution.PerMarbleIsolationAndTeleport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRelocationRestitutionTest::RunTest(const FString& Parameters)
{
	FRelocationTestWorld Fixture;
	ARelocationSourceZone* Zone = Fixture.World->SpawnActor<ARelocationSourceZone>();
	TestFalse(TEXT("Restitution change defaults off"), Zone->bEnableRestitutionChange);
	TestEqual(TEXT("Target restitution defaults to zero"), Zone->RestitutionAfterCrossing, 0.f);
	UClass* MarbleClass = LoadClass<AActor>(nullptr, TEXT("/Game/角色/弹珠.弹珠_C"));
	if (!TestNotNull(TEXT("Marble class loads"), MarbleClass)) return false;
	AActor* First = Fixture.World->SpawnActor<AActor>(MarbleClass, FVector(-10000, 0, 10000), FRotator::ZeroRotator);
	AActor* Second = Fixture.World->SpawnActor<AActor>(MarbleClass, FVector(-20000, 0, 10000), FRotator::ZeroRotator);
	UPrimitiveComponent* Body = First ? Cast<UPrimitiveComponent>(First->GetRootComponent()) : nullptr;
	UPrimitiveComponent* OtherBody = Second ? Cast<UPrimitiveComponent>(Second->GetRootComponent()) : nullptr;
	if (!TestNotNull(TEXT("First physics body exists"), Body) || !TestNotNull(TEXT("Second physics body exists"), OtherBody)) return false;
	Body->SetSimulatePhysics(true);
	OtherBody->SetSimulatePhysics(true);
	UPhysicalMaterial* Shared = NewObject<UPhysicalMaterial>(Fixture.World);
	Shared->Restitution = .8f;
	Shared->Friction = .6f;
	Body->SetPhysMaterialOverride(Shared);
	OtherBody->SetPhysMaterialOverride(Shared);
	TestFalse(TEXT("Disabled setting does nothing"), Zone->ApplyCrossingRestitution(First, Body));
	TestTrue(TEXT("Disabled setting preserves material"), Body->BodyInstance.GetPhysMaterialOverride() == Shared);

	Zone->bEnableRestitutionChange = true;
	TestTrue(TEXT("Enabled setting accepts physical marble"), Zone->ApplyCrossingRestitution(First, Body));
	UPhysicalMaterial* Runtime = Body->BodyInstance.GetPhysMaterialOverride();
	if (!TestNotNull(TEXT("A runtime material is assigned"), Runtime)) return false;
	TestTrue(TEXT("Material belongs to this body only"), Runtime != Shared && Runtime->GetOuter() == Body && Runtime->HasAnyFlags(RF_Transient));
	TestEqual(TEXT("Only restitution is changed"), Runtime->Restitution, 0.f);
	TestEqual(TEXT("Friction is retained"), Runtime->Friction, Shared->Friction);
	TestEqual(TEXT("Shared material stays unchanged"), Shared->Restitution, .8f);
	TestTrue(TEXT("Other marble keeps shared material"), OtherBody->BodyInstance.GetPhysMaterialOverride() == Shared);
	Zone->RestitutionAfterCrossing = .65f;
	TestTrue(TEXT("Later crossing changes restitution again"), Zone->ApplyCrossingRestitution(First, Body));
	TestTrue(TEXT("Repeated crossing reuses the isolated override"), Body->BodyInstance.GetPhysMaterialOverride() == Runtime);
	TestEqual(TEXT("Later value replaces previous value"), Runtime->Restitution, .65f);
	Zone->RestitutionAfterCrossing = 2.f;
	Zone->ApplyCrossingRestitution(First, Body);
	TestEqual(TEXT("Restitution clamps to one"), Runtime->Restitution, 1.f);
	Zone->RestitutionAfterCrossing = -1.f;
	Zone->ApplyCrossingRestitution(First, Body);
	TestEqual(TEXT("Restitution clamps to zero"), Runtime->Restitution, 0.f);
	TestFalse(TEXT("Invalid body is rejected"), Zone->ApplyCrossingRestitution(First, nullptr));
	TestFalse(TEXT("Another actor's body is rejected"), Zone->ApplyCrossingRestitution(First, OtherBody));
	ARelocationManagerActor* Manager = Fixture.World->SpawnActor<ARelocationManagerActor>();
	TestFalse(TEXT("Non-marble actor is rejected"), Zone->ApplyCrossingRestitution(Manager, Body));

	URelocationManagerComponent* Queue = NewObject<URelocationManagerComponent>(Manager);
	Manager->AddInstanceComponent(Queue);
	Queue->RegisterComponent();
	Manager->RespawnLocation = FVector(30000, 0, 10000);
	Queue->MinimumReleaseInterval = 0;
	Zone->RelocationManager = Manager;
	Zone->RestitutionAfterCrossing = .2f;
	struct FEntranceParams
	{
		UPrimitiveComponent* OverlappedComponent;
		AActor* OtherActor;
		UPrimitiveComponent* OtherComp;
		int32 OtherBodyIndex = 0;
		bool bFromSweep = false;
		FHitResult SweepResult;
	} Args{ Zone->GetStaticMeshComponent(), First, Body };
	UFunction* Entrance = Zone->FindFunction(TEXT("HandleEntrance"));
	if (!TestNotNull(TEXT("Original overlap callback exists"), Entrance)) return false;
	// This isolated world has not begun play; bypass AActor's script-event play-state guard.
	Zone->UObject::ProcessEvent(Entrance, &Args);
	TestEqual(TEXT("Overlap still queues the marble for teleport"), Queue->PendingMarbles.Num(), 1);
	TestEqual(TEXT("Restitution was changed before queue paused physics"), Runtime->Restitution, .2f);
	Queue->TickComponent(0, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Original teleport still completes"), Queue->PendingMarbles.Num(), 0);
	TestTrue(TEXT("Teleport preserves isolated material"), Body->BodyInstance.GetPhysMaterialOverride() == Runtime);
	TestEqual(TEXT("Restitution persists after teleport"), Runtime->Restitution, .2f);
	TestEqual(TEXT("Other marble remains unaffected after teleport"), Shared->Restitution, .8f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDetectionLineRestitutionTest,
	"MarbleRace.DetectionLine.Restitution.BlueprintOverlapPreservesScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDetectionLineRestitutionTest::RunTest(const FString& Parameters)
{
	FRelocationTestWorld Fixture;
	UClass* LineClass = LoadClass<AActor>(nullptr, TEXT("/Game/赛道组件/检测线.检测线_C"));
	UClass* MarbleClass = LoadClass<AActor>(nullptr, TEXT("/Game/角色/弹珠.弹珠_C"));
	if (!TestNotNull(TEXT("Detection line class loads"), LineClass) || !TestNotNull(TEXT("Marble class loads"), MarbleClass)) return false;
	AStaticMeshActor* Line = Fixture.World->SpawnActor<AStaticMeshActor>(LineClass, FVector(30000, 0, 10000), FRotator::ZeroRotator);
	FBoolProperty* Enable = FindFProperty<FBoolProperty>(LineClass, TEXT("开启修改恢复力"));
	FNumericProperty* Restitution = FindFProperty<FNumericProperty>(LineClass, TEXT("过线后恢复力"));
	FNumericProperty* Scale = FindFProperty<FNumericProperty>(LineClass, TEXT("目标缩放"));
	if (!TestNotNull(TEXT("Enable setting exists"), Enable) || !TestNotNull(TEXT("Restitution setting exists"), Restitution) || !TestNotNull(TEXT("Original scale setting exists"), Scale)) return false;
	TestFalse(TEXT("Restitution change defaults off"), Enable->GetPropertyValue_InContainer(Line));
	TestEqual(TEXT("Target restitution defaults to zero"), Restitution->GetFloatingPointPropertyValue(Restitution->ContainerPtrToValuePtr<void>(Line)), 0.0);
	UFunction* Overlap = nullptr;
	for (TFieldIterator<UFunction> It(LineClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		if (It->GetName().StartsWith(TEXT("BndEvt__"))) { Overlap = *It; break; }
	}
	if (!TestNotNull(TEXT("Original component overlap callback exists"), Overlap)) return false;
	AActor* First = Fixture.World->SpawnActor<AActor>(MarbleClass, FVector(-10000, 0, 10000), FRotator::ZeroRotator);
	AActor* Second = Fixture.World->SpawnActor<AActor>(MarbleClass, FVector(-20000, 0, 10000), FRotator::ZeroRotator);
	UPrimitiveComponent* Body = First ? Cast<UPrimitiveComponent>(First->GetRootComponent()) : nullptr;
	UPrimitiveComponent* OtherBody = Second ? Cast<UPrimitiveComponent>(Second->GetRootComponent()) : nullptr;
	if (!TestNotNull(TEXT("First body exists"), Body) || !TestNotNull(TEXT("Second body exists"), OtherBody)) return false;
	Body->SetSimulatePhysics(true);
	OtherBody->SetSimulatePhysics(true);
	UPhysicalMaterial* Shared = NewObject<UPhysicalMaterial>(Fixture.World);
	Shared->Restitution = .8f;
	Shared->Friction = .6f;
	Body->SetPhysMaterialOverride(Shared);
	OtherBody->SetPhysMaterialOverride(Shared);
	struct FOverlapParams
	{
		UPrimitiveComponent* OverlappedComponent;
		AActor* OtherActor;
		UPrimitiveComponent* OtherComp;
		int32 OtherBodyIndex = 0;
		bool bFromSweep = false;
		FHitResult SweepResult;
	} Args{ Line->GetStaticMeshComponent(), First, Body };
	const double TargetScale = Scale->GetFloatingPointPropertyValue(Scale->ContainerPtrToValuePtr<void>(Line));
	Line->UObject::ProcessEvent(Overlap, &Args);
	TestTrue(TEXT("Disabled feature retains original scale change"), First->GetActorScale3D().Equals(FVector(TargetScale), 1.e-5));
	TestTrue(TEXT("Disabled feature retains original shared material"), Body->BodyInstance.GetPhysMaterialOverride() == Shared);
	Enable->SetPropertyValue_InContainer(Line, true);
	Restitution->SetFloatingPointPropertyValue(Restitution->ContainerPtrToValuePtr<void>(Line), .45);
	Line->UObject::ProcessEvent(Overlap, &Args);
	UPhysicalMaterial* Runtime = Body->BodyInstance.GetPhysMaterialOverride();
	if (!TestNotNull(TEXT("Enabled feature assigns material"), Runtime)) return false;
	TestTrue(TEXT("Enabled feature changes only this body"), Runtime != Shared && Runtime->GetOuter() == Body);
	TestEqual(TEXT("Blueprint passes configured restitution"), Runtime->Restitution, .45f);
	TestTrue(TEXT("Enabled feature still preserves original resizing"), First->GetActorScale3D().Equals(FVector(TargetScale), 1.e-5));
	TestEqual(TEXT("Shared material is not edited"), Shared->Restitution, .8f);
	TestTrue(TEXT("Second marble is not affected"), OtherBody->BodyInstance.GetPhysMaterialOverride() == Shared);
	Restitution->SetFloatingPointPropertyValue(Restitution->ContainerPtrToValuePtr<void>(Line), .2);
	Line->UObject::ProcessEvent(Overlap, &Args);
	TestTrue(TEXT("Later line crossing reuses per-body material"), Body->BodyInstance.GetPhysMaterialOverride() == Runtime);
	TestEqual(TEXT("Later crossing updates restitution"), Runtime->Restitution, .2f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIndependentExitTest,
	"MarbleRace.Relocation.RoundRobin.IndependentExits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FIndependentExitTest::RunTest(const FString& Parameters)
{
	FRelocationTestWorld Fixture;
	auto* Manager = Fixture.World->SpawnActor<ARoundRobinRelocationManagerActor>();
	Manager->RespawnLocations = {FVector(10000, 0, 10000), FVector(20000, 0, 10000)};
	auto* Queue = NewObject<URelocationManagerComponent>(Manager);
	Manager->AddInstanceComponent(Queue);
	Queue->RegisterComponent();
	Queue->MinimumReleaseInterval = 0;
	UClass* Class = LoadClass<AActor>(nullptr, TEXT("/Game/角色/弹珠.弹珠_C"));
	if (!TestNotNull(TEXT("Marble class"), Class)) return false;
	auto Spawn = [&](FVector Position)
	{
		auto* Marble = Fixture.World->SpawnActor<AActor>(Class, Position, FRotator::ZeroRotator);
		Cast<UPrimitiveComponent>(Marble->GetRootComponent())->SetSimulatePhysics(true);
		return Marble;
	};
	auto* Blocker = Spawn(Manager->RespawnLocations[0]);
	auto* Left = Spawn(FVector(-10000, 0, 10000));
	auto* Right = Spawn(FVector(-20000, 0, 10000));
	Manager->CommitMarbleRespawn(Right);
	Queue->EnqueueMarble(Left, Cast<UPrimitiveComponent>(Left->GetRootComponent()));
	Queue->EnqueueMarble(Right, Cast<UPrimitiveComponent>(Right->GetRootComponent()));
	Queue->TickComponent(0, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Only blocked marble remains"), Queue->PendingMarbles.Num(), 1);
	TestTrue(TEXT("Free right exit releases behind blocked left"), Right->GetActorLocation().Equals(Manager->RespawnLocations[1]));
	FVector Next;
	Manager->TryRollRespawnLocation(Left, Next);
	TestTrue(TEXT("Blocked marble keeps its index"), Next.Equals(Manager->RespawnLocations[0]));
	Manager->TryRollRespawnLocation(Right, Next);
	TestTrue(TEXT("Released marble advances"), Next.Equals(Manager->RespawnLocations[0]));
	Blocker->SetActorLocation(FVector(30000, 0, 10000), false, nullptr, ETeleportType::TeleportPhysics);
	Queue->TickComponent(0, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Left releases after obstacle leaves"), Queue->PendingMarbles.IsEmpty());
	Left->SetActorLocation(FVector(-10000, 0, 10000), false, nullptr, ETeleportType::TeleportPhysics);
	Right->SetActorLocation(FVector(-20000, 0, 10000), false, nullptr, ETeleportType::TeleportPhysics);
	Queue->EnqueueMarble(Left, Cast<UPrimitiveComponent>(Left->GetRootComponent()));
	Queue->EnqueueMarble(Right, Cast<UPrimitiveComponent>(Right->GetRootComponent()));
	Queue->TickComponent(0, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Both free exits release in the same pass without starvation"), Queue->PendingMarbles.IsEmpty());
	TestTrue(TEXT("Previously left marble now uses right"), Left->GetActorLocation().Equals(Manager->RespawnLocations[1]));
	TestTrue(TEXT("Previously right marble now uses left"), Right->GetActorLocation().Equals(Manager->RespawnLocations[0]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FComparisonRelocationTest,
	"MarbleRace.Relocation.Comparison.TrapNumberAndQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FComparisonRelocationTest::RunTest(const FString& Parameters)
{
	FRelocationTestWorld Fixture;
	auto* Manager = Fixture.World->SpawnActor<AComparisonRelocationManagerActor>();
	auto* Queue = NewObject<URelocationManagerComponent>(Manager);
	Manager->AddInstanceComponent(Queue);
	Queue->RegisterComponent();
	Queue->MinimumReleaseInterval = 0;
	UClass* Class = LoadClass<AActor>(nullptr, TEXT("/Game/角色/弹珠.弹珠_C"));
	if (!TestNotNull(TEXT("Marble class"), Class)) return false;
	auto* Marble = Fixture.World->SpawnActor<AActor>(Class, FVector(-30000, 0, 10000), FRotator::ZeroRotator);
	auto* Body = Cast<UPrimitiveComponent>(Marble->GetRootComponent());
	Body->SetSimulatePhysics(true);
	FVector Position;
	Manager->SetMarbleRespawnSource(Marble, 1);
	TestFalse(TEXT("Empty locations wait"), Manager->TryRollRespawnLocation(Marble, Position));
	Manager->RespawnLocations = {FVector(10000, 0, 10000), FVector(20000, 0, 10000)};
	auto* Zone = Fixture.World->SpawnActor<ARelocationSourceZone>();
	Zone->RelocationManager = Manager;
	struct FEntranceParams
	{
		UPrimitiveComponent* OverlappedComponent;
		AActor* OtherActor;
		UPrimitiveComponent* OtherComp;
		int32 OtherBodyIndex = 0;
		bool bFromSweep = false;
		FHitResult SweepResult;
	} Args{Zone->GetStaticMeshComponent(), Marble, Body};
	auto* Entrance = Zone->FindFunction(TEXT("HandleEntrance"));
	for (int32 Number : {1, 1, 2, 3, 0})
	{
		Zone->TrapNumber = Number;
		Zone->UObject::ProcessEvent(Entrance, &Args);
		TestEqual(TEXT("Trap enqueues marble"), Queue->PendingMarbles.Num(), 1);
		// A duplicate notification must not replace the source of an already queued ball.
		Queue->EnqueueMarble(Marble, Body, Number + 1);
		Queue->TickComponent(0, LEVELTICK_All, nullptr);
		TestTrue(TEXT("Queue releases"), Queue->PendingMarbles.IsEmpty());
		TestTrue(TEXT("Trigger number selects position independent of history"),
			Marble->GetActorLocation().Equals(Manager->RespawnLocations[Number % 2]));
	}
	Manager->RespawnLocations.Add(FVector(30000, 0, 10000));
	Manager->SetMarbleRespawnSource(Marble, 3);
	Manager->TryRollRespawnLocation(Marble, Position);
	TestTrue(TEXT("Third trap wraps with three positions"), Position.Equals(Manager->RespawnLocations[0]));
	Manager->SetMarbleRespawnSource(Zone, 2);
	Manager->TryRollRespawnLocation(Zone, Position);
	TestTrue(TEXT("Other actor source is independent"), Position.Equals(Manager->RespawnLocations[2]));
	Manager->TryRollRespawnLocation(Marble, Position);
	TestTrue(TEXT("Other actor does not alter this marble"), Position.Equals(Manager->RespawnLocations[0]));
	return true;
}

#endif
