#include "Race/MarbleRaceLevelGameMode.h"
#include "MarbleCameraFollow.h"
#include "Race/MarbleRacePresentation.h"
#include "Race/MarbleDrumSpawn.h"
#include "Race/MarbleRaceRecorderSubsystem.h"
#include "Race/RelocationManagerActor.h"
#include "RelocationManagerComponent.h"
#include "Settings/MarbleRaceSettingsSubsystem.h"
#include "GameFramework/WorldSettings.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Roster/MarbleRaceRosterSubsystem.h"
#include "Roster/RaceRosterTypes.h"
#include "UI/MarbleRaceHUD.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName MarbleColorParameter(TEXT("颜色"));
	/** 落后不超过这个距离时，仍算同一名，避免镜头在并排时来回跳。 */
	const float LeadStickiness = 40.f;
	class FMarbleRaceEscapeProcessor : public IInputProcessor
	{
	public:
		explicit FMarbleRaceEscapeProcessor(AMarbleRaceLevelGameMode* InMode) : Mode(InMode) {}
		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
		{
			if (Event.GetKey() != EKeys::Escape || !Mode.IsValid()) return false;
			const auto* Client = Mode->GetGameInstance() ? Mode->GetGameInstance()->GetGameViewportClient() : nullptr;
			const auto Viewport = Client ? Client->GetGameViewportWidget() : nullptr;
			if (!Viewport || (!Viewport->HasAnyUserFocus() && !Viewport->HasFocusedDescendants())) return false;
			// Consume ESC before the editor's Stop PIE shortcut, only while the
			// game viewport has focus. Packaged games use the same return action.
			Mode->AbortRaceAndReturnToMainMenu();
			return true;
		}
	private:
		TWeakObjectPtr<AMarbleRaceLevelGameMode> Mode;
	};

}

AMarbleRaceLevelGameMode::AMarbleRaceLevelGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = nullptr;
	HUDClass = AMarbleRaceHUD::StaticClass();
	bStartPlayersAsSpectators = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("RaceAudioRoot")));
	ThemePlayerA = CreateDefaultSubobject<UAudioComponent>(TEXT("ThemePlayerA"));
	ThemePlayerB = CreateDefaultSubobject<UAudioComponent>(TEXT("ThemePlayerB"));
	for (UAudioComponent* Player : {ThemePlayerA.Get(), ThemePlayerB.Get()})
	{
		Player->SetupAttachment(GetRootComponent());
		Player->bAutoActivate = false;
		Player->bIsUISound = true;
		Player->bAllowSpatialization = false;
		Player->SetPitchMultiplier(1.f);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Game/GGST/Materials/M_GGSTMarble"));
	CharacterMaterial = MaterialFinder.Object;

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
	if (FSlateApplication::IsInitialized())
	{
		EscapeProcessor = MakeShared<FMarbleRaceEscapeProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(EscapeProcessor, 0);
	}
	// Previously saved Blueprint defaults can retain a null HUD/material override.
	CharacterMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/GGST/Materials/M_GGSTMarble.M_GGSTMarble"));
	if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
	{
		Controller->ClientSetHUD(AMarbleRaceHUD::StaticClass());
		AddTickPrerequisiteActor(Controller);
	}

	CacheRaceAnchors();
	SpawnMarblesInDrum();
	if (const auto* Settings = GetGameInstance()->GetSubsystem<UMarbleRaceSettingsSubsystem>())
	{
		bFinishSlowMotionEnabled = Settings->IsFinishSlowMotionEnabled();
		bCountdownZoomEnabled = Settings->IsCountdownZoomEnabled();
		bFinishBGM = Settings->ShouldFinishBGM();
		bBackgroundMusic = Settings->IsBackgroundMusicEnabled();
		if (Settings->ShouldRecordRace()) GetGameInstance()->GetSubsystem<UMarbleRaceRecorderSubsystem>()->StartRecording();
	}
	StartCountdownZoom(GetWorld()->GetRealTimeSeconds());

	SecondsLeft = FMath::Max(1, CountdownSeconds);
	ShowCountdown(SecondsLeft);
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			CountdownTimer, this, &AMarbleRaceLevelGameMode::AdvanceCountdown, 1.f, true);
	}
}

void AMarbleRaceLevelGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (EscapeProcessor && FSlateApplication::IsInitialized())
		FSlateApplication::Get().UnregisterInputPreProcessor(EscapeProcessor);
	EscapeProcessor.Reset();
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CountdownTimer);
	}
	HideCountdown();
	RestoreWorldSpeed();
	RestoreCameraZoom();
	ThemePlayerA->Stop();
	ThemePlayerB->Stop();
	if (GetGameInstance())
	{
		auto* Recorder = GetGameInstance()->GetSubsystem<UMarbleRaceRecorderSubsystem>();
		if (bRaceAborted) Recorder->DiscardRecording(); else Recorder->StopRecording();
	}
	Super::EndPlay(EndPlayReason);
}

void AMarbleRaceLevelGameMode::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bReturningToMainMenu) return;
	const double Now = GetWorld()->GetRealTimeSeconds();
	UpdateCountdownZoom(Now);
	if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
	{
		if (Controller->WasInputKeyJustPressed(EKeys::Escape))
		{
			AbortRaceAndReturnToMainMenu();
			return;
		}
		if (Controller->WasInputKeyJustPressed(EKeys::L)) LockMusicSwitching();
		if (Controller->WasInputKeyJustPressed(EKeys::U)) UnlockMusicSwitching();
	}
	if (bFollowLeader)
	{
		MarkFinishedMarbles(Now);
		if (UpdateRaceCompletion(Now))
		{
			if (MarbleRace::ResultsDisplayFinished(Now - ResultsStartedAt, FinishResults.Num())) ReturnToMainMenu();
			return;
		}
	}

	if (!bFollowLeader || !FollowCamera)
	{
		RestoreWorldSpeed();
		return;
	}

	AActor* Leader = FindLeadingMarble();
	UpdateFinishSlowMotion(Leader, Now);
	if (!Leader)
	{
		// No valid race position remains: hold the camera in place.
		CurrentLeader.Reset();
		UpdateLeaderMusic(nullptr);
		return;
	}

	CurrentLeader = Leader;
	UpdateLeaderMusic(Leader);
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
			CameraFieldOfView = CameraComponent->FieldOfView;
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
	// 赛道以滚筒所在的 X/Y 为中心线，用来判断弹珠有没有横向掉出赛道。
	CourseCentre = FVector2D(DrumOrigin.X, DrumOrigin.Y);

	const UGameInstance* GameInstance = GetGameInstance();
	UMarbleRaceRosterSubsystem* Roster = GameInstance
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
	FRandomStream Random(GetTypeHash(FGuid::NewGuid()));
	for (int32 Index = EnabledIndices.Num() - 1; Index > 0; --Index)
		EnabledIndices.Swap(Index, Random.RandRange(0, Index));
	const UStaticMeshComponent* DefaultMesh = MarbleClass->GetDefaultObject<AActor>()->FindComponentByClass<UStaticMeshComponent>();
	const float MarbleRadius = DefaultMesh && DefaultMesh->GetStaticMesh()
		? DefaultMesh->GetStaticMesh()->GetBounds().BoxExtent.GetMax() * FMath::Max(.05f, MarbleSpawnScale) : 90.f;
	const TArray<FVector> Offsets = MarbleRace::SampleDrumOffsets(EnabledIndices.Num(),
		FMath::Max(0.f, HoleRadius - MarbleRadius), 2.f * MarbleRadius + 10.f, Random);

	for (int32 Slot = 0; Slot < EnabledIndices.Num(); ++Slot)
	{
		const FRaceCharacterEntry& Entry = Entries[EnabledIndices[Slot]];
		FVector Location = DrumOrigin + Offsets[Slot];
		// 包围盒中心落在滚筒厚度中间（Y=25）。侧视平面是 Y=0。
		Location.Y = 0.f;
		// 原始圆盘面朝 Z，侧视赛道位于 X/Z 平面；绕 X 轴旋转 90° 让图案朝向镜头。
		const FTransform SpawnTransform(FRotator(0.f, 0.f, 90.f), Location,
		                                FVector(FMath::Max(0.05f, MarbleSpawnScale)));
		AActor* Marble = World->SpawnActor<AActor>(MarbleClass, SpawnTransform, SpawnParams);
		if (!Marble)
		{
			continue;
		}

		Marbles.Add(Marble);
		MarbleRosterIndices.Add(Marble, EnabledIndices[Slot]);
		UStaticMeshComponent* Mesh = Marble->FindComponentByClass<UStaticMeshComponent>();
		if (!Mesh)
		{
			continue;
		}

		if (CharacterMaterial)
		{
			Mesh->SetMaterial(0, CharacterMaterial);
		}
		if (UMaterialInstanceDynamic* Material = Mesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			FLinearColor OpaqueColor = Entry.Color;
			OpaqueColor.A = 1.f;
			Material->SetVectorParameterValue(MarbleColorParameter, OpaqueColor);
			if (UTexture2D* Portrait = Roster->GetPortraitTexture(EnabledIndices[Slot]))
			{
				Material->SetTextureParameterValue(TEXT("Portrait"), Portrait);
				Material->SetScalarParameterValue(TEXT("HasPortrait"), 1.f);
			}
		}
	}
}

