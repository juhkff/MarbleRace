#include "Race/MarbleRaceLevelGameMode.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Roster/MarbleRaceRosterSubsystem.h"
#include "Roster/RaceRosterTypes.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/SOverlay.h"

namespace
{
	const FName MarbleColorParameter(TEXT("颜色"));
	/** 落后不超过这个距离时，仍算同一名，避免镜头在并排时来回跳。 */
	const float LeadStickiness = 40.f;

	/** 在滚筒洞里排开，避免出生时叠在一起。 */
	FVector MarbleOffsetInDrum(int32 Index, int32 Count, float Radius)
	{
		if (Count <= 1 || Radius <= KINDA_SMALL_NUMBER)
		{
			return FVector::ZeroVector;
		}

		const int32 Columns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count))));
		const int32 Rows = FMath::DivideAndRoundUp(Count, Columns);
		const int32 Column = Index % Columns;
		const int32 Row = Index / Columns;
		const float X = Column - 0.5f * (Columns - 1);
		const float Z = 0.5f * (Rows - 1) - Row;
		const float Furthest = FMath::Sqrt(
			FMath::Square(0.5f * (Columns - 1)) + FMath::Square(0.5f * (Rows - 1)));
		const float Scale = Furthest > KINDA_SMALL_NUMBER ? Radius / Furthest : 0.f;
		return FVector(X * Scale, 0.f, Z * Scale);
	}
}

AMarbleRaceLevelGameMode::AMarbleRaceLevelGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = nullptr;
	HUDClass = nullptr;
	bStartPlayersAsSpectators = true;

	static ConstructorHelpers::FClassFinder<AActor> MarbleFinder(TEXT("/Game/角色/弹珠"));
	if (MarbleFinder.Succeeded())
	{
		MarbleClass = MarbleFinder.Class;
	}

	static ConstructorHelpers::FClassFinder<AActor> DrumFinder(TEXT("/Game/赛道组件/滚筒"));
	if (DrumFinder.Succeeded())
	{
		DrumClass = DrumFinder.Class;
	}

	static ConstructorHelpers::FClassFinder<AActor> FinishFinder(TEXT("/Game/赛道组件/终点"));
	if (FinishFinder.Succeeded())
	{
		FinishClass = FinishFinder.Class;
	}
}

void AMarbleRaceLevelGameMode::BeginPlay()
{
	Super::BeginPlay();

	CacheRaceAnchors();
	SpawnMarblesInDrum();

	SecondsLeft = FMath::Max(1, CountdownSeconds);
	ShowCountdown(SecondsLeft);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			CountdownTimer, this, &AMarbleRaceLevelGameMode::AdvanceCountdown, 1.f, true);
	}
}

void AMarbleRaceLevelGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CountdownTimer);
	}
	HideCountdown();
	Super::EndPlay(EndPlayReason);
}

void AMarbleRaceLevelGameMode::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bFollowLeader || !FollowCamera)
	{
		return;
	}

	MarkFinishedMarbles();
	AActor* Leader = FindLeadingMarble();
	if (!Leader)
	{
		Leader = CurrentLeader.Get();
	}
	if (!Leader)
	{
		return;
	}

	CurrentLeader = Leader;
	FollowLeader(Leader, DeltaSeconds);
}

void AMarbleRaceLevelGameMode::CacheRaceAnchors()
{
	TArray<AActor*> Cameras;
	UGameplayStatics::GetAllActorsOfClass(this, ACameraActor::StaticClass(), Cameras);
	FollowCamera = Cameras.IsEmpty() ? nullptr : Cast<ACameraActor>(Cameras[0]);
	if (FollowCamera)
	{
		CameraLockX = FollowCamera->GetActorLocation().X;
		CameraSideY = FollowCamera->GetActorLocation().Y;
		if (const UCameraComponent* CameraComponent = FollowCamera->GetCameraComponent())
		{
			CameraOrthoWidth = CameraComponent->OrthoWidth;
		}
		if (APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		{
			PlayerController->SetViewTarget(FollowCamera);
		}
	}

	if (!FinishClass)
	{
		return;
	}

	TArray<AActor*> FinishActors;
	UGameplayStatics::GetAllActorsOfClass(this, FinishClass, FinishActors);
	if (FinishActors.IsEmpty() || !FinishActors[0])
	{
		UE_LOG(LogTemp, Warning, TEXT("比赛关卡里没有终点，过线后镜头不会切换"));
		return;
	}

	FinishLineZ = FinishActors[0]->GetActorLocation().Z;
	bHasFinishLine = true;
}

void AMarbleRaceLevelGameMode::SpawnMarblesInDrum()
{
	UWorld* World = GetWorld();
	if (!World || !MarbleClass || !DrumClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("弹珠或滚筒蓝图没加载，无法生成弹珠"));
		return;
	}

	TArray<AActor*> FoundDrums;
	UGameplayStatics::GetAllActorsOfClass(this, DrumClass, FoundDrums);
	Drums.Reset();
	for (AActor* Found : FoundDrums)
	{
		Drums.Add(Found);
	}
	if (Drums.IsEmpty() || !Drums[0])
	{
		UE_LOG(LogTemp, Warning, TEXT("比赛关卡里没有滚筒，无法生成弹珠"));
		return;
	}

	AActor* Drum = Drums[0];
	FVector DrumOrigin = Drum->GetActorLocation();
	FVector DrumExtent = FVector::ZeroVector;
	Drum->GetActorBounds(false, DrumOrigin, DrumExtent);
	const float HoleRadius = FMath::Min(DrumExtent.X, DrumExtent.Z) * 0.65f;

	const UGameInstance* GameInstance = GetGameInstance();
	const UMarbleRaceRosterSubsystem* Roster = GameInstance
		? GameInstance->GetSubsystem<UMarbleRaceRosterSubsystem>()
		: nullptr;
	if (!Roster)
	{
		return;
	}

	const TArray<FRaceCharacterEntry>& Entries = Roster->GetEntries();
	TArray<int32> EnabledIndices;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		if (Entries[Index].bEnabled)
		{
			EnabledIndices.Add(Index);
		}
	}
	if (EnabledIndices.IsEmpty())
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 Slot = 0; Slot < EnabledIndices.Num(); ++Slot)
	{
		const FRaceCharacterEntry& Entry = Entries[EnabledIndices[Slot]];
		const FVector Location = DrumOrigin + MarbleOffsetInDrum(Slot, EnabledIndices.Num(), HoleRadius);
		AActor* Marble = World->SpawnActor<AActor>(MarbleClass, Location, FRotator::ZeroRotator, SpawnParams);
		if (!Marble)
		{
			continue;
		}

		Marbles.Add(Marble);
		UStaticMeshComponent* Mesh = Marble->FindComponentByClass<UStaticMeshComponent>();
		if (!Mesh)
		{
			continue;
		}

		if (UMaterialInstanceDynamic* Material = Mesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			Material->SetVectorParameterValue(MarbleColorParameter, Entry.Color);
		}
	}
}

