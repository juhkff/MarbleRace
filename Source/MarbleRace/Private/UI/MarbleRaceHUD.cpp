#include "UI/MarbleRaceHUD.h"

#include "CanvasItem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Styling/CoreStyle.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Settings/MarbleRaceSettingsSubsystem.h"
#include "UI/MarbleNameStability.h"
#include "GameFramework/PlayerController.h"
#include "Race/MarbleRaceLevelGameMode.h"
#include "Roster/MarbleRaceRosterSubsystem.h"
#include "UI/MarbleRaceResultsLayout.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"

void AMarbleRaceHUD::DrawHUD()
{
	Super::DrawHUD();
	const AMarbleRaceLevelGameMode* Mode = GetWorld()->GetAuthGameMode<AMarbleRaceLevelGameMode>();
	UMarbleRaceRosterSubsystem* Roster = GetGameInstance()->GetSubsystem<UMarbleRaceRosterSubsystem>();
	if (!Canvas || !GEngine || !PlayerOwner || !Mode || !Roster)
	{
		return;
	}
	bResultClickConsumed = false;
	if (Mode->IsRaceComplete())
	{
		StableNamePositions.Reset();
		DrawResults(Mode->GetFinishResults(), Mode->GetResultsPage(GetWorld()->GetRealTimeSeconds()));
		if (const FMarbleRaceFinishResult* Result = Mode->GetFinishNotification(GetWorld()->GetRealTimeSeconds()))
			DrawFinishNotification(*Result, true);
		return;
	}
	UFont* Font = GEngine->GetSmallFont();
	if (Mode->IsMusicSwitchingLocked())
	{
		const FVector2D Centre(8.f, Canvas->ClipY - 8.f);
		TArray<FCanvasUVTri> Triangles;
		for (int32 Index = 0; Index < 16; ++Index)
		{
			FCanvasUVTri Triangle;
			Triangle.V0_Pos = Centre;
			const double A = Index * 2.0 * PI / 16.0, B = (Index + 1) * 2.0 * PI / 16.0;
			Triangle.V1_Pos = Centre + FVector2D(FMath::Cos(A), FMath::Sin(A)) * 2.5;
			Triangle.V2_Pos = Centre + FVector2D(FMath::Cos(B), FMath::Sin(B)) * 2.5;
			Triangle.V0_Color = Triangle.V1_Color = Triangle.V2_Color = FLinearColor(1.f, .08f, .06f, 1.f);
			Triangles.Add(Triangle);
		}
		FCanvasTriangleItem Dot(Triangles, GWhiteTexture);
		Dot.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Dot);
	}
	const float Scale = FMath::Clamp(Canvas->ClipY / 900.f, 0.9f, 1.5f);
	const auto* Settings = GetGameInstance()->GetSubsystem<UMarbleRaceSettingsSubsystem>();
	const bool bStable = Settings && Settings->AreMarbleNamesStable();
	const FVector2D CanvasSize(Canvas->ClipX, Canvas->ClipY);
	if (!bStable || CanvasSize != PreviousCanvasSize) StableNamePositions.Reset();
	PreviousCanvasSize = CanvasSize;
	TSet<TWeakObjectPtr<AActor>> VisibleNames;
	for (const auto& Pair : Mode->GetMarbleRosterIndices())
	{
		const AActor* Marble = Pair.Key;
		if (!IsValid(Marble) || Marble->IsHidden() || !Roster->IsValidIndex(Pair.Value))
		{
			continue;
		}
		const UStaticMeshComponent* Mesh = Marble->FindComponentByClass<UStaticMeshComponent>();
		// 世界 Z 向上偏移，再投影到屏幕；不使用弹珠的局部 UpVector。
		float Radius = Mesh ? Mesh->Bounds.BoxExtent.GetMax() : 90.f;
		if (bStable && Mesh && Mesh->GetStaticMesh())
		{
			// Local bounds do not expand and contract as the disc rolls.
			Radius = Mesh->GetStaticMesh()->GetBounds().BoxExtent.GetMax() * Mesh->GetComponentScale().GetAbsMax();
		}
		FVector2D Screen;
		// HUD Canvas is relative to the camera's constrained view, excluding letterbox bars.
		if (!PlayerOwner->ProjectWorldLocationToScreen(Marble->GetActorLocation() + FVector(0.f, 0.f, Radius + 12.f), Screen, true))
		{
			continue;
		}
		if (bStable)
		{
			const TWeakObjectPtr<AActor> Key(const_cast<AActor*>(Marble));
			VisibleNames.Add(Key);
			if (FVector2D* Previous = StableNamePositions.Find(Key))
			{
				Screen = MarbleRace::StabilizeNamePosition(*Previous, Screen, GetWorld()->GetDeltaSeconds());
			}
			StableNamePositions.Add(Key, Screen);
		}
		const FRaceCharacterEntry& Entry = Roster->GetEntries()[Pair.Value];
		float Width = 0.f, Height = 0.f;
		Canvas->StrLen(Font, Entry.DisplayName, Width, Height);
		Width *= Scale;
		Height *= Scale;
		const FVector2D TopLeft(Screen.X - Width * 0.5f, Screen.Y - Height);
		if (TopLeft.X + Width < 0.f || TopLeft.X > Canvas->ClipX || TopLeft.Y + Height < 0.f || TopLeft.Y > Canvas->ClipY)
		{
			continue;
		}
		FCanvasTextItem Name(TopLeft, FText::FromString(Entry.DisplayName), Font, FLinearColor::White);
		Name.Scale = FVector2D(Scale);
		Name.EnableShadow(FLinearColor::Black, FVector2D(1.f, 1.f));
		Canvas->DrawItem(Name);
	}
	if (Mode->GetCountdownRemaining() > 0)
	{
		FCanvasTextItem Countdown(FVector2D(Canvas->ClipX * .5f, Canvas->ClipY * .5f),
			FText::FromString(FString::FromInt(Mode->GetCountdownRemaining())),
			FCoreStyle::GetDefaultFontStyle("Bold", 120), FLinearColor::White);
		// Canvas requires a UFont even when glyphs come from a Slate font style.
		Countdown.Font = Font;
		Countdown.bCentreX = Countdown.bCentreY = true;
		Countdown.EnableShadow(FLinearColor(0.f, 0.f, 0.f, .85f), FVector2D(3.f));
		Canvas->DrawItem(Countdown);
	}
	for (auto It = StableNamePositions.CreateIterator(); It; ++It)
	{
		if (!VisibleNames.Contains(It.Key())) It.RemoveCurrent();
	}
	if (const FMarbleRaceFinishResult* Result = Mode->GetFinishNotification(GetWorld()->GetRealTimeSeconds()))
		DrawFinishNotification(*Result);
}

