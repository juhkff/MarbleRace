#include "Race/MarbleMovementLibrary.h"

#include "Components/ChildActorComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

FVector UMarbleMovementLibrary::ResolveMovementEndpoint(AActor* MovingActor, const FVector Endpoint)
{
	if (!IsValid(MovingActor))
	{
		return Endpoint;
	}

	// Match relocation's coordinate frame: the owning ChildActorComponent placement
	// is NOT the level origin. Endpoint settings use its attachment parent's axes.
	const UChildActorComponent* OwningChildComponent = MovingActor->GetParentComponent();
	const USceneComponent* ReferenceComponent = OwningChildComponent
		? OwningChildComponent->GetAttachParent()
		: (MovingActor->GetRootComponent() ? MovingActor->GetRootComponent()->GetAttachParent() : nullptr);
	return IsValid(ReferenceComponent)
		? ReferenceComponent->GetComponentTransform().TransformPosition(Endpoint)
		: MovingActor->GetActorLocation() + Endpoint;
}
