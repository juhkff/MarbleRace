#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MarbleRaceMenuHUD.generated.h"

class FMarbleRaceMenuImeContext;
class FMarbleRaceMenuWheelProcessor;
class SWidget;
class UMarbleRaceRosterSubsystem;
class UTexture2D;
struct FKey;

/** 画布菜单当前画的是哪一页。 */
UENUM(BlueprintType, meta=(DisplayName="菜单页面"))
enum class EMarbleRaceMenuPage : uint8
{
	MainMenu		UMETA(DisplayName="主菜单"),
	CharacterSetup	UMETA(DisplayName="角色设置")
};

/** 当前接收输入的文本框。 */
UENUM(BlueprintType, meta=(DisplayName="菜单文本框"))
enum class EMarbleRaceMenuTextField : uint8
{
	None UMETA(DisplayName="无"),
	DisplayName UMETA(DisplayName="姓名"),
	PortraitPath UMETA(DisplayName="头像路径")
};

/**
 * 即时模式主菜单。第一页是开始游戏、角色设置、退出。
 * 第二页编辑名单：姓名、颜色、头像、领跑主题曲，数据在角色名单子系统里。
 *
 * 全部用画布按原始字号绘制，和比赛界面一样，不需要控件或材质资源。
 * 鼠标点击从所属玩家控制器读取。字符输入经过视口上一个可聚焦的 Slate 控件，
 * 再加上输入法上下文，中文输入才能工作。具体原因见菜单界面的实现文件。
 */
UCLASS(meta=(DisplayName="主菜单界面"))
class MARBLERACE_API AMarbleRaceMenuHUD : public AHUD
{
	GENERATED_BODY()

public:
	AMarbleRaceMenuHUD();

	virtual void DrawHUD() override;

	/** “开始游戏”要打开的关卡。资产在 /Game/Maps/NewMap。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单", meta=(DisplayName="比赛关卡名"))
	FName RaceLevelName = FName(TEXT("/Game/Maps/NewMap"));

	/** 主菜单上的大标题。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单", meta=(DisplayName="标题"))
	FString TitleText = TEXT("弹珠竞速");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|样式", meta=(DisplayName="背景色"))
	FLinearColor BackgroundColor = FLinearColor(0.016f, 0.024f, 0.038f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|样式", meta=(DisplayName="面板色"))
	FLinearColor PanelColor = FLinearColor(0.055f, 0.075f, 0.105f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|样式", meta=(DisplayName="按钮色"))
	FLinearColor ButtonColor = FLinearColor(0.10f, 0.14f, 0.19f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|样式", meta=(DisplayName="按钮悬停色"))
	FLinearColor ButtonHoverColor = FLinearColor(0.18f, 0.25f, 0.34f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|样式", meta=(DisplayName="强调色"))
	FLinearColor AccentColor = FLinearColor(0.25f, 0.62f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|样式", meta=(DisplayName="文字色"))
	FLinearColor TextColor = FLinearColor(0.96f, 0.97f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|样式", meta=(DisplayName="次要文字色"))
	FLinearColor MutedTextColor = FLinearColor(0.62f, 0.67f, 0.75f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|样式", meta=(DisplayName="错误色"))
	FLinearColor ErrorColor = FLinearColor(1.0f, 0.42f, 0.36f, 1.0f);

	/** 原始字号。画布字体不会把一张很小的字图放大。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|样式", meta=(DisplayName="标题字号", ClampMin="10", ClampMax="120"))
	int32 TitleFontSize = 56;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|样式", meta=(DisplayName="栏目标题字号", ClampMin="10", ClampMax="80"))
	int32 HeadingFontSize = 26;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|样式", meta=(DisplayName="正文字号", ClampMin="10", ClampMax="60"))
	int32 BodyFontSize = 18;

	/** 次要标签和提示。正文保持在 16 到 22 像素。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|样式", meta=(DisplayName="小字号", ClampMin="12", ClampMax="40"))
	int32 SmallFontSize = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|样式", meta=(DisplayName="按钮字号", ClampMin="10", ClampMax="60"))
	int32 ButtonFontSize = 22;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|布局", meta=(DisplayName="菜单按钮宽度", ClampMin="80.0"))
	float MenuButtonWidth = 320.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|布局", meta=(DisplayName="菜单按钮高度", ClampMin="24.0"))
	float MenuButtonHeight = 62.0f;

	/** 角色设置页里一行名单的高度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|布局", meta=(DisplayName="行高", ClampMin="30.0", ClampMax="120.0"))
	float RowHeight = 46.0f;

	/** 左侧名单占屏幕宽度的比例。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|布局", meta=(DisplayName="列表宽度比例", ClampMin="0.2", ClampMax="0.7"))
	float ListWidthFraction = 0.42f;

	/** 主题曲选择器每一页列出几首。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|布局", meta=(DisplayName="每页主题曲数", ClampMin="1", ClampMax="30"))
	int32 ThemesPerPage = 8;

	/** 头像选择器每一页列出几个文件。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|布局", meta=(DisplayName="每页头像数", ClampMin="1", ClampMax="20"))
	int32 PortraitFilesPerPage = 6;

	/** 光标在列表上时，滚轮每一格滚动的像素。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|布局", meta=(DisplayName="滚轮像素", ClampMin="8.0", ClampMax="400.0"))
	float WheelScrollPixels = 90.0f;

	/** 给选中小球用的颜色行。第一项也是缺省颜色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="菜单|角色", meta=(DisplayName="调色板"))
	TArray<FLinearColor> PaletteColors;

	/** 视口按键控件把平台送来的每个字符转过来。 */
	void HandleKeyChar(const TCHAR Character);

