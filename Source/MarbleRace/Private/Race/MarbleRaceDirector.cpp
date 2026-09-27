#include "Race/MarbleRaceDirector.h"

#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Roster/MarbleRaceRosterSubsystem.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Roster/RaceCharacterProfile.h"
#include "Race/RaceCheckpoint.h"
#include "Race/RaceFrameLogic.h"
#include "Audio/RaceMusicDirectorComponent.h"
#include "Race/RaceParticipantComponent.h"
#include "Race/RaceStartDrum.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogMarbleRaceDirector, Log, All);

namespace
{
	constexpr int32 MaxTestRacers = 8;
	constexpr float TestRacerMaxAbsX = 340.0f;
	constexpr float StallSpeed = 40.0f;
	constexpr float StallBeforeNudge = 1.8f;

	/** 每颗未完赛小球几乎停住了多久。不放进导演对象里。 */
	TMap<int32, float> StallSeconds;

	UPhysicalMaterial* CreateSlipperyMaterial()
	{
		// 别挂在导演上。导演名下的材质若还被引用，
		// 会把整个 PIE 世界留住，触发 PlayLevel.cpp:553。
		UPhysicalMaterial* Material = NewObject<UPhysicalMaterial>(GetTransientPackage());
		Material->Friction = 0.05f;
		Material->StaticFriction = 0.05f;
		Material->Restitution = 0.0f;
		Material->FrictionCombineMode = EFrictionCombineMode::Min;
		Material->bOverrideFrictionCombineMode = true;
		Material->RestitutionCombineMode = EFrictionCombineMode::Min;
		Material->bOverrideRestitutionCombineMode = true;
		return Material;
	}
}

AMarbleRaceDirector::AMarbleRaceDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	MusicDirector = CreateDefaultSubobject<URaceMusicDirectorComponent>(TEXT("MusicDirector"));
}

void AMarbleRaceDirector::BeginPlay()
{
	Super::BeginPlay();

	// 棋盘图案以前是贴在门平面上的实心地板，
	// 停在上面的球永远过不了门。松手之前先把这块图案让开。
	DisableFinishLineCollision();
	ApplyCourseSurfacePhysics();
	EnsureCourseMechanisms();

	BuildRoute();
	BuildPlaceholderProfiles();

	if (bAutoRegisterTaggedRacers)
	{
		AutoRegisterTaggedRacers();
	}
	if (bSpawnTestRacers)
	{
		SpawnTestRacers();
	}
	if (bUseRoster)
	{
		ApplyRosterIdentities();
	}

	if (!bRouteUsable)
	{
		State = ERaceState::Ready;
		UE_LOG(LogMarbleRaceDirector, Error, TEXT("比赛不会自动开始：%s"), *RouteStatusText.ToString());
		return;
	}

	// 倒计时之前先定好开局机关，好让准备阶段把小球放进去；
	// 路线不可用时整段跳过。
	StartDrum = ResolveStartDrum();

	if (bAutoStartCountdown)
	{
		PrepareRace();
	}
	else
	{
		State = ERaceState::Ready;
	}
}

void AMarbleRaceDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bRacePaused)
	{
		return;
	}

	const float Step = FMath::Max(0.0f, DeltaSeconds);
	switch (State)
	{
	case ERaceState::Countdown:
		CountdownRemaining -= Step;
		UpdateDrumHold(Step);
		if (CountdownRemaining <= 0.0f)
		{
			StartRace();
		}
		break;

	case ERaceState::Racing:
		RaceClock += Step;
		UpdateRacers(Step);
		UpdateMusic(Step);
		break;

	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// 准备
// ---------------------------------------------------------------------------

void AMarbleRaceDirector::BuildRoute()
{
	Route.Reset();
	bRouteUsable = false;
	bUsingVerticalFallback = false;

	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(this, ARaceCheckpoint::StaticClass(), Found);
	for (AActor* Actor : Found)
	{
		if (ARaceCheckpoint* Checkpoint = Cast<ARaceCheckpoint>(Actor))
		{
			Route.Add(Checkpoint);
		}
	}

	if (Route.Num() == 0)
	{
		if (bUseVerticalFallbackWhenNoCheckpoints)
		{
			bUsingVerticalFallback = true;
			bRouteUsable = true;
			RouteStatusText = FText::FromString(FString::Printf(
				TEXT("高度兼容模式：未放置检查点，按高度判定进度（终点 Z=%.0f，仅适用于竖直下降地图）"), FallbackFinishZ));
			UE_LOG(LogMarbleRaceDirector, Warning, TEXT("%s"), *RouteStatusText.ToString());
		}
		else
		{
			RouteStatusText = FText::FromString(TEXT("错误：没有检查点且已关闭高度兼容模式，比赛无法计分"));
			UE_LOG(LogMarbleRaceDirector, Error, TEXT("%s"), *RouteStatusText.ToString());
		}
		return;
	}

	// 用插入排序，避免对象指针数组上的比较含义不清。
	for (int32 Index = 1; Index < Route.Num(); ++Index)
	{
		const TObjectPtr<ARaceCheckpoint> Key = Route[Index];
		int32 Scan = Index - 1;
		while (Scan >= 0 && Route[Scan] && Key && Route[Scan]->Order > Key->Order)
		{
			Route[Scan + 1] = Route[Scan];
			--Scan;
		}
		Route[Scan + 1] = Key;
	}

	TSet<int32> SeenOrders;
	for (const TObjectPtr<ARaceCheckpoint>& Checkpoint : Route)
	{
		if (!Checkpoint)
		{
			continue;
		}
		bool bAlreadyInSet = false;
		SeenOrders.Add(Checkpoint->Order, &bAlreadyInSet);
		if (bAlreadyInSet)
		{
			RouteStatusText = FText::FromString(FString::Printf(
				TEXT("错误：检查点顺序号 %d 重复，请改正后再开始比赛"), Checkpoint->Order));
			UE_LOG(LogMarbleRaceDirector, Error, TEXT("%s"), *RouteStatusText.ToString());
			return;
		}
	}

	int32 FinishCount = 0;
	for (const TObjectPtr<ARaceCheckpoint>& Checkpoint : Route)
	{
		if (Checkpoint && Checkpoint->bIsFinish)
		{
			++FinishCount;
		}
	}

	if (FinishCount != 1)
	{
		RouteStatusText = FText::FromString(FString::Printf(
			TEXT("错误：需要且仅需要一个终点检查点，当前为 %d 个"), FinishCount));
		UE_LOG(LogMarbleRaceDirector, Error, TEXT("%s"), *RouteStatusText.ToString());
		return;
	}

	if (!Route.Last() || !Route.Last()->bIsFinish)
	{
		RouteStatusText = FText::FromString(TEXT("错误：终点检查点必须是顺序号最大的那一个"));
		UE_LOG(LogMarbleRaceDirector, Error, TEXT("%s"), *RouteStatusText.ToString());
		return;
	}

	bRouteUsable = true;
	RouteStatusText = FText::FromString(FString::Printf(TEXT("正式路线：%d 个检查点"), Route.Num()));
	UE_LOG(LogMarbleRaceDirector, Log, TEXT("%s"), *RouteStatusText.ToString());
}

void AMarbleRaceDirector::BuildPlaceholderProfiles()
{
	PlaceholderProfiles.Reset();
	if (!bCreatePlaceholderProfiles)
	{
		return;
	}

	static const TCHAR* Names[] = {
		TEXT("占位选手 A"), TEXT("占位选手 B"), TEXT("占位选手 C"),
		TEXT("占位选手 D"), TEXT("占位选手 E"), TEXT("占位选手 F")
	};
	static const FLinearColor Colors[] = {
		FLinearColor(0.86f, 0.20f, 0.20f),
		FLinearColor(0.16f, 0.42f, 0.92f),
		FLinearColor(0.14f, 0.70f, 0.34f),
		FLinearColor(0.94f, 0.66f, 0.12f),
		FLinearColor(0.62f, 0.28f, 0.84f),
		FLinearColor(0.10f, 0.74f, 0.78f)
	};

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
	{
		URaceCharacterProfile* Profile = NewObject<URaceCharacterProfile>(this, NAME_None, RF_Transient);
		Profile->CharacterId = FName(*FString::Printf(TEXT("Placeholder_%d"), Index));
		Profile->DisplayName = FText::FromString(Names[Index]);
		Profile->Color = Colors[Index];
		PlaceholderProfiles.Add(Profile);
	}
	UE_LOG(LogMarbleRaceDirector, Log,
		TEXT("未配置角色资产，已创建 %d 个占位身份（无头像与主题曲）"), PlaceholderProfiles.Num());
}

void AMarbleRaceDirector::AutoRegisterTaggedRacers()
{
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsWithTag(this, RacerTag, Found);

	// 先按演员名排序，选手编号不跟着生成顺序变。
	TArray<TPair<FString, URaceParticipantComponent*>> Pending;
	for (AActor* Actor : Found)
	{
		if (!Actor)
		{
			continue;
		}

		URaceParticipantComponent* Participant = Actor->FindComponentByClass<URaceParticipantComponent>();
		if (!Participant)
		{
			Participant = NewObject<URaceParticipantComponent>(Actor, URaceParticipantComponent::StaticClass(), TEXT("RaceParticipant"));
			Participant->RegisterComponent();
			UE_LOG(LogMarbleRaceDirector, Log, TEXT("为 %s 自动添加参赛者组件"), *Actor->GetName());
		}

		if (!Participant->bAutoRegister)
		{
			continue;
		}
		Pending.Emplace(Actor->GetName(), Participant);
	}

	Pending.Sort([](const TPair<FString, URaceParticipantComponent*>& A, const TPair<FString, URaceParticipantComponent*>& B)
	{
		return A.Key < B.Key;
	});

	for (const TPair<FString, URaceParticipantComponent*>& Pair : Pending)
	{
		RegisterParticipant(Pair.Value);
	}
}

void AMarbleRaceDirector::SpawnTestRacers()
{
	if (bUseRoster)
	{
		UE_LOG(LogMarbleRaceDirector, Log,
			TEXT("已启用角色名单，测试球生成被跳过（名单会补足参赛人数）"));
		return;
	}

	const int32 Count = FMath::Clamp(TestRacerCount, 1, MaxTestRacers);
	int32 Spawned = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (SpawnRacerFromTemplate(Index))
		{
			++Spawned;
		}
	}

	UE_LOG(LogMarbleRaceDirector, Log, TEXT("已生成 %d 个测试球用于验证反超与音乐切换"), Spawned);
}

bool AMarbleRaceDirector::SpawnRacerFromTemplate(int32 SpawnIndex)
{
	if (Racers.Num() == 0)
	{
		UE_LOG(LogMarbleRaceDirector, Warning, TEXT("没有可用的参赛球作为模板，跳过小球生成"));
		return false;
	}

	UWorld* World = GetWorld();
	URaceParticipantComponent* Template = Racers[0].Participant;
	UStaticMeshComponent* TemplateMesh = Template ? Cast<UStaticMeshComponent>(Template->GetPhysicsComponent()) : nullptr;
	AActor* TemplateActor = Template ? Template->GetOwner() : nullptr;
	if (!World || !TemplateMesh || !TemplateActor)
	{
		UE_LOG(LogMarbleRaceDirector, Warning, TEXT("生成小球需要以带静态网格的物理球为模板，已跳过"));
		return false;
	}

	const int32 TotalRacers = FMath::Max(1, Racers.Num());
	FTransform SpawnTransform = Template->GetStartTransform();
	FVector Location = SpawnTransform.GetLocation();
	Location.X = FMath::Clamp(Location.X + TestRacerSpacingX * (SpawnIndex + 1), -TestRacerMaxAbsX, TestRacerMaxAbsX);
	SpawnTransform.SetLocation(Location);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), SpawnTransform, SpawnParameters);
	if (!Actor)
	{
		return false;
	}

	UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
	if (!Mesh)
	{
		Actor->Destroy();
		return false;
	}

	Actor->SetActorScale3D(TemplateActor->GetActorScale3D());
	// 给生成的小球打标签、起大纲名称，调试和大纲都能找到；
	// 不然按标签搜索只能看到关卡里原来那一颗。
	Actor->Tags.AddUnique(RacerTag);
#if WITH_EDITOR
	Actor->SetActorLabel(FString::Printf(TEXT("比赛小球_%d"), TotalRacers + 1));
#endif
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetStaticMesh(TemplateMesh->GetStaticMesh());
	const int32 MaterialCount = TemplateMesh->GetNumMaterials();
	for (int32 Slot = 0; Slot < MaterialCount; ++Slot)
	{
		Mesh->SetMaterial(Slot, TemplateMesh->GetMaterial(Slot));
	}
	Mesh->SetCollisionProfileName(TemplateMesh->GetCollisionProfileName());
	Mesh->SetCollisionEnabled(TemplateMesh->GetCollisionEnabled());
	Mesh->SetSimulatePhysics(true);
	Mesh->SetEnableGravity(true);
	Mesh->SetNotifyRigidBodyCollision(true);
	if (bEnsureContinuousCollisionOnRacers)
	{
		Mesh->SetUseCCD(true);
	}
	if (FBodyInstance* BodyInstance = Mesh->GetBodyInstance())
	{
		BodyInstance->SetDOFLock(EDOFMode::XZPlane);
	}

	URaceParticipantComponent* Participant = NewObject<URaceParticipantComponent>(Actor, URaceParticipantComponent::StaticClass(), TEXT("RaceParticipant"));
	Participant->RegisterComponent();
	RegisterParticipant(Participant);
	return true;
}

