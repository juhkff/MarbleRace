#include "Race/PlatformMoveComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

UPlatformMoveComponent::UPlatformMoveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	USceneComponent::SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FClassFinder<AActor> MarbleFinder(TEXT("/Game/角色/弹珠"));
	if (MarbleFinder.Succeeded())
	{
		MarbleClass = MarbleFinder.Class;
	}
}

void UPlatformMoveComponent::BeginPlay()
{
	Super::BeginPlay();

	if (USceneComponent* Parent = GetAttachParent())
	{
		Parent->SetMobility(EComponentMobility::Movable);
		Origin = Parent->GetRelativeLocation();
	}
	FindSideWalls();
}

void UPlatformMoveComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const FMarbleEscape& Escape : ActiveEscapes)
	{
		FinishEscape(Escape);
	}
	ActiveEscapes.Empty();
	Super::EndPlay(EndPlayReason);
}

void UPlatformMoveComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
                                           FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateEscapingMarbles(DeltaTime);

	USceneComponent* Parent = GetAttachParent();
	const float CycleDuration = RightMoveTime + LeftMoveTime;
	if (!Parent || CycleDuration <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	Elapsed += DeltaTime;
	const float CycleTime = FMath::Fmod(Elapsed, CycleDuration);

	float Offset = 0.f;
	if (CycleTime < RightMoveTime)
	{
		// 正方向均分两段，时间也均分。每段速度为 cos(0→π/2)，先快后慢。
		const float SegmentDuration = RightMoveTime * 0.5f;
		const bool bSecondSegment = CycleTime >= SegmentDuration;
		const float SegmentTime = bSecondSegment ? CycleTime - SegmentDuration : CycleTime;
		const float SegmentAlpha = SegmentDuration > KINDA_SMALL_NUMBER ? SegmentTime / SegmentDuration : 1.f;
		const float SegmentOffset = Distance * 0.5f * FMath::Sin(SegmentAlpha * PI * 0.5f);
		Offset = (bSecondSegment ? Distance * 0.5f : 0.f) + SegmentOffset;
	}
	else
	{
		const float ReturnAlpha = LeftMoveTime > KINDA_SMALL_NUMBER
			? (CycleTime - RightMoveTime) / LeftMoveTime
			: 1.f;
		Offset = Distance * (1.f - ReturnAlpha);
	}

	FVector Delta = FVector::ZeroVector;
	switch (MoveAxis)
	{
	case EAxis::Y:
		Delta.Y = Offset;
		break;
	case EAxis::Z:
		Delta.Z = Offset;
		break;
	default:
		Delta.X = Offset;
		break;
	}

	const FVector NextRelativeLocation = Origin + Delta;
	const FVector RelativeMove = NextRelativeLocation - Parent->GetRelativeLocation();
	const FVector WorldMove = Parent->GetAttachParent()
		? Parent->GetAttachParent()->GetComponentTransform().TransformVector(RelativeMove)
		: RelativeMove;
	if (UPrimitiveComponent* Platform = Cast<UPrimitiveComponent>(Parent))
	{
		DetectSideWallPinch(*Platform, WorldMove);
	}

	// 保持所有平板原有的同步轨迹；防夹逻辑只作用于弹珠。
	Parent->SetRelativeLocation(NextRelativeLocation);
}

