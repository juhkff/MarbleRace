#include "Race/DetectionLineScaleComponent.h"

#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"
#include "Components/PrimitiveComponent.h"
#include "MarbleRestitution.h"


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

bool UDetectionLineScaleComponent::SetMarbleRestitution(AActor* OtherActor, const float Restitution) const
{
	if (!IsValid(OtherActor) || !MarbleClass || !OtherActor->IsA(MarbleClass))
	{
		return false;
	}
	UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(OtherActor->GetRootComponent());
	if (!MarbleRace::SetBodyRestitution(Body, Restitution))
	{
		return false;
	}
	UE_LOG(LogTemp, Log, TEXT("检测线 %s：弹珠 %s 恢复力设置为 %.3f"),
		*GetNameSafe(GetOwner()), *OtherActor->GetName(), FMath::Clamp(Restitution, 0.f, 1.f));
	return true;
}