int32 AMarbleRaceDirector::EnsureRacerCount(int32 DesiredCount)
{
	if (!bSpawnRosterMarbles)
	{
		return Racers.Num();
	}

	// 生成一直失败时，防止循环停不下来。
	int32 Attempts = 0;
	const int32 MaxAttempts = FMath::Max(0, DesiredCount - Racers.Num()) + 4;
	while (Racers.Num() < DesiredCount && Attempts < MaxAttempts)
	{
		++Attempts;
		if (!SpawnRacerFromTemplate(Racers.Num()))
		{
			break;
		}
	}
	return Racers.Num();
}

void AMarbleRaceDirector::ApplyRosterIdentities()
{
	RosterProfiles.Reset();

	if (!bUseRoster)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UMarbleRaceRosterSubsystem* Roster = GameInstance
		? GameInstance->GetSubsystem<UMarbleRaceRosterSubsystem>()
		: nullptr;
	if (!Roster || Roster->GetEnabledCount() == 0)
	{
		UE_LOG(LogMarbleRaceDirector, Warning, TEXT("角色名单为空，沿用关卡内的球与占位身份"));
		return;
	}

	const int32 DesiredCount = Roster->GetEnabledCount();
	const int32 Available = EnsureRacerCount(DesiredCount);
	if (Available < DesiredCount)
	{
		UE_LOG(LogMarbleRaceDirector, Warning, TEXT("名单有 %d 个球，但只放置了 %d 个"), DesiredCount, Available);
	}

	int32 AssignedCount = 0;
	for (int32 Slot = 0; Slot < Racers.Num(); ++Slot)
	{
		URaceParticipantComponent* Participant = Racers[Slot].Participant;
		if (!Participant)
		{
			continue;
		}

		const int32 EntryIndex = Roster->GetEnabledEntryIndex(Slot);
		if (EntryIndex == INDEX_NONE)
		{
			// 小球比名单条目多：保留占位身份。
			continue;
		}

		const FRaceCharacterEntry& Entry = Roster->GetEntries()[EntryIndex];
		URaceCharacterProfile* Profile = NewObject<URaceCharacterProfile>(this);
		Profile->CharacterId = FName(*FString::Printf(TEXT("Roster_%d"), EntryIndex));
		Profile->DisplayName = FText::FromString(Entry.DisplayName);
		Profile->Color = Entry.Color;
		Profile->Portrait = Roster->GetPortraitTexture(EntryIndex);
		// 比赛开始前就加载好，超车时不再加载。
		Profile->ThemeMusic = Entry.ThemeMusic.IsNull() ? nullptr : Entry.ThemeMusic.LoadSynchronous();
		Profile->MusicGain = 1.0f;

		RosterProfiles.Add(Profile);
		Participant->CharacterProfile = Profile;
		++AssignedCount;
	}

	UE_LOG(LogMarbleRaceDirector, Log, TEXT("已按角色名单配置 %d 个球（名单共 %d 个）"),
		AssignedCount, DesiredCount);
}

ARaceStartDrum* AMarbleRaceDirector::ResolveStartDrum()
{
	if (StartDrum)
	{
		return StartDrum;
	}
	if (!bUseStartDrum)
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// 关卡里已经摆好的滚筒优先。
	if (AActor* Placed = UGameplayStatics::GetActorOfClass(World, ARaceStartDrum::StaticClass()))
	{
		StartDrum = Cast<ARaceStartDrum>(Placed);
		return StartDrum;
	}
	if (!bSpawnDrumIfMissing)
	{
		return nullptr;
	}

	FVector StartLocation = FVector::ZeroVector;
	float RacerRadius = 25.0f;
	if (Racers.Num() > 0 && Racers[0].Participant)
	{
		StartLocation = Racers[0].Participant->GetStartTransform().GetLocation();
		RacerRadius = Racers[0].Participant->GetWorldRadius();
	}

	// 人数多就放大环，否则保持摆好的样子。
	const int32 FieldSize = FMath::Max(1, Racers.Num());
	const float NeededRadius = (static_cast<float>(FieldSize) * (2.0f * RacerRadius + 12.0f)) / (2.0f * UE_PI)
		+ RacerRadius + 12.0f;

	// 生成演员要的是类指针；两种类引用混在三元表达式里会有歧义。
	UClass* DrumClass = StartDrumClass ? StartDrumClass.Get() : ARaceStartDrum::StaticClass();
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 把滚筒抬高，让环的底部落在小球原来的出发高度：
	// 松手后，小球仍从赛道设计时的那个高度落下。
	StartDrum = World->SpawnActor<ARaceStartDrum>(DrumClass, StartLocation + StartDrumOffset, FRotator::ZeroRotator, SpawnParameters);
	if (!StartDrum)
	{
		UE_LOG(LogMarbleRaceDirector, Warning, TEXT("开局滚筒生成失败，将使用固定起始位置"));
		return nullptr;
	}

	StartDrum->Radius = FMath::Max(StartDrum->Radius, NeededRadius);
	// 把环的中心放在出发点上，整只滚筒留在画面里；
	// 小球围着环出发，而不是悬在环的上方。
	StartDrum->SetActorLocation(StartLocation + StartDrumOffset);
	if (StartDrumMaterial)
	{
		StartDrum->DrumMaterial = StartDrumMaterial;
	}
	StartDrum->RebuildGeometry();

	UE_LOG(LogMarbleRaceDirector, Log, TEXT("已生成开局滚筒：半径 %.0f，容纳 %d 个球"),
		StartDrum->Radius, FieldSize);
	return StartDrum;
}

