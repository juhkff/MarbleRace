#include "Race/OscillatingRotationComponent.h"

UOscillatingRotationComponent::UOscillatingRotationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetMobility(EComponentMobility::Movable);
}

void UOscillatingRotationComponent::Capture(USceneComponent* Parent)
{
	RotationTarget = Parent;
	BaseRotation = Parent->GetRelativeRotation().Quaternion();
	Parent->SetMobility(EComponentMobility::Movable);
	const double Low = FMath::Min(LeftLimit, RightLimit);
	const double High = FMath::Max(LeftLimit, RightLimit);
	const double Amplitude = (High - Low) * .5;
	Phase = FMath::IsFinite(InitialAngle) && FMath::IsFinite(Amplitude) && Amplitude > UE_DOUBLE_SMALL_NUMBER
		? FMath::Acos(FMath::Clamp(((Low + High) * .5 - InitialAngle) / Amplitude, -1.0, 1.0)) : 0.0;
}

double UOscillatingRotationComponent::GetCurrentAngle() const
{
	if (!FMath::IsFinite(LeftLimit) || !FMath::IsFinite(RightLimit)) return 0.0;
	return (double(LeftLimit) + RightLimit) * .5 - FMath::Abs(double(RightLimit) - LeftLimit) * .5 * FMath::Cos(Phase);
}

void UOscillatingRotationComponent::Apply(USceneComponent* Parent) const
{
	if (SpinAxis == EAxis::None || !FMath::IsFinite(LeftLimit) || !FMath::IsFinite(RightLimit)) return;
	const double Angle = FMath::Fmod(GetCurrentAngle(), 360.0);
	const FRotator Delta(SpinAxis == EAxis::Y ? Angle : 0.0,
		SpinAxis == EAxis::Z ? Angle : 0.0, SpinAxis == EAxis::X ? Angle : 0.0);
	Parent->SetRelativeRotation((BaseRotation * Delta.Quaternion()).GetNormalized());
}

void UOscillatingRotationComponent::BeginPlay()
{
	Super::BeginPlay();
	if (USceneComponent* Parent = GetAttachParent()) { Capture(Parent); Apply(Parent); }
}

void UOscillatingRotationComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	USceneComponent* Parent = GetAttachParent();
	if (!IsValid(Parent)) return;
	if (RotationTarget.Get() != Parent) Capture(Parent);
	const double Amplitude = FMath::Abs(double(RightLimit) - LeftLimit) * .5;
	if (FMath::IsFinite(Amplitude) && Amplitude > UE_DOUBLE_SMALL_NUMBER &&
		FMath::IsFinite(RotationSpeed) && RotationSpeed > 0.f && FMath::IsFinite(DeltaTime) && DeltaTime > 0.f && SpinAxis != EAxis::None)
	{
		// 最大角速度 = 振幅 × 相位速度；保留跨帧余量，避免端点跳变和累积漂移。
		Phase = FMath::Fmod(Phase + double(RotationSpeed) / Amplitude * DeltaTime, 2.0 * UE_DOUBLE_PI);
	}
	Apply(Parent);
}
