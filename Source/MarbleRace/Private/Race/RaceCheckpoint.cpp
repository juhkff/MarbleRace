#include "Race/RaceCheckpoint.h"
#include "Race/RaceFrameLogic.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"

namespace
{
	constexpr float GateVisualThickness = 8.0f;
}

ARaceCheckpoint::ARaceCheckpoint()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	GateVisual = CreateDefaultSubobject<UBoxComponent>(TEXT("GateVisual"));
	GateVisual->SetupAttachment(SceneRoot);
	GateVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GateVisual->SetGenerateOverlapEvents(false);
	GateVisual->SetHiddenInGame(true);
	GateVisual->SetLineThickness(2.0f);

	RouteSpline = CreateDefaultSubobject<USplineComponent>(TEXT("RouteSpline"));
	RouteSpline->SetupAttachment(SceneRoot);
	RouteSpline->SetHiddenInGame(true);

	SyncGateVisual();
}

FRaceGate ARaceCheckpoint::MakeGate() const
{
	FRaceGate Gate;
	Gate.Transform = GetActorTransform();
	Gate.HalfExtent = HalfExtent;
	return Gate;
}

bool ARaceCheckpoint::HasUsableSpline() const
{
	return HasAuthoredRaceSpline(RouteSpline);
}

float ARaceCheckpoint::EvaluateSplineProgress(const FVector& WorldLocation) const
{
	if (!HasUsableSpline())
	{
		return -1.0f;
	}

	const float Key = RouteSpline->FindInputKeyClosestToWorldLocation(WorldLocation);
	const float Distance = RouteSpline->GetDistanceAlongSplineAtSplineInputKey(Key);
	return FMath::Clamp(Distance / RouteSpline->GetSplineLength(), 0.0f, 1.0f);
}

void ARaceCheckpoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SyncGateVisual();
}

#if WITH_EDITOR
void ARaceCheckpoint::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SyncGateVisual();
}
#endif

void ARaceCheckpoint::SyncGateVisual()
{
	if (GateVisual)
	{
		GateVisual->SetBoxExtent(FVector(FMath::Max(1.0f, HalfExtent.X), FMath::Max(1.0f, HalfExtent.Y), GateVisualThickness));
	}
}
