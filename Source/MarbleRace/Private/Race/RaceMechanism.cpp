#include "Race/RaceMechanism.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"

namespace
{
	const TCHAR* GCubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* GTrackMaterialPath = TEXT("/Game/Materials/M_2DTrack.M_2DTrack");
	const TCHAR* GShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	constexpr float GCubeEdge = 100.0f;
	FName RacerTag()
	{
		static const FName Tag(TEXT("RaceMarble"));
		return Tag;
	}

	float ExitSignFromPitch(float PitchDegrees)
	{
		// 负俯仰往右侧泄，和左侧层板一致。
		return PitchDegrees <= 0.0f ? 1.0f : -1.0f;
	}

	void LaunchMarble(UPrimitiveComponent* Other, float ExitSign, float UpSpeed, float SpeedCap)
	{
		FVector Velocity = Other->GetPhysicsLinearVelocity();
		if (FMath::Abs(Velocity.X) < 220.0f)
		{
			Velocity.X = ExitSign * 260.0f;
		}
		Velocity.Y = 0.0f;
		Velocity.Z = FMath::Max(Velocity.Z, UpSpeed);
		const float Speed = Velocity.Size();
		if (Speed > SpeedCap && Speed > UE_KINDA_SMALL_NUMBER)
		{
			Velocity *= SpeedCap / Speed;
		}
		Other->SetPhysicsLinearVelocity(Velocity, false);
		Other->WakeRigidBody();
	}
}

ARaceMechanism::ARaceMechanism()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

void ARaceMechanism::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildBody();
}

void ARaceMechanism::BeginPlay()
{
	Super::BeginPlay();
	RebuildBody();
	MotionOrigin = GetActorLocation();
	ConfigureMotion();
}

void ARaceMechanism::RebuildBody()
{
	if (Body)
	{
		Body->DestroyComponent();
		Body = nullptr;
	}

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, GCubeMeshPath);
	if (!Cube)
	{
		return;
	}

	Body = NewObject<UStaticMeshComponent>(this);
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetupAttachment(SceneRoot);
	Body->RegisterComponent();
	Body->SetStaticMesh(Cube);
	Body->SetRelativeLocation(FVector::ZeroVector);
	Body->SetRelativeRotation(FRotator::ZeroRotator);
	const FVector SafeSize(
		FMath::Max(4.0f, Size.X),
		FMath::Max(4.0f, Size.Y),
		FMath::Max(4.0f, Size.Z));
	Body->SetRelativeScale3D(SafeSize / GCubeEdge);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Body->SetNotifyRigidBodyCollision(true);
	Body->SetSimulatePhysics(false);
	Body->SetEnableGravity(false);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCastShadow(false);
	Body->SetCanEverAffectNavigation(false);

	if (UMaterialInterface* Parent = ResolveColorMaterial())
	{
		if (UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(Parent, this))
		{
			Tint->SetVectorParameterValue(TEXT("Color"), Color);
			Tint->SetVectorParameterValue(TEXT("BaseColor"), Color);
			Body->SetMaterial(0, Tint);
		}
		else
		{
			Body->SetMaterial(0, Parent);
		}
	}

	Body->OnComponentHit.RemoveDynamic(this, &ARaceMechanism::HandleHit);
	if (Mode == ERaceMechanismMode::Bounce)
	{
		Body->OnComponentHit.AddDynamic(this, &ARaceMechanism::HandleHit);
	}
}

UMaterialInterface* ARaceMechanism::ResolveColorMaterial() const
{
	// 赛道材质是别处也在用的无光照黑。上色的零件
	// 退回引擎形状材质，那个材质才真有颜色参数。
	if (Mode == ERaceMechanismMode::Spin)
	{
		if (UMaterialInterface* Track = LoadObject<UMaterialInterface>(nullptr, GTrackMaterialPath))
		{
			return Track;
		}
	}
	if (UMaterialInterface* Shape = LoadObject<UMaterialInterface>(nullptr, GShapeMaterialPath))
	{
		return Shape;
	}
	return LoadObject<UMaterialInterface>(nullptr, GTrackMaterialPath);
}

void ARaceMechanism::ConfigureMotion()
{
	if (!Body || Mode == ERaceMechanismMode::Bounce)
	{
		if (Body && Mode == ERaceMechanismMode::Bounce)
		{
			SurfaceMaterial = NewObject<UPhysicalMaterial>(this);
			SurfaceMaterial->Friction = 0.05f;
			SurfaceMaterial->StaticFriction = 0.05f;
			SurfaceMaterial->Restitution = 0.0f;
			SurfaceMaterial->FrictionCombineMode = EFrictionCombineMode::Min;
			SurfaceMaterial->RestitutionCombineMode = EFrictionCombineMode::Min;
			Body->SetPhysMaterialOverride(SurfaceMaterial);
		}
		return;
	}

	Body->SetSimulatePhysics(true);
	Body->SetEnableGravity(false);
	Body->SetMassOverrideInKg(NAME_None, 8000.0f, true);
	Body->SetLinearDamping(0.0f);
	Body->SetAngularDamping(Mode == ERaceMechanismMode::Spin ? 0.0f : 8.0f);
	if (FBodyInstance* Instance = Body->GetBodyInstance())
	{
		// 六自由度会遵守锁定标记。往复件保持直立；拨板
		// 只能绕世界 Y 转，扫过的是 XZ 赛道平面。
		Instance->bLockXTranslation = Mode == ERaceMechanismMode::Spin;
		Instance->bLockYTranslation = true;
		Instance->bLockZTranslation = Mode == ERaceMechanismMode::Spin;
		Instance->bLockXRotation = true;
		Instance->bLockYRotation = Mode != ERaceMechanismMode::Spin;
		Instance->bLockZRotation = true;
		Instance->SetDOFLock(EDOFMode::SixDOF);
	}
	Body->WakeRigidBody();
}

