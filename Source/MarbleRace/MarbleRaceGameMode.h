// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MarbleRaceGameMode.generated.h"

/**
 * 
 */
UCLASS()
class MARBLERACE_API AMarbleRaceGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMarbleRaceGameMode();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	TWeakObjectPtr<class ACameraActor> FollowCamera;
	TWeakObjectPtr<AActor> RaceMarble;
};