void AMarbleRaceLevelGameMode::StartCountdownZoom(const double Now)
{
	if (!bCountdownZoomEnabled || !FollowCamera) return;
	CountdownZoomStartedAt = Now;
	CountdownZoomDuration = FMath::Min(MarbleRace::CountdownZoomSeconds, FMath::Max(1, CountdownSeconds) * 0.75);
	bCountdownZoomActive = true;
	UpdateCountdownZoom(Now);
}

void AMarbleRaceLevelGameMode::UpdateCountdownZoom(const double Now)
{
	if (!bCountdownZoomActive || !FollowCamera) return;
	const double Elapsed = Now - CountdownZoomStartedAt;
	const float Scale = MarbleRace::CountdownZoomScale(Elapsed, CountdownZoomDuration);
	UCameraComponent* Camera = FollowCamera->GetCameraComponent();
	if (Camera->ProjectionMode == ECameraProjectionMode::Orthographic) Camera->SetOrthoWidth(CameraOrthoWidth * Scale);
	else Camera->SetFieldOfView(CameraFieldOfView * Scale);
	if (Elapsed >= CountdownZoomDuration) RestoreCameraZoom();
}

void AMarbleRaceLevelGameMode::RestoreCameraZoom()
{
	if (!bCountdownZoomActive) return;
	if (FollowCamera)
	{
		FollowCamera->GetCameraComponent()->SetOrthoWidth(CameraOrthoWidth);
		FollowCamera->GetCameraComponent()->SetFieldOfView(CameraFieldOfView);
	}
	bCountdownZoomActive = false;
}

void AMarbleRaceLevelGameMode::UpdateFinishSlowMotion(AActor* Leader, const double Now)
{
	if (!bFinishSlowMotionEnabled || !bHasFinishLine || !IsLeaderCandidate(Leader) || Leader->IsHidden())
	{
		RestoreWorldSpeed();
		SlowMotionLeader.Reset();
		return;
	}
	const double Distance = Leader->GetActorLocation().Z - FinishLineZ;
	const double Speed = -Leader->GetVelocity().Z;
	if (Distance <= 0.0 || Distance > MarbleRace::FinishApproachDistance || Speed <= 1.0)
	{
		RestoreWorldSpeed();
		SlowMotionLeader.Reset();
		return;
	}
	if (SlowMotionLeader.Get() != Leader)
	{
		RestoreWorldSpeed();
		SlowMotionLeader.Reset();
		if (SlowMotionTriggeredMarbles.Contains(Leader)) return;
		// Avoid slowing a distant, very slow marble for a long stretch of track.
		if (Distance / Speed > 0.65) return;
		SlowMotionTriggeredMarbles.Add(Leader);
		SlowMotionLeader = Leader;
		SlowMotionStartedAt = Now;
		OriginalTimeDilation = GetWorld()->GetWorldSettings()->TimeDilation;
	}
	const double Remaining = MarbleRace::FinishApproachSeconds - (Now - SlowMotionStartedAt);
	if (Remaining <= 0.0)
	{
		// Keep the last slow step until crossing, rather than accelerating just
		// before the line if collisions delayed the estimated arrival. Release
		// after a short grace period if the marble has become stuck.
		if (Now - SlowMotionStartedAt >= MarbleRace::FinishApproachSeconds + 1.0) RestoreWorldSpeed();
		return;
	}
	const float Scale = MarbleRace::FinishTimeScale(Distance, Speed, Remaining);
	UGameplayStatics::SetGlobalTimeDilation(this, FMath::Min(OriginalTimeDilation, Scale));
	bFinishTimeDilationApplied = true;
}

