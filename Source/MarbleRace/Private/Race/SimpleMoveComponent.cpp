#include "Race/SimpleMoveComponent.h"
#include "Components/PrimitiveComponent.h"

USimpleMoveComponent::USimpleMoveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetMobility(EComponentMobility::Movable);
}

void USimpleMoveComponent::CaptureStart(USceneComponent* Target)
{
	MovementTarget = Target;
	InitialRelativeLocation = Target->GetRelativeLocation();
	CycleTime = 0.0;
	bCycleSpeedsSampled = false;
	Target->SetMobility(EComponentMobility::Movable);
	if (bEnhancedMovingCollision)
	{
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Target))
		{
			if (FBodyInstance* Body = Primitive->GetBodyInstance())
			{
				Body->SetUseCCD(true);
				Body->SetUseMACD(true);
			}
		}
	}
}

void USimpleMoveComponent::InitializeStartup()
{
	const float MaxDelay = FMath::IsFinite(StartDelay) ? FMath::Max(0.f, StartDelay) : 0.f;
	ActualStartDelay = MaxDelay > 0.f ? FMath::FRandRange(0.f, MaxDelay) : 0.f;
	StartupElapsed = 0.0;
	bStartupComplete = false;
	bStartupInitialized = true;
}

void USimpleMoveComponent::BeginPlay()
{
	Super::BeginPlay();
	InitializeStartup();
	if (USceneComponent* Target = GetAttachParent())
	{
		CaptureStart(Target);
	}
}

void USimpleMoveComponent::SampleCycleSpeeds()
{
	ActualOutboundSpeed = FMath::FRandRange(FMath::Min(OutboundSpeed, OutboundSpeedMax),
		FMath::Max(OutboundSpeed, OutboundSpeedMax));
	ActualReturnSpeed = bReciprocate
		? FMath::FRandRange(FMath::Min(ReturnSpeed, ReturnSpeedMax), FMath::Max(ReturnSpeed, ReturnSpeedMax))
		: 0.f;
	bCycleSpeedsSampled = true;
}

void USimpleMoveComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.f) return;
	if (!bStartupInitialized) InitializeStartup();
	double MovementDelta = DeltaTime;
	if (!bStartupComplete)
	{
		StartupElapsed += DeltaTime;
		const double Delay = ActualStartDelay;
		if (StartupElapsed < Delay) return;
		// Only the portion of this frame after the delay belongs to movement.
		MovementDelta = StartupElapsed - Delay;
		bStartupComplete = true;
		if (MovementDelta <= 0.0) return;
	}
	USceneComponent* Target = GetAttachParent();
	if (!IsValid(Target)) return;
	if (MovementTarget.Get() != Target) CaptureStart(Target);
	if (MoveDirection.ContainsNaN() ||
		!FMath::IsFinite(MoveDistance) || !FMath::IsFinite(CycleInterval) ||
		!FMath::IsFinite(OutboundSpeed) || OutboundSpeed <= 0.f ||
		!FMath::IsFinite(OutboundSpeedMax) || OutboundSpeedMax <= 0.f ||
		(bReciprocate && (!FMath::IsFinite(ReturnSpeed) || ReturnSpeed <= 0.f ||
			!FMath::IsFinite(ReturnSpeedMax) || ReturnSpeedMax <= 0.f)))
	{
		return;
	}

	const FVector Direction = MoveDirection.GetSafeNormal();
	if (MoveDistance <= 0.f || Direction.IsNearlyZero())
	{
		CycleTime = 0.0;
		bCycleSpeedsSampled = false;
		Target->SetRelativeLocation(InitialRelativeLocation);
		return;
	}

	const double Distance = MoveDistance;
	const double Interval = FMath::Max(0.f, CycleInterval);
	if (!bCycleSpeedsSampled || (bReciprocate && ActualReturnSpeed <= 0.f)) SampleCycleSpeeds();
	auto GetDuration = [&]()
	{
		return Distance / ActualOutboundSpeed + (bReciprocate ? Distance / ActualReturnSpeed : 0.0)
			+ Interval * (bReciprocate ? 2.0 : 1.0);
	};
	CycleTime += MovementDelta;
	double Duration = GetDuration();
	if (OutboundSpeed == OutboundSpeedMax && (!bReciprocate || ReturnSpeed == ReturnSpeedMax))
	{
		// Fixed ranges can skip any number of identical cycles in constant time.
		if (CycleTime >= Duration)
		{
			CycleTime -= Duration;
			SampleCycleSpeeds();
			Duration = GetDuration();
			CycleTime = FMath::Fmod(CycleTime, Duration);
		}
	}
	else
	{
		// Each crossed cycle has its own random duration; retain frame overshoot.
		while (CycleTime >= Duration)
		{
			CycleTime -= Duration;
			SampleCycleSpeeds();
			Duration = GetDuration();
		}
	}
	const double OutboundDuration = Distance / ActualOutboundSpeed;
	double Offset;
	if (CycleTime < OutboundDuration)
	{
		Offset = CycleTime * ActualOutboundSpeed;
	}
	else if (bReciprocate)
	{
		const double ReturnElapsed = FMath::Max(0.0, CycleTime - OutboundDuration - Interval);
		Offset = FMath::Max(0.0, Distance - ReturnElapsed * ActualReturnSpeed);
	}
	else
	{
		Offset = Distance;
	}

	FVector RelativeOffset = Direction * Offset;
	if (USceneComponent* Frame = Target->GetAttachParent())
	{
		// Follow the containing level's orientation, but keep distance/speed in world centimeters.
		const FTransform& FrameTransform = Frame->GetComponentTransform();
		const FVector Scale = FrameTransform.GetScale3D();
		if (FMath::IsNearlyZero(Scale.X) || FMath::IsNearlyZero(Scale.Y) || FMath::IsNearlyZero(Scale.Z)) return;
		RelativeOffset = FrameTransform.InverseTransformVector(
			FrameTransform.TransformVectorNoScale(RelativeOffset));
	}
	// No blocking sweep: Chaos receives a kinematic target and interpolates it across
	// physics substeps. The prescribed trajectory pushes dynamic bodies without stalling.
	Target->SetRelativeLocation(InitialRelativeLocation + RelativeOffset, false, nullptr, ETeleportType::None);
}
