// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "RingSpinComponent.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent, DisplayName="圆环旋转"))
class MARBLERACE_API URingSpinComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	URingSpinComponent();

	UPROPERTY(EditAnywhere, Category = "圆环", meta = ( DisplayName = "转速" ))
	float DegreesPerSecond = 90.0f;
	UPROPERTY(EditAnywhere, Category="圆环", meta=(DisplayName="转轴"))
	TEnumAsByte<EAxis::Type> SpinAxis = EAxis::Z;

	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;
};
