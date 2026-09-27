// 比赛与主菜单的游戏模式。


#include "Race/MarbleRaceGameMode.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Race/MarbleRaceDirector.h"
#include "UI/MarbleRaceHUD.h"
#include "Race/RaceStartDrum.h"

AMarbleRaceGameMode::AMarbleRaceGameMode()
{
	// 没有玩家操控的默认棋子：小球是物理演员。
	DefaultPawnClass = nullptr;
	PrimaryActorTick.bCanEverTick = true;
	HUDClass = AMarbleRaceHUD::StaticClass();
	DirectorClass = AMarbleRaceDirector::StaticClass();
}

void AMarbleRaceGameMode::BeginPlay()
{
	Super::BeginPlay();

	ResolveDirector();

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

	UE_LOG(LogTemp, Warning, TEXT("这一关里没有带 MainCamera 标签的摄像机。"));
}

void AMarbleRaceGameMode::ResolveDirector()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (AMarbleRaceDirector* Found = Cast<AMarbleRaceDirector>(
		UGameplayStatics::GetActorOfClass(World, AMarbleRaceDirector::StaticClass())))
	{
		RaceDirector = Found;
		return;
	}

	if (!DirectorClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("没有配置比赛导演类，镜头将跟随第一颗带标签的小球。"));
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMarbleRaceDirector* Spawned = World->SpawnActor<AMarbleRaceDirector>(DirectorClass, FTransform::Identity, SpawnParameters);
	RaceDirector = Spawned;

	if (Spawned)
	{
		UE_LOG(LogTemp, Log, TEXT("关卡中没有比赛导演，已在运行时创建 %s"), *Spawned->GetName());
	}
}

void AMarbleRaceGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!FollowCamera.IsValid())
	{
		return;
	}

	// 开局滚筒还在转时，镜头框住滚筒本身：小球散在
	// 整圈环上，只跟一颗会把其余的切出画面。
	if (RaceDirector.IsValid() && RaceDirector->GetRaceState() == ERaceState::Countdown)
	{
		if (const ARaceStartDrum* Drum = RaceDirector->GetStartDrum())
		{
			const FVector DrumLocation = Drum->GetActorLocation();
			const FVector CurrentLocation = FollowCamera->GetActorLocation();
			const FVector DrumView(CurrentLocation.X, CurrentLocation.Y, DrumLocation.Z);
			FollowCamera->SetActorLocation(FMath::VInterpTo(CurrentLocation, DrumView, DeltaSeconds, CameraFollowInterpSpeed));
			return;
		}
	}

	// 镜头看比赛导演的目标（未完赛的最前面那颗），
	// 没有导演时退回原来那颗单独的球。
	AActor* Target = RaceDirector.IsValid() ? RaceDirector->GetCameraTargetActor() : nullptr;
	if (!Target && RaceMarble.IsValid())
	{
		Target = RaceMarble.Get();
	}
	if (!Target)
	{
		return;
	}

	const FVector TargetLocation = Target->GetActorLocation();
	const FVector CurrentLocation = FollowCamera->GetActorLocation();
	// 赛道是纵向下落，宽度上能看全，所以
	// 镜头保持摆好的横向位置，只跟着小球往下。让
	// 镜头低于小球，球就在画面中线以上，前方赛道也能看见。
	// 固定的世界空间提前量，在偏矮的横屏里会把目标推出画面。
	// 任何宽高比下，前瞻都留在画面中间那一半里。
	float SafeFollowOffset = CameraFollowOffsetZ;
	if (const UCameraComponent* CameraComponent = FollowCamera->GetCameraComponent())
	{
		float ViewAspect = CameraComponent->AspectRatio;
		if (!CameraComponent->bConstrainAspectRatio)
		{
			if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0))
			{
				int32 ViewWidth = 0;
				int32 ViewHeight = 0;
				PlayerController->GetViewportSize(ViewWidth, ViewHeight);
				if (ViewWidth > 0 && ViewHeight > 0)
				{
					ViewAspect = static_cast<float>(ViewWidth) / ViewHeight;
				}
			}
		}
		const float OffsetLimit = CameraComponent->OrthoWidth / FMath::Max(0.1f, ViewAspect) * 0.25f;
		SafeFollowOffset = FMath::Clamp(SafeFollowOffset, -OffsetLimit, OffsetLimit);
	}
	const FVector DesiredLocation(CurrentLocation.X, CurrentLocation.Y,
		TargetLocation.Z - SafeFollowOffset);
	FollowCamera->SetActorLocation(FMath::VInterpTo(CurrentLocation, DesiredLocation, DeltaSeconds, CameraFollowInterpSpeed));
}
