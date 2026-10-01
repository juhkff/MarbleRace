#include "Race/PeriodicRotationComponent.h"

UPeriodicRotationComponent::UPeriodicRotationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetMobility(EComponentMobility::Movable);
}

void UPeriodicRotationComponent::CaptureInitialRotation(USceneComponent* Parent)
{
	RotationTarget = Parent;
	InitialRelativeRotation = Parent->GetRelativeRotation().Quaternion();
	InitialRelativeLocation = Parent->GetRelativeLocation();
	InitialRelativeScale3D = Parent->GetRelativeScale3D();
	PhaseDegrees = 0.0;
	Parent->SetMobility(EComponentMobility::Movable);
}

void UPeriodicRotationComponent::BeginPlay()
{
	Super::BeginPlay();
	if (USceneComponent* Parent = GetAttachParent())
	{
		CaptureInitialRotation(Parent);
	}
}

double UPeriodicRotationComponent::GetCurrentCycleAngle() const
{
	if (!FMath::IsFinite(RotationAmount)) return 0.0;
	const double Span = FMath::Abs(static_cast<double>(RotationAmount));
	if (Span <= UE_DOUBLE_SMALL_NUMBER) return 0.0;
	const double Period = bReciprocate ? 2.0 * Span : Span;
	const double Phase = FMath::Fmod(PhaseDegrees, Period);
	const double Angle = bReciprocate && Phase > Span ? Period - Phase : Phase;
	return RotationAmount < 0.f ? -Angle : Angle;
}

void UPeriodicRotationComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	USceneComponent* Parent = GetAttachParent();
	if (!IsValid(Parent)) return;
	if (RotationTarget.Get() != Parent) CaptureInitialRotation(Parent);
	if (!FMath::IsFinite(RotationAmount) || !FMath::IsFinite(RotationSpeed) ||
		!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.f || SpinAxis == EAxis::None)
	{
		return;
	}

	const double Span = FMath::Abs(static_cast<double>(RotationAmount));
	if (Span <= UE_DOUBLE_SMALL_NUMBER)
	{
		PhaseDegrees = 0.0;
		Parent->SetRelativeRotation(InitialRelativeRotation);
		if (bRotateAroundPivot) Parent->SetRelativeLocation(InitialRelativeLocation);
		return;
	}
	if (RotationSpeed <= 0.f) return;
	const double Period = bReciprocate ? 2.0 * Span : Span;
	// Keep fractional overshoot across boundaries; large frames can cross any number of cycles.
	PhaseDegrees = FMath::Fmod(PhaseDegrees + static_cast<double>(RotationSpeed) * DeltaTime, Period);
	const double Angle = FMath::Fmod(GetCurrentCycleAngle(), 360.0);
	const FRotator Delta(SpinAxis == EAxis::Y ? Angle : 0.0,
		SpinAxis == EAxis::Z ? Angle : 0.0, SpinAxis == EAxis::X ? Angle : 0.0);
	// Absolute offset from the start avoids cumulative quaternion drift and preserves initial orientation.
	const FQuat DeltaQuat = Delta.Quaternion();
	Parent->SetRelativeRotation((InitialRelativeRotation * DeltaQuat).GetNormalized());
	if (bRotateAroundPivot)
	{
		// 让「初始时落在支点上的那个物体点」全程停在那里，转轴因此过支点。
		// 支点是组件的本地偏移，先换算到父坐标系：P = Loc0 + q0 * (S0 ⊙ 本地偏移)。
		// 因为换算用的是组件自己的初始变换，摆到关卡任何位置、嵌进别的蓝图都不会错位。
		const FVector PivotPoint = InitialRelativeLocation
			+ InitialRelativeRotation.RotateVector(InitialRelativeScale3D * PivotLocation);
		// 物体初始朝向 q0 把增量旋转 d 共轭成父坐标系里的实际转轴：R = q0 * d * q0⁻¹。
		const FQuat PivotDelta = (InitialRelativeRotation * DeltaQuat * InitialRelativeRotation.Inverse()).GetNormalized();
		Parent->SetRelativeLocation(PivotPoint + PivotDelta.RotateVector(InitialRelativeLocation - PivotPoint));
	}
}
