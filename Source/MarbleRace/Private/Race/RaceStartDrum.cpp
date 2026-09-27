#include "Race/RaceStartDrum.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Race/RaceParticipantComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 引擎自带的立方体，每边 100，中心在原点。 */
	const TCHAR* GCubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	constexpr float GCubeExtent = 100.0f;

	/** 工程里的扁平黑赛道材质，让滚筒和赛道看起来一致。 */
	const TCHAR* GDefaultDrumMaterialPath = TEXT("/Game/Materials/M_2DTrack.M_2DTrack");
}

ARaceStartDrum::ARaceStartDrum()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Rotor = CreateDefaultSubobject<USceneComponent>(TEXT("Rotor"));
	Rotor->SetupAttachment(Root);

	// 构造时查找：这里是安全的，也能让滚筒
	// 和赛道其余部分外观一致，不必手填材质。
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DrumMaterialFinder(GDefaultDrumMaterialPath);
	if (DrumMaterialFinder.Succeeded())
	{
		DrumMaterial = DrumMaterialFinder.Object;
	}

	// 旧环形网格已删除。滚筒网格由蓝图指定，
	// 构造期再去查找会在每次启动时报错。
}

namespace
{
	bool UsesAuthoredMeshScale(const UStaticMesh* Mesh)
	{
		return Mesh && !Mesh->GetPathName().Contains(TEXT("SM_RaceStartRing"));
	}
}

UStaticMesh* ARaceStartDrum::ResolveCubeMesh()
{
	if (CubeMesh)
	{
		return CubeMesh;
	}

	// 用 LoadObject，不用构造期查找器：滚筒在运行时生成、
	// 或从细节面板重建时，构造期查找器已经不能用。
	CubeMesh = LoadObject<UStaticMesh>(nullptr, GCubeMeshPath);
	if (!CubeMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("开局滚筒找不到引擎基础方块网格：%s"), GCubeMeshPath);
	}
	return CubeMesh;
}

float ARaceStartDrum::GetHoldingRadius(float RacerRadius) const
{
	const float SafeRadius = FMath::Max(60.0f, Radius);
	const float SafeRacerRadius = FMath::Max(1.0f, RacerRadius);
	// 在拨片内侧、离开环壁，和摆放选手用的是同一选择。
	return FMath::Max(GetBladeLength() + SafeRacerRadius + 4.0f, SafeRadius - SafeRacerRadius - 6.0f);
}

float ARaceStartDrum::GetBladeLength() const
{
	// 环形网格上的拨片伸到内半径的 60%；备用
	// 方块也用这个比例，两种外形下小球的摆放位置相同。
	return FMath::Max(60.0f, Radius) * 0.6f;
}

int32 ARaceStartDrum::ComputeShapeSignature() const
{
	return FMath::RoundToInt(Radius) * 7919
		+ FMath::RoundToInt(BandThickness) * 131
		+ FMath::RoundToInt(DrumDepth) * 17
		+ RimSegments * 3
		+ BladeCount * 101
		+ FMath::RoundToInt(BladeThickness);
}

void ARaceStartDrum::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (ComputeShapeSignature() != BuiltSignature)
	{
		RebuildGeometry();
	}
}

void ARaceStartDrum::BeginPlay()
{
	Super::BeginPlay();

	// 运行的世界里总是重建：关卡里摆好的滚筒会被
	// PIE 复制，这样无论副本做了什么，环和碰撞都在。
	RebuildGeometry();
}

void ARaceStartDrum::AddPiece(const FTransform& LocalTransform, const FVector& Scale, bool bVisible)
{
	UStaticMesh* Mesh = ResolveCubeMesh();
	if (!Mesh)
	{
		return;
	}

	UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(this);
	// 故意不做瞬态：PIE 会复制关卡演员并跳过瞬态
	// 组件，手摆的滚筒在运行时就会没有环、也没有碰撞。
	// 重建外形会先清掉全部网格组件，所以不会越积越多。
	Piece->SetMobility(EComponentMobility::Movable);
	Piece->SetupAttachment(Rotor);
	Piece->RegisterComponent();
	Piece->AttachToComponent(Rotor, FAttachmentTransformRules::KeepRelativeTransform);
	Piece->SetStaticMesh(Mesh);
	Piece->SetRelativeTransform(FTransform(LocalTransform.GetRotation(), LocalTransform.GetLocation(), Scale));
	Piece->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Piece->SetCollisionProfileName(TEXT("BlockAll"));
	Piece->SetSimulatePhysics(false);
	Piece->SetEnableGravity(false);
	Piece->SetGenerateOverlapEvents(false);
	if (!bVisible)
	{
		// 不可见碰撞体：玩家看见的是导入的那只环。
		Piece->SetVisibility(false);
		Piece->SetHiddenInGame(true);
	}

	if (DrumMaterial)
	{
		Piece->SetMaterial(0, DrumMaterial);
	}

	Pieces.Add(Piece);
}

