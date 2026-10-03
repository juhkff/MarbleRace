#pragma once
#include "CoreMinimal.h"

namespace MarbleRace
{
	/** Low-pass tiny vibration; catch up promptly during travel and snap on teleports. */
	inline FVector2D StabilizeNamePosition(const FVector2D& Previous, const FVector2D& Target, const float DeltaSeconds)
	{
		const double Distance = FVector2D::Distance(Previous, Target);
		if (Distance > 64.0 || DeltaSeconds > 0.2f) return Target;
		const double Speed = FMath::Lerp(18.0, 60.0, FMath::Clamp(Distance / 24.0, 0.0, 1.0));
		const double Alpha = 1.0 - FMath::Exp(-Speed * FMath::Max(0.f, DeltaSeconds));
		return FMath::Lerp(Previous, Target, Alpha);
	}
}
