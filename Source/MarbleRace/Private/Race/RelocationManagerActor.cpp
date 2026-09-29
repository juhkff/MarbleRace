#include "Race/RelocationManagerActor.h"

#include "Components/SceneComponent.h"
#include "RelocationManagerComponent.h"

ARelocationManagerActor::ARelocationManagerActor()
{
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("重定位根节点"));
	RootComponent = SceneRoot;
}

URelocationManagerComponent* ARelocationManagerActor::GetRelocationComponent() const
{
	return FindComponentByClass<URelocationManagerComponent>();
}
