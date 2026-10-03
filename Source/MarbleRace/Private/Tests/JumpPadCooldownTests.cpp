#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Race/JumpPadImpulseComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJumpPadCooldownTest, "MarbleRace.JumpPad.PerMarbleCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJumpPadCooldownTest::RunTest(const FString& Parameters)
{
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	AActor* Pad = World->SpawnActor<AActor>();
	auto* Launch = NewObject<UJumpPadImpulseComponent>(Pad);
	Pad->AddInstanceComponent(Launch);
	Launch->LaunchCooldownSeconds = 2.f;
	Launch->RandomAngleDegrees = Launch->RandomStrengthRange = 0.f;
	Launch->RegisterComponent();
	UClass* MarbleClass = LoadClass<AActor>(nullptr, TEXT("/Game/角色/弹珠.弹珠_C"));
	AActor* A = World->SpawnActor<AActor>(MarbleClass, FVector(0., 0., 1000.), FRotator::ZeroRotator);
	AActor* B = World->SpawnActor<AActor>(MarbleClass, FVector(500., 0., 1000.), FRotator::ZeroRotator);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();
	const auto Hit = [&](AActor* Marble, double Now)
	{
		World->TimeSeconds = Now;
		auto* Body = Cast<UPrimitiveComponent>(Marble->GetRootComponent());
		Body->SetSimulatePhysics(true);
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Launch->HandlePadHit(nullptr, Marble, Body, FVector::ZeroVector, FHitResult());
		return Body->GetPhysicsLinearVelocity().Z;
	};
	TestEqual(TEXT("First hit launches immediately"), Hit(A, 0.0), 1000.0);
	TestEqual(TEXT("Repeated contact is ignored"), Hit(A, 0.1), 0.0);
	TestEqual(TEXT("Another marble has its own cooldown"), Hit(B, 0.1), 1000.0);
	TestEqual(TEXT("Blocked contact does not extend the cooldown"), Hit(A, 1.99), 0.0);
	TestEqual(TEXT("The same marble can launch at the exact two-second boundary"), Hit(A, 2.0), 1000.0);
	TestEqual(TEXT("Other marble is still cooling down"), Hit(B, 2.0), 0.0);
	Launch->LaunchCooldownSeconds = 4.f;
	TestEqual(TEXT("An edited cooldown takes effect"), Hit(A, 5.99), 0.0);
	TestEqual(TEXT("Edited cooldown expires"), Hit(A, 6.0), 1000.0);
	Launch->LaunchCooldownSeconds = 0.f;
	TestEqual(TEXT("Zero disables the cooldown"), Hit(A, 6.0), 1000.0);
	Launch->EndPlay(EEndPlayReason::Destroyed);
	TestEqual(TEXT("Cooldown records are cleared when play ends"), Launch->LastLaunchTimes.Num(), 0);
	World->EndPlay(EEndPlayReason::Destroyed);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	World->RemoveFromRoot();
	return true;
}
#endif