void AMarbleRaceLevelGameMode::RestoreWorldSpeed()
{
	if (!bFinishTimeDilationApplied) return;
	UGameplayStatics::SetGlobalTimeDilation(this, OriginalTimeDilation);
	bFinishTimeDilationApplied = false;
}

void AMarbleRaceLevelGameMode::LockMusicSwitching()
{
	bMusicSwitchingLocked = true;
}

void AMarbleRaceLevelGameMode::UnlockMusicSwitching()
{
	bMusicSwitchingLocked = false;
	// Check now, even if the camera has not updated yet. An unchanged leader
	// keeps playing uninterrupted through UpdateLeaderMusic's existing guard.
	if (bFollowLeader)
	{
		MarkFinishedMarbles();
		if (UpdateRaceCompletion(GetWorld()->GetRealTimeSeconds())) return;
		UpdateLeaderMusic(FindLeadingMarble());
	}
}

bool AMarbleRaceLevelGameMode::GetQueuedPosition(const AActor* Marble, FVector& OutPosition) const
{
	if (!IsValid(Marble) || !Marble->IsHidden()) return false;
	for (TActorIterator<ARelocationManagerActor> It(GetWorld()); It; ++It)
		if (const URelocationManagerComponent* Queue = It->GetRelocationComponent();
			Queue && Queue->GetQueuedMarblePosition(Marble, OutPosition)) return true;
	return false;
}

FVector AMarbleRaceLevelGameMode::GetRacePosition(const AActor* Marble) const
{
	FVector Position;
	return GetQueuedPosition(Marble, Position) ? Position : Marble->GetActorLocation();
}

bool AMarbleRaceLevelGameMode::UpdateRaceCompletion(const double Now)
{
	if (!bRaceComplete)
	{
		if (!bHasFinishLine || Marbles.IsEmpty()) return false;
		for (AActor* Marble : Marbles) if (IsValid(Marble) && !HasFinished(Marble)) return false;
		bRaceComplete = true;
		ResultsStartedAt = Now;
		CurrentLeader.Reset();
		RestoreWorldSpeed();
		if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
		{
			Controller->SetShowMouseCursor(true);
			Controller->SetInputMode(FInputModeGameAndUI()
				.SetHideCursorDuringCapture(false).SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock));
		}
		FMarbleThemePlayback& Playing = bUseThemePlayerA ? ThemePlaybackB : ThemePlaybackA;
		MusicFinishAt = Now;
		if (bFinishBGM && Playing.RosterIndex != INDEX_NONE && Playing.Duration > 0.0 &&
			Playing.StopsAt == TNumericLimits<double>::Max())
		{
			MusicFinishAt = Now + Playing.Duration - Playing.PositionAt(Now);
			Playing.StopsAt = MusicFinishAt;
		}
	}
	if (!bPostRaceMusicFinished && Now >= MusicFinishAt)
	{
		ThemePlayerA->Stop();
		ThemePlayerB->Stop();
		ThemePlaybackA.StopsAt = FMath::Min(ThemePlaybackA.StopsAt, Now);
		ThemePlaybackB.StopsAt = FMath::Min(ThemePlaybackB.StopsAt, Now);
		bPostRaceMusicFinished = true;
		// Keep recording the automatic results pages until normal menu travel.
	}
	return true;
}