void AMarbleRaceDirector::UpdateDrumHold(float DeltaSeconds)
{
	if (!StartDrum || State != ERaceState::Countdown)
	{
		return;
	}

	const FVector DrumLocation = StartDrum->GetActorLocation();
	const float SpinDegrees = StartDrum->GetSpinAngleDegrees();

	for (FRacerRuntime& Racer : Racers)
	{
		URaceParticipantComponent* Participant = Racer.Participant;
		AActor* RacerActor = Participant ? Participant->GetOwner() : nullptr;
		if (!Participant || !RacerActor)
		{
			continue;
		}

		const float HoldRadius = StartDrum->GetHoldingRadius(Participant->GetWorldRadius());
		const float Angle = FMath::DegreesToRadians(-SpinDegrees + Racer.DrumPhaseDegrees);
		const FVector Target(DrumLocation.X + FMath::Cos(Angle) * HoldRadius,
			DrumLocation.Y,
			DrumLocation.Z + FMath::Sin(Angle) * HoldRadius);

		RacerActor->SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);
		if (UPrimitiveComponent* Body = Participant->GetPhysicsComponent())
		{
			Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		}
	}
}

void AMarbleRaceDirector::PlaceRacersInDrum()
{
	if (!StartDrum)
	{
		return;
	}

	const FVector DrumLocation = StartDrum->GetActorLocation();
	const float SpinDegrees = StartDrum->GetSpinAngleDegrees();

	for (FRacerRuntime& Racer : Racers)
	{
		URaceParticipantComponent* Participant = Racer.Participant;
		AActor* RacerActor = Participant ? Participant->GetOwner() : nullptr;
		if (!Participant || !RacerActor)
		{
			continue;
		}

		// 和按住滚筒用同一套环上公式，按住的第一帧不会跳一下。
		const float HoldRadius = StartDrum->GetHoldingRadius(Participant->GetWorldRadius());
		const float Angle = FMath::DegreesToRadians(-SpinDegrees + Racer.DrumPhaseDegrees);
		const FVector Target(DrumLocation.X + FMath::Cos(Angle) * HoldRadius,
			DrumLocation.Y,
			DrumLocation.Z + FMath::Sin(Angle) * HoldRadius);

		RacerActor->SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);
		if (UPrimitiveComponent* Body = Participant->GetPhysicsComponent())
		{
			Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		}
		// 用新位置初始化进度，比赛开始的第一帧
		// 不会从旧的出发位置量出一段假的超长距离。
		Participant->SetPreviousLocation(Target);
	}
}

void AMarbleRaceDirector::DisableFinishLineCollision()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AActor*> Visuals;
	UGameplayStatics::GetAllActorsWithTag(World, FName(TEXT("FinishLineVisual")), Visuals);
	for (AActor* Visual : Visuals)
	{
		if (!Visual)
		{
			continue;
		}
		TInlineComponentArray<UPrimitiveComponent*> Primitives(Visual);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
}

void AMarbleRaceDirector::ApplyCourseSurfacePhysics()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 网格通过形体实例持有这份材质。它不挂在
	// 导演下面，也没有任何静态强指针留着它。
	UPhysicalMaterial* CourseSurfaceMaterial = CreateSlipperyMaterial();

	TArray<AActor*> MeshActors;
	UGameplayStatics::GetAllActorsOfClass(World, AStaticMeshActor::StaticClass(), MeshActors);
	for (AActor* MeshActor : MeshActors)
	{
		AStaticMeshActor* MeshActorTyped = Cast<AStaticMeshActor>(MeshActor);
		if (!MeshActorTyped || MeshActorTyped->ActorHasTag(RacerTag) || MeshActorTyped->ActorHasTag(FName(TEXT("FinishLineVisual"))))
		{
			continue;
		}
		UStaticMeshComponent* Mesh = MeshActorTyped->GetStaticMeshComponent();
		if (!Mesh || Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
		{
			continue;
		}
		Mesh->SetPhysMaterialOverride(CourseSurfaceMaterial);
	}
}

void AMarbleRaceDirector::EnsureCourseMechanisms()
{
	// 赛道由关卡里手摆的零件组成。内容浏览器「赛道组件」里是斜面、
	// 弹性板、挡杆、拨板这些预制体。这里不再生成一整条赛道。
}


// ---------------------------------------------------------------------------
// 比赛流程
// ---------------------------------------------------------------------------

void AMarbleRaceDirector::PrepareRace()
{
	StallSeconds.Reset();
	if (!bRouteUsable)
	{
		State = ERaceState::Ready;
		UE_LOG(LogMarbleRaceDirector, Error, TEXT("比赛未开始：%s"), *RouteStatusText.ToString());
		return;
	}

	Results.Reset();
	NextFinishRank = 0;
	RaceClock = 0.0f;
	CountdownRemaining = FMath::Max(0.0f, CountdownSeconds);
	ChampionRacerId = INDEX_NONE;
	ChampionCelebrationEndTime = 0.0f;
	MusicTargetRacerId = INDEX_NONE;
	LeaderRacerId = INDEX_NONE;
	bChampionThemeLocked = false;
	bWasCelebrating = false;
	bRacePaused = false;
	LeaderDebounce.Reset();

	FallbackStartZ = -UE_BIG_NUMBER;
	for (FRacerRuntime& Racer : Racers)
	{
		if (!Racer.Participant)
		{
			continue;
		}
		Racer.bLoggedOutOfBounds = false;
		Racer.Participant->ResetRuntimeState();
		// 有滚筒时，小球放进下面的滚筒里，不留在原来的标记上。
		if (!StartDrum)
		{
			Racer.Participant->TeleportToStart();
		}
		FallbackStartZ = FMath::Max(FallbackStartZ, Racer.Participant->GetStartTransform().GetLocation().Z);
	}
	if (Racers.Num() == 0 || FallbackStartZ <= -UE_BIG_NUMBER)
	{
		FallbackStartZ = 0.0f;
	}

	if (StartDrum)
	{
		// 倒计时同时就是旋转。小球被运动学按在环上，
		// 不交给真实物理：从环中间掉下去的球可能
		// 穿出环壁，拨片也可能在开赛前就把球甩下赛道。
		// 冻住再加脚本运动，这两种都不会发生。
		StartDrum->ResetDrum();
		SetRacersFrozen(true);

		const int32 FieldSize = FMath::Max(1, Racers.Num());
		TArray<int32> Slots;
		for (int32 Index = 0; Index < Racers.Num(); ++Index)
		{
			Slots.Add(Index);
		}
		const float GlobalPhase = bRandomizeDrumPlacement ? FMath::FRandRange(0.0f, 360.0f) : 0.0f;
		if (bRandomizeDrumPlacement)
		{
			for (int32 Index = Slots.Num() - 1; Index > 0; --Index)
			{
				Slots.Swap(Index, FMath::RandRange(0, Index));
			}
		}
		for (int32 Index = 0; Index < Racers.Num(); ++Index)
		{
			Racers[Index].DrumPhaseDegrees = GlobalPhase
				+ (360.0f * static_cast<float>(Slots[Index])) / static_cast<float>(FieldSize);
		}

		// 关卡里摆好的滚筒不改尺寸，但容量不够要明确说出来。
		// 按真实半径算：小球不一定都是模板那个大小。
		bool bPlacementFits = true;
		for (int32 First = 0; First < Racers.Num(); ++First)
		{
			if (!Racers[First].Participant) { continue; }
			const float FirstRadius = Racers[First].Participant->GetWorldRadius();
			const float FirstHold = StartDrum->GetHoldingRadius(FirstRadius);
			bPlacementFits &= FirstHold + FirstRadius <= FMath::Max(60.0f, StartDrum->Radius);
			for (int32 Second = First + 1; Second < Racers.Num(); ++Second)
			{
				if (!Racers[Second].Participant) { continue; }
				const float SecondRadius = Racers[Second].Participant->GetWorldRadius();
				const float SecondHold = StartDrum->GetHoldingRadius(SecondRadius);
				const float AngleDifference = FMath::DegreesToRadians(
					Racers[First].DrumPhaseDegrees - Racers[Second].DrumPhaseDegrees);
				const float DistanceSquared = FMath::Max(0.0f, FirstHold * FirstHold + SecondHold * SecondHold
					- 2.0f * FirstHold * SecondHold * FMath::Cos(AngleDifference));
				bPlacementFits &= DistanceSquared >= FMath::Square(FirstRadius + SecondRadius + 2.0f);
			}
		}
		if (!bPlacementFits)
		{
			UE_LOG(LogMarbleRaceDirector, Warning,
				TEXT("滚筒容量不足：%d 个球无法安全间隔摆放，请减少参赛人数或手动增大滚筒；未修改关卡滚筒尺寸"), Racers.Num());
		}

		PlaceRacersInDrum();
		StartDrum->BeginSpin(DrumSpinSpeedDegrees);
	}
	else
	{
		SetRacersFrozen(true);
	}

	if (MusicDirector)
	{
		MusicDirector->ResetMusic();
	}

	State = ERaceState::Countdown;
	UE_LOG(LogMarbleRaceDirector, Log, TEXT("倒计时开始：%.1f 秒，参赛者 %d 名%s"),
		CountdownRemaining, Racers.Num(), StartDrum ? TEXT("（滚筒旋转中）") : TEXT(""));

	if (CountdownRemaining <= 0.0f)
	{
		StartRace();
	}
}