void ARaceMechanism::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	(void)DeltaSeconds;
	if (!Body)
	{
		return;
	}

	if (Mode == ERaceMechanismMode::Bounce)
	{
		UWorld* World = GetWorld();
		if (!World)
		{
			return;
		}
		// 停在垫子上的球不会再产生碰撞，所以
		// 静止接触要靠重叠检测弹起来，否则会一直停着。
		const float Now = World->GetTimeSeconds();
		const FVector Extent(
			FMath::Max(4.0f, Size.X) * 0.5f,
			FMath::Max(4.0f, Size.Y) * 0.5f,
			FMath::Max(4.0f, Size.Z) * 0.5f + 18.0f);
		TArray<FOverlapResult> Overlaps;
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BouncePad), false, this);
		World->OverlapMultiByObjectType(
			Overlaps,
			Body->GetComponentLocation(),
			Body->GetComponentQuat(),
			Objects,
			FCollisionShape::MakeBox(Extent),
			Params);
		const float ExitSign = ExitSignFromPitch(GetActorRotation().Pitch);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			UPrimitiveComponent* Other = Overlap.GetComponent();
			AActor* OtherActor = Other ? Other->GetOwner() : nullptr;
			if (!Other || !OtherActor || Other == Body || !Other->IsSimulatingPhysics())
			{
				continue;
			}
			if (!OtherActor->ActorHasTag(RacerTag()))
			{
				continue;
			}
			if (const float* LastTime = BounceCooldowns.Find(Other))
			{
				if (Now - *LastTime < BounceCooldownSeconds)
				{
					continue;
				}
			}
			const FVector Velocity = Other->GetPhysicsLinearVelocity();
			if (Velocity.SizeSquared() > FMath::Square(90.0f))
			{
				continue;
			}
			LaunchMarble(Other, ExitSign, BounceSpeed, FMath::Max(BounceSpeed, MaxCarrySpeed));
			BounceCooldowns.Add(Other, Now);
		}
		return;
	}

	if (!Body->IsSimulatingPhysics())
	{
		return;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	FVector DesiredLocation = MotionOrigin;
	FVector DesiredVelocity = FVector::ZeroVector;

	if (Mode == ERaceMechanismMode::Oscillate)
	{
		const float Period = FMath::Max(0.2f, OscillatePeriod);
		const float Omega = 2.0f * UE_PI / Period;
		const float Angle = Omega * (Now + PhaseSeconds);
		const FVector Axis = bOscillateAlongX ? FVector::XAxisVector : FVector::ZAxisVector;
		DesiredLocation = MotionOrigin + Axis * OscillateAmplitude * FMath::Sin(Angle);
		DesiredVelocity = Axis * OscillateAmplitude * Omega * FMath::Cos(Angle);
	}

	const FVector Correction = (DesiredLocation - Body->GetComponentLocation()) * 8.0f;
	FVector Velocity = DesiredVelocity + Correction;
	Velocity.Y = (MotionOrigin.Y - Body->GetComponentLocation().Y) * 8.0f;
	Body->SetPhysicsLinearVelocity(Velocity, false);

	if (Mode == ERaceMechanismMode::Spin)
	{
		Body->SetPhysicsAngularVelocityInDegrees(FVector(0.0f, SpinDegreesPerSecond, 0.0f), false);
	}
	else
	{
		Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector, false);
	}
}

void ARaceMechanism::HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	(void)HitComponent;
	(void)OtherActor;
	(void)NormalImpulse;
	if (Mode != ERaceMechanismMode::Bounce || !OtherComp || !OtherComp->IsSimulatingPhysics())
	{
		return;
	}
	// 侧面擦过不加能量。只有落在顶面才弹。
	if (Hit.ImpactNormal.Z < 0.45f)
	{
		return;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if (const float* LastTime = BounceCooldowns.Find(OtherComp))
	{
		if (Now - *LastTime < BounceCooldownSeconds)
		{
			return;
		}
	}

	FVector Velocity = OtherComp->GetPhysicsLinearVelocity();
	if (Velocity.Z > 120.0f)
	{
		return;
	}

	LaunchMarble(OtherComp, ExitSignFromPitch(GetActorRotation().Pitch), BounceSpeed, FMath::Max(BounceSpeed, MaxCarrySpeed));
	BounceCooldowns.Add(OtherComp, Now);
}
