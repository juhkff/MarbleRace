#include "Race/PlatformMoveComponent.h"

UPlatformMoveComponent::UPlatformMoveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	USceneComponent::SetMobility(EComponentMobility::Movable);
}

void UPlatformMoveComponent::BeginPlay()
{
	Super::BeginPlay();

	if (USceneComponent* Parent = GetAttachParent())
	{
		Parent->SetMobility(EComponentMobility::Movable);
		Origin = Parent->GetRelativeLocation();
	}
}

void UPlatformMoveComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
                                           FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	USceneComponent* Parent = GetAttachParent();
	if (!Parent || Period <= 0.f)
	{
		return;
	}

	Elapsed += DeltaTime;
	const float HalfPeriod = Period * 0.5f;
	const float CycleTime = FMath::Fmod(Elapsed, Period);
	const float Offset = CycleTime <= HalfPeriod
		? Distance * (CycleTime / HalfPeriod)
		: Distance * (1.f - (CycleTime - HalfPeriod) / HalfPeriod);

	FVector Delta = FVector::ZeroVector;
	switch (MoveAxis)
	{
	case EAxis::Y:
		Delta.Y = Offset;
		break;
	case EAxis::Z:
		Delta.Z = Offset;
		break;
	default:
		Delta.X = Offset;
		break;
	}

	Parent->SetRelativeLocation(Origin + Delta);
}
