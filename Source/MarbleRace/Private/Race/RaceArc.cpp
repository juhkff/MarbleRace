#include "Race/RaceArc.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"

namespace
{
	const TCHAR* GCubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* GTrackMaterialPath = TEXT("/Game/Materials/M_2DTrack.M_2DTrack");
	constexpr float GCubeEdge = 100.0f;
	constexpr float GMarbleClearance = 80.0f;

	bool IsNearlyZeroSpin(float DegreesPerSecond)
	{
		return FMath::Abs(DegreesPerSecond) < 0.01f;
	}
}

ARaceArc::ARaceArc()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

bool ARaceArc::ShouldSpin() const
{
	return !IsNearlyZeroSpin(SpinDegreesPerSecond);
}

void ARaceArc::ClampShape()
{
	Radius = FMath::Max(Radius, 90.0f);
	Thickness = FMath::Clamp(Thickness, 8.0f, Radius * 0.45f);
	Depth = FMath::Max(Depth, 40.0f);
	SegmentCount = FMath::Clamp(SegmentCount, 4, 32);

	const float InnerRadius = FMath::Max(Radius - Thickness * 0.5f, 1.0f);
	const float HalfChord = GMarbleClearance * 0.5f;
	const float GapRatio = FMath::Clamp(HalfChord / InnerRadius, 0.0f, 1.0f);
	const float MinGapDegrees = FMath::RadiansToDegrees(2.0f * FMath::Asin(GapRatio));
	SweepDegrees = FMath::Clamp(SweepDegrees, 30.0f, 360.0f - MinGapDegrees);
}

float ARaceArc::SegmentAngleRadians(int32 Index) const
{
	const float SweepRadians = FMath::DegreesToRadians(SweepDegrees);
	const float Step = SweepRadians / static_cast<float>(SegmentCount);
	const float Start = -SweepRadians * 0.5f;
	return Start + (static_cast<float>(Index) + 0.5f) * Step;
}

void ARaceArc::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildSegments();
}

void ARaceArc::BeginPlay()
{
	Super::BeginPlay();
	RebuildSegments();
	ConfigureMotion();
}

void ARaceArc::RebuildSegments()
{
	for (UStaticMeshComponent* Segment : Segments)
	{
		if (Segment)
		{
			Segment->DestroyComponent();
		}
	}
	Segments.Empty();

	ClampShape();

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, GCubeMeshPath);
	UMaterialInterface* Track = LoadObject<UMaterialInterface>(nullptr, GTrackMaterialPath);
	if (!Cube)
	{
		return;
	}

	const float SweepRadians = FMath::DegreesToRadians(SweepDegrees);
	const float Step = SweepRadians / static_cast<float>(SegmentCount);
	const float Chord = 2.0f * Radius * FMath::Sin(Step * 0.5f) * 1.15f;
	const FVector Scale(Chord / GCubeEdge, Depth / GCubeEdge, Thickness / GCubeEdge);
	const bool bSpins = ShouldSpin();

	for (int32 Index = 0; Index < SegmentCount; ++Index)
	{
		const float Angle = SegmentAngleRadians(Index);
		UStaticMeshComponent* Segment = NewObject<UStaticMeshComponent>(this);
		Segment->SetMobility(bSpins ? EComponentMobility::Movable : EComponentMobility::Static);
		Segment->SetupAttachment(SceneRoot);
		Segment->RegisterComponent();
		Segment->SetStaticMesh(Cube);
		Segment->SetRelativeLocation(FVector(FMath::Sin(Angle) * Radius, 0.0f, FMath::Cos(Angle) * Radius));
		Segment->SetRelativeRotation(FRotator(-FMath::RadiansToDegrees(Angle), 0.0f, 0.0f));
		Segment->SetRelativeScale3D(Scale);
		Segment->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Segment->SetCollisionProfileName(bSpins ? TEXT("BlockAllDynamic") : TEXT("BlockAll"));
		Segment->SetNotifyRigidBodyCollision(false);
		Segment->SetSimulatePhysics(false);
		Segment->SetEnableGravity(false);
		Segment->SetGenerateOverlapEvents(false);
		Segment->SetCastShadow(false);
		Segment->SetCanEverAffectNavigation(false);
		if (Track)
		{
			Segment->SetMaterial(0, Track);
		}
		Segments.Add(Segment);
	}
}

void ARaceArc::ConfigureMotion()
{
	SurfaceMaterial = NewObject<UPhysicalMaterial>(this);
	SurfaceMaterial->Friction = 0.05f;
	SurfaceMaterial->StaticFriction = 0.05f;
	SurfaceMaterial->Restitution = 0.0f;
	SurfaceMaterial->FrictionCombineMode = EFrictionCombineMode::Min;
	SurfaceMaterial->RestitutionCombineMode = EFrictionCombineMode::Min;

	const bool bSpins = ShouldSpin();
	for (UStaticMeshComponent* Segment : Segments)
	{
		if (!Segment)
		{
			continue;
		}
		Segment->SetPhysMaterialOverride(SurfaceMaterial);
		if (!bSpins)
		{
			continue;
		}
		Segment->SetSimulatePhysics(true);
		Segment->SetEnableGravity(false);
		Segment->SetMassOverrideInKg(NAME_None, 2500.0f, true);
		Segment->SetLinearDamping(0.0f);
		Segment->SetAngularDamping(0.0f);
		if (FBodyInstance* Instance = Segment->GetBodyInstance())
		{
			// 侧视赛道只绕世界 Y 转。平移留给每帧的圆周速度。
			Instance->bLockXTranslation = false;
			Instance->bLockYTranslation = true;
			Instance->bLockZTranslation = false;
			Instance->bLockXRotation = true;
			Instance->bLockYRotation = false;
			Instance->bLockZRotation = true;
			Instance->SetDOFLock(EDOFMode::SixDOF);
		}
		Segment->WakeRigidBody();
	}
}

void ARaceArc::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	(void)DeltaSeconds;

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || !ShouldSpin() || Segments.IsEmpty())
	{
		return;
	}

	const FQuat ActorRotation = GetActorQuat();
	const FVector Axis = ActorRotation.GetAxisY();
	const float SpinRadians = FMath::DegreesToRadians(SpinDegreesPerSecond * World->GetTimeSeconds());
	const FQuat SpinRotation(FVector::YAxisVector, SpinRadians);
	const float Omega = FMath::DegreesToRadians(SpinDegreesPerSecond);
	const FVector ActorLocation = GetActorLocation();
	const FTransform ActorTransform = GetActorTransform();

	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		UStaticMeshComponent* Segment = Segments[Index];
		if (!Segment || !Segment->IsSimulatingPhysics())
		{
			continue;
		}

		const float Angle = SegmentAngleRadians(Index);
		const FVector LocalCenter(FMath::Sin(Angle) * Radius, 0.0f, FMath::Cos(Angle) * Radius);
		const FVector Desired = ActorTransform.TransformPosition(SpinRotation.RotateVector(LocalCenter));
		const FVector Offset = Desired - ActorLocation;
		const FVector Tangential = FVector::CrossProduct(Axis, Offset) * Omega;
		FVector Velocity = Tangential + (Desired - Segment->GetComponentLocation()) * 8.0f;
		Velocity.Y = (Desired.Y - Segment->GetComponentLocation().Y) * 8.0f;
		Segment->SetPhysicsLinearVelocity(Velocity, false);
		Segment->SetPhysicsAngularVelocityInDegrees(Axis * SpinDegreesPerSecond, false);
	}
}
