#pragma once
#include "CoreMinimal.h"

namespace MarbleRace
{
	/** Uniform disk proposals; reject overlaps, never assign roster order to rows. */
	inline TArray<FVector> SampleDrumOffsets(int32 Count, float Radius, float MinimumSpacing, FRandomStream& Random)
	{
		TArray<FVector> Points;
		Points.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FVector Best = FVector::ZeroVector;
			double BestDistance = -1.0;
			for (int32 Attempt = 0; Attempt < 2048; ++Attempt)
			{
				const double Angle = Random.FRand() * 2.0 * PI;
				const double R = FMath::Sqrt(Random.FRand()) * FMath::Max(0.f, Radius);
				const FVector Candidate(R * FMath::Cos(Angle), 0.0, R * FMath::Sin(Angle));
				double Distance = TNumericLimits<double>::Max();
				for (const FVector& Point : Points) Distance = FMath::Min(Distance, FVector::DistSquared(Point, Candidate));
				if (Distance > BestDistance) { BestDistance = Distance; Best = Candidate; }
				if (Distance >= FMath::Square(MinimumSpacing)) break;
			}
			Points.Add(Best);
		}
		return Points;
	}
}