	/** 视口按键控件把有文字含义的按键转过来。处理了就返回 true。 */
	bool HandleKeyDown(const FKey& Key);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 输入法上下文直接读写当前获得焦点的文本。 */
	friend class FMarbleRaceMenuImeContext;

	/** 滚轮预处理只上报格数，不是界面的公开部分。 */
	friend class FMarbleRaceMenuWheelProcessor;

	//~ 页面和选中状态。使用前都会做范围检查。 ~
	EMarbleRaceMenuPage CurrentPage = EMarbleRaceMenuPage::MainMenu;
	int32 SelectedEntryIndex = INDEX_NONE;
	int32 ThemePageIndex = 0;
	int32 PortraitFilePageIndex = 0;

	/** 第一条可见名单行的像素偏移。 */
	float RosterScrollOffset = 0.0f;

	/** 选中项变化时，列表只把它滚进视野一次，不跟用户抢滚动。 */
	bool bScrollSelectionPending = false;

	/** 上一帧以来积下的滚轮格数，由名单列表消耗。 */
	float PendingWheelDelta = 0.0f;

	/** 左键按在列表里时为真，用来拖动滚动。 */
	bool bDraggingRoster = false;
	float LastDragMouseY = 0.0f;

	//~ 文本编辑。光标始终在缓冲区末尾。 ~
	EMarbleRaceMenuTextField FocusedTextField = EMarbleRaceMenuTextField::None;
	FString NameEditBuffer;
	FString PathEditBuffer;
	int32 CompositionBeginIndex = INDEX_NONE;
	int32 CompositionLength = 0;

	/** 当前文本框的屏幕矩形。绘制时记下，好让输入法摆放自己的窗口。 */
	FVector2D ActiveTextFieldOrigin = FVector2D::ZeroVector;
	FVector2D ActiveTextFieldSize = FVector2D::ZeroVector;

	/** 最近一次头像导入的结果或提示，画在头像选择器下面。 */
	FString StatusMessage;
	bool bStatusIsError = false;

	/** 每帧只消费一次点击：第一个包含这次按下的控件把它用掉。 */
	bool bClickConsumedThisFrame = false;
	bool bAppliedMouseInputMode = false;

	/** 标成 mutable，让只读的绘制函数也能在第一次用到时解析它。 */
	mutable TWeakObjectPtr<UMarbleRaceRosterSubsystem> CachedRoster;
	TSharedPtr<SWidget> KeyCatcherWidget;
	TSharedPtr<FMarbleRaceMenuWheelProcessor> WheelProcessor;
	TSharedPtr<FMarbleRaceMenuImeContext> ImeContext;
	bool bImeContextActive = false;