void AMarbleRaceDirector::StartRace()
{
	if (StartDrum)
	{
		// 关掉碰撞，球才能离开。速度就是旋转留下的惯性：
		// 之前每一帧都是瞬移到环上、速度被清掉，
		// 只解冻的话，它们会从冻住的姿势垂直掉下去。
		StartDrum->ReleaseDrum();
	}

	SetRacersFrozen(false);
	StallSeconds.Reset();
	if (StartDrum)
	{
		const float SpinDegrees = StartDrum->GetSpinAngleDegrees();
		const float Omega = FMath::DegreesToRadians(StartDrum->SpinSpeedDegrees);
		for (FRacerRuntime& Racer : Racers)
		{
			URaceParticipantComponent* Participant = Racer.Participant;
			UPrimitiveComponent* Body = Participant ? Participant->GetPhysicsComponent() : nullptr;
			if (!Body)
			{
				continue;
			}
			const float HoldRadius = StartDrum->GetHoldingRadius(Participant->GetWorldRadius());
			const float Theta = FMath::DegreesToRadians(-SpinDegrees + Racer.DrumPhaseDegrees);
			// 环上位置对时间求导，角度取负的旋转角。曲线和按住滚筒时相同。
			const FVector Inertia(
				HoldRadius * Omega * FMath::Sin(Theta),
				0.0f,
				-HoldRadius * Omega * FMath::Cos(Theta));
			Body->SetPhysicsLinearVelocity(Inertia, false);
			Body->SetPhysicsAngularVelocityInDegrees(FVector(0.0f, -StartDrum->SpinSpeedDegrees, 0.0f), false);
			Body->WakeRigidBody();
			UE_LOG(LogMarbleRaceDirector, Log, TEXT("滚筒松手：%s 切线速度 %.0f"),
				*Participant->GetDisplayName().ToString(), Inertia.Size());
		}
	}
	RaceClock = 0.0f;
	CountdownRemaining = 0.0f;
	State = ERaceState::Racing;
	LeaderDebounce.Reset();
	MusicTargetRacerId = INDEX_NONE;

	// 滚筒把所有人都挪过了，进度要从小球实际所在的位置量起。
	FallbackStartZ = -UE_BIG_NUMBER;
	for (FRacerRuntime& Racer : Racers)
	{
		if (!Racer.Participant)
		{
			continue;
		}
		if (AActor* RacerActor = Racer.Participant->GetOwner())
		{
			const FVector Location = RacerActor->GetActorLocation();
			Racer.Participant->SetPreviousLocation(Location);
			FallbackStartZ = FMath::Max(FallbackStartZ, Location.Z);
		}
	}
	if (Racers.Num() == 0 || FallbackStartZ <= -UE_BIG_NUMBER)
	{
		FallbackStartZ = 0.0f;
	}

	if (MusicDirector)
	{
		// 人还挤在一起时放中性音乐；领跑者的主题曲
		// 要等候选人领跑满确认秒数再接上。
		MusicDirector->RequestTheme(MusicDirector->DefaultMusic, 0.0f, 1.0f, true);
	}

	UE_LOG(LogMarbleRaceDirector, Log, TEXT("比赛开始"));
}

void AMarbleRaceDirector::EnterResults()
{
	if (State == ERaceState::Results)
	{
		return;
	}
	State = ERaceState::Results;
	UE_LOG(LogMarbleRaceDirector, Log, TEXT("比赛结束，共 %d 条成绩"), Results.Num());
}

void AMarbleRaceDirector::RestartRace()
{
	if (bRacePaused)
	{
		SetRacePaused(false);
	}
	PrepareRace();
}

void AMarbleRaceDirector::SetRacePaused(bool bPaused)
{
	if (bRacePaused == bPaused)
	{
		return;
	}
	// 暂停整个世界，不清掉小球的动量。倒计时里
	// 的刚体保持冻住，滚筒和其他会动的障碍也一起停。
	if (!UGameplayStatics::SetGamePaused(this, bPaused))
	{
		UE_LOG(LogMarbleRaceDirector, Warning, TEXT("无法切换世界暂停状态"));
		return;
	}
	bRacePaused = bPaused;
	if (MusicDirector)
	{
		MusicDirector->SetMusicPaused(bPaused);
	}
	UE_LOG(LogMarbleRaceDirector, Log, TEXT("比赛%s"), bPaused ? TEXT("已暂停") : TEXT("已继续"));
}

