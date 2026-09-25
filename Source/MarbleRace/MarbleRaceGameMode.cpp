// Fill out your copyright notice in the Description page of Project Settings.


#include "MarbleRaceGameMode.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

AMarbleRaceGameMode::AMarbleRaceGameMode()
{
	// No player-controlled DefaultPawn: the marbles are physics actors.
	DefaultPawnClass = nullptr;
	PrimaryActorTick.bCanEverTick = true;
}

void AMarbleRaceGameMode::BeginPlay()
{
	Super::BeginPlay();

	TArray<AActor*> Marbles;
	UGameplayStatics::GetAllActorsWithTag(this, FName(TEXT("RaceMarble")), Marbles);
	if (!Marbles.IsEmpty())
	{
		RaceMarble = Marbles[0];
	}

	TArray<AActor*> Cameras;
	UGameplayStatics::GetAllActorsOfClass(this, ACameraActor::StaticClass(), Cameras);
	for (AActor* Actor : Cameras)
	{
		ACameraActor* MainCamera = Cast<ACameraActor>(Actor);
		if (!MainCamera || !MainCamera->ActorHasTag(FName(TEXT("MainCamera"))))
		{
			continue;
		}

		if (UCameraComponent* CameraComponent = MainCamera->GetCameraComponent())
		{
			CameraComponent->SetProjectionMode(ECameraProjectionMode::Orthographic);
			CameraComponent->SetOrthoWidth(1000.0f);
		}

		FollowCamera = MainCamera;
		if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
		{
			PlayerController->SetViewTarget(MainCamera);
		}
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("No CameraActor with the tag 'MainCamera' was found in this level."));
}

void AMarbleRaceGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!FollowCamera.IsValid() || !RaceMarble.IsValid())
	{
		return;
	}

	const FVector MarbleLocation = RaceMarble->GetActorLocation();
	const FVector CurrentLocation = FollowCamera->GetActorLocation();
	// The course is a vertical descent and fully visible across its width, so the
	// camera keeps its authored X and only follows the marble downwards. Keeping the
	// camera below the marble places the marble above centre and shows the track ahead.
	const FVector TargetLocation(CurrentLocation.X, CurrentLocation.Y,
		MarbleLocation.Z - 275.0f);
	FollowCamera->SetActorLocation(FMath::VInterpTo(CurrentLocation, TargetLocation, DeltaSeconds, 4.0f));
}
