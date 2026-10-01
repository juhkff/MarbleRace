#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/ChildActorComponent.h"
#include "Race/PeriodicRotationComponent.h"

namespace
{
	struct FPeriodicRotationFixture
	{
		UWorld* World;
		USceneComponent* Parent;
		UPeriodicRotationComponent* Rotation;
		FQuat Initial;
		FPeriodicRotationFixture()
		{
			const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
				ERHIFeatureLevel::SM5, &Values);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			AActor* Owner = World->SpawnActor<AActor>();
			Parent = NewObject<USceneComponent>(Owner);
			Owner->AddInstanceComponent(Parent);
			Owner->SetRootComponent(Parent);
			Parent->RegisterComponent();
			Parent->SetRelativeLocation(FVector(11, 22, 33));
			Parent->SetRelativeRotation(FRotator(15, 25, 35));
			Parent->SetRelativeScale3D(FVector(2, 3, 4));
			Initial = Parent->GetRelativeRotation().Quaternion();
			Rotation = NewObject<UPeriodicRotationComponent>(Owner);
			Owner->AddInstanceComponent(Rotation);
			Rotation->SetupAttachment(Parent);
			Rotation->RegisterComponent();
			Rotation->RotationAmount = 60;
			Rotation->RotationSpeed = 30;
		}
		void Tick(float Seconds) { Rotation->TickComponent(Seconds, LEVELTICK_All, nullptr); }
		~FPeriodicRotationFixture()
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}
	};

	/** 一个可以往里 SpawnActor 的最简游戏世界，用来验证「把资产摆到关卡里」之后的行为。 */
	struct FScratchWorld
	{
		UWorld* World;
		FScratchWorld()
		{
			const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
				ERHIFeatureLevel::SM5, &Values);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		}
		~FScratchWorld()
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			World->RemoveFromRoot();
		}
	};

	UPeriodicRotationComponent* FindRotation(AActor* Actor)
	{
		TArray<UPeriodicRotationComponent*> Found;
		Actor->GetComponents(Found);
		return Found.Num() > 0 ? Found[0] : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPeriodicResetTest, "MarbleRace.PeriodicRotation.ResetAndOvershoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPeriodicResetTest::RunTest(const FString& Parameters)
{
	FPeriodicRotationFixture F;
	F.Tick(.5f);
	TestEqual(TEXT("Speed is degrees per second"), F.Rotation->GetCurrentCycleAngle(), 15.0);
	TestTrue(TEXT("Initial orientation is retained"), F.Parent->GetRelativeRotation().Quaternion().Equals(F.Initial * FRotator(0, 15, 0).Quaternion(), 1.e-6));
	F.Tick(1.5f);
	TestEqual(TEXT("Non-reciprocal endpoint resets to initial angle"), F.Rotation->GetCurrentCycleAngle(), 0.0);
	TestTrue(TEXT("Reset restores initial orientation"), F.Parent->GetRelativeRotation().Quaternion().Equals(F.Initial, 1.e-6));
	F.Tick(3.25f);
	TestEqual(TEXT("Frame overshoot is retained across cycles"), F.Rotation->GetCurrentCycleAngle(), 37.5);
	TestTrue(TEXT("Location is unchanged"), F.Parent->GetRelativeLocation().Equals(FVector(11, 22, 33)));
	TestTrue(TEXT("Scale is unchanged"), F.Parent->GetRelativeScale3D().Equals(FVector(2, 3, 4)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPeriodicReciprocalTest, "MarbleRace.PeriodicRotation.ReciprocalEndpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPeriodicReciprocalTest::RunTest(const FString& Parameters)
{
	FPeriodicRotationFixture F;
	F.Rotation->bReciprocate = true;
	F.Tick(2.f);
	TestEqual(TEXT("Reciprocal motion reaches full configured angle"), F.Rotation->GetCurrentCycleAngle(), 60.0);
	F.Tick(.5f);
	TestEqual(TEXT("Motion reverses after reaching endpoint"), F.Rotation->GetCurrentCycleAngle(), 45.0);
	F.Tick(1.5f);
	TestEqual(TEXT("Return leg reaches initial angle"), F.Rotation->GetCurrentCycleAngle(), 0.0);
	F.Tick(9.f);
	TestEqual(TEXT("Large frame handles multiple round trips"), F.Rotation->GetCurrentCycleAngle(), 30.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPeriodicEdgeTest, "MarbleRace.PeriodicRotation.DirectionAxesAndPause",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPeriodicEdgeTest::RunTest(const FString& Parameters)
{
	FPeriodicRotationFixture F;
	F.Rotation->RotationAmount = -60;
	F.Rotation->SpinAxis = EAxis::Y;
	F.Tick(.5f);
	TestEqual(TEXT("Negative amount reverses rotation"), F.Rotation->GetCurrentCycleAngle(), -15.0);
	TestTrue(TEXT("Y axis follows RingSpin pitch convention"), F.Parent->GetRelativeRotation().Quaternion().Equals(F.Initial * FRotator(-15, 0, 0).Quaternion(), 1.e-6));
	F.Rotation->RotationSpeed = 0;
	F.Tick(100.f);
	TestEqual(TEXT("Zero speed pauses without resetting"), F.Rotation->GetCurrentCycleAngle(), -15.0);
	F.Rotation->RotationSpeed = -1;
	F.Tick(100.f);
	TestEqual(TEXT("Negative speed safely pauses"), F.Rotation->GetCurrentCycleAngle(), -15.0);
	F.Rotation->RotationAmount = 0;
	F.Tick(.1f);
	TestTrue(TEXT("Zero amount restores starting rotation"), F.Parent->GetRelativeRotation().Quaternion().Equals(F.Initial, 1.e-6));
	F.Rotation->RotationAmount = 720;
	F.Rotation->RotationSpeed = 360;
	F.Rotation->SpinAxis = EAxis::X;
	F.Tick(1.5f);
	TestEqual(TEXT("Amounts larger than one turn do not wrap prematurely"), F.Rotation->GetCurrentCycleAngle(), 540.0);
	TestTrue(TEXT("X axis follows RingSpin roll convention"), F.Parent->GetRelativeRotation().Quaternion().Equals(F.Initial * FRotator(0, 0, 180).Quaternion(), 1.e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPeriodicTriangleTest, "MarbleRace.PeriodicRotation.TriangleBlueprintAttachment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPeriodicTriangleTest::RunTest(const FString& Parameters)
{
	FPeriodicRotationFixture F;
	UClass* TriangleClass = LoadClass<AActor>(nullptr, TEXT("/Game/赛道组件/旋转三角.旋转三角_C"));
	if (!TestNotNull(TEXT("Triangle blueprint loads"), TriangleClass)) return false;
	AActor* Triangle = F.World->SpawnActor<AActor>(TriangleClass, FVector(10000, 0, 0), FRotator::ZeroRotator);
	UPeriodicRotationComponent* Rotation = Triangle->FindComponentByClass<UPeriodicRotationComponent>();
	if (!TestNotNull(TEXT("Triangle includes periodic rotation component"), Rotation)) return false;
	TestTrue(TEXT("Script rotates the triangle root"), Rotation->GetAttachParent() == Triangle->GetRootComponent());
	TestEqual(TEXT("Triangle rotates within race plane around Y"), static_cast<int32>(Rotation->SpinAxis.GetValue()), static_cast<int32>(EAxis::Y));
	const FQuat Initial = Triangle->GetRootComponent()->GetRelativeRotation().Quaternion();
	Rotation->TickComponent(.5f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("Actual triangle rotates at configured speed"), Triangle->GetRootComponent()->GetRelativeRotation().Quaternion().Equals(Initial * FRotator(45, 0, 0).Quaternion(), 1.e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPeriodicPivotTest, "MarbleRace.PeriodicRotation.PivotLocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPeriodicPivotTest::RunTest(const FString& Parameters)
{
	const FVector Start(11, 22, 33);
	// 支点是组件本地坐标里的偏移；fixture 的缩放 (2,3,4) 会把它换算成父空间里的另一个点。
	const FVector LocalPivot(0, 0, -200);

	// Pivot support is opt-in: a configured pivot must not move anything while disabled.
	{
		FPeriodicRotationFixture F;
		F.Rotation->SpinAxis = EAxis::Y;
		F.Rotation->PivotLocation = LocalPivot;
		F.Tick(.5f);
		TestTrue(TEXT("Disabled pivot keeps the rotation-only behavior"),
			F.Parent->GetRelativeLocation().Equals(Start));
	}

	FPeriodicRotationFixture F;
	F.Rotation->SpinAxis = EAxis::Y;
	F.Rotation->bRotateAroundPivot = true;
	F.Rotation->PivotLocation = LocalPivot;
	const FVector PivotPoint = Start + F.Initial.RotateVector(LocalPivot * F.Parent->GetRelativeScale3D());

	F.Tick(.5f);
	const FQuat PivotDelta = (F.Initial * FRotator(15, 0, 0).Quaternion() * F.Initial.Inverse()).GetNormalized();
	const FVector Expected = PivotPoint + PivotDelta.RotateVector(Start - PivotPoint);
	TestTrue(TEXT("Origin orbits the configured pivot"),
		F.Parent->GetRelativeLocation().Equals(Expected, 1.e-3));
	TestTrue(TEXT("Pivot mode keeps the rotation convention"),
		F.Parent->GetRelativeRotation().Quaternion().Equals(F.Initial * FRotator(15, 0, 0).Quaternion(), 1.e-6));

	F.Tick(1.5f);
	TestEqual(TEXT("Cycle end reaches zero angle"), F.Rotation->GetCurrentCycleAngle(), 0.0);
	TestTrue(TEXT("Cycle end returns to the starting location"),
		F.Parent->GetRelativeLocation().Equals(Start, 1.e-3));

	F.Rotation->RotationAmount = 0;
	F.Tick(.1f);
	TestTrue(TEXT("Zero amount restores the starting location"),
		F.Parent->GetRelativeLocation().Equals(Start, 1.e-4));

	// Mirrors 旋转三角: the base midpoint of a cone scaled (8,1,4) must hold still while it spins.
	{
		FPeriodicRotationFixture G;
		G.Rotation->SpinAxis = EAxis::Y;
		G.Rotation->bRotateAroundPivot = true;
		const FVector LocalBaseMid(0, 0, -50);
		const FVector BaseMid = Start + G.Initial.RotateVector(LocalBaseMid * G.Parent->GetRelativeScale3D());
		G.Rotation->PivotLocation = LocalBaseMid;
		auto BaseMidNow = [&G, LocalBaseMid]()
		{
			return G.Parent->GetRelativeLocation() + G.Parent->GetRelativeRotation().Quaternion()
				.RotateVector(LocalBaseMid * G.Parent->GetRelativeScale3D());
		};
		G.Tick(.5f);
		TestTrue(TEXT("Base midpoint holds still at 15 degrees"), BaseMidNow().Equals(BaseMid, 1.e-3));
		G.Tick(1.f);
		TestTrue(TEXT("Base midpoint holds still at 45 degrees"), BaseMidNow().Equals(BaseMid, 1.e-3));
	}
	return true;
}

/**
 * 回归测试：支点必须跟着资产走。
 * 把 旋转三角 摆到离原点很远的地方、再旋转它自己，底边中点在世界里必须钉住不动；
 * 以 ChildActor 嵌在别的蓝图里也同理。
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPeriodicPivotPlacementTest, "MarbleRace.PeriodicRotation.PivotPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPeriodicPivotPlacementTest::RunTest(const FString& Parameters)
{
	UClass* TriangleClass = LoadClass<AActor>(nullptr, TEXT("/Game/赛道组件/旋转三角.旋转三角_C"));
	if (!TestNotNull(TEXT("旋转三角 blueprint loads"), TriangleClass)) return false;

	// Cone 的底边中点：mesh 本地 (0,0,-50)，组件缩放 (8,1,4) 会把它变成 (0,0,-200)。
	const FVector BaseLocal(0, 0, -50);

	FScratchWorld Scratch;

	// 1) 直接摆进世界：离原点足够远，任何「把支点当世界坐标」的写法都会露馅。
	{
		AActor* Placed = Scratch.World->SpawnActor<AActor>(TriangleClass,
			FVector(1000, 200, 500), FRotator(0, 35, 0));
		if (TestNotNull(TEXT("Triangle spawns"), Placed))
		{
			USceneComponent* Root = Placed->GetRootComponent();
			TestNull(TEXT("Root of the triangle is not attached to anything"), Root->GetAttachParent());
			TestEqual(TEXT("Root relative location equals the actor location (unattached root)"),
				Root->GetRelativeLocation(), FVector(1000, 200, 500));

			UPeriodicRotationComponent* Rotation = FindRotation(Placed);
			if (TestNotNull(TEXT("Placed triangle has the rotation component"), Rotation))
			{
				auto BaseWorld = [Root, BaseLocal]()
				{
					return Root->GetComponentTransform().TransformPosition(BaseLocal);
				};
				const FVector Before = BaseWorld();
				Rotation->TickComponent(0.5f, LEVELTICK_All, nullptr);
				const FVector After = BaseWorld();
				AddInfo(FString::Printf(TEXT("placed: base was %s, now %s"), *Before.ToString(), *After.ToString()));
				TestTrue(TEXT("Base midpoint holds still for an actor placed off-origin"),
					After.Equals(Before, 1.e-2));
			}
		}
	}

	// 2) 嵌在宿主蓝图里（ChildActor），宿主自己也有位移和旋转。
	{
		AActor* Host = Scratch.World->SpawnActor<AActor>();
		USceneComponent* HostRoot = NewObject<USceneComponent>(Host, TEXT("HostRoot"));
		Host->AddInstanceComponent(HostRoot);
		Host->SetRootComponent(HostRoot);
		HostRoot->RegisterComponent();
		Host->SetActorLocation(FVector(-700, 150, 250));

		UChildActorComponent* Slot = NewObject<UChildActorComponent>(Host, TEXT("TriangleSlot"));
		Host->AddInstanceComponent(Slot);
		Slot->SetupAttachment(HostRoot);
		Slot->SetChildActorClass(TriangleClass);
		Slot->RegisterComponent();
		Slot->SetRelativeLocation(FVector(500, 0, 300));
		Slot->SetRelativeRotation(FRotator(0, 20, 0));

		AActor* Child = Slot->GetChildActor();
		if (TestNotNull(TEXT("Child actor is spawned"), Child))
		{
			USceneComponent* ChildRoot = Child->GetRootComponent();
			ChildRoot->SetMobility(EComponentMobility::Movable);
			UPeriodicRotationComponent* Rotation = FindRotation(Child);
			if (TestNotNull(TEXT("Child triangle has the rotation component"), Rotation))
			{
				auto BaseWorld = [ChildRoot, BaseLocal]()
				{
					return ChildRoot->GetComponentTransform().TransformPosition(BaseLocal);
				};
				const FVector Before = BaseWorld();
				AddInfo(FString::Printf(TEXT("child: root rel loc=%s, attach parent=%s, base=%s"),
					*ChildRoot->GetRelativeLocation().ToString(),
					ChildRoot->GetAttachParent() ? *ChildRoot->GetAttachParent()->GetName() : TEXT("<none>"),
					*Before.ToString()));
				Rotation->TickComponent(0.5f, LEVELTICK_All, nullptr);
				const FVector After = BaseWorld();
				AddInfo(FString::Printf(TEXT("child: base was %s, now %s"), *Before.ToString(), *After.ToString()));
				TestTrue(TEXT("Base midpoint holds still inside a host blueprint"),
					After.Equals(Before, 1.e-2));
			}
		}
	}

	return true;
}
#endif
