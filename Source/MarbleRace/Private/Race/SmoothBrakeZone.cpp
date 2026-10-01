#include "Race/SmoothBrakeZone.h"

#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "UObject/ConstructorHelpers.h"

ASmoothBrakeZone::ASmoothBrakeZone()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	BrakeVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("制动范围"));
	SetRootComponent(BrakeVolume);
	BrakeVolume->SetBoxExtent(FVector(400, 150, 300));
	BrakeVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BrakeVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	BrakeVolume->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	BrakeVolume->SetGenerateOverlapEvents(true);
	BrakeVolume->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FClassFinder<AActor> MarbleFinder(TEXT("/Game/角色/弹珠"));
	if (MarbleFinder.Succeeded()) MarbleClass = MarbleFinder.Class;
}

FVector ASmoothBrakeZone::CalculateBrakeAcceleration(const FVector& Velocity, float DesiredSpeed,
	float Strength, float MaxAcceleration, float Weight, float DeltaSeconds)
{
	if (Velocity.ContainsNaN() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f ||
		!FMath::IsFinite(DesiredSpeed) || !FMath::IsFinite(Strength) ||
		!FMath::IsFinite(MaxAcceleration) || !FMath::IsFinite(Weight)) return FVector::ZeroVector;
	const double Speed = Velocity.Size();
	const double Excess = FMath::Max(0.0, Speed - FMath::Max(0.f, DesiredSpeed));
	if (Excess <= KINDA_SMALL_NUMBER) return FVector::ZeroVector;
	const double Acceleration = FMath::Min3(
		Excess * FMath::Max(0.f, Strength) * FMath::Clamp(Weight, 0.f, 1.f),
		double(FMath::Max(0.f, MaxAcceleration)), Excess / DeltaSeconds);
	return -Velocity.GetSafeNormal() * Acceleration;
}

float ASmoothBrakeZone::GetSpatialWeight(const FVector& WorldPosition) const
{
	const FVector Local = BrakeVolume->GetComponentTransform().InverseTransformPosition(WorldPosition).GetAbs();
	const FVector Distance = (BrakeVolume->GetUnscaledBoxExtent() - Local) * BrakeVolume->GetComponentScale().GetAbs();
	const double EdgeDistance = Distance.GetMin();
	if (EdgeDistance <= 0.0) return 0.f;
	if (BoundaryFadeDistance <= KINDA_SMALL_NUMBER) return 1.f;
	const float Alpha = FMath::Clamp(float(EdgeDistance / BoundaryFadeDistance), 0.f, 1.f);
	return Alpha * Alpha * (3.f - 2.f * Alpha);
}

void ASmoothBrakeZone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (DeltaSeconds <= 0.f) return;
	if (!bEnableBraking)
	{
		BrakeWeights.Empty();
		return;
	}
	TSet<TWeakObjectPtr<UPrimitiveComponent>> Inside;
	TArray<UPrimitiveComponent*> Overlapping;
	if (MarbleClass) BrakeVolume->GetOverlappingComponents(Overlapping);
	for (UPrimitiveComponent* Body : Overlapping)
	{
		AActor* Marble = Body ? Body->GetOwner() : nullptr;
		if (IsValid(Marble) && Marble->IsA(MarbleClass) && !Marble->IsHidden() &&
			Body == Marble->GetRootComponent() && Body->IsSimulatingPhysics())
		{
			Inside.Add(Body);
			BrakeWeights.FindOrAdd(Body);
		}
	}
	const float BlendAlpha = BlendTime > KINDA_SMALL_NUMBER ? 1.f - FMath::Exp(-DeltaSeconds / BlendTime) : 1.f;
	for (auto It = BrakeWeights.CreateIterator(); It; ++It)
	{
		UPrimitiveComponent* Body = It.Key().Get();
		if (!IsValid(Body) || !IsValid(Body->GetOwner()) || Body->GetOwner()->IsHidden() || !Body->IsSimulatingPhysics())
		{
			// 排队隐藏、销毁或暂停物理的弹珠不继承旧的制动进度。
			It.RemoveCurrent();
			continue;
		}
		const float TargetWeight = Inside.Contains(It.Key()) ? GetSpatialWeight(Body->GetComponentLocation()) : 0.f;
		float& Weight = It.Value();
		Weight = FMath::Lerp(Weight, TargetWeight, BlendAlpha);
		if (Weight < 1.e-4f && TargetWeight == 0.f)
		{
			It.RemoveCurrent();
			continue;
		}
		Body->AddForce(CalculateBrakeAcceleration(Body->GetPhysicsLinearVelocity(), TargetSpeed,
			BrakeStrength, MaximumBrakeAcceleration, Weight, DeltaSeconds), NAME_None, true);
		if (bBrakeRotation)
		{
			const float Radius = Body->CalcBounds(FTransform(FQuat::Identity, FVector::ZeroVector,
				Body->GetComponentScale())).BoxExtent.GetMax();
			if (Radius > KINDA_SMALL_NUMBER)
			{
				const FVector Acceleration = CalculateBrakeAcceleration(Body->GetPhysicsAngularVelocityInRadians(),
					FMath::Max(0.f, TargetSpeed) / Radius, AngularBrakeStrength, MAX_flt, Weight, DeltaSeconds);
				Body->AddTorqueInRadians(Acceleration, NAME_None, true);
			}
		}
	}
}

void ASmoothBrakeZone::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	BrakeWeights.Empty();
	Super::EndPlay(EndPlayReason);
}
