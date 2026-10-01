#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Race/OscillatingRotationComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOscillatingRotationTest, "MarbleRace.OscillatingRotation.CosineAndTransforms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOscillatingRotationTest::RunTest(const FString& Parameters)
{
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::SM5, &Values);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto Make = [&]()
	{
		auto* Owner = World->SpawnActor<AActor>();
		auto* Parent = NewObject<USceneComponent>(Owner);
		Owner->AddInstanceComponent(Parent);
		Owner->SetRootComponent(Parent);
		Parent->RegisterComponent();
		Parent->SetRelativeRotation(FRotator(15, 25, 35));
		Parent->SetRelativeLocation(FVector(10, 20, 30));
		Parent->SetRelativeScale3D(FVector(2, 3, 4));
		auto* Rotation = NewObject<UOscillatingRotationComponent>(Owner);
		Owner->AddInstanceComponent(Rotation);
		Rotation->SetupAttachment(Parent);
		Rotation->RegisterComponent();
		Rotation->InitialAngle = -45;
		return Rotation;
	};
	auto* R = Make();
	auto Tick = [&](double Time) { R->TickComponent(float(Time), LEVELTICK_All, nullptr); };
	const FQuat Base = R->GetAttachParent()->GetRelativeRotation().Quaternion();
	Tick(0);
	TestEqual(TEXT("Initial left angle applies immediately"), R->GetCurrentAngle(), -45.0);
	Tick(UE_DOUBLE_PI / 4.0); // Amplitude 45, peak speed 90 => phase speed 2 rad/s.
	TestTrue(TEXT("Quarter cycle reaches midpoint"), FMath::IsNearlyZero(R->GetCurrentAngle(), 1.e-4));
	TestTrue(TEXT("Base orientation retained"), R->GetAttachParent()->GetRelativeRotation().Quaternion().Equals(Base, 1.e-5));
	Tick(UE_DOUBLE_PI / 4.0);
	TestTrue(TEXT("Half cycle reaches right endpoint"), FMath::IsNearlyEqual(R->GetCurrentAngle(), 45.0, 1.e-4));
	Tick(.001);
	TestTrue(TEXT("Endpoint velocity approaches zero"), FMath::Abs(R->GetCurrentAngle() - 45.0) < .001);
	R->RotationSpeed = 0;
	const double Paused = R->GetCurrentAngle();
	Tick(100);
	TestEqual(TEXT("Zero speed pauses"), R->GetCurrentAngle(), Paused);
	TestTrue(TEXT("Position preserved"), R->GetAttachParent()->GetRelativeLocation().Equals(FVector(10, 20, 30)));
	TestTrue(TEXT("Scale preserved"), R->GetAttachParent()->GetRelativeScale3D().Equals(FVector(2, 3, 4)));
	auto* SmallSteps = Make();
	auto* LargeStep = Make();
	for (int32 I = 0; I < 120; ++I) SmallSteps->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr);
	LargeStep->TickComponent(2.f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Frame rate independent phase"), FMath::IsNearlyEqual(SmallSteps->GetCurrentAngle(), LargeStep->GetCurrentAngle(), 1.e-4));
	auto* Clamped = Make();
	Clamped->InitialAngle = 100;
	Clamped->TickComponent(0, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Out of range initial angle clamps"), FMath::IsNearlyEqual(Clamped->GetCurrentAngle(), 45.0, 1.e-4));
	Clamped->LeftLimit = Clamped->RightLimit = 12;
	Clamped->TickComponent(1, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Equal limits hold fixed angle"), Clamped->GetCurrentAngle(), 12.0);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	World->RemoveFromRoot();
	return true;
}
#endif
