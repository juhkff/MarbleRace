#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MarbleRaceHUD.generated.h"

class AMarbleRaceDirector;
class UCanvas;

/** 尾迹上采样到的一个点。 */
struct FRacerTrailSample
{
	FVector Location = FVector::ZeroVector;
	float Time = 0.0f;
};

/**
 * 绘制实时角色层：头像、姓名、同色尾迹、倒计时、领跑和音乐读数、成绩列表。
 * 故意用朴素的画布绘制，这样在没有界面资源和材质时也能测试。
 */
UCLASS(meta=(DisplayName="比赛界面"))
class MARBLERACE_API AMarbleRaceHUD : public AHUD
{
	GENERATED_BODY()

public:
	AMarbleRaceHUD();

	virtual void DrawHUD() override;

	/** 这个界面在画角色层时，隐藏原来的小球网格。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|界面", meta=(DisplayName="隐藏小球网格"))
	bool bHideRacerMeshes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|界面", meta=(DisplayName="显示调试读数"))
	bool bShowDebugReadout = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|界面", meta=(DisplayName="允许暂停和重开按键"))
	bool bAllowPauseAndRestartKeys = true;

	/** 倒计时数字的字号。读数和成绩用 14 到 20 像素。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|界面", meta=(DisplayName="倒计时字号", ClampMin="24", ClampMax="260"))
	int32 CountdownFontSize = 120;

	/**
	 * 可选的状态环：领跑白色、音乐目标绿色、冠军金色。
	 * 默认关闭。镜头已经跟着未完赛的领先者，这些环容易显得多余。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|界面", meta=(DisplayName="显示状态环"))
	bool bShowStatusRings = false;

	/** 每颗球后面的彩色尾迹，颜色跟角色一致。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|界面|尾迹", meta=(DisplayName="显示尾迹"))
	bool bShowTrails = true;

	/** 短尾。最旧的采样点先丢掉。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|界面|尾迹", meta=(DisplayName="尾迹采样上限", ClampMin="2", ClampMax="200"))
	int32 TrailMaxSamples = 32;

	/** 小球移动这么多世界单位后，再记一个新点。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|界面|尾迹", meta=(DisplayName="采样间距", ClampMin="1.0"))
	float TrailSampleDistance = 5.0f;

	/** 尾迹长度，按小球半径的倍数。用来把尾巴保持得短。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|界面|尾迹", meta=(DisplayName="尾迹长度倍数", ClampMin="0.5", ClampMax="20.0"))
	float TrailLengthInRadii = 6.0f;

	/** 比这更旧的历史也会淡出，停住的球不会留下尾巴。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|界面|尾迹", meta=(DisplayName="尾迹寿命", ClampMin="0.05", ClampMax="5.0"))
	float TrailLifetimeSeconds = 0.5f;

	/** 靠近小球一端的不透明度。带子是半透明的，越往尾越淡。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|界面|尾迹", meta=(DisplayName="尾迹最大透明度", ClampMin="0.0", ClampMax="1.0"))
	float TrailMaxAlpha = 0.45f;

	/** 靠近小球处的半宽，按小球在屏幕上的半径比例。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|界面|尾迹", meta=(DisplayName="尾迹宽度", ClampMin="0.05", ClampMax="2.0"))
	float TrailWidthScale = 0.7f;

	/** 越大，带子越快收成一个尖。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="比赛|界面|尾迹", meta=(DisplayName="尾迹收尖", ClampMin="0.2", ClampMax="4.0"))
	float TrailTaperPower = 1.5f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	TWeakObjectPtr<AMarbleRaceDirector> CachedDirector;

	/** 导演可能比这个界面更晚生成，所以外观覆盖会推迟到能找到导演时再做。 */
	bool bAppliedVisualOverride = false;

	/** 每个选手编号一份世界空间尾迹。只用于显示。 */
	TMap<int32, TArray<FRacerTrailSample>> RacerTrails;

	/** 用来发现比赛重开，以便丢掉过期尾迹。 */
	float LastTrailRaceClock = 0.0f;

	AMarbleRaceDirector* ResolveDirector();
	void EnsureVisualOverride(AMarbleRaceDirector& Director);
	void HandleShortcutKeys(AMarbleRaceDirector& Director);

	void DrawTrails(AMarbleRaceDirector& Director);
	/** 半透明带子，靠近小球最宽，末端收成尖。 */
	void DrawTrailRibbon(const TArray<FRacerTrailSample>& Samples, float ScreenRadius,
		const FLinearColor& Color, float Now, float Lifetime);
	void DrawRacerLayer(AMarbleRaceDirector& Director);
	/** 按目标字号光栅化字形，而不是把一张很小的字图放大。 */
	void DrawSharpText(const FString& Text, float X, float Y, int32 FontSize,
		const FLinearColor& Color, bool bOutline, bool bCenterOnPosition);
	void DrawSharpCenteredText(const FString& Text, float CenterX, float CenterY, int32 FontSize,
		const FLinearColor& Color, bool bOutline);
	/** 画实心圆（内半径为 0）或圆环，由光栅器做抗锯齿。 */
	void DrawCircleBand(float CenterX, float CenterY, float InnerRadius, float OuterRadius, const FLinearColor& Color);
	void DrawReadout(AMarbleRaceDirector& Director);
	void DrawCountdown(AMarbleRaceDirector& Director);
	void DrawFinishCallouts(AMarbleRaceDirector& Director);
	void DrawResults(AMarbleRaceDirector& Director);
	bool ConsumeContinueClick();
	static FString FormatRaceTime(float Seconds);
	static FString FormatOrdinal(int32 ZeroBasedRank);
};