void ARaceStartDrum::RebuildGeometry()
{
	// 清掉这个演员上的全部网格组件，不只是这次新建的：
	// 手摆的滚筒可能从关卡里加载了以前生成的零件。
	TArray<UStaticMeshComponent*> ExistingPieces;
	GetComponents<UStaticMeshComponent>(ExistingPieces);
	for (UStaticMeshComponent* Piece : ExistingPieces)
	{
		if (Piece)
		{
			Piece->DestroyComponent();
		}
	}
	Pieces.Reset();
	RingMesh = nullptr;

	const float SafeRadius = FMath::Max(60.0f, Radius);
	const float SafeThickness = FMath::Max(2.0f, BandThickness);
	const float SafeDepth = FMath::Max(10.0f, DrumDepth);

	if (!DrumMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("开局滚筒没有指定网格，退化为可见方块拼接"));
		RebuildFallbackBlocks(SafeRadius, SafeThickness, SafeDepth, /*bVisible=*/true);
		BuiltSignature = ComputeShapeSignature();
		return;
	}

	// 旧的环形网格按内半径 100 制作，下面会按半径缩放。
	// 自己挖空的圆柱用网格原尺寸，半径只决定小球贴在哪一圈。
	RingMesh = NewObject<UStaticMeshComponent>(this);
	RingMesh->SetMobility(EComponentMobility::Movable);
	RingMesh->SetupAttachment(Rotor);
	RingMesh->RegisterComponent();
	RingMesh->AttachToComponent(Rotor, FAttachmentTransformRules::KeepRelativeTransform);
	RingMesh->SetStaticMesh(DrumMesh);
	RingMesh->SetRelativeLocation(FVector::ZeroVector);
	// 旧环按内半径 100 制作，圆已经在 XZ 平面上，所以要按半径缩放。
	// 自己挖空的圆柱轴在网格的 Z 上，尺寸已经定好：保持 1 倍，
	// 再把轴转到局部 Y，和下面的碰撞环、比赛平面对齐。
	const bool bAuthoredMesh = UsesAuthoredMeshScale(DrumMesh);
	const FVector MeshScale = bAuthoredMesh
		? FVector::OneVector
		: FVector(SafeRadius / 100.0f, SafeDepth / 100.0f, SafeRadius / 100.0f);
	RingMesh->SetRelativeRotation(bAuthoredMesh
		? FQuat(FVector::XAxisVector, FMath::DegreesToRadians(-90.0f))
		: FQuat::Identity);
	RingMesh->SetRelativeScale3D(MeshScale);
	if (bAuthoredMesh)
	{
		const FRotator ActorRotation = GetActorRotation();
		if (FMath::IsNearlyEqual(FMath::Abs(ActorRotation.Roll), 90.0f, 2.0f))
		{
			// 静态网格阶段用 Roll 90 把圆柱立到侧面。
			// 组件自己转好之后，演员上再留这个角度会转两次。
			SetActorRotation(FRotator(ActorRotation.Pitch, ActorRotation.Yaw, 0.0f));
		}
	}
	// 只负责外观：网格是一整圈，碰撞来自下面
	// 那圈不可见方块。逐多边形碰撞能保住中间的洞，
	// 但引擎只允许静态形体使用；生成的凸包
	// 会留缝，小球会从底部漏出去。
	RingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RingMesh->SetSimulatePhysics(false);
	RingMesh->SetEnableGravity(false);
	RingMesh->SetGenerateOverlapEvents(false);

	if (DrumMaterial)
	{
		RingMesh->SetMaterial(0, DrumMaterial);
	}

	RebuildFallbackBlocks(SafeRadius, SafeThickness, SafeDepth, /*bVisible=*/false);
	BuiltSignature = ComputeShapeSignature();
}

