#include "Race/DetectionLineScaleComponent.h"

#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"


UDetectionLineScaleComponent::UDetectionLineScaleComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	static ConstructorHelpers::FClassFinder<AActor> MarbleFinder(TEXT("/Game/角色/弹珠"));
	if (MarbleFinder.Succeeded())
	{
		MarbleClass = MarbleFinder.Class;
	}
}

void UDetectionLineScaleComponent::ResizeMarble(AActor* OtherActor, const float Scale) const
{
	if (!OtherActor || !MarbleClass || !OtherActor->IsA(MarbleClass) || Scale <= 0.f)
	{
		return;
	}

	OtherActor->SetActorScale3D(FVector(Scale));
}
