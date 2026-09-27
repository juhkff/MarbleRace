#include "Race/RaceParticipantComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Roster/RaceCharacterProfile.h"

namespace
{
	/** 固定的备用颜色，占位选手之间还能分得开。 */
	FLinearColor PlaceholderColorForRacer(int32 RacerId)
	{
		static const FLinearColor Palette[] = {
			FLinearColor(0.85f, 0.24f, 0.24f),
			FLinearColor(0.24f, 0.45f, 0.90f),
			FLinearColor(0.20f, 0.72f, 0.38f),
			FLinearColor(0.92f, 0.68f, 0.16f),
			FLinearColor(0.66f, 0.32f, 0.82f),
			FLinearColor(0.18f, 0.76f, 0.80f)
		};
		const int32 Count = UE_ARRAY_COUNT(Palette);
		const int32 Index = RacerId >= 0 ? RacerId % Count : 0;
		return Palette[Index];
	}
}

URaceParticipantComponent::URaceParticipantComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FText URaceParticipantComponent::GetDisplayName() const
{
	if (CharacterProfile && !CharacterProfile->DisplayName.IsEmpty())
	{
		return CharacterProfile->DisplayName;
	}
	return FText::FromString(FString::Printf(TEXT("选手 %d"), RacerId >= 0 ? RacerId + 1 : 0));
}

FLinearColor URaceParticipantComponent::GetDisplayColor() const
{
	if (CharacterProfile)
	{
		return CharacterProfile->Color;
	}
	return PlaceholderColorForRacer(RacerId);
}

FString URaceParticipantComponent::GetInitialText() const
{
	const FString Name = GetDisplayName().ToString();
	return Name.IsEmpty() ? FString(TEXT("?")) : Name.Left(1);
}

UTexture2D* URaceParticipantComponent::GetPortrait() const
{
	return CharacterProfile ? CharacterProfile->Portrait : nullptr;
}

UPrimitiveComponent* URaceParticipantComponent::GetPhysicsComponent()
{
	if (CachedPhysicsComponent.IsValid())
	{
		return CachedPhysicsComponent.Get();
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	TArray<UPrimitiveComponent*> Primitives;
	Owner->GetComponents<UPrimitiveComponent>(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (Primitive && Primitive->IsSimulatingPhysics())
		{
			CachedPhysicsComponent = Primitive;
			return Primitive;
		}
	}
	// 退回到第一个图元，关卡里关掉物理的小球
	// 在倒计时里仍能被冻住。
	if (Primitives.Num() > 0)
	{
		CachedPhysicsComponent = Primitives[0];
		return Primitives[0];
	}
	return nullptr;
}

float URaceParticipantComponent::GetWorldRadius()
{
	UPrimitiveComponent* Primitive = GetPhysicsComponent();
	if (!Primitive)
	{
		return 0.0f;
	}
	Primitive->UpdateBounds();
	return Primitive->Bounds.SphereRadius;
}

void URaceParticipantComponent::SetRacerId(int32 InRacerId)
{
	RacerId = InRacerId;
}

void URaceParticipantComponent::AssignProfileIfMissing(URaceCharacterProfile* InProfile)
{
	if (!CharacterProfile && InProfile)
	{
		CharacterProfile = InProfile;
	}
}

void URaceParticipantComponent::BeginRaceRegistration(const FTransform& InStartTransform)
{
	StartTransform = InStartTransform;
	PreviousLocation = InStartTransform.GetLocation();
	bRegistered = true;
	ResetRuntimeState();
}

void URaceParticipantComponent::EndRaceRegistration()
{
	bRegistered = false;
}

void URaceParticipantComponent::CaptureStartTransform()
{
	if (const AActor* Owner = GetOwner())
	{
		StartTransform = Owner->GetActorTransform();
		PreviousLocation = StartTransform.GetLocation();
	}
}

void URaceParticipantComponent::ResetRuntimeState()
{
	bFinished = false;
	FinishRank = INDEX_NONE;
	FinishTime = 0.0f;
	bOutOfBounds = false;
	NextCheckpointIndex = 0;
	SegmentProgress = 0.0f;
	PreviousLocation = StartTransform.GetLocation();
}

void URaceParticipantComponent::TeleportToStart()
{
	if (AActor* Owner = GetOwner())
	{
		Owner->SetActorTransform(StartTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (UPrimitiveComponent* Primitive = GetPhysicsComponent())
	{
		Primitive->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Primitive->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}

	PreviousLocation = StartTransform.GetLocation();
}

void URaceParticipantComponent::SetPreviousLocation(const FVector& InLocation)
{
	PreviousLocation = InLocation;
}

void URaceParticipantComponent::SetNextCheckpointIndex(int32 InIndex)
{
	NextCheckpointIndex = InIndex;
}

void URaceParticipantComponent::SetSegmentProgress(float InProgress)
{
	SegmentProgress = InProgress;
}

void URaceParticipantComponent::SetOutOfBounds(bool bInOutOfBounds)
{
	bOutOfBounds = bInOutOfBounds;
}

void URaceParticipantComponent::RecordFinish(float InFinishTime, int32 InFinishRank)
{
	bFinished = true;
	FinishTime = InFinishTime;
	FinishRank = InFinishRank;
}