void ARaceStartDrum::RebuildFallbackBlocks(float SafeRadius, float SafeThickness, float SafeDepth, bool bVisible)
{
	const int32 SafeSegments = FMath::Max(6, RimSegments);
	const float BandCenterRadius = SafeRadius + SafeThickness * 0.5f;

	// 方块围在 XZ 平面的环上。局部 X 朝外（径向），
	// 局部 Y 沿转轴，局部 Z 沿切线。
	const float SegmentLength = (2.0f * UE_PI * BandCenterRadius) / static_cast<float>(SafeSegments) * 1.15f;
	for (int32 Index = 0; Index < SafeSegments; ++Index)
	{
		const float Angle = (2.0f * UE_PI * static_cast<float>(Index)) / static_cast<float>(SafeSegments);
		const FVector RadialDirection(FMath::Cos(Angle), 0.0f, FMath::Sin(Angle));
		// 取负：绕 +Y 旋转会把局部 X 转到 (cos, 0, -sin)，正角度会
		// 把每块都歪到另一边，环就合不成圆。
		const FQuat SegmentRotation(FVector::YAxisVector, -Angle);

		const FTransform LocalTransform(
			SegmentRotation,
			RadialDirection * BandCenterRadius,
			FVector::OneVector);
		AddPiece(LocalTransform, FVector(SafeThickness / GCubeExtent, SafeDepth / GCubeExtent, SegmentLength / GCubeExtent), bVisible);
	}

	// 拨片已经做进导入的环形网格，碰撞环不再另加
	// 它们：生在拨片里面的球会被挤出时甩飞。
	const int32 SafeBlades = bVisible ? FMath::Clamp(BladeCount, 0, 12) : 0;
	const float BladeLength = GetBladeLength();
	for (int32 Index = 0; Index < SafeBlades; ++Index)
	{
		const float Angle = (2.0f * UE_PI * (static_cast<float>(Index) + 0.5f)) / static_cast<float>(SafeBlades);
		const FVector RadialDirection(FMath::Cos(Angle), 0.0f, FMath::Sin(Angle));
		const FQuat BladeRotation(FVector::YAxisVector, -Angle);

		const FTransform LocalTransform(
			BladeRotation,
			RadialDirection * (BladeLength * 0.5f),
			FVector::OneVector);
		AddPiece(LocalTransform, FVector(BladeLength / GCubeExtent, SafeDepth / GCubeExtent, BladeThickness / GCubeExtent), bVisible);
	}
}

void ARaceStartDrum::SetPiecesCollisionEnabled(bool bEnabled)
{
	if (RingMesh)
	{
		// 导入网格只负责看，重置滚筒和开始旋转之后也一样。
		// 再打开它的凸包会把洞填上，也和环壁碰撞体打架。
		RingMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	for (UStaticMeshComponent* Piece : Pieces)
	{
		if (Piece)
		{
			Piece->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		}
	}
}

void ARaceStartDrum::BeginSpin(float SpeedDegrees)
{
	SpinSpeedDegrees = SpeedDegrees;
	SetPiecesCollisionEnabled(true);
	bSpinning = true;
}

void ARaceStartDrum::ReleaseDrum()
{
	// 关掉碰撞就是松手：小球从底部掉出去。
	SetPiecesCollisionEnabled(false);
	bSpinning = bKeepSpinningAfterRelease;
}

void ARaceStartDrum::ResetDrum()
{
	bSpinning = false;
	CurrentSpinDegrees = 0.0f;
	if (Rotor)
	{
		Rotor->SetRelativeRotation(FQuat::Identity);
	}
	SetPiecesCollisionEnabled(true);
}

void ARaceStartDrum::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bSpinning || !Rotor || DeltaSeconds <= 0.0f)
	{
		return;
	}

	// 绕局部 Y 轴转：比赛平面是 XZ，Y 就是轴。
	const float DeltaDegrees = SpinSpeedDegrees * DeltaSeconds;
	CurrentSpinDegrees = FMath::Fmod(CurrentSpinDegrees + DeltaDegrees, 360.0f);
	Rotor->AddLocalRotation(FQuat(FVector::YAxisVector, FMath::DegreesToRadians(DeltaDegrees)));
}

void ARaceStartDrum::PlaceRacer(AActor* Racer, int32 Index, int32 Total, float RacerRadius, bool bRandomizeAngle)
{
	if (!Racer)
	{
		return;
	}

	const float SafeRadius = FMath::Max(60.0f, Radius);
	const float SafeRacerRadius = FMath::Max(1.0f, RacerRadius);
	const int32 SafeTotal = FMath::Max(1, Total);
	const int32 SafeIndex = FMath::Max(0, Index);

	// 均匀的角度把小球隔开；随机来自旋转本身。
	const float BaseAngle = (2.0f * UE_PI * static_cast<float>(SafeIndex)) / static_cast<float>(SafeTotal);
	const float Jitter = bRandomizeAngle ? FMath::DegreesToRadians(PlacementJitterDegrees) : 0.0f;
	const float Angle = BaseAngle + (Jitter > 0.0f ? FMath::FRandRange(-Jitter, Jitter) : 0.0f);

	// 贴在内壁上，明显离开拨片尖端，避免一开始就嵌进去。
	const float PlaceRadius = FMath::Max(GetBladeLength() + SafeRacerRadius + 4.0f,
		SafeRadius - SafeRacerRadius - 6.0f);
	const FVector LocalOffset(FMath::Cos(Angle) * PlaceRadius, 0.0f, FMath::Sin(Angle) * PlaceRadius);

	const FVector DrumLocation = GetActorLocation();
	const FVector TargetLocation(DrumLocation.X + LocalOffset.X, DrumLocation.Y, DrumLocation.Z + LocalOffset.Z);

	Racer->SetActorLocation(TargetLocation, false, nullptr, ETeleportType::TeleportPhysics);

	// 清掉上一局留下的运动，结果由滚筒决定。
	if (URaceParticipantComponent* Participant = Racer->FindComponentByClass<URaceParticipantComponent>())
	{
		if (UPrimitiveComponent* Body = Participant->GetPhysicsComponent())
		{
			Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		}
	}
}