void UPlatformMoveComponent::FindSideWalls()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 优先使用边墙标签；旧关卡没有标签时，只认高而窄的左右边界墙。
	float LeftHeight = 0.f;
	float RightHeight = 0.f;
	bool bTaggedLeft = false;
	bool bTaggedRight = false;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		AStaticMeshActor* Actor = *It;
		UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
		if (!Mesh || Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision ||
		    Mesh->GetCollisionResponseToChannel(ECC_PhysicsBody) != ECR_Block)
		{
			continue;
		}

		const FBox CandidateBox = Mesh->Bounds.GetBox();
		const bool bLeftTag = Actor->ActorHasTag(TEXT("MarbleRace.LeftWall"));
		const bool bRightTag = Actor->ActorHasTag(TEXT("MarbleRace.RightWall"));
		if (!bLeftTag && !bRightTag && (CandidateBox.GetSize().Z < 5000.f || CandidateBox.GetSize().X > 400.f))
		{
			continue;
		}

		if ((bLeftTag || (!bRightTag && CandidateBox.GetCenter().X < 0.f)) &&
		    (!LeftWall.IsValid() || (bLeftTag && !bTaggedLeft) ||
		     (bLeftTag == bTaggedLeft && CandidateBox.GetSize().Z > LeftHeight)))
		{
			LeftWall = Actor;
			LeftHeight = CandidateBox.GetSize().Z;
			bTaggedLeft = bLeftTag;
		}
		if ((bRightTag || (!bLeftTag && CandidateBox.GetCenter().X > 0.f)) &&
		    (!RightWall.IsValid() || (bRightTag && !bTaggedRight) ||
		     (bRightTag == bTaggedRight && CandidateBox.GetSize().Z > RightHeight)))
		{
			RightWall = Actor;
			RightHeight = CandidateBox.GetSize().Z;
			bTaggedRight = bRightTag;
		}
	}
}

void UPlatformMoveComponent::DetectSideWallPinch(UPrimitiveComponent& Platform, const FVector& WorldMove)
{
	if (!MarbleClass || FMath::Abs(WorldMove.X) <= KINDA_SMALL_NUMBER ||
	    FMath::Abs(WorldMove.X) < FMath::Abs(WorldMove.Y) + FMath::Abs(WorldMove.Z))
	{
		return;
	}

	const bool bMovingRight = WorldMove.X > 0.f;
	AStaticMeshActor* Wall = bMovingRight ? RightWall.Get() : LeftWall.Get();
	if (!Wall || !Wall->GetStaticMeshComponent())
	{
		return;
	}

	const FBox WallBox = Wall->GetStaticMeshComponent()->Bounds.GetBox();
	const FBox PlatformBox = Platform.Bounds.GetBox();
	const float WallFace = bMovingRight ? WallBox.Min.X : WallBox.Max.X;
	const float PlatformFace = bMovingRight ? PlatformBox.Max.X : PlatformBox.Min.X;
	const float GapNow = bMovingRight ? WallFace - PlatformFace : PlatformFace - WallFace;
	const float GapNext = GapNow - FMath::Abs(WorldMove.X);
	if (GapNext >= GapNow)
	{
		return;
	}
	// 某些已摆放的平板从一开始就伸到侧墙外，间隙可能为负；不能因此漏掉墙边弹珠。

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 仅查询平板前缘与选中的左右边墙之间的弹珠，而不是扫描所有障碍物。
	const FVector SearchCenter((PlatformFace + WallFace) * 0.5f,
	                           PlatformBox.GetCenter().Y, PlatformBox.GetCenter().Z);
	const FVector SearchExtent(FMath::Abs(GapNow) * 0.5f + 250.f,
	                           PlatformBox.GetExtent().Y + 250.f,
	                           PlatformBox.GetExtent().Z + 250.f);
	FCollisionObjectQueryParams MarbleObjects;
	MarbleObjects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PlatformWallPinch));
	Params.AddIgnoredActor(GetOwner());
	TArray<FOverlapResult> NearbyBodies;
	if (!World->OverlapMultiByObjectType(NearbyBodies, SearchCenter, FQuat::Identity,
	                                    MarbleObjects, FCollisionShape::MakeBox(SearchExtent), Params))
	{
		return;
	}

	TSet<UPrimitiveComponent*> SeenBodies;
	for (const FOverlapResult& Overlap : NearbyBodies)
	{
		AActor* OtherActor = Overlap.GetActor();
		UPrimitiveComponent* Body = Overlap.GetComponent();
		if (!OtherActor || !OtherActor->IsA(MarbleClass) || !Body ||
		    !Body->IsSimulatingPhysics() || SeenBodies.Contains(Body))
		{
			continue;
		}
		SeenBodies.Add(Body);

		const FVector Center = Body->Bounds.Origin;
		// 球的 X/Z 投影半径；Bounds.SphereRadius 是外接球，不能作为物理半径。
		const float Radius = FMath::Max(Body->Bounds.BoxExtent.X, Body->Bounds.BoxExtent.Z);
		if (Radius <= KINDA_SMALL_NUMBER || GapNext > 2.f * Radius + PinchClearance ||
		    Center.Y + Radius < WallBox.Min.Y || Center.Y - Radius > WallBox.Max.Y ||
		    Center.Z + Radius < WallBox.Min.Z || Center.Z - Radius > WallBox.Max.Z ||
		    Center.Z > PlatformBox.Max.Z + Radius * 0.25f ||
		    Center.Z < PlatformBox.Min.Z - Radius * 0.25f)
		{
			continue;
		}

		const float DistanceFromFront = bMovingRight ? Center.X - PlatformFace : PlatformFace - Center.X;
		const float DistanceFromWall = bMovingRight ? WallFace - Center.X : Center.X - WallFace;
		const float AllowedBehindFront = GapNow <= 0.f ? PlatformBox.GetSize().X : Radius * 0.25f;
		const FBox NextBox = PlatformBox.ShiftBy(WorldMove);
		if (DistanceFromFront < -AllowedBehindFront ||
		    DistanceFromFront > Radius + FMath::Abs(WorldMove.X) + PinchClearance ||
		    DistanceFromWall < -Radius ||
		    DistanceFromWall > Radius + PinchClearance + FMath::Abs(WorldMove.X) ||
		    FVector::DistSquared(NextBox.GetClosestPointTo(Center), Center) >
		    FMath::Square(Radius + PinchClearance))
		{
			continue;
		}

		FMarbleEscape Escape;
		Escape.Body = Body;
		Escape.StartLocation = Body->GetComponentLocation();
		Escape.SavedVelocity = Body->GetPhysicsLinearVelocity();
		Escape.DirectionZ = Center.Z < PlatformBox.GetCenter().Z ? -1.f : 1.f;
		Escape.PreviousCollision = Body->GetCollisionEnabled();
		Escape.bWasSimulatingPhysics = true;
		Escape.bHadGravity = Body->IsGravityEnabled();

		// 固定速度与固定时间；期间不受碰撞、重力或附近弹珠影响。
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Body->SetSimulatePhysics(false);
		ActiveEscapes.Add(Escape);
	}
}

