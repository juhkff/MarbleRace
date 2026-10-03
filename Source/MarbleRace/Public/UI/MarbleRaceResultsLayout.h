#pragma once

#include "CoreMinimal.h"
#include "Race/MarbleRaceResults.h"

namespace MarbleRace
{
	struct FResultsLayout
	{
		float Scale;
		float Left;
		float Width;
		float HeaderY;
		float RowsY;
		float RowHeight;
		float NavigationY;
		float ContinueY;
		int32 VisibleRows;
	};

	inline FResultsLayout MakeResultsLayout(const FVector2D Size)
	{
		FResultsLayout Layout;
		Layout.Scale = FMath::Clamp(FMath::Min(Size.X / 360.f, Size.Y / 900.f), .5f, 1.5f);
		Layout.Width = FMath::Min(Size.X - 40.f * Layout.Scale, 700.f * Layout.Scale);
		Layout.Left = (Size.X - Layout.Width) * .5f;
		Layout.RowHeight = 60.f * Layout.Scale;
		Layout.ContinueY = Size.Y - 90.f * Layout.Scale;
		Layout.NavigationY = Layout.ContinueY - 32.f * Layout.Scale;
		Layout.VisibleRows = ResultsPerPage;
		Layout.HeaderY = FMath::Min(Size.Y * .18f,
			Layout.NavigationY - (12.f + 40.f + ResultsPerPage * 60.f) * Layout.Scale);
		Layout.RowsY = Layout.HeaderY + 40.f * Layout.Scale;
		return Layout;
	}

	/** Canvas excludes the centered camera letterbox; controller mouse coordinates do not. */
	inline FVector2D MouseToRaceCanvas(const FVector2D Mouse, const FVector2D Viewport, const FVector2D Canvas)
	{
		return Mouse - (Viewport - Canvas) * .5;
	}
}
