// Fill out your copyright notice in the Description page of Project Settings.


#include "MarbleRaceGameMode.h"

#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

void AMarbleRaceGameMode::BeginPlay()
{
	Super::BeginPlay();

	TArray<AActor*> Cameras;
	UGameplayStatics::GetAllActorsOfClass(this, ACameraActor::StaticClass(), Cameras);

	for (AActor* Camera : Cameras)
	{
		if (Camera && Camera->ActorHasTag(FName(TEXT("MainCamera"))))
		{
			if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
			{
				PlayerController->SetViewTarget(Camera);
				return;
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("No CameraActor with the tag 'MainCamera' was found in this level."));
}
