#pragma once

#include "CoreMinimal.h"

namespace MarbleRace
{
	// Queued marbles are hidden. Visible anti-pinch animations remain eligible even
	// while their physics is temporarily disabled.
	inline bool IsCameraTargetEligible(const bool bValid, const bool bHidden, const bool bFinished)
	{
		return bValid && !bHidden && !bFinished;
	}

	inline float AdvanceCameraZ(const float CurrentZ, const float TargetZ, const float DeltaSeconds,
	                           const float FollowSpeed, const float MaxSpeed)
	{
		if (DeltaSeconds <= 0.f || MaxSpeed <= 0.f)
		{
			return CurrentZ;
		}
		const float SmoothedZ = FMath::FInterpTo(CurrentZ, TargetZ, DeltaSeconds, FollowSpeed);
		const float MaxStep = MaxSpeed * DeltaSeconds;
		return CurrentZ + FMath::Clamp(SmoothedZ - CurrentZ, -MaxStep, MaxStep);
	}
}