void AMarbleRaceDirector::SetVisualOverrideEnabled(bool bEnabled)
{
	bVisualOverrideEnabled = bEnabled;
	ApplyMeshVisibility();
}

void AMarbleRaceDirector::SetRacersFrozen(bool bFrozen)
{
	for (FRacerRuntime& Racer : Racers)
	{
		URaceParticipantComponent* Participant = Racer.Participant;
		if (!Participant)
		{
			continue;
		}
		UPrimitiveComponent* Primitive = Participant->GetPhysicsComponent();
		if (!Primitive)
		{
			continue;
		}

		if (bFrozen)
		{
			if (Primitive->IsSimulatingPhysics())
			{
				Primitive->SetPhysicsLinearVelocity(FVector::ZeroVector);
				Primitive->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
				Primitive->SetSimulatePhysics(false);
			}
		}
		else if (!Primitive->IsSimulatingPhysics())
		{
			Primitive->SetSimulatePhysics(true);
		}
	}
}

void AMarbleRaceDirector::ApplyMeshVisibility()
{
	const bool bHide = bVisualOverrideEnabled && bHideRacerMeshesWhenPortraitsDrawn;
	for (FRacerRuntime& Racer : Racers)
	{
		if (!Racer.Participant)
		{
			continue;
		}
		if (UPrimitiveComponent* Primitive = Racer.Participant->GetPhysicsComponent())
		{
			Primitive->SetVisibility(!bHide);
		}
	}
}

// ---------------------------------------------------------------------------
// 报名
// ---------------------------------------------------------------------------

void AMarbleRaceDirector::RegisterParticipant(URaceParticipantComponent* Participant)
{
	if (!Participant)
	{
		return;
	}
	for (const FRacerRuntime& Existing : Racers)
	{
		if (Existing.Participant == Participant)
		{
			return;
		}
	}

	FRacerRuntime Runtime;
	Runtime.Participant = Participant;

	Participant->SetRacerId(NextRacerId++);
	Participant->AssignProfileIfMissing(PickProfileForIndex(Racers.Num()));
	Participant->CaptureStartTransform();
	Participant->BeginRaceRegistration(Participant->GetStartTransform());

	if (UPrimitiveComponent* Primitive = Participant->GetPhysicsComponent())
	{
		if (Primitive->Mobility == EComponentMobility::Static)
		{
			UE_LOG(LogMarbleRaceDirector, Warning,
				TEXT("选手 %s 的网格是 Static，无法模拟物理；请在关卡中把它设为 Movable"),
				*Participant->GetDisplayName().ToString());
		}
		if (!Primitive->IsSimulatingPhysics())
		{
			Primitive->SetSimulatePhysics(true);
			Runtime.bPhysicsEnabledByDirector = true;
			UE_LOG(LogMarbleRaceDirector, Warning,
				TEXT("选手 %s 原本未启用物理，已自动启用"), *Participant->GetDisplayName().ToString());
		}
		if (bEnsureContinuousCollisionOnRacers)
		{
			Primitive->SetUseCCD(true);
		}
		Primitive->SetNotifyRigidBodyCollision(true);
		Primitive->SetPhysMaterialOverride(CreateSlipperyMaterial());
	}
	else
	{
		UE_LOG(LogMarbleRaceDirector, Warning,
			TEXT("选手 %s 找不到物理组件，将不会移动"), *Participant->GetDisplayName().ToString());
	}

	Racers.Add(Runtime);
	ApplyMeshVisibility();

	UE_LOG(LogMarbleRaceDirector, Log, TEXT("注册参赛者 %d：%s"),
		Participant->GetRacerId(), *Participant->GetDisplayName().ToString());
}

void AMarbleRaceDirector::UnregisterParticipant(URaceParticipantComponent* Participant)
{
	for (int32 Index = Racers.Num() - 1; Index >= 0; --Index)
	{
		if (Racers[Index].Participant == Participant)
		{
			Racers.RemoveAt(Index);
		}
	}
	if (Participant)
	{
		Participant->EndRaceRegistration();
	}
}