void AMarbleRaceLevelGameMode::UpdateLeaderMusic(AActor* Leader)
{
	if (bMusicSwitchingLocked || bRaceComplete) return;
	const FMarbleThemePlayback& CurrentPlayback = bUseThemePlayerA ? ThemePlaybackB : ThemePlaybackA;
	if (Leader == MusicLeader.Get() && (Leader || CurrentPlayback.StopsAt != TNumericLimits<double>::Max()))
	{
		return;
	}
	MusicLeader = Leader;
	// UI music continues through game pauses, so use undilated real time.
	const double Now = GetWorld()->GetRealTimeSeconds();
	if (!Leader)
	{
		SwitchThemeMusic(nullptr, INDEX_NONE, Now);
		return;
	}
	const int32* Index = MarbleRosterIndices.Find(Leader);
	UMarbleRaceRosterSubsystem* Roster = GetGameInstance()->GetSubsystem<UMarbleRaceRosterSubsystem>();
	if (!Index || !Roster || !Roster->IsValidIndex(*Index))
	{
		SwitchThemeMusic(nullptr, INDEX_NONE, Now);
		return;
	}
	USoundBase* Music = Roster->GetEntries()[*Index].ThemeMusic.LoadSynchronous();
	if (!Music)
	{
		SwitchThemeMusic(nullptr, INDEX_NONE, Now);
		return;
	}
	SwitchThemeMusic(Music, *Index, Now, Roster->GetEntries()[*Index].ThemeStartTimeSeconds);
	const FRaceCharacterEntry& Entry = Roster->GetEntries()[*Index];
	const FMarbleThemePlayback& Playback = bUseThemePlayerA ? ThemePlaybackB : ThemePlaybackA;
	UE_LOG(LogTemp, Log, TEXT("领跑音乐：%s / %s（从 %.2f 秒继续）"), *Entry.DisplayName, *Entry.ThemeTitle, Playback.StartedFrom);
}

void AMarbleRaceLevelGameMode::SwitchThemeMusic(USoundBase* Music, const int32 RosterIndex, const double Now, const double InitialStartTime)
{
	UAudioComponent* Outgoing = bUseThemePlayerA ? ThemePlayerB.Get() : ThemePlayerA.Get();
	FMarbleThemePlayback& OutgoingPlayback = bUseThemePlayerA ? ThemePlaybackB : ThemePlaybackA;
	FMarbleThemePlayback& IncomingPlayback = bUseThemePlayerA ? ThemePlaybackA : ThemePlaybackB;
	// Capture both channels before either is reused. The most recently active
	// channel wins if the same song was previously played on both channels.
	for (const FMarbleThemePlayback* Playback : {&IncomingPlayback, &OutgoingPlayback})
	{
		if (!bBackgroundMusic && Playback->RosterIndex != INDEX_NONE)
		{
			ThemeResumePositions.Add(Playback->RosterIndex, Playback->PositionAt(Now));
		}
	}
	OutgoingPlayback.StopsAt = FMath::Min(OutgoingPlayback.StopsAt, Now);
	Outgoing->Stop();
	if (!Music || RosterIndex == INDEX_NONE)
	{
		return;
	}
	// GetDuration() reports infinity for a looping SoundWave; Duration holds
	// the actual song length required to wrap the resume position.
	const USoundWave* Wave = Cast<USoundWave>(Music);
	const double Duration = Wave ? Wave->Duration : Music->GetDuration();
	const double* SavedPosition = ThemeResumePositions.Find(RosterIndex);
	// A shared clock advances every song, including songs that have never led.
	// Only the audible song needs decoding; silent songs ignore initial offsets.
	const double ResumeAt = bBackgroundMusic
		? FMarbleThemePlayback{RosterIndex, MusicTimelineStartedAt, 0.0, Duration}.PositionAt(Now)
		: SavedPosition
		? (Duration > 0.0 ? FMath::Fmod(*SavedPosition, Duration) : 0.0)
		: FMarbleThemePlayback::ClampStartTime(InitialStartTime, Duration);
	if (!bBackgroundMusic) ThemeResumePositions.Add(RosterIndex, ResumeAt);
	UAudioComponent* Incoming = bUseThemePlayerA ? ThemePlayerA.Get() : ThemePlayerB.Get();
	Incoming->Stop();
	Incoming->SetSound(Music);
	IncomingPlayback = {RosterIndex, Now, ResumeAt, Duration};
	const auto* Settings = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMarbleRaceSettingsSubsystem>() : nullptr;
	Incoming->SetVolumeMultiplier(0.65f * (Settings ? Settings->GetThemeVolumeGain(FSoftObjectPath(Music)) : 1.f));
	Incoming->Play(static_cast<float>(ResumeAt));
	bUseThemePlayerA = !bUseThemePlayerA;
}

void AMarbleRaceLevelGameMode::ShowCountdown(const int32 SecondsRemaining)
{
	CountdownDisplay = SecondsRemaining;
}

