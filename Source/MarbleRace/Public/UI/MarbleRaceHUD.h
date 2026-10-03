#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Race/MarbleRaceResults.h"
#include "MarbleRaceHUD.generated.h"

/** 屏幕空间姓名：跟随位置，字号和方向不受物理弹珠旋转影响。 */
UCLASS()
class MARBLERACE_API AMarbleRaceHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
private:
	friend class FMarbleRaceResultsRenderingTest;
	void DrawFinishNotification(const FMarbleRaceFinishResult& Result, bool bAboveResults = false);
	void DrawResults(const TArray<FMarbleRaceFinishResult>& Results, int32 Page = 0);
	void DrawResultText(const FString& Text, FVector2D Position, int32 FontSize,
		const FLinearColor& Color = FLinearColor::White, bool bCentered = false);
	FVector2D MeasureResultText(const FString& Text, int32 FontSize) const;
	void DrawFittedResultText(const FString& Text, FVector2D Position, int32 FontSize, float MaxWidth,
		const FLinearColor& Color = FLinearColor::White);
	void DrawResultSong(const FString& Text, FVector2D Position, int32 FontSize, float MaxWidth, float LineHeight);
	bool GetCanvasMousePosition(FVector2D& OutPosition) const;
	bool DrawResultButton(const FString& Text, FVector2D Position, FVector2D Size, int32 FontSize, bool bEnabled = true);
	bool bResultClickConsumed = false;
	TMap<TWeakObjectPtr<AActor>, FVector2D> StableNamePositions;
	FVector2D PreviousCanvasSize = FVector2D::ZeroVector;
};