FVector2D AMarbleRaceHUD::MeasureResultText(const FString& Text, const int32 FontSize) const
{
	FSlateFontInfo Font = GEngine->GetMediumFont()->GetLegacySlateFontInfo();
	Font.Size = FontSize;
	return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font);
}

void AMarbleRaceHUD::DrawResultText(const FString& Text, const FVector2D Position, const int32 FontSize,
	const FLinearColor& Color, const bool bCentered)
{
	FSlateFontInfo Font = GEngine->GetMediumFont()->GetLegacySlateFontInfo();
	Font.Size = FontSize;
	FCanvasTextItem Item(FVector2D(FMath::RoundToFloat(Position.X), FMath::RoundToFloat(Position.Y)),
		FText::FromString(Text), Font, Color);
	Item.bOutlined = true;
	Item.OutlineColor = FLinearColor(0.f, 0.f, 0.f, .95f);
	Item.bCentreX = Item.bCentreY = bCentered;
	Canvas->DrawItem(Item);
}

void AMarbleRaceHUD::DrawFittedResultText(const FString& Text, const FVector2D Position,
	int32 FontSize, const float MaxWidth, const FLinearColor& Color)
{
	const int32 MinimumSize = FMath::Max(8, FMath::RoundToInt(FontSize * .7f));
	while (FontSize > MinimumSize && MeasureResultText(Text, FontSize).X > MaxWidth) --FontSize;
	FString Shown = Text;
	if (MeasureResultText(Shown, FontSize).X > MaxWidth)
	{
		while (!Shown.IsEmpty() && MeasureResultText(Shown + TEXT("…"), FontSize).X > MaxWidth)
			Shown.LeftChopInline(1);
		Shown += TEXT("…");
	}
	DrawResultText(Shown, Position, FontSize, Color);
}