void AMarbleRaceLevelGameMode::ShowCountdown(const int32 SecondsRemaining)
{
	if (!GEngine || !GEngine->GameViewport)
	{
		return;
	}

	const FText Label = FText::AsNumber(SecondsRemaining);
	if (CountdownLabel.IsValid())
	{
		CountdownLabel->SetText(Label);
		return;
	}

	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 120);
	const TSharedRef<SOverlay> Overlay = SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SAssignNew(CountdownLabel, STextBlock)
			.Text(Label)
			.Font(Font)
			.ColorAndOpacity(FLinearColor::White)
			.ShadowOffset(FVector2D(3.f, 3.f))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f))
		];
	CountdownWidget = Overlay;
	GEngine->GameViewport->AddViewportWidgetContent(Overlay, 1000);
}

void AMarbleRaceLevelGameMode::HideCountdown()
{
	if (GEngine && GEngine->GameViewport && CountdownWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(CountdownWidget.ToSharedRef());
	}
	CountdownWidget.Reset();
	CountdownLabel.Reset();
}

void AMarbleRaceLevelGameMode::AdvanceCountdown()
{
	--SecondsLeft;
	if (SecondsLeft > 0)
	{
		ShowCountdown(SecondsLeft);
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CountdownTimer);
	}
	HideCountdown();
	RemoveDrums();
}

void AMarbleRaceLevelGameMode::RemoveDrums()
{
	for (AActor* Drum : Drums)
	{
		if (IsValid(Drum))
		{
			Drum->Destroy();
		}
	}
	Drums.Reset();
	bFollowLeader = true;
}

void AMarbleRaceLevelGameMode::MarkFinishedMarbles()
{
	if (!bHasFinishLine)
	{
		return;
	}

	for (AActor* Marble : Marbles)
	{
		if (!IsValid(Marble) || HasFinished(Marble))
		{
			continue;
		}

		if (Marble->GetActorLocation().Z <= FinishLineZ)
		{
			FinishedMarbles.Add(Marble);
		}
	}
}

bool AMarbleRaceLevelGameMode::HasFinished(const AActor* Marble) const
{
	return FinishedMarbles.Contains(Marble);
}

AActor* AMarbleRaceLevelGameMode::FindLeadingMarble() const
{
	AActor* Best = nullptr;
	float BestZ = 0.f;
	for (AActor* Marble : Marbles)
	{
		if (!IsValid(Marble) || HasFinished(Marble))
		{
			continue;
		}

		const float MarbleZ = Marble->GetActorLocation().Z;
		if (!Best || MarbleZ < BestZ)
		{
			Best = Marble;
			BestZ = MarbleZ;
		}
	}

	AActor* StickyLeader = CurrentLeader.Get();
	if (Best && IsValid(StickyLeader) && !HasFinished(StickyLeader))
	{
		const float StickyZ = StickyLeader->GetActorLocation().Z;
		if (StickyZ <= BestZ + LeadStickiness)
		{
			return StickyLeader;
		}
	}

	return Best;
}

void AMarbleRaceLevelGameMode::FollowLeader(const AActor* Leader, const float DeltaSeconds)
{
	if (!FollowCamera || !Leader)
	{
		return;
	}

	const float TargetZ = Leader->GetActorLocation().Z;
	const float CurrentZ = FollowCamera->GetActorLocation().Z;
	float SmoothedZ = FMath::FInterpTo(CurrentZ, TargetZ, DeltaSeconds, CameraFollowSpeed);

	const float MaxLag = FMath::Max(200.f, CameraOrthoWidth * 0.22f);
	const float LagZ = SmoothedZ - TargetZ;
	if (FMath::Abs(LagZ) > MaxLag)
	{
		SmoothedZ = TargetZ + FMath::Sign(LagZ) * MaxLag;
	}

	FollowCamera->SetActorLocation(FVector(CameraLockX, CameraSideY, SmoothedZ));
}