void AMarbleRaceLevelGameMode::HideCountdown()
{
	CountdownDisplay = 0;
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
	RestoreCameraZoom();
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
	RaceStartedAt = MusicTimelineStartedAt = GetWorld()->GetRealTimeSeconds();
	PreviousFinishPositions.Reset();
	for (AActor* Marble : Marbles)
		if (IsValid(Marble) && !Marble->IsHidden())
			PreviousFinishPositions.Add(Marble, {Marble->GetActorLocation().Z, RaceStartedAt});
	bFollowLeader = true;
}

void AMarbleRaceLevelGameMode::MarkFinishedMarbles(double Now)
{
	if (!bHasFinishLine || bRaceComplete) return;
	if (Now < 0.0) Now = GetWorld()->GetRealTimeSeconds();
	struct FCrossing { AActor* Marble; double FinishedAt; };
	TArray<FCrossing> Crossings;

	for (AActor* Marble : Marbles)
	{
		if (!IsEligibleMarble(Marble) || Marble->IsHidden())
		{
			// A queued marble must not interpolate across its hidden/respawn interval.
			PreviousFinishPositions.Remove(Marble);
			continue;
		}

		const double Z = Marble->GetActorLocation().Z;
		if (Z <= FinishLineZ)
		{
			double FinishedAt = Now;
			if (const FFinishPositionSample* Previous = PreviousFinishPositions.Find(Marble);
				Previous && Previous->Z > FinishLineZ && Previous->SampledAt <= Now)
			{
				const double Fraction = FMath::Clamp((Previous->Z - FinishLineZ) / (Previous->Z - Z), 0.0, 1.0);
				FinishedAt = FMath::Lerp(Previous->SampledAt, Now, Fraction);
			}
			Crossings.Add({Marble, FinishedAt});
			PreviousFinishPositions.Remove(Marble);
		}
		else PreviousFinishPositions.Add(Marble, {Z, Now});
	}
	// Stable ties preserve a consistent order; otherwise compare the crossing time.
	Crossings.StableSort([](const FCrossing& A, const FCrossing& B) { return A.FinishedAt < B.FinishedAt; });
	const auto* Roster = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMarbleRaceRosterSubsystem>() : nullptr;
	for (const FCrossing& Crossing : Crossings)
	{
		AActor* Marble = Crossing.Marble;
		if (Marble == CurrentLeader.Get()) bFinishCameraCatchUpActive = true;
		FinishedMarbles.Add(Marble);
		FMarbleRaceFinishResult& Result = FinishResults.AddDefaulted_GetRef();
		Result.Rank = FinishResults.Num();
		Result.ElapsedSeconds = FMath::Max(0.0, Crossing.FinishedAt - RaceStartedAt);
		const int32* Index = MarbleRosterIndices.Find(Marble);
		Result.RosterIndex = Index ? *Index : INDEX_NONE;
		if (Roster && Roster->IsValidIndex(Result.RosterIndex))
		{
			const FRaceCharacterEntry& Entry = Roster->GetEntries()[Result.RosterIndex];
			Result.CharacterName = Entry.DisplayName;
			Result.ThemeTitle = !Entry.ThemeTitle.IsEmpty() ? Entry.ThemeTitle
				: Entry.ThemeMusic.IsNull() ? TEXT("未设置 BGM") : Entry.ThemeMusic.GetAssetName();
		}
		else Result.CharacterName = Marble->GetName();
		LastFinishNotificationAt = Now;
	}
}

const FMarbleRaceFinishResult* AMarbleRaceLevelGameMode::GetFinishNotification(const double Now) const
{
	return !FinishResults.IsEmpty() && Now >= LastFinishNotificationAt &&
		Now - LastFinishNotificationAt < MarbleRace::FinishNotificationSeconds ? &FinishResults.Last() : nullptr;
}

void AMarbleRaceLevelGameMode::ReturnToMainMenu()
{
	if (bReturningToMainMenu || (!bRaceComplete && !bRaceAborted)) return;
	bReturningToMainMenu = true;
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Maps/MainMenu")));
}

void AMarbleRaceLevelGameMode::AbortRaceAndReturnToMainMenu()
{
	if (bReturningToMainMenu) return;
	bRaceAborted = true;
	if (GetGameInstance()) GetGameInstance()->GetSubsystem<UMarbleRaceRecorderSubsystem>()->DiscardRecording();
	ReturnToMainMenu();
}