void UPlatformMoveComponent::FinishEscape(const FMarbleEscape& Escape)
{
	UPrimitiveComponent* Body = Escape.Body.Get();
	if (!Body)
	{
		return;
	}

	Body->SetCollisionEnabled(Escape.PreviousCollision);
	Body->SetSimulatePhysics(Escape.bWasSimulatingPhysics);
	Body->SetEnableGravity(Escape.bHadGravity);
	if (Escape.bWasSimulatingPhysics)
	{
		// 保留原有水平速度，竖直方向交还重力，避免动画结束时再次被旧速度挤向夹点。
		Body->SetPhysicsLinearVelocity(FVector(Escape.SavedVelocity.X, Escape.SavedVelocity.Y, 0.f));
	}
}

void UPlatformMoveComponent::UpdateEscapingMarbles(const float DeltaTime)
{
	if (DeltaTime <= 0.f)
	{
		return;
	}

	for (int32 Index = ActiveEscapes.Num() - 1; Index >= 0; --Index)
	{
		FMarbleEscape& Escape = ActiveEscapes[Index];
		UPrimitiveComponent* Body = Escape.Body.Get();
		if (!Body)
		{
			ActiveEscapes.RemoveAtSwap(Index);
			continue;
		}

		Escape.ElapsedTime = FMath::Min(EscapeDuration, Escape.ElapsedTime + DeltaTime);
		Body->SetWorldLocation(Escape.StartLocation + FVector::UpVector *
		                      (Escape.DirectionZ * EscapeSpeed * Escape.ElapsedTime),
		                      false, nullptr, ETeleportType::TeleportPhysics);
		if (Escape.ElapsedTime >= EscapeDuration)
		{
			// 到指定时间立刻恢复，不再因为落点被占用而改变时长。
			FinishEscape(Escape);
			ActiveEscapes.RemoveAtSwap(Index);
		}
	}
}