URaceCharacterProfile* AMarbleRaceDirector::PickProfileForIndex(int32 Index) const
{
	if (AutoAssignProfiles.Num() > 0)
	{
		return AutoAssignProfiles[Index % AutoAssignProfiles.Num()];
	}
	if (PlaceholderProfiles.Num() > 0)
	{
		return PlaceholderProfiles[Index % PlaceholderProfiles.Num()];
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// 进度
// ---------------------------------------------------------------------------

void AMarbleRaceDirector::UpdateRacers(float DeltaSeconds)
{
	TArray<FRacePendingFinish> PendingFinishes;
	for (int32 RacerIndex = 0; RacerIndex < Racers.Num(); ++RacerIndex)
	{
		FRacerRuntime& Racer = Racers[RacerIndex];
		URaceParticipantComponent* Participant = Racer.Participant;
		if (!Participant || !Participant->IsRegistered())
		{
			continue;
		}
		AActor* RacerActor = Participant->GetOwner();
		if (!RacerActor)
		{
			continue;
		}

		const FVector Location = RacerActor->GetActorLocation();
		if (Participant->IsFinished())
		{
			Participant->SetPreviousLocation(Location);
			continue;
		}

		const FVector Previous = Participant->GetPreviousLocation();
		UpdateOutOfBounds(Racer, Location);
		if (Participant->IsOutOfBounds())
		{
			StallSeconds.Remove(Participant->GetRacerId());
			Participant->SetPreviousLocation(Location);
			continue;
		}

		// 停住的球是死路。把它往赛道上方、朝中间弹回去，
		// 没弹中就再走一遍这一段，而不是冻在角落里。
		if (UPrimitiveComponent* Body = Participant->GetPhysicsComponent())
		{
			if (Body->GetPhysicsLinearVelocity().SizeSquared() < FMath::Square(StallSpeed))
			{
				float& Stall = StallSeconds.FindOrAdd(Participant->GetRacerId());
				Stall += DeltaSeconds;
				if (Stall >= StallBeforeNudge)
				{
					Stall = 0.0f;
					const float TowardCenter = Location.X >= 0.0f ? -220.0f : 220.0f;
					Body->SetPhysicsLinearVelocity(FVector(TowardCenter, 0.0f, 720.0f), false);
					Body->WakeRigidBody();
					UE_LOG(LogMarbleRaceDirector, Log, TEXT("选手 %s 停住了，向上弹回赛道"),
						*Participant->GetDisplayName().ToString());
				}
			}
			else
			{
				StallSeconds.Remove(Participant->GetRacerId());
			}
		}

		if (bUsingVerticalFallback)
		{
			if (Location.Z <= FallbackFinishZ)
			{
				const float Span = Previous.Z - Location.Z;
				const float Alpha = Span > UE_KINDA_SMALL_NUMBER
					? FMath::Clamp((Previous.Z - FallbackFinishZ) / Span, 0.0f, 1.0f)
					: 0.0f;
				PendingFinishes.Add({RacerIndex, Participant->GetRacerId(), RaceClock - DeltaSeconds * (1.0f - Alpha)});
			}
			else
			{
				Participant->SetSegmentProgress(EvaluateVerticalProgress(FallbackStartZ, FallbackFinishZ, Location.Z));
			}
		}
		else
		{
			float LastCrossingAlpha = 0.0f;
			while (Participant->GetNextCheckpointIndex() < Route.Num())
			{
				ARaceCheckpoint* Checkpoint = Route[Participant->GetNextCheckpointIndex()];
				if (!Checkpoint)
				{
					break;
				}
				const FRaceCrossing Crossing = EvaluateGateCrossing(Previous, Location, Checkpoint->MakeGate());
				if (!AcceptOrderedRaceCrossing(Crossing, LastCrossingAlpha))
				{
					break;
				}

				const int32 PassedIndex = Participant->GetNextCheckpointIndex();
				const bool bWasFinish = Route[PassedIndex] && Route[PassedIndex]->bIsFinish;
				Participant->SetNextCheckpointIndex(PassedIndex + 1);

				if (bWasFinish)
				{
					PendingFinishes.Add({RacerIndex, Participant->GetRacerId(), RaceClock - DeltaSeconds * (1.0f - Crossing.Alpha)});
					break;
				}
			}

			if (!Participant->IsFinished())
			{
				Participant->SetSegmentProgress(EvaluateSegmentProgress(*Participant, Location));
			}
		}

		Participant->SetPreviousLocation(Location);
	}

	SortPendingRaceFinishes(PendingFinishes);
	for (const FRacePendingFinish& Finish : PendingFinishes)
	{
		OnRacerFinished(Racers[Finish.RacerIndex], Finish.FinishTime);
	}

	// 等所有过门和淘汰都处理完再检查，包括空场和最后一名
	// 出界选手。在完赛回调里结束会跟着数组顺序变。
	if (FindFrontMostUnfinishedRacerId() == INDEX_NONE)
	{
		EnterResults();
	}
}

void AMarbleRaceDirector::UpdateOutOfBounds(FRacerRuntime& Racer, const FVector& Location)
{
	URaceParticipantComponent* Participant = Racer.Participant;
	if (!Participant || Participant->IsOutOfBounds())
	{
		return;
	}
	if (Location.Z < OutOfBoundsZ)
	{
		Participant->SetOutOfBounds(true);
		if (!Racer.bLoggedOutOfBounds)
		{
			Racer.bLoggedOutOfBounds = true;
			UE_LOG(LogMarbleRaceDirector, Warning,
				TEXT("选手 %s 掉出赛道（Z=%.0f），不再参与领跑判定，也不会获得名次"),
				*Participant->GetDisplayName().ToString(), Location.Z);
		}
	}
}

float AMarbleRaceDirector::EvaluateSegmentProgress(const URaceParticipantComponent& Participant, const FVector& Location) const
{
	const int32 Next = Participant.GetNextCheckpointIndex();
	if (Next >= Route.Num())
	{
		return 1.0f;
	}

	const ARaceCheckpoint* Checkpoint = Route[Next];
	if (!Checkpoint)
	{
		return 0.0f;
	}

	const float SplineProgress = Checkpoint->EvaluateSplineProgress(Location);
	if (SplineProgress >= 0.0f)
	{
		return SplineProgress;
	}

	const FVector SegmentStart = (Next > 0 && Route[Next - 1])
		? Route[Next - 1]->GetActorLocation()
		: Participant.GetStartTransform().GetLocation();
	return EvaluateLinearSegmentProgress(SegmentStart, Checkpoint->GetActorLocation(), Location);
}

void AMarbleRaceDirector::OnRacerFinished(FRacerRuntime& Racer, float FinishTime)
{
	URaceParticipantComponent* Participant = Racer.Participant;
	if (!Participant || Participant->IsFinished())
	{
		return;
	}

	const int32 Rank = NextFinishRank++;
	Participant->RecordFinish(FinishTime, Rank);

	FRaceResultEntry Entry;
	Entry.RacerId = Participant->GetRacerId();
	Entry.DisplayName = Participant->GetDisplayName();
	Entry.Rank = Rank;
	Entry.FinishTime = FinishTime;
	Entry.Portrait = Participant->GetPortrait();
	Entry.Color = Participant->GetDisplayColor();
	Results.Add(Entry);

	UE_LOG(LogMarbleRaceDirector, Log, TEXT("第 %d 名：%s，用时 %.2f 秒"),
		Rank + 1, *Entry.DisplayName.ToString(), FinishTime);

	if (Rank == 0)
	{
		ChampionRacerId = Participant->GetRacerId();
		ChampionCelebrationEndTime = RaceClock + FMath::Max(0.0f, ChampionCelebrationSeconds);
		bWasCelebrating = true;
		RequestThemeForRacer(ChampionRacerId, true);
	}

}

FRaceRankState AMarbleRaceDirector::MakeRankState(const FRacerRuntime& Racer) const
{
	FRaceRankState RankState;
	if (!Racer.Participant)
	{
		return RankState;
	}
	RankState.RacerId = Racer.Participant->GetRacerId();
	RankState.bFinished = Racer.Participant->IsFinished();
	RankState.FinishTime = Racer.Participant->GetFinishTime();
	RankState.LastCheckpointIndex = Racer.Participant->GetLastCheckpointIndex();
	RankState.SegmentProgress = Racer.Participant->GetSegmentProgress();
	return RankState;
}

FRaceRankState AMarbleRaceDirector::MakeRankStateById(int32 RacerId) const
{
	if (const URaceParticipantComponent* Participant = FindRacerComponent(RacerId))
	{
		for (const FRacerRuntime& Racer : Racers)
		{
			if (Racer.Participant == Participant)
			{
				return MakeRankState(Racer);
			}
		}
	}
	return FRaceRankState();
}

int32 AMarbleRaceDirector::FindFrontMostUnfinishedRacerId() const
{
	int32 BestId = INDEX_NONE;
	FRaceRankState BestState;
	for (const FRacerRuntime& Racer : Racers)
	{
		if (!Racer.Participant || !Racer.Participant->IsRegistered())
		{
			continue;
		}
		if (Racer.Participant->IsFinished() || Racer.Participant->IsOutOfBounds())
		{
			continue;
		}
		const FRaceRankState Candidate = MakeRankState(Racer);
		if (BestId == INDEX_NONE || IsRankedAhead(Candidate, BestState))
		{
			BestId = Candidate.RacerId;
			BestState = Candidate;
		}
	}
	return BestId;
}

// ---------------------------------------------------------------------------
// 音乐
// ---------------------------------------------------------------------------

void AMarbleRaceDirector::UpdateMusic(float DeltaSeconds)
{
	if (!MusicDirector || State != ERaceState::Racing)
	{
		return;
	}

	LeaderRacerId = FindFrontMostUnfinishedRacerId();

	if (IsChampionCelebrating())
	{
		bWasCelebrating = true;
		return;
	}

	if (bWasCelebrating)
	{
		// 庆祝刚结束：从未完赛的最前面那名重新确认，
		// 不用五秒前的旧目标。
		bWasCelebrating = false;
		LeaderDebounce.Reset();
		MusicTargetRacerId = INDEX_NONE;
	}

	if (bKeepChampionThemeAfterCelebration && ChampionRacerId != INDEX_NONE)
	{
		return;
	}

	const int32 CandidateId = LeaderRacerId;
	const FRaceRankState CandidateState = MakeRankStateById(CandidateId);
	const int32 CommittedId = LeaderDebounce.GetCommittedId();
	const FRaceRankState CommittedState = MakeRankStateById(CommittedId);
	const URaceParticipantComponent* CommittedParticipant = FindRacerComponent(CommittedId);

	bool bMeetsMargin = false;
	if (!CommittedParticipant || !CommittedParticipant->IsRegistered() || CommittedParticipant->IsOutOfBounds()
		|| CommittedId == INDEX_NONE || CommittedState.RacerId == INDEX_NONE
		|| CommittedState.bFinished || CommittedState.RacerId == ChampionRacerId)
	{
		bMeetsMargin = true;
	}
	else if (CandidateId != INDEX_NONE)
	{
		if (CandidateState.LastCheckpointIndex != CommittedState.LastCheckpointIndex)
		{
			bMeetsMargin = CandidateState.LastCheckpointIndex > CommittedState.LastCheckpointIndex;
		}
		else
		{
			bMeetsMargin = (CandidateState.SegmentProgress - CommittedState.SegmentProgress) >= LeaderProgressMargin;
		}
	}

	int32 NewCommittedId = INDEX_NONE;
	if (LeaderDebounce.Update(CandidateId, bMeetsMargin, DeltaSeconds, LeaderConfirmSeconds, NewCommittedId))
	{
		if (NewCommittedId != MusicTargetRacerId)
		{
			RequestThemeForRacer(NewCommittedId, false);
		}
	}
}

void AMarbleRaceDirector::RequestThemeForRacer(int32 RacerId, bool bForce)
{
	if (!MusicDirector)
	{
		return;
	}

	URaceParticipantComponent* Participant = FindRacerComponent(RacerId);
	URaceCharacterProfile* Profile = Participant ? Participant->GetCharacterProfile() : nullptr;
	USoundBase* Sound = Profile ? Profile->ThemeMusic : nullptr;

	MusicDirector->RequestTheme(
		Sound,
		Profile ? Profile->MusicStartOffset : 0.0f,
		Profile ? Profile->MusicGain : 1.0f,
		bForce);

	MusicTargetRacerId = RacerId;

	UE_LOG(LogMarbleRaceDirector, Log, TEXT("主题音乐目标 -> %s（%s）"),
		Participant ? *Participant->GetDisplayName().ToString() : TEXT("<无参赛者>"),
		Sound ? *Sound->GetName() : TEXT("默认曲或静音"));
}

void AMarbleRaceDirector::DebugRequestThemeForRacer(int32 RacerId)
{
	UE_LOG(LogMarbleRaceDirector, Log, TEXT("调试：手动请求选手 %d 的主题曲"), RacerId);
	RequestThemeForRacer(RacerId, true);
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

int32 AMarbleRaceDirector::GetLeaderRacerId() const
{
	return FindFrontMostUnfinishedRacerId();
}

bool AMarbleRaceDirector::IsChampionCelebrating() const
{
	return ChampionRacerId != INDEX_NONE
		&& State == ERaceState::Racing
		&& RaceClock < ChampionCelebrationEndTime;
}

FText AMarbleRaceDirector::GetMusicTargetDisplayName() const
{
	if (const URaceParticipantComponent* Participant = FindRacerComponent(MusicTargetRacerId))
	{
		return Participant->GetDisplayName();
	}
	return FText::FromString(TEXT("默认曲"));
}

ARaceCheckpoint* AMarbleRaceDirector::GetCheckpointAt(int32 Index) const
{
	return Route.IsValidIndex(Index) ? Route[Index].Get() : nullptr;
}

AActor* AMarbleRaceDirector::GetCameraTargetActor() const
{
	// 镜头跟着当前最前面的未完赛小球，不做防抖。
	// 领跑防抖是为了主题曲不在选手之间来回闪；拿它来
	// 跟镜头，相机会落后或停在音乐已经认定的那个人身上，
	// 那和看着第一名不是一回事。
	const int32 LeaderId = FindFrontMostUnfinishedRacerId();
	const URaceParticipantComponent* Target = LeaderId != INDEX_NONE ? FindRacerComponent(LeaderId) : nullptr;

	// 全部完赛：退回冠军，镜头留在获胜者身上。
	if (!Target && ChampionRacerId != INDEX_NONE)
	{
		Target = FindRacerComponent(ChampionRacerId);
	}
	if (!Target && Racers.Num() > 0)
	{
		Target = Racers[0].Participant;
	}
	return Target ? Target->GetOwner() : nullptr;
}

URaceParticipantComponent* AMarbleRaceDirector::FindRacerComponent(int32 RacerId) const
{
	if (RacerId == INDEX_NONE)
	{
		return nullptr;
	}
	for (const FRacerRuntime& Racer : Racers)
	{
		if (Racer.Participant && Racer.Participant->GetRacerId() == RacerId)
		{
			return Racer.Participant.Get();
		}
	}
	return nullptr;
}