	//~ 子系统和输入 ~
	/** 每一行绘制都会用到，所以做了缓存。不假设它一定存在。 */
	UMarbleRaceRosterSubsystem* GetRoster() const;
	void ApplyMouseInputModeOnce();
	void EnsureKeyCatcherFocus();
	void HandlePendingTextCommit();
	/** 滚轮预处理调用。名单列表来消耗。 */
	void AddRosterScroll(float WheelDelta);

	void HandleEscapeToMainMenu();
	void RequestQuit();
	void EnterCharacterSetupPage();
	void FocusTextField(EMarbleRaceMenuTextField Field);
	void CommitFocusedText();
	void CancelFocusedText();
	FString* GetFocusedTextBuffer();
	void ActivateImeContext();
	void DeactivateImeContext();
	void AppendTypedCharacter(const TCHAR Character);
	void RemoveLastTypedCharacter();

	void ImportPortraitFromPath(const FString& SourcePath);
	void SetStatus(const FString& Message, bool bIsError);

	//~ 名单访问。不假设当前选中下标一定有效。 ~
	int32 GetEntryTotal() const;
	FString GetEntryName(int32 EntryIndex) const;
	FLinearColor GetEntryColor(int32 EntryIndex) const;
	bool IsEntryEnabled(int32 EntryIndex) const;
	FString GetEntrySourcePath(int32 EntryIndex) const;
	UTexture2D* GetEntryPortrait(int32 EntryIndex);
	FLinearColor GetPaletteColorAt(int32 PaletteIndex) const;
	/** 把选中行保持在列表可见范围内。 */
	void ScrollSelectionIntoView(float ListHeight);

	//~ 绘制 ~
	void DrawSharpText(const FString& Text, float X, float Y, int32 FontSize,
		const FLinearColor& InColor, bool bOutline, bool bCenterOnPosition);
	void DrawSharpCenteredText(const FString& Text, float CenterX, float CenterY, int32 FontSize,
		const FLinearColor& InColor, bool bOutline);
	/** 用三角形拼实心圆或圆环，不用一叠矩形去凑。 */
	void DrawCircleBand(float CenterX, float CenterY, float InnerRadius, float OuterRadius, const FLinearColor& InColor);
	void DrawPanel(float X, float Y, float Width, float Height, bool bHighlight);
	void DrawOutline(float X, float Y, float Width, float Height, const FLinearColor& InColor, float Thickness);

	bool IsMouseOverRect(float X, float Y, float Width, float Height) const;
	/** 左键在这个矩形里按下，且还没有别的控件用掉这次点击。 */
	bool ConsumeClickInRect(float X, float Y, float Width, float Height);
	bool DrawButton(const FString& Label, float X, float Y, float Width, float Height, int32 FontSize, bool bSelected);
	/** 看起来可编辑的输入框。被点中、应当获得焦点时返回 true。 */
	bool DrawTextField(const FString& Text, float X, float Y, float Width, float Height, bool bFocused);

	void DrawMainMenuPage(float ScreenWidth, float ScreenHeight);
	void DrawCharacterSetupPage(float ScreenWidth, float ScreenHeight);
	void DrawRosterList(float ListX, float ListY, float ListWidth, float ListHeight);
	void DrawRosterRow(int32 EntryIndex, float RowX, float RowY, float RowWidth, float RowHeight);
	void DrawEntrySettings(float PanelX, float& PanelY, float PanelWidth);
	void DrawNameSection(float PanelX, float& PanelY, float PanelWidth);
	void DrawColorSection(float PanelX, float& PanelY, float PanelWidth);
	void DrawPortraitSection(float PanelX, float& PanelY, float PanelWidth);
	void DrawThemeSection(float PanelX, float& PanelY, float PanelWidth);
	void DrawBottomBar(float ScreenWidth, float ScreenHeight);

	/** 截短过长的文件名或资源名，避免一行冲到旁边去。 */
	static FString ShortenForRow(const FString& InText, int32 MaxCharacters);
};
