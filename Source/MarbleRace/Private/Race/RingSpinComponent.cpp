// Fill out your copyright notice in the Description page of Project Settings.


#include "Race/RingSpinComponent.h"

// Sets default values for this component's properties
URingSpinComponent::URingSpinComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	USceneComponent::SetMobility(EComponentMobility::Movable);
}


// Called when the game starts
void URingSpinComponent::BeginPlay()
{
	Super::BeginPlay();

	if (USceneComponent* Parent = GetAttachParent())
	{
		Parent->SetMobility(EComponentMobility::Movable);
	}
}


// Called every frame
void URingSpinComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
                                       FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	USceneComponent* Parent = GetAttachParent();
	if (!Parent)
	{
		return;
	}
	const float Angle = DegreesPerSecond * DeltaTime;
	const FRotator Delta(
		SpinAxis == EAxis::Y ? Angle : 0.f,
		SpinAxis == EAxis::Z ? Angle : 0.f,
		SpinAxis == EAxis::X ? Angle : 0.f);
	Parent->AddLocalRotation(Delta);
}