void AMarbleRaceHUD::DrawFinishNotification(const FMarbleRaceFinishResult& Result, bool bAboveResults)
{
	FString Text = FString::Printf(TEXT("第 %d 名：%s"), Result.Rank, *Result.CharacterName);
	const float Scale = MarbleRace::MakeResultsLayout(FVector2D(Canvas->ClipX, Canvas->ClipY)).Scale;
	int32 FontSize = FMath::Max(12, FMath::RoundToInt(26.f * Scale));
	while (FontSize > 12 && MeasureResultText(Text, FontSize).X > Canvas->ClipX - 20.f * Scale) --FontSize;
	if (MeasureResultText(Text, FontSize).X > Canvas->ClipX - 20.f * Scale)
	{
		while (!Text.IsEmpty() && MeasureResultText(Text + TEXT("…"), FontSize).X > Canvas->ClipX - 20.f * Scale)
			Text.LeftChopInline(1);
		Text += TEXT("…");
	}
	const float Y = bAboveResults ? MarbleRace::MakeResultsLayout(FVector2D(Canvas->ClipX, Canvas->ClipY)).HeaderY * .5f : Canvas->ClipY * .17f;
	DrawResultText(Text, FVector2D(Canvas->ClipX * .5f, Y), FontSize, FLinearColor::White, true);
}

void AMarbleRaceHUD::DrawResultSong(const FString& Text, const FVector2D Position, int32 FontSize,
	const float MaxWidth, const float LineHeight)
{
	// Give long built-in song titles two footnote lines instead of cutting them off.
	const auto SplitLine = [this, MaxWidth](const FString& Value, const int32 Size, FString& First, FString& Rest)
	{
		int32 Count = Value.Len();
		while (Count > 0 && MeasureResultText(Value.Left(Count), Size).X > MaxWidth) --Count;
		if (Count < Value.Len())
		{
			int32 Space;
			if (Value.Left(Count + 1).FindLastChar(TEXT(' '), Space) && Space > 0) Count = Space;
		}
		First = Value.Left(Count).TrimEnd();
		Rest = Value.Mid(Count).TrimStart();
	};
	FString First, Second;
	SplitLine(Text, FontSize, First, Second);
	while (FontSize > 8 && MeasureResultText(Second, FontSize).X > MaxWidth)
	{
		--FontSize;
		SplitLine(Text, FontSize, First, Second);
	}
	const FLinearColor Color(.9f, .95f, 1.f);
	DrawResultText(First, Position, FontSize, Color);
	if (!Second.IsEmpty()) DrawFittedResultText(Second, Position + FVector2D(0.f, LineHeight), FontSize, MaxWidth, Color);
}

bool AMarbleRaceHUD::GetCanvasMousePosition(FVector2D& OutPosition) const
{
	float X, Y;
	int32 Width, Height;
	if (!PlayerOwner || !PlayerOwner->GetMousePosition(X, Y)) return false;
	PlayerOwner->GetViewportSize(Width, Height);
	OutPosition = MarbleRace::MouseToRaceCanvas(FVector2D(X, Y), FVector2D(Width, Height),
		FVector2D(Canvas->ClipX, Canvas->ClipY));
	return true;
}