bool AMarbleRaceLevelGameMode::HasFinished(const AActor* Marble) const
{
	return FinishedMarbles.Contains(Marble);
}

bool AMarbleRaceLevelGameMode::IsEligibleMarble(const AActor* Marble) const
{
	const bool bValid = IsValid(Marble);
	FVector QueuedPosition;
	if (bValid && !HasFinished(Marble) && GetQueuedPosition(Marble, QueuedPosition)) return true;
	return MarbleRace::IsCameraTargetEligible(bValid, bValid && Marble->IsHidden(),
	                                        bValid && HasFinished(Marble));
}

bool AMarbleRaceLevelGameMode::IsOnCourse(const AActor* Marble) const
{
	if (!IsValid(Marble))
	{
		return false;
	}

	// 横向离开赛道中心线的弹珠（例如被滚筒甩出去、正在往深渊掉的）不能当第一名，
	// 否则镜头会跟着它一路往下掉。
	const FVector Location = GetRacePosition(Marble);
	const float LateralOffset = FVector2D(Location.X - CourseCentre.X, Location.Y - CourseCentre.Y).Size();
	if (MaxLeaderLateralOffset > 0.f && LateralOffset > MaxLeaderLateralOffset)
	{
		return false;
	}

	// 赛道上的弹珠不会长时间自由落体，向下速度过大说明它已经离开赛道。
	const float FallSpeed = Marble->IsHidden() ? 0.f : -Marble->GetVelocity().Z;
	return !(MaxLeaderFallSpeed > 0.f && FallSpeed > MaxLeaderFallSpeed);
}

bool AMarbleRaceLevelGameMode::IsLeaderCandidate(const AActor* Marble) const
{
	return IsEligibleMarble(Marble) && IsOnCourse(Marble);
}

AActor* AMarbleRaceLevelGameMode::FindLeadingMarble() const
{
	AActor* Best = nullptr;
	float BestZ = 0.f;
	for (AActor* Marble : Marbles)
	{
		if (!IsLeaderCandidate(Marble))
		{
			continue;
		}

		const float MarbleZ = GetRacePosition(Marble).Z;
		if (!Best || MarbleZ < BestZ)
		{
			Best = Marble;
			BestZ = MarbleZ;
		}
	}

	AActor* StickyLeader = CurrentLeader.Get();
	if (Best && IsLeaderCandidate(StickyLeader))
	{
		const float StickyZ = GetRacePosition(StickyLeader).Z;
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

	const float TargetZ = GetRacePosition(Leader).Z;
	const float CurrentZ = FollowCamera->GetActorLocation().Z;
	if (bFinishCameraCatchUpActive)
	{
		// Use real frame time so finish slow motion does not slow down the camera handoff.
		const float RealDelta = GetWorld()->DeltaRealTimeSeconds > 0.f
			? GetWorld()->DeltaRealTimeSeconds : DeltaSeconds;
		const float NextZ = MarbleRace::AdvanceDistanceCameraZ(CurrentZ, TargetZ, RealDelta, CameraFinishCatchUpRate);
		FollowCamera->SetActorLocation(FVector(CameraLockX, CameraSideY, NextZ));
		if (APlayerController* Controller = GetWorld()->GetFirstPlayerController();
			Controller && Controller->PlayerCameraManager)
		{
			// Present the continuous movement in this frame, preserving motion history.
			Controller->PlayerCameraManager->UpdateCamera(0.f);
		}
		if (FMath::Abs(TargetZ - NextZ) <= FMath::Max(1.f, CameraOrthoWidth * .1f))
			bFinishCameraCatchUpActive = false;
		return;
	}
	// Ordinary overtakes and relocations retain interpolation and the travel bound.
	const float MaxSpeed = FMath::Max(200.f, CameraOrthoWidth) *
	                       FMath::Max(0.1f, CameraMaxViewportWidthsPerSecond);
	const float SmoothedZ = MarbleRace::AdvanceCameraZ(CurrentZ, TargetZ, DeltaSeconds,
	                                                CameraFollowSpeed, MaxSpeed);

	FollowCamera->SetActorLocation(FVector(CameraLockX, CameraSideY, SmoothedZ));
}