bool AMarbleRaceHUD::DrawResultButton(const FString& Text, const FVector2D Position, const FVector2D Size,
	const int32 FontSize, const bool bEnabled)
{
	FVector2D Mouse;
	const bool bHovered = bEnabled && GetCanvasMousePosition(Mouse) &&
		Mouse.X >= Position.X && Mouse.X <= Position.X + Size.X && Mouse.Y >= Position.Y && Mouse.Y <= Position.Y + Size.Y;
	const FLinearColor Color = !bEnabled ? FLinearColor(.06f, .07f, .15f, .5f)
		: bHovered ? FLinearColor(.08f, .16f, .72f, .95f) : FLinearColor(.015f, .025f, .46f, .95f);
	DrawRect(Color, Position.X, Position.Y, Size.X, Size.Y);
	DrawResultText(Text, Position + Size * .5, FontSize, bEnabled ? FLinearColor::White : FLinearColor(.6f, .65f, .75f), true);
	if (bHovered && !bResultClickConsumed && PlayerOwner->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		bResultClickConsumed = true;
		return true;
	}
	return false;
}

void AMarbleRaceHUD::DrawResults(const TArray<FMarbleRaceFinishResult>& Results, int32 Page)
{
	const auto Layout = MarbleRace::MakeResultsLayout(FVector2D(Canvas->ClipX, Canvas->ClipY));
	const float Scale = Layout.Scale;
	Page = FMath::Clamp(Page, 0, MarbleRace::ResultsPageCount(Results.Num()) - 1);
	const int32 ShownOffset = Page * MarbleRace::ResultsPerPage;
	const int32 VisibleRows = FMath::Min(MarbleRace::ResultsPerPage, Results.Num() - ShownOffset);
	const float Right = Layout.Left + Layout.Width;
	const float NameX = Layout.Left + 34.f * Scale;
	const float TimeWidth = 96.f * Scale;
	const float NameWidth = Right - TimeWidth - 12.f * Scale - NameX;
	const int32 NameSize = FMath::RoundToInt(19.f * Scale), SongSize = FMath::RoundToInt(12.f * Scale);
	DrawResultText(TEXT("比赛结果"), FVector2D(Layout.Left, Layout.HeaderY), FMath::RoundToInt(24.f * Scale));
	const FString TimeHeading(TEXT("用时"));
	DrawResultText(TimeHeading, FVector2D(Right - MeasureResultText(TimeHeading, NameSize).X, Layout.HeaderY + 4.f * Scale), NameSize);
	DrawRect(FLinearColor(1.f, 1.f, 1.f, .55f), Layout.Left, Layout.RowsY - 8.f * Scale, Layout.Width, Scale);
	for (int32 Row = 0; Row < VisibleRows; ++Row)
	{
		const FMarbleRaceFinishResult& Result = Results[ShownOffset + Row];
		const float Y = Layout.RowsY + Row * Layout.RowHeight;
		DrawResultText(FString::Printf(TEXT("%d."), Result.Rank), FVector2D(Layout.Left, Y), NameSize);
		DrawFittedResultText(Result.CharacterName, FVector2D(NameX, Y), NameSize, NameWidth);
		DrawResultSong(Result.ThemeTitle, FVector2D(NameX, Y + 25.f * Scale), SongSize, NameWidth, 16.f * Scale);
		const FString Time = MarbleRace::FormatFinishTime(Result.ElapsedSeconds);
		DrawResultText(Time, FVector2D(Right - MeasureResultText(Time, NameSize).X, Y), NameSize);
	}
	const FString Range = FString::Printf(TEXT("%d–%d / %d    %d/%d"), FMath::Min(ShownOffset + 1, Results.Num()),
		ShownOffset + VisibleRows, Results.Num(), Page + 1, MarbleRace::ResultsPageCount(Results.Num()));
	DrawResultText(Range, FVector2D(Canvas->ClipX * .5f, Layout.NavigationY + 14.f * Scale), SongSize, FLinearColor::White, true);
	const float ButtonWidth = FMath::Min(Layout.Width, 250.f * Scale);
	if (DrawResultButton(TEXT("继续"), FVector2D((Canvas->ClipX - ButtonWidth) * .5f, Layout.ContinueY),
		FVector2D(ButtonWidth, 56.f * Scale), FMath::RoundToInt(26.f * Scale)))
		if (auto* Mode = GetWorld()->GetAuthGameMode<AMarbleRaceLevelGameMode>()) Mode->ReturnToMainMenu();
}
