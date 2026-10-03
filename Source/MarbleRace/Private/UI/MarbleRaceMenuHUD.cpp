#include "UI/MarbleRaceMenuHUD.h"

#include "CanvasItem.h"
#include "Components/AudioComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/ITextInputMethodSystem.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Layout/Visibility.h"
#include "Roster/MarbleRaceRosterSubsystem.h"
#include "Roster/RaceRosterTypes.h"
#include "Settings/MarbleRaceSettingsSubsystem.h"
#include "Race/MarbleRaceRecorderSubsystem.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "String/LexFromString.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

namespace MarbleRaceMenuLayout
{
	/** 各页共用的边距。 */
	constexpr float Margin = 24.0f;
	constexpr float HeaderHeight = 78.0f;
	constexpr float BottomBarHeight = 96.0f;
	constexpr float HintHeight = 44.0f;
	constexpr float Gap = 16.0f;

	/** 每个字形的大致前进宽度，只用于光标和输入法候选窗。 */
	constexpr float LatinGlyphWidth = 0.56f;
	constexpr float IdeographicGlyphWidth = 1.0f;

	/** 按目标字号估算一串字画出来的宽度。 */
	static float ApproximateTextWidth(const FString& InText, int32 FontSize)
	{
		float Width = 0.0f;
		for (int32 Index = 0; Index < InText.Len(); ++Index)
		{
			// 中日韩和全角标点大约占一个字宽，拉丁字形略大于半个。
			Width += (InText[Index] < 0x2E80) ? FontSize * LatinGlyphWidth : FontSize * IdeographicGlyphWidth;
		}
		return Width;
	}
}

/**
 * 菜单用来占住界面键盘焦点的不可见控件。
 *
 * UE 5.8 没有可绑定的按键字符全局委托：字符沿着焦点路径送进来
 * （应用处理按键字符，再到获得焦点的控件，源头是 Windows 的字符消息）。
 * 没有获得焦点的控件时，打出的字会被丢掉，所以菜单把这个控件放进视口
 * 叠层，并在编辑文本框时让它获得焦点。
 *
 * 它故意设成点击穿透：鼠标事件仍到达游戏视口，按钮用的鼠标位置和
 * “刚按下”都是从那里读的。
 */
class SMarbleRaceMenuKeyCatcher : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMarbleRaceMenuKeyCatcher)
		{
		}

		SLATE_ARGUMENT(TWeakObjectPtr<AMarbleRaceMenuHUD>, MenuHUD)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		MenuHUD = InArgs._MenuHUD;
	}

	virtual bool SupportsKeyboardFocus() const override { return true; }

	virtual FReply OnKeyChar(const FGeometry&, const FCharacterEvent& InCharacterEvent) override
	{
		AMarbleRaceMenuHUD* Menu = MenuHUD.Get();
		if (!Menu)
		{
			return FReply::Unhandled();
		}
		Menu->HandleKeyChar(InCharacterEvent.GetCharacter());
		return FReply::Handled();
	}

	virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& InKeyEvent) override
	{
		AMarbleRaceMenuHUD* Menu = MenuHUD.Get();
		if (Menu && Menu->HandleKeyDown(InKeyEvent.GetKey()))
		{
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:
	TWeakObjectPtr<AMarbleRaceMenuHUD> MenuHUD;
};

/**
 * 把鼠标滚轮格数报给菜单的输入预处理。
 *
 * 视口控件是点击穿透的，滚轮到不了它那里；输入预处理在分发之前就被调用，
 * 这是观察滚轮的正规办法。返回 false，普通输入完全不受影响。
 */
class FMarbleRaceMenuWheelProcessor : public IInputProcessor
{
public:
	explicit FMarbleRaceMenuWheelProcessor(AMarbleRaceMenuHUD* InMenuHUD)
		: MenuHUD(InMenuHUD)
	{
	}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
	{
	}

	virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent,
	                                            const FPointerEvent* InGestureEvent) override
	{
		if (AMarbleRaceMenuHUD* Menu = MenuHUD.Get())
		{
			Menu->AddRosterScroll(InWheelEvent.GetWheelDelta());
		}
		// 不吃掉这个事件：编辑器和比赛关卡仍要用滚轮。
		return false;
	}

private:
	TWeakObjectPtr<AMarbleRaceMenuHUD> MenuHUD;
};

/**
 * 菜单文本框用的输入法上下文。
 *
 * Windows 上引擎会丢掉输入法字符消息（文本输入系统直接返回 0，不转给消息处理），
 * 所以组好的中文只能经输入法上下文到达画布界面，可编辑文本用的也是这个接口。
 * 关掉输入法时打出的普通字符仍从按键控件进来，两条路互补，不重复。
 */
class FMarbleRaceMenuImeContext : public ITextInputMethodContext
{
public:
	explicit FMarbleRaceMenuImeContext(AMarbleRaceMenuHUD* InMenuHUD)
		: MenuHUD(InMenuHUD)
	{
	}

	//~ 输入法上下文接口开始
	virtual bool IsComposing() override { return bComposing; }

	virtual bool IsReadOnly() override { return false; }

	virtual uint32 GetTextLength() override
	{
		const FString* EditBuffer = GetEditBuffer();
		return EditBuffer ? static_cast<uint32>(EditBuffer->Len()) : 0u;
	}

	/** 画布输入框不能移动光标，所以光标始终在末尾。 */
	virtual void GetSelectionRange(uint32& OutBeginIndex, uint32& OutLength, ECaretPosition& OutCaretPosition) override
	{
		OutBeginIndex = GetTextLength();
		OutLength = 0u;
		OutCaretPosition = ECaretPosition::Ending;
	}

	virtual void SetSelectionRange(const uint32 InBeginIndex, const uint32 InLength,
	                               const ECaretPosition InCaretPosition) override
	{
		// 有意忽略：打字、组字和退格都作用在缓冲区末尾。
	}

	virtual void GetTextInRange(const uint32 InBeginIndex, const uint32 InLength, FString& OutString) override
	{
		const FString* EditBuffer = GetEditBuffer();
		if (!EditBuffer)
		{
			OutString.Reset();
			return;
		}

		const int32 BufferLength = EditBuffer->Len();
		const int32 Start = FMath::Clamp(static_cast<int32>(InBeginIndex), 0, BufferLength);
		const int32 Count = FMath::Clamp(static_cast<int32>(InLength), 0, BufferLength - Start);
		OutString = EditBuffer->Mid(Start, Count);
	}

	virtual void SetTextInRange(const uint32 InBeginIndex, const uint32 InLength, const FString& InString) override
	{
		FString* EditBuffer = GetEditBuffer();
		if (!EditBuffer)
		{
			return;
		}

		// 组字串和上屏结果都从这里来：替换这一段
		// 才能在打字时显示拼音，上屏时换成中文。
		const int32 BufferLength = EditBuffer->Len();
		const int32 Start = FMath::Clamp(static_cast<int32>(InBeginIndex), 0, BufferLength);
		const int32 Count = FMath::Clamp(static_cast<int32>(InLength), 0, BufferLength - Start);
		*EditBuffer = EditBuffer->Left(Start) + InString + EditBuffer->Mid(Start + Count);
	}

	virtual int32 GetCharacterIndexFromPoint(const FVector2D& InPoint) override { return INDEX_NONE; }

	virtual bool GetTextBounds(const uint32 InBeginIndex, const uint32 InLength, FVector2D& OutPosition,
	                           FVector2D& OutSize) override
	{
		GetScreenBounds(OutPosition, OutSize);
		return false;
	}

	virtual void GetScreenBounds(FVector2D& OutPosition, FVector2D& OutSize) override
	{
		if (const AMarbleRaceMenuHUD* Menu = MenuHUD.Get())
		{
			OutPosition = Menu->ActiveTextFieldOrigin;
			OutSize = Menu->ActiveTextFieldSize;
			return;
		}
		OutPosition = FVector2D::ZeroVector;
		OutSize = FVector2D::ZeroVector;
	}

	virtual TSharedPtr<FGenericWindow> GetWindow() override
	{
		if (!NativeWindow.IsValid())
		{
			// Windows 输入法摆放
			// 候选窗时会解引用这个窗口，所以要等窗口解析出来再激活上下文。
			if (TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow())
			{
				NativeWindow = ActiveWindow->GetNativeWindow();
			}
		}
		return NativeWindow;
	}

	virtual void BeginComposition() override
	{
		bComposing = true;
	}

	virtual void UpdateCompositionRange(const int32 InBeginIndex, const uint32 InLength) override
	{
		if (AMarbleRaceMenuHUD* Menu = MenuHUD.Get())
		{
			Menu->CompositionBeginIndex = InBeginIndex;
			Menu->CompositionLength = static_cast<int32>(InLength);
		}
	}

	virtual void EndComposition() override
	{
		bComposing = false;
		if (AMarbleRaceMenuHUD* Menu = MenuHUD.Get())
		{
			Menu->CompositionBeginIndex = INDEX_NONE;
			Menu->CompositionLength = 0;
		}
	}

	//~ 输入法上下文接口结束

private:
	FString* GetEditBuffer() const
	{
		AMarbleRaceMenuHUD* Menu = MenuHUD.Get();
		return Menu ? Menu->GetFocusedTextBuffer() : nullptr;
	}

	TWeakObjectPtr<AMarbleRaceMenuHUD> MenuHUD;
	TSharedPtr<FGenericWindow> NativeWindow;
	bool bComposing = false;
};

AMarbleRaceMenuHUD::AMarbleRaceMenuHUD()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PreviewPlayer = CreateDefaultSubobject<UAudioComponent>(TEXT("ThemePreviewPlayer"));
	SetRootComponent(PreviewPlayer);
	PreviewPlayer->bAutoActivate = false;
	PreviewPlayer->bIsUISound = true;
	PreviewPlayer->bAllowSpatialization = false;
	PreviewPlayer->SetVolumeMultiplier(0.65f);

	// 颜色行提供的颜色。名单才持有小球的真实颜色；这份列表
	// 只是设置页可以盖到选中条目上的选项。
	PaletteColors = TArray<FLinearColor>{
		FLinearColor(0.92f, 0.28f, 0.28f),
		FLinearColor(0.95f, 0.60f, 0.18f),
		FLinearColor(0.95f, 0.86f, 0.24f),
		FLinearColor(0.42f, 0.84f, 0.34f),
		FLinearColor(0.22f, 0.80f, 0.64f),
		FLinearColor(0.26f, 0.62f, 0.96f),
		FLinearColor(0.52f, 0.44f, 0.96f),
		FLinearColor(0.90f, 0.40f, 0.78f),
		FLinearColor(0.96f, 0.96f, 0.98f),
		FLinearColor(0.42f, 0.46f, 0.54f)
	};
}

void AMarbleRaceMenuHUD::BeginPlay()
{
	Super::BeginPlay();

	// 字符：视口叠层里一个不可见、可聚焦的控件接收
	// 按键字符。不用全局委托的原因写在菜单按键控件里。
	if (FSlateApplication::IsInitialized())
	{
		if (UGameViewportClient* MenuViewportClient = GEngine ? GEngine->GameViewport : nullptr)
		{
			// 设成点击穿透，鼠标仍能到达游戏视口，同时控件
			// 仍能沿着焦点路径拿到键盘焦点。
			KeyCatcherWidget = SNew(SMarbleRaceMenuKeyCatcher)
				.MenuHUD(this)
				.Visibility(EVisibility::HitTestInvisible);
			MenuViewportClient->AddViewportWidgetContent(KeyCatcherWidget.ToSharedRef(), 10);
		}

		// 鼠标滚轮格数：只观察，不吃掉。
		WheelProcessor = MakeShared<FMarbleRaceMenuWheelProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(WheelProcessor);

		// 中文输入：输入法上下文只注册一次，文本框获得焦点时再激活。
		if (ITextInputMethodSystem* ImeSystem = FSlateApplication::Get().GetTextInputMethodSystem())
		{
			ImeContext = MakeShared<FMarbleRaceMenuImeContext>(this);
			ImeSystem->RegisterContext(StaticCastSharedRef<ITextInputMethodContext>(ImeContext.ToSharedRef()));
		}
	}
}

void AMarbleRaceMenuHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopThemePreview();
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication& SlateApplicationRef = FSlateApplication::Get();

		DeactivateImeContext();
		if (ImeContext.IsValid())
		{
			if (ITextInputMethodSystem* ImeSystem = SlateApplicationRef.GetTextInputMethodSystem())
			{
				// 注销上下文之前会检查它已经停用。
				ImeSystem->UnregisterContext(StaticCastSharedRef<ITextInputMethodContext>(ImeContext.ToSharedRef()));
			}
			ImeContext.Reset();
		}

		if (WheelProcessor.IsValid())
		{
			SlateApplicationRef.UnregisterInputPreProcessor(WheelProcessor);
			WheelProcessor.Reset();
		}

		if (KeyCatcherWidget.IsValid())
		{
			if (SlateApplicationRef.GetUserFocusedWidget(0) == KeyCatcherWidget)
			{
				SlateApplicationRef.ClearKeyboardFocus();
			}

			if (UGameViewportClient* MenuViewportClient = GEngine ? GEngine->GameViewport : nullptr)
			{
				MenuViewportClient->RemoveViewportWidgetContent(KeyCatcherWidget.ToSharedRef());
				// 把键盘焦点还给游戏视口，比赛界面的按键才能继续工作。
				if (TSharedPtr<SViewport> GameViewportWidget = MenuViewportClient->GetGameViewportWidget())
				{
					SlateApplicationRef.SetKeyboardFocus(GameViewportWidget, EFocusCause::SetDirectly);
				}
			}
			KeyCatcherWidget.Reset();
		}
	}

	Super::EndPlay(EndPlayReason);
}

UMarbleRaceRosterSubsystem* AMarbleRaceMenuHUD::GetRoster() const
{
	if (CachedRoster.IsValid())
	{
		return CachedRoster.Get();
	}

	if (UGameInstance* MenuGameInstance = GetGameInstance())
	{
		if (UMarbleRaceRosterSubsystem* Roster = MenuGameInstance->GetSubsystem<UMarbleRaceRosterSubsystem>())
		{
			CachedRoster = Roster;
			return Roster;
		}
	}
	return nullptr;
}

void AMarbleRaceMenuHUD::DrawHUD()
{
	Super::DrawHUD();

	// 菜单关卡是空的，整屏都由界面画；没有画布就
	// 没有地方可画，下面的辅助函数也都不安全。
	if (!Canvas)
	{
		return;
	}

	bClickConsumedThisFrame = false;

	ApplyMouseInputModeOnce();
	if (bQuitRequested)
	{
		DrawRect(BackgroundColor, 0.f, 0.f, Canvas->SizeX, Canvas->SizeY);
		DrawRecordingExitPage(Canvas->SizeX, Canvas->SizeY);
		PendingWheelDelta = 0.f;
		return;
	}
	EnsureKeyCatcherFocus();
	HandlePendingTextCommit();
	if (CurrentPage != EMarbleRaceMenuPage::CharacterSetup || DetailTabIndex != 2)
		StopThemePreview();
	if (FocusedTextField == EMarbleRaceMenuTextField::None)
	{
		if (APlayerController* Controller = GetOwningPlayerController(); Controller && Controller->WasInputKeyJustPressed(EKeys::Escape))
		{
			HandleEscapeToMainMenu();
		}
	}

	const float ScreenWidth = static_cast<float>(Canvas->SizeX);
	const float ScreenHeight = static_cast<float>(Canvas->SizeY);
	DrawRect(BackgroundColor, 0.0f, 0.0f, ScreenWidth, ScreenHeight);

	if (CurrentPage == EMarbleRaceMenuPage::CharacterSetup)
	{
		DrawCharacterSetupPage(ScreenWidth, ScreenHeight);
	}
	else if (CurrentPage == EMarbleRaceMenuPage::GameSettings)
	{
		DrawGameSettingsPage(ScreenWidth, ScreenHeight);
	}
	else
	{
		DrawMainMenuPage(ScreenWidth, ScreenHeight);
	}

	// 名单列表没用掉的滚轮格数，在帧末丢掉。
	PendingWheelDelta = 0.0f;
}

void AMarbleRaceMenuHUD::ApplyMouseInputModeOnce()
{
	if (bAppliedMouseInputMode)
	{
		return;
	}

	APlayerController* MenuController = GetOwningPlayerController();
	if (!MenuController)
	{
		// 第一帧可能还没有玩家控制器；下一帧再试。
		return;
	}
	bAppliedMouseInputMode = true;

	// 菜单用鼠标操作，这一关没有棋子。游戏加界面模式让光标可见，
	// 点击仍到达游戏视口，按键刚按下就是在那里读的。
	MenuController->SetShowMouseCursor(true);
	MenuController->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false));
}

void AMarbleRaceMenuHUD::EnsureKeyCatcherFocus()
{
	if (FocusedTextField == EMarbleRaceMenuTextField::None || !KeyCatcherWidget.IsValid())
	{
		return;
	}
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	// 点击视口会把焦点交给视口控件，所以编辑文本时要把焦点
	// 拿回来。这一关没有别的东西需要键盘焦点。
	FSlateApplication& SlateApplicationRef = FSlateApplication::Get();
	if (SlateApplicationRef.GetUserFocusedWidget(0) != KeyCatcherWidget)
	{
		SlateApplicationRef.SetKeyboardFocus(KeyCatcherWidget, EFocusCause::SetDirectly);
	}
}

void AMarbleRaceMenuHUD::HandlePendingTextCommit()
{
	if (FocusedTextField == EMarbleRaceMenuTextField::None)
	{
		return;
	}

	APlayerController* MenuController = GetOwningPlayerController();
	if (!MenuController || !MenuController->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		return;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (!MenuController->GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	// 点到输入框外面就提交，和普通文本框一样。这次
	// 点击故意不吃掉，光标下面的控件仍能被激活。
	const float TouchPadding = 6.0f;
	const bool bInsideField =
		MouseX >= ActiveTextFieldOrigin.X - TouchPadding
		&& MouseX <= ActiveTextFieldOrigin.X + ActiveTextFieldSize.X + TouchPadding
		&& MouseY >= ActiveTextFieldOrigin.Y - TouchPadding
		&& MouseY <= ActiveTextFieldOrigin.Y + ActiveTextFieldSize.Y + TouchPadding;

	if (!bInsideField)
	{
		CommitFocusedText();
	}
}

void AMarbleRaceMenuHUD::HandleKeyChar(const TCHAR Character)
{
	if (bQuitRequested) return;
	if (FocusedTextField == EMarbleRaceMenuTextField::None)
	{
		return;
	}

	// 控制字符以按键按下到来（退格、回车、退出、跳格）；
	// 有些键盘布局还会再以字符形式送一次。
	if (Character < 0x20 || Character == 0x7F)
	{
		return;
	}

	AppendTypedCharacter(Character);
}

bool AMarbleRaceMenuHUD::HandleKeyDown(const FKey& Key)
{
	if (bQuitRequested) return true;
	if (FocusedTextField == EMarbleRaceMenuTextField::None)
	{
		return false;
	}

	if (Key == EKeys::BackSpace)
	{
		RemoveLastTypedCharacter();
		return true;
	}
	if (Key == EKeys::Enter)
	{
		CommitFocusedText();
		return true;
	}
	if (Key == EKeys::Escape)
	{
		CancelFocusedText();
		return true;
	}
	if (Key == EKeys::Tab)
	{
		// 跳格在设置页的两个文本框之间切换。
		const EMarbleRaceMenuTextField NextField =
			(FocusedTextField == EMarbleRaceMenuTextField::DisplayName)
				? EMarbleRaceMenuTextField::PortraitPath
				: EMarbleRaceMenuTextField::DisplayName;
		DetailTabIndex = NextField == EMarbleRaceMenuTextField::DisplayName ? 0 : 1;
		StopThemePreview();
		FocusTextField(NextField);
		return true;
	}
	return false;
}

void AMarbleRaceMenuHUD::AppendTypedCharacter(const TCHAR Character)
{
	FString* EditBuffer = GetFocusedTextBuffer();
	if (!EditBuffer)
	{
		return;
	}

	// 输入法和按键控件都从这里进，所以光标始终在末尾，
	// 一个 TCHAR 就是一个码元：多字节中文会作为一个 TCHAR 到来。
	*EditBuffer += Character;
	CompositionBeginIndex = INDEX_NONE;
	CompositionLength = 0;
}

void AMarbleRaceMenuHUD::RemoveLastTypedCharacter()
{
	FString* EditBuffer = GetFocusedTextBuffer();
	if (!EditBuffer || EditBuffer->IsEmpty())
	{
		return;
	}

	const int32 LastIndex = EditBuffer->Len() - 1;
	int32 RemoveCount = 1;
	// 代理对成对删除，表情和生僻字才不会把字符串截坏。
	const TCHAR LastGlyph = (*EditBuffer)[LastIndex];
	if (LastIndex > 0 && LastGlyph >= 0xDC00 && LastGlyph <= 0xDFFF)
	{
		RemoveCount = 2;
	}
	*EditBuffer = EditBuffer->Left(EditBuffer->Len() - RemoveCount);
	CompositionBeginIndex = INDEX_NONE;
	CompositionLength = 0;
}

FString* AMarbleRaceMenuHUD::GetFocusedTextBuffer()
{
	switch (FocusedTextField)
	{
	case EMarbleRaceMenuTextField::DisplayName:
		return &NameEditBuffer;
	case EMarbleRaceMenuTextField::PortraitPath:
		return &PathEditBuffer;
	case EMarbleRaceMenuTextField::MusicStartTime:
		return &MusicStartEditBuffer;
	default:
		return nullptr;
	}
}

void AMarbleRaceMenuHUD::FocusTextField(EMarbleRaceMenuTextField Field)
{
	if (FocusedTextField == Field)
	{
		return;
	}

	// 新文本框接管之前，先把正在编辑的内容写回去。
	CommitFocusedText();

	FocusedTextField = Field;
	CompositionBeginIndex = INDEX_NONE;
	CompositionLength = 0;

	if (Field == EMarbleRaceMenuTextField::DisplayName)
	{
		NameEditBuffer = GetEntryName(SelectedEntryIndex);
	}
	else if (Field == EMarbleRaceMenuTextField::MusicStartTime)
	{
		if (UMarbleRaceRosterSubsystem* Roster = GetRoster(); Roster && Roster->IsValidIndex(SelectedEntryIndex))
			MusicStartEditBuffer = FString::Printf(TEXT("%.2f"), Roster->GetEntries()[SelectedEntryIndex].ThemeStartTimeSeconds);
	}

	ActivateImeContext();
	EnsureKeyCatcherFocus();
}

void AMarbleRaceMenuHUD::CommitFocusedText()
{
	const EMarbleRaceMenuTextField CommittedField = FocusedTextField;
	if (CommittedField == EMarbleRaceMenuTextField::None)
	{
		return;
	}

	DeactivateImeContext();
	FocusedTextField = EMarbleRaceMenuTextField::None;
	CompositionBeginIndex = INDEX_NONE;
	CompositionLength = 0;

	UMarbleRaceRosterSubsystem* Roster = GetRoster();
	if (!Roster)
	{
		return;
	}

	if (CommittedField == EMarbleRaceMenuTextField::DisplayName)
	{
		if (Roster->SetDisplayName(SelectedEntryIndex, NameEditBuffer))
		{
			SetStatus(FString::Printf(TEXT("名称已保存：%s"), *NameEditBuffer), false);
		}
	}
	else if (CommittedField == EMarbleRaceMenuTextField::PortraitPath)
	{
		ImportPortraitFromPath(PathEditBuffer);
	}
	else if (CommittedField == EMarbleRaceMenuTextField::MusicStartTime)
	{
		float Seconds = 0.f;
		if (!LexTryParseString(Seconds, *MusicStartEditBuffer.TrimStartAndEnd()) ||
			!FMath::IsFinite(Seconds) || Seconds < 0.f)
			SetStatus(TEXT("请输入有效的秒数，例如 45.5"), true);
		else if (Roster->SetThemeStartTime(SelectedEntryIndex, Seconds))
			SetStatus(TEXT("首次播放起点已保存"), false);
	}
}

void AMarbleRaceMenuHUD::CancelFocusedText()
{
	DeactivateImeContext();
	FocusedTextField = EMarbleRaceMenuTextField::None;
	CompositionBeginIndex = INDEX_NONE;
	CompositionLength = 0;
	SetStatus(TEXT("已取消编辑"), false);
}

void AMarbleRaceMenuHUD::ActivateImeContext()
{
	if (bImeContextActive || !ImeContext.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	ITextInputMethodSystem* ImeSystem = FSlateApplication::Get().GetTextInputMethodSystem();
	// 没有宿主窗口时，Windows 实现摆不了候选窗，
	// 所以上下文保持停用，避免输入法内部空指针。
	if (!ImeSystem || !ImeContext->GetWindow().IsValid())
	{
		return;
	}

	ImeSystem->ActivateContext(StaticCastSharedRef<ITextInputMethodContext>(ImeContext.ToSharedRef()));
	bImeContextActive = true;
}

void AMarbleRaceMenuHUD::DeactivateImeContext()
{
	if (!bImeContextActive)
	{
		return;
	}
	bImeContextActive = false;

	if (!ImeContext.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	if (ITextInputMethodSystem* ImeSystem = FSlateApplication::Get().GetTextInputMethodSystem())
	{
		ImeSystem->DeactivateContext(StaticCastSharedRef<ITextInputMethodContext>(ImeContext.ToSharedRef()));
	}
}

void AMarbleRaceMenuHUD::AddRosterScroll(float WheelDelta)
{
	PendingWheelDelta += WheelDelta;
}

void AMarbleRaceMenuHUD::ImportPortraitFromPath(const FString& SourcePath)
{
	UMarbleRaceRosterSubsystem* Roster = GetRoster();
	if (!Roster || !Roster->IsValidIndex(SelectedEntryIndex))
	{
		SetStatus(TEXT("请先在左侧选中一个小球"), true);
		return;
	}

	FString TrimmedPath = SourcePath;
	TrimmedPath.TrimStartAndEndInline();
	// 资源管理器的“复制为路径”会带引号；去掉之后导入才能直接用。
	TrimmedPath.RemoveFromStart(TEXT("\""));
	TrimmedPath.RemoveFromEnd(TEXT("\""));
	TrimmedPath.TrimStartAndEndInline();

	if (TrimmedPath.IsEmpty())
	{
		SetStatus(TEXT("请输入图片的完整路径，或直接点击上面的文件名"), true);
		return;
	}

	FString ErrorText;
	if (Roster->SetPortraitFromFile(SelectedEntryIndex, TrimmedPath, ErrorText))
	{
		PathEditBuffer = TrimmedPath;
		SetStatus(TEXT("头像已更新"), false);
	}
	else
	{
		// 子系统会说明哪里出错（文件不存在、读不了、格式不支持）。
		SetStatus(ErrorText, true);
	}
}

void AMarbleRaceMenuHUD::SetStatus(const FString& Message, bool bIsError)
{
	StatusMessage = Message;
	bStatusIsError = bIsError;
}

int32 AMarbleRaceMenuHUD::GetEntryTotal() const
{
	UMarbleRaceRosterSubsystem* Roster = GetRoster();
	return Roster ? Roster->GetEntryCount() : 0;
}

FString AMarbleRaceMenuHUD::GetEntryName(int32 EntryIndex) const
{
	if (UMarbleRaceRosterSubsystem* Roster = GetRoster())
	{
		if (Roster->IsValidIndex(EntryIndex))
		{
			return Roster->GetEntries()[EntryIndex].DisplayName;
		}
	}
	return TEXT("-");
}

FLinearColor AMarbleRaceMenuHUD::GetEntryColor(int32 EntryIndex) const
{
	if (UMarbleRaceRosterSubsystem* Roster = GetRoster())
	{
		if (Roster->IsValidIndex(EntryIndex))
		{
			return Roster->GetEntries()[EntryIndex].Color;
		}
	}
	return GetPaletteColorAt(0);
}

bool AMarbleRaceMenuHUD::IsEntryEnabled(int32 EntryIndex) const
{
	if (UMarbleRaceRosterSubsystem* Roster = GetRoster())
	{
		if (Roster->IsValidIndex(EntryIndex))
		{
			return Roster->GetEntries()[EntryIndex].bEnabled;
		}
	}
	return false;
}

FString AMarbleRaceMenuHUD::GetEntrySourcePath(int32 EntryIndex) const
{
	if (UMarbleRaceRosterSubsystem* Roster = GetRoster())
	{
		if (Roster->IsValidIndex(EntryIndex))
		{
			return Roster->GetEntries()[EntryIndex].PortraitSourcePath;
		}
	}
	return FString();
}

UTexture2D* AMarbleRaceMenuHUD::GetEntryPortrait(int32 EntryIndex)
{
	UMarbleRaceRosterSubsystem* Roster = GetRoster();
	return Roster ? Roster->GetPortraitTexture(EntryIndex) : nullptr;
}

FLinearColor AMarbleRaceMenuHUD::GetPaletteColorAt(int32 PaletteIndex) const
{
	if (PaletteColors.IsValidIndex(PaletteIndex))
	{
		return PaletteColors[PaletteIndex];
	}
	return FLinearColor(0.85f, 0.85f, 0.88f);
}

void AMarbleRaceMenuHUD::ScrollSelectionIntoView(float ListHeight)
{
	const int32 EntryCount = GetEntryTotal();
	if (EntryCount <= 0 || SelectedEntryIndex < 0 || SelectedEntryIndex >= EntryCount)
	{
		RosterScrollOffset = FMath::Max(0.0f, RosterScrollOffset);
		return;
	}

	const float RowHeightPixels = FMath::Max(58.0f, RowHeight);
	const float SelectedTop = SelectedEntryIndex * RowHeightPixels;
	const float SelectedBottom = SelectedTop + RowHeightPixels;

	if (SelectedTop < RosterScrollOffset)
	{
		RosterScrollOffset = SelectedTop;
	}
	else if (SelectedBottom > RosterScrollOffset + ListHeight)
	{
		RosterScrollOffset = SelectedBottom - ListHeight;
	}
	RosterScrollOffset = FMath::Max(0.0f, RosterScrollOffset);
}

void AMarbleRaceMenuHUD::EnterCharacterSetupPage()
{
	CurrentPage = EMarbleRaceMenuPage::CharacterSetup;
	ThemePageIndex = 0;
	PortraitFilePageIndex = 0;
	RosterScrollOffset = 0.0f;
	StatusMessage.Reset();
	bStatusIsError = false;

	if (UMarbleRaceRosterSubsystem* Roster = GetRoster())
	{
		// 主题曲来自资源注册表，头像来自 Saved/Portraits，所以两份列表
		// 进入页面时都重新扫描，把游戏没开时新加的文件收进来。
		Roster->RefreshAvailableThemes();
		Roster->RefreshPortraitFolder();

		if (!Roster->IsValidIndex(SelectedEntryIndex) && Roster->GetEntryCount() > 0)
		{
			SelectedEntryIndex = 0;
		}
		PathEditBuffer = GetEntrySourcePath(SelectedEntryIndex);
		bScrollSelectionPending = true;
	}
}

void AMarbleRaceMenuHUD::HandleEscapeToMainMenu()
{
	CommitFocusedText();
	StopThemePreview();
	CurrentPage = EMarbleRaceMenuPage::MainMenu;
	StatusMessage.Reset();
	bStatusIsError = false;
}

void AMarbleRaceMenuHUD::RequestQuit()
{
	if (bQuitRequested) return;
	CommitFocusedText();
	StopThemePreview();
	bQuitRequested = true;
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (auto* Recorder = Instance->GetSubsystem<UMarbleRaceRecorderSubsystem>())
			Recorder->StopRecording();
	}
	CompleteQuitIfReady();
}

bool AMarbleRaceMenuHUD::CanCompleteQuit() const
{
	if (!bQuitRequested || bQuitIssued) return false;
	if (UGameInstance* Instance = GetGameInstance())
	{
		if (const auto* Recorder = Instance->GetSubsystem<UMarbleRaceRecorderSubsystem>())
			return Recorder->GetPendingSaveCount() == 0;
	}
	return true;
}

void AMarbleRaceMenuHUD::CompleteQuitIfReady()
{
	if (!CanCompleteQuit()) return;
	bQuitIssued = true;
	// Only begin engine shutdown once recording saves are ready. Deinitialize's
	// protective wait then has no encoding work to block the game thread on.
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayerController(), EQuitPreference::Quit, false);
}

void AMarbleRaceMenuHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	CompleteQuitIfReady();
}

void AMarbleRaceMenuHUD::DrawRecordingExitPage(float ScreenWidth, float ScreenHeight)
{
	const int32 Dots = static_cast<int32>(FPlatformTime::Seconds() * 2.) % 4;
	const FString Label = TEXT("正在保存录制") + FString::ChrN(Dots, TEXT('.'));
	const float Scale = FMath::Min(1.f, ScreenWidth / 600.f);
	DrawSharpCenteredText(Label, ScreenWidth * .5f, ScreenHeight * .46f,
		FMath::Max(16, FMath::RoundToInt(HeadingFontSize * Scale)), TextColor, false);
	DrawSharpCenteredText(TEXT("保存完成后自动退出"), ScreenWidth * .5f, ScreenHeight * .46f + 48.f,
		FMath::Max(12, FMath::RoundToInt(BodyFontSize * Scale)), MutedTextColor, false);
}

//~ 绘制 -------------------------------------------------------------------------------

void AMarbleRaceMenuHUD::DrawSharpText(const FString& Text, float X, float Y, int32 FontSize,
                                       const FLinearColor& InColor, bool bOutline, bool bCenterOnPosition)
{
	if (!Canvas || !GEngine || !GEngine->GetMediumFont() || Text.IsEmpty())
	{
		return;
	}

	// 和比赛界面同一理由：要目标字号的字形，不去放大
	// 大约 10 像素的默认字体，那会让中文变糊。
	FSlateFontInfo FontInfo = GEngine->GetMediumFont()->GetLegacySlateFontInfo();
	FontInfo.Size = FMath::Max(6, FontSize);
	FCanvasTextItem TextItem(FVector2D::ZeroVector, FText::FromString(Text), FontInfo, InColor);
	TextItem.Scale = FVector2D(1.0f, 1.0f);
	if (bOutline)
	{
		TextItem.bOutlined = true;
		TextItem.OutlineColor = FLinearColor(0.012f, 0.02f, 0.03f, 0.9f);
	}
	TextItem.bCentreX = bCenterOnPosition;
	TextItem.bCentreY = bCenterOnPosition;
	TextItem.Position = FVector2D(FMath::RoundToFloat(X), FMath::RoundToFloat(Y));
	Canvas->DrawItem(TextItem);
}

void AMarbleRaceMenuHUD::DrawSharpCenteredText(const FString& Text, float CenterX, float CenterY, int32 FontSize,
                                               const FLinearColor& InColor, bool bOutline)
{
	DrawSharpText(Text, CenterX, CenterY, FontSize, InColor, bOutline, true);
}

void AMarbleRaceMenuHUD::DrawCircleBand(float CenterX, float CenterY, float InnerRadius, float OuterRadius,
                                        const FLinearColor& InColor)
{
	if (!Canvas || !Canvas->DefaultTexture || OuterRadius <= 0.0f)
	{
		return;
	}

	// 三角扇（实心圆）或三角带（圆环），不用一叠矩形：和
	// 比赛界面同一做法，色块在任何尺寸下都平滑。
	const float Inner = FMath::Clamp(InnerRadius, 0.0f, OuterRadius);
	const bool bSolid = Inner <= 0.01f;
	constexpr int32 Segments = 48;
	const FVector2D Center(CenterX, CenterY);

	TArray<FCanvasUVTri> Triangles;
	Triangles.Reserve(Segments * (bSolid ? 1 : 2));
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float AngleA = (2.0f * UE_PI * static_cast<float>(Index)) / static_cast<float>(Segments);
		const float AngleB = (2.0f * UE_PI * static_cast<float>(Index + 1)) / static_cast<float>(Segments);
		const FVector2D DirectionA(FMath::Cos(AngleA), FMath::Sin(AngleA));
		const FVector2D DirectionB(FMath::Cos(AngleB), FMath::Sin(AngleB));
		const FVector2D OuterA = Center + DirectionA * OuterRadius;
		const FVector2D OuterB = Center + DirectionB * OuterRadius;

		if (bSolid)
		{
			FCanvasUVTri Fan;
			Fan.V0_Pos = Center;
			Fan.V1_Pos = OuterA;
			Fan.V2_Pos = OuterB;
			Fan.V0_Color = InColor;
			Fan.V1_Color = InColor;
			Fan.V2_Color = InColor;
			Triangles.Add(Fan);
			continue;
		}

		const FVector2D InnerA = Center + DirectionA * Inner;
		const FVector2D InnerB = Center + DirectionB * Inner;

		FCanvasUVTri QuadA;
		QuadA.V0_Pos = InnerA;
		QuadA.V1_Pos = OuterA;
		QuadA.V2_Pos = OuterB;
		QuadA.V0_Color = InColor;
		QuadA.V1_Color = InColor;
		QuadA.V2_Color = InColor;
		Triangles.Add(QuadA);

		FCanvasUVTri QuadB;
		QuadB.V0_Pos = InnerA;
		QuadB.V1_Pos = OuterB;
		QuadB.V2_Pos = InnerB;
		QuadB.V0_Color = InColor;
		QuadB.V1_Color = InColor;
		QuadB.V2_Color = InColor;
		Triangles.Add(QuadB);
	}

	FCanvasTriangleItem Item(Triangles, Canvas->DefaultTexture->GetResource());
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void AMarbleRaceMenuHUD::DrawOutline(float X, float Y, float Width, float Height, const FLinearColor& InColor,
                                     float Thickness)
{
	const float EdgeThickness = FMath::Max(1.0f, Thickness);
	DrawRect(InColor, X, Y, Width, EdgeThickness);
	DrawRect(InColor, X, Y + Height - EdgeThickness, Width, EdgeThickness);
	DrawRect(InColor, X, Y + EdgeThickness, EdgeThickness, FMath::Max(0.0f, Height - EdgeThickness * 2.0f));
	DrawRect(InColor, X + Width - EdgeThickness, Y + EdgeThickness, EdgeThickness,
	         FMath::Max(0.0f, Height - EdgeThickness * 2.0f));
}

void AMarbleRaceMenuHUD::DrawPanel(float X, float Y, float Width, float Height, bool bHighlight)
{
	DrawRect(PanelColor, X, Y, Width, Height);
	DrawOutline(X, Y, Width, Height, bHighlight ? AccentColor : FLinearColor(0.14f, 0.18f, 0.24f),
	            bHighlight ? 2.0f : 1.0f);
}

bool AMarbleRaceMenuHUD::IsMouseOverRect(float X, float Y, float Width, float Height) const
{
	APlayerController* MenuController = GetOwningPlayerController();
	if (!MenuController)
	{
		return false;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (!MenuController->GetMousePosition(MouseX, MouseY))
	{
		return false;
	}
	return MouseX >= X && MouseX <= X + Width && MouseY >= Y && MouseY <= Y + Height;
}

bool AMarbleRaceMenuHUD::ConsumeClickInRect(float X, float Y, float Width, float Height)
{
	if (bClickConsumedThisFrame)
	{
		return false;
	}

	APlayerController* MenuController = GetOwningPlayerController();
	if (!MenuController || !MenuController->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		return false;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (!MenuController->GetMousePosition(MouseX, MouseY))
	{
		return false;
	}
	if (MouseX < X || MouseX > X + Width || MouseY < Y || MouseY > Y + Height)
	{
		return false;
	}

	// 只有光标下的第一个控件响应，叠在一起的矩形不会点两次。
	bClickConsumedThisFrame = true;
	return true;
}

bool AMarbleRaceMenuHUD::DrawButton(const FString& Label, float X, float Y, float Width, float Height,
                                    int32 FontSize, bool bSelected)
{
	const bool bHovered = IsMouseOverRect(X, Y, Width, Height);

	FLinearColor FillColor = bSelected ? AccentColor : ButtonColor;
	if (bHovered && !bSelected)
	{
		FillColor = ButtonHoverColor;
	}
	else if (bSelected && bHovered)
	{
		FillColor = FLinearColor(
			FMath::Min(1.0f, AccentColor.R + 0.12f),
			FMath::Min(1.0f, AccentColor.G + 0.12f),
			FMath::Min(1.0f, AccentColor.B + 0.12f),
			AccentColor.A);
	}
	DrawRect(FillColor, X, Y, Width, Height);
	DrawOutline(X, Y, Width, Height, bHovered ? TextColor : FLinearColor(0.20f, 0.25f, 0.33f), bHovered ? 2.0f : 1.0f);

	// 选中的小块用强调色填充，标签必须用深色才
	// 看得清；普通按钮仍是深底浅字。
	DrawSharpCenteredText(Label, X + Width * 0.5f, Y + Height * 0.5f, FontSize,
	                      bSelected ? FLinearColor(0.06f, 0.08f, 0.11f, 1.0f) : TextColor, !bSelected);

	return ConsumeClickInRect(X, Y, Width, Height);
}

bool AMarbleRaceMenuHUD::DrawTextField(const FString& Text, float X, float Y, float Width, float Height,
                                       bool bFocused)
{
	DrawRect(FLinearColor(0.035f, 0.045f, 0.062f, 1.0f), X, Y, Width, Height);
	DrawOutline(X, Y, Width, Height, bFocused ? AccentColor : FLinearColor(0.20f, 0.25f, 0.33f),
	            bFocused ? 2.0f : 1.0f);

	const float TextX = X + 10.0f;
	const float TextY = Y + Height * 0.5f - BodyFontSize * 0.5f;
	const auto MeasureWidth = [this](const FString& Value)
	{
		if (!GEngine || !GEngine->GetMediumFont() || !FSlateApplication::IsInitialized())
			return MarbleRaceMenuLayout::ApproximateTextWidth(Value, BodyFontSize);
		FSlateFontInfo Font = GEngine->GetMediumFont()->GetLegacySlateFontInfo();
		Font.Size = BodyFontSize;
		return static_cast<float>(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Value, Font).X);
	};
	FString VisibleText = Text;
	int32 HiddenPrefix = 0;
	if (bFocused)
	{
		while (VisibleText.Len() > 1 && MeasureWidth(VisibleText) > Width - 24.f)
		{
			const int32 Chop = VisibleText[0] >= 0xD800 && VisibleText[0] <= 0xDBFF ? 2 : 1;
			VisibleText.RightChopInline(Chop);
			HiddenPrefix += Chop;
		}
	}
	else VisibleText = FitText(Text, Width - 24.f, BodyFontSize);
	DrawSharpText(VisibleText, TextX, TextY, BodyFontSize, TextColor, false, false);

	if (bFocused)
	{
		// 记下来，好让输入法候选窗贴着文本框（也用来
		// 判断点击是否落在框内）。
		ActiveTextFieldOrigin = FVector2D(X, Y);
		ActiveTextFieldSize = FVector2D(FMath::Max(Width, 120.0f), Height);

		const float TextWidth = MeasureWidth(VisibleText);
		const float CaretX = FMath::Min(TextX + TextWidth + 1.0f, X + Width - 6.0f);
		DrawRect(AccentColor, CaretX, Y + 6.0f, 2.0f, Height - 12.0f);

		// 输入法正在组的那一段加下划线，和界面文本框的反馈一样。
		if (CompositionBeginIndex != INDEX_NONE && CompositionLength > 0
			&& CompositionBeginIndex >= HiddenPrefix && CompositionBeginIndex + CompositionLength <= Text.Len())
		{
			const float PrefixWidth = MeasureWidth(VisibleText.Left(CompositionBeginIndex - HiddenPrefix));
			const float CompositionWidth = MeasureWidth(Text.Mid(CompositionBeginIndex, CompositionLength));
			DrawRect(AccentColor, TextX + PrefixWidth, Y + Height - 8.0f, FMath::Max(4.0f, CompositionWidth), 2.0f);
		}
	}

	return ConsumeClickInRect(X, Y, Width, Height);
}

//~ 页面 ---------------------------------------------------------------------------------

void AMarbleRaceMenuHUD::DrawMainMenuPage(float ScreenWidth, float ScreenHeight)
{
	const float CenterX = ScreenWidth * 0.5f;
	const float TitleY = ScreenHeight * 0.19f;
	DrawSharpCenteredText(TitleText, CenterX, TitleY, TitleFontSize, TextColor, true);
	DrawSharpCenteredText(TEXT("侧视弹珠赛"), CenterX, TitleY + TitleFontSize * 0.95f, SmallFontSize + 2,
	                      MutedTextColor, false);

	const float ButtonWidth = FMath::Max(140.0f, MenuButtonWidth);
	const float ButtonHeight = FMath::Clamp(MenuButtonHeight, 32.0f, ScreenHeight * 0.085f);
	float ButtonY = FMath::Max(TitleY + TitleFontSize * 1.9f, ScreenHeight * 0.34f);

	if (DrawButton(TEXT("开始游戏"), CenterX - ButtonWidth * 0.5f, ButtonY, ButtonWidth, ButtonHeight,
	               ButtonFontSize, false))
	{
		CommitFocusedText();
		// 由比赛关卡的 World Settings 决定游戏模式，便于在蓝图中调整弹珠生成大小。
		UGameplayStatics::OpenLevel(
			this,
			RaceLevelName,
			true,
			TEXT(""));
		return;
	}
	ButtonY += ButtonHeight + MarbleRaceMenuLayout::Gap;

	if (DrawButton(TEXT("角色设置"), CenterX - ButtonWidth * 0.5f, ButtonY, ButtonWidth, ButtonHeight,
	               ButtonFontSize, false))
	{
		EnterCharacterSetupPage();
		return;
	}
	ButtonY += ButtonHeight + MarbleRaceMenuLayout::Gap;

	if (DrawButton(TEXT("游戏设置"), CenterX - ButtonWidth * 0.5f, ButtonY, ButtonWidth, ButtonHeight,
	               ButtonFontSize, false))
	{
		CurrentPage = EMarbleRaceMenuPage::GameSettings;
		HoveredSettingIndex = INDEX_NONE;
		return;
	}
	ButtonY += ButtonHeight + MarbleRaceMenuLayout::Gap;

	if (DrawButton(TEXT("退出"), CenterX - ButtonWidth * 0.5f, ButtonY, ButtonWidth, ButtonHeight,
	               ButtonFontSize, false))
	{
		RequestQuit();
		return;
	}

	// 名单摘要，主页面上就能看到下一场要用谁。
	if (UMarbleRaceRosterSubsystem* Roster = GetRoster())
	{
		DrawSharpCenteredText(
			FString::Printf(TEXT("角色名单：%d 个小球，其中 %d 个参赛"), Roster->GetEntryCount(), Roster->GetEnabledCount()),
			CenterX, ScreenHeight - 64.0f, BodyFontSize, MutedTextColor, false);
	}
	DrawSharpCenteredText(TEXT("提示：小球的数量、名称、头像与主题曲都在「角色设置」里修改"), CenterX,
	                      ScreenHeight - 38.0f, SmallFontSize, MutedTextColor, false);
}

void AMarbleRaceMenuHUD::DrawSettingsTooltip(const FString& Text, const FVector2D& Anchor, const float ScreenWidth, const float ScreenHeight)
{
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	const float Width = FMath::Min(550.f, ScreenWidth - 48.f);
	const float Height = 24.f + Lines.Num() * 28.f;
	const float X = FMath::Clamp(static_cast<float>(Anchor.X), 24.f, ScreenWidth - Width - 24.f);
	const float Y = FMath::Clamp(static_cast<float>(Anchor.Y), 24.f, ScreenHeight - Height - 24.f);
	DrawRect(FLinearColor(0.022f, 0.03f, 0.045f, 1.f), X, Y, Width, Height);
	DrawOutline(X, Y, Width, Height, FLinearColor(0.3f, 0.4f, 0.54f), 1.f);
	for (int32 Line = 0; Line < Lines.Num(); ++Line)
	{
		DrawSharpText(FitText(Lines[Line], Width - 28.f, SmallFontSize), X + 14.f, Y + 12.f + Line * 28.f,
			SmallFontSize, TextColor, false, false);
	}
}

void AMarbleRaceMenuHUD::DrawGameSettingsPage(float ScreenWidth, float ScreenHeight)
{
	const float Width = FMath::Min(900.f, ScreenWidth - 80.f);
	const float X = (ScreenWidth - Width) * 0.5f;
	const float Y = FMath::Max(42.f, ScreenHeight * 0.12f);
	DrawSharpText(TEXT("游戏设置"), X, Y, HeadingFontSize + 6, TextColor, false, false);
	const float TabsY = Y + 60.f;
	const TCHAR* Tabs[] = {TEXT("按键设置"), TEXT("显示设置"), TEXT("音频设置"), TEXT("录制设置")};
	for (int32 Tab = 0; Tab < 4; ++Tab)
	{
		if (DrawButton(Tabs[Tab], X + Tab * 156.f, TabsY, 144.f, 40.f, BodyFontSize, SettingsTabIndex == Tab))
		{
			SettingsTabIndex = Tab;
			HoveredSettingIndex = INDEX_NONE;
		}
	}
	const float PanelY = TabsY + 56.f;
	const float PanelHeight = 238.f;
	DrawPanel(X, PanelY, Width, PanelHeight, false);
	auto* Settings = GetGameInstance()->GetSubsystem<UMarbleRaceSettingsSubsystem>();
	const TCHAR* DisplayNames[] = {TEXT("稳定名称显示"), TEXT("终点慢动作"), TEXT("倒计时缩放过渡")};
	const TCHAR* DisplayHelp[] = {
		TEXT("减轻姓名抖动，关闭后保留原有滚动效果。"),
		TEXT("临近终点时放慢场景，冲线阶段约 2.4 秒。\nBGM 仍以正常速度播放。"),
		TEXT("倒计时开始时镜头由近拉远，约 1.8 秒完成。")};
	const TCHAR* KeyNames[] = {TEXT("锁定 BGM"), TEXT("解锁 BGM"), TEXT("中断比赛")};
	const TCHAR* KeyLabels[] = {TEXT("L"), TEXT("U"), TEXT("ESC")};
	const TCHAR* KeyHelp[] = {
		TEXT("保持当前 BGM。左下角出现小红点表示已锁定。"),
		TEXT("解锁后立即检查当前领跑角色的 BGM。"),
		TEXT("立即返回主菜单，并丢弃本场录制。")};
	const TCHAR* AudioNames[] = {TEXT("统一 BGM 响度"), TEXT("赛后播完当前 BGM"), TEXT("后台播放 BGM")};
	const TCHAR* AudioHelp[] = {
		TEXT("让各角色歌曲的整体响度一致，保留歌曲本身的强弱变化。\n比赛和菜单试听使用同一校准。"),
		TEXT("最后一颗弹珠冲线后，当前歌曲继续到本轮结尾。\n播完停止，不再循环；退出比赛时立即停止。"),
		TEXT("开赛后所有角色歌曲从头同步推进，只有领跑者可听到。\n忽略角色的播放起点；歌曲播完后从头循环。")};
	const TCHAR* RecordingNames[] = {TEXT("自动录制比赛"), TEXT("录制保存位置")};
	const TCHAR* RecordingHelp[] = {
		TEXT("录制游戏画面与声音，排行榜展示结束后保存 MP4。\nESC 中断会丢弃本场录制；最高 1080p、30 帧。"),
		TEXT("打开 Saved/Recordings 文件夹。\n录制只包含游戏画面和游戏声音，不录桌面或麦克风。")};
	const int32 RowCount = SettingsTabIndex == 3 ? 2 : 3;
	int32 HoveredRow = INDEX_NONE;
	for (int32 Row = 0; Row < RowCount; ++Row)
	{
		const float RowY = PanelY + 16.f + Row * 68.f;
		const bool bHovered = IsMouseOverRect(X + 12.f, RowY, Width - 24.f, 62.f);
		if (bHovered) HoveredRow = Row;
		DrawRect(bHovered ? ButtonColor : FLinearColor(0.045f, 0.06f, 0.086f), X + 12.f, RowY, Width - 24.f, 62.f);
		DrawSharpText(SettingsTabIndex == 1 ? DisplayNames[Row] : SettingsTabIndex == 2 ? AudioNames[Row]
			: SettingsTabIndex == 3 ? RecordingNames[Row] : KeyNames[Row], X + 32.f, RowY + 18.f,
			BodyFontSize, TextColor, false, false);
		const float ControlX = X + Width - 144.f;
		if (SettingsTabIndex == 1)
		{
			const bool bEnabled = Settings && (Row == 0 ? Settings->AreMarbleNamesStable()
				: Row == 1 ? Settings->IsFinishSlowMotionEnabled() : Settings->IsCountdownZoomEnabled());
			if (DrawButton(bEnabled ? TEXT("启用") : TEXT("禁用"), ControlX, RowY + 13.f, 112.f, 36.f, BodyFontSize, bEnabled) && Settings)
			{
				if (Row == 0) Settings->SetStableMarbleNames(!bEnabled);
				else if (Row == 1) Settings->SetFinishSlowMotion(!bEnabled);
				else Settings->SetCountdownZoom(!bEnabled);
			}
		}
		else if (SettingsTabIndex == 2 || (SettingsTabIndex == 3 && Row == 0))
		{
			const bool bEnabled = Settings && (SettingsTabIndex == 3 ? Settings->ShouldRecordRace()
				: Row == 0 ? Settings->IsMusicNormalizationEnabled()
				: Row == 1 ? Settings->ShouldFinishBGM() : Settings->IsBackgroundMusicEnabled());
			if (DrawButton(bEnabled ? TEXT("启用") : TEXT("禁用"), ControlX, RowY + 13.f, 112.f, 36.f, BodyFontSize, bEnabled) && Settings)
			{
				if (SettingsTabIndex == 3) Settings->SetRecordRace(!bEnabled);
				else if (Row == 0) Settings->SetMusicNormalization(!bEnabled);
				else if (Row == 1) Settings->SetFinishBGM(!bEnabled);
				else Settings->SetBackgroundMusic(!bEnabled);
			}
		}
		else if (SettingsTabIndex == 3)
		{
			if (DrawButton(TEXT("打开文件夹"), ControlX - 32.f, RowY + 13.f, 144.f, 36.f, SmallFontSize, false))
			{
				const FString Folder = UMarbleRaceRecorderSubsystem::GetRecordingsDirectory();
				IFileManager::Get().MakeDirectory(*Folder, true);
				FPlatformProcess::ExploreFolder(*Folder);
			}
		}
		else
		{
			DrawRect(ButtonColor, ControlX, RowY + 13.f, 112.f, 36.f);
			DrawOutline(ControlX, RowY + 13.f, 112.f, 36.f, FLinearColor(0.25f, 0.32f, 0.42f), 1.f);
			DrawSharpCenteredText(KeyLabels[Row], ControlX + 56.f, RowY + 31.f,
				BodyFontSize + 2, TextColor, false);
		}
	}
	const float BottomY = PanelY + PanelHeight + 24.f;
	if (DrawButton(TEXT("返回主菜单"), X, BottomY, 168.f, 40.f, BodyFontSize, false)) HandleEscapeToMainMenu();
	DrawSharpText(TEXT("设置自动保存"), X + Width - 140.f, BottomY + 10.f, SmallFontSize, MutedTextColor, false, false);
	if (SettingsTabIndex == 3)
	{
		const auto* Recorder = GetGameInstance()->GetSubsystem<UMarbleRaceRecorderSubsystem>();
		DrawSharpText(FitText(Recorder->GetStatus().IsEmpty() ? TEXT("保存至 Saved/Recordings") : Recorder->GetStatus(), Width, SmallFontSize),
			X, PanelY + 166.f, SmallFontSize, MutedTextColor, false, false);
	}
	const double Now = GetWorld()->GetRealTimeSeconds();
	if (HoveredRow != HoveredSettingIndex)
	{
		HoveredSettingIndex = HoveredRow;
		SettingHoverStartedAt = Now;
	}
	if (HoveredRow != INDEX_NONE && Now - SettingHoverStartedAt >= 0.35)
	{
		float MouseX = X, MouseY = BottomY;
		if (APlayerController* Controller = GetOwningPlayerController()) Controller->GetMousePosition(MouseX, MouseY);
		DrawSettingsTooltip(SettingsTabIndex == 1 ? DisplayHelp[HoveredRow] : SettingsTabIndex == 2 ? AudioHelp[HoveredRow]
			: SettingsTabIndex == 3 ? RecordingHelp[HoveredRow] : KeyHelp[HoveredRow],
			FVector2D(MouseX + 18.f, MouseY + 24.f), ScreenWidth, ScreenHeight);
	}
}

void AMarbleRaceMenuHUD::DrawCharacterSetupPage(float ScreenWidth, float ScreenHeight)
{
	const float Margin = MarbleRaceMenuLayout::Margin;
	DrawSharpText(TEXT("角色设置"), Margin, 12.0f, HeadingFontSize + 6, TextColor, false, false);
	DrawSharpText(TEXT("点击左侧小球选中，再在右侧修改名称／颜色／头像／主题曲"),
	              Margin, 58.0f, SmallFontSize, MutedTextColor, false, false);

	const float ContentTop = MarbleRaceMenuLayout::HeaderHeight + 10.0f;
	const float HintTop = ScreenHeight - MarbleRaceMenuLayout::BottomBarHeight - MarbleRaceMenuLayout::HintHeight;
	const float ContentHeight = FMath::Max(180.0f, HintTop - ContentTop - 6.0f);

	const float ListWidth = FMath::Clamp(ScreenWidth * FMath::Min(ListWidthFraction, 0.32f), 280.0f, 390.0f);
	const float ListX = Margin;
	const float DetailX = ListX + ListWidth + MarbleRaceMenuLayout::Gap;
	const float DetailWidth = FMath::Max(300.0f, ScreenWidth - DetailX - Margin);

	DrawRosterList(ListX, ContentTop, ListWidth, ContentHeight);

	DrawPanel(DetailX, ContentTop, DetailWidth, ContentHeight, false);
	DetailBottom = ContentTop + ContentHeight - 16.f;
	float DetailY = ContentTop + 16.f;
	DrawEntrySettings(DetailX + 18.f, DetailY, DetailWidth - 36.f);

	// 提示行：规格里的限制，加上头像选择器读取的文件夹。
	DrawSharpText(TEXT("修改自动保存 · 左侧参赛开关决定下一场名单 · Esc 返回主菜单"), Margin, HintTop, SmallFontSize,
	              MutedTextColor, false, false);
	if (!StatusMessage.IsEmpty())
	{
		DrawSharpText(FitText(StatusMessage, ScreenWidth - Margin * 2.f, SmallFontSize), Margin, HintTop + 20.0f, SmallFontSize,
		              bStatusIsError ? ErrorColor : AccentColor, false, false);
	}

	DrawBottomBar(ScreenWidth, ScreenHeight);
}

void AMarbleRaceMenuHUD::DrawRosterList(float ListX, float ListY, float ListWidth, float ListHeight)
{
	DrawPanel(ListX, ListY, ListWidth, ListHeight, false);

	const float HeaderHeight = 36.0f;
	const float FooterHeight = 34.0f;
	const float RowsTop = ListY + HeaderHeight;
	const float RowsHeight = FMath::Max(RowHeight + 4.0f, ListHeight - HeaderHeight - FooterHeight);
	const float RowsBottom = RowsTop + RowsHeight;

	const int32 EntryCount = GetEntryTotal();
	const float RowHeightPixels = FMath::Max(58.0f, RowHeight);
	const float ContentHeight = EntryCount * RowHeightPixels;
	const float MaxScroll = FMath::Max(0.0f, ContentHeight - RowsHeight);

	// 滚动输入：滚轮格数（经输入预处理）以及在
	// 列表里拖动。选中行始终被拉回可见范围，偏移每帧
	// 都做限制，名单变短时也不会滚过末尾。
	APlayerController* MenuController = GetOwningPlayerController();
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	const bool bHasMouse = MenuController && MenuController->GetMousePosition(MouseX, MouseY);
	const bool bMouseOverRows = bHasMouse && MouseX >= ListX && MouseX <= ListX + ListWidth
		&& MouseY >= RowsTop && MouseY <= RowsBottom;

	if (bHasMouse && MenuController)
	{
		const bool bLeftDown = MenuController->IsInputKeyDown(EKeys::LeftMouseButton);
		if (bLeftDown && !bDraggingRoster && bMouseOverRows && !bClickConsumedThisFrame)
		{
			bDraggingRoster = true;
			LastDragMouseY = MouseY;
		}
		if (!bLeftDown)
		{
			bDraggingRoster = false;
		}

		if (bDraggingRoster && bMouseOverRows)
		{
			RosterScrollOffset += LastDragMouseY - MouseY;
			LastDragMouseY = MouseY;
		}
	}

	if (bMouseOverRows && FMath::Abs(PendingWheelDelta) > UE_KINDA_SMALL_NUMBER)
	{
		RosterScrollOffset -= PendingWheelDelta * WheelScrollPixels;
		PendingWheelDelta = 0.0f;
	}

	// 只有选中项变化时才把它拉回视野，用滚轮或拖动
	// 做的手动滚动不会在下一帧被撤掉。
	if (bScrollSelectionPending)
	{
		ScrollSelectionIntoView(RowsHeight);
		bScrollSelectionPending = false;
	}
	RosterScrollOffset = FMath::Clamp(RosterScrollOffset, 0.0f, MaxScroll);

	// Only draw complete rows: Canvas glyphs can extend past a partially
	// covered row and otherwise bleed through the footer into the hints.
	const bool bPreviouslyConsumed = bClickConsumedThisFrame;
	if (!bMouseOverRows) bClickConsumedThisFrame = true;
	for (int32 EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
	{
		const float RowY = RowsTop + EntryIndex * RowHeightPixels - RosterScrollOffset;
		if (RowY < RowsTop || RowY + RowHeightPixels > RowsBottom)
		{
			continue;
		}
		DrawRosterRow(EntryIndex, ListX + 4.0f, RowY, ListWidth - 8.0f, RowHeightPixels);
	}
	if (!bMouseOverRows) bClickConsumedThisFrame = bPreviouslyConsumed;

	// 页眉
	DrawRect(PanelColor, ListX + 1.0f, ListY + 1.0f, ListWidth - 2.0f, HeaderHeight);
	DrawSharpText(FString::Printf(TEXT("小球列表（%d 个）"), EntryCount), ListX + 12.0f, ListY + 9.0f,
	              BodyFontSize, TextColor, false, false);

	// 页脚带明确的滚动按钮，没有滚轮也能用列表。
	DrawRect(PanelColor, ListX + 1.0f, RowsBottom, ListWidth - 2.0f, FooterHeight);
	if (DrawButton(TEXT("上滚"), ListX + 12.0f, RowsBottom + 4.0f, 68.0f, 26.0f, SmallFontSize, false))
	{
		RosterScrollOffset -= RowHeightPixels * 2.0f;
	}
	if (DrawButton(TEXT("下滚"), ListX + 86.0f, RowsBottom + 4.0f, 68.0f, 26.0f, SmallFontSize, false))
	{
		RosterScrollOffset += RowHeightPixels * 2.0f;
	}

	const int32 FirstVisibleRow = EntryCount > 0
		                              ? FMath::Clamp(FMath::CeilToInt(RosterScrollOffset / RowHeightPixels) + 1, 1,
		                                             EntryCount)
		                              : 0;
	DrawSharpText(FString::Printf(TEXT("第 %d / %d 行"), FirstVisibleRow, EntryCount), ListX + 166.0f,
	              RowsBottom + 9.0f, SmallFontSize, MutedTextColor, false, false);
}

void AMarbleRaceMenuHUD::DrawRosterRow(int32 EntryIndex, float RowX, float RowY, float RowWidth,
                                       float RowHeightPixels)
{
	UMarbleRaceRosterSubsystem* Roster = GetRoster();
	const bool bSelected = (EntryIndex == SelectedEntryIndex);
	const bool bEnabled = IsEntryEnabled(EntryIndex);
	const FLinearColor EntryColor = GetEntryColor(EntryIndex);
	const float CenterY = RowY + RowHeightPixels * 0.5f;

	if (bSelected)
	{
		DrawRect(FLinearColor(AccentColor.R, AccentColor.G, AccentColor.B, 0.22f), RowX, RowY, RowWidth,
		         RowHeightPixels);
	}
	DrawOutline(RowX, RowY, RowWidth, RowHeightPixels,
	            bSelected ? AccentColor : FLinearColor(0.13f, 0.16f, 0.21f), 1.0f);

	// 色块。条目未参赛时变暗。
	const FLinearColor SwatchColor = bEnabled ? EntryColor : EntryColor * FLinearColor(0.45f, 0.45f, 0.45f, 1.0f);
	DrawCircleBand(RowX + 20.0f, CenterY, 0.0f, 12.0f, SwatchColor);

	// 色块旁边的头像缩略图（没有则是占位框）。
	const float ThumbnailSize = 30.0f;
	const float ThumbnailX = RowX + 38.0f;
	const float ThumbnailY = CenterY - ThumbnailSize * 0.5f;
	if (UTexture2D* Portrait = GetEntryPortrait(EntryIndex))
	{
		DrawTexture(Portrait, ThumbnailX, ThumbnailY, ThumbnailSize, ThumbnailSize, 0.0f, 0.0f, 1.0f, 1.0f,
		            FLinearColor::White, BLEND_Translucent);
	}
	else
	{
		DrawRect(FLinearColor(0.09f, 0.11f, 0.15f, 1.0f), ThumbnailX, ThumbnailY, ThumbnailSize, ThumbnailSize);
		DrawSharpCenteredText(TEXT("无"), ThumbnailX + ThumbnailSize * 0.5f, CenterY, SmallFontSize - 4,
		                      MutedTextColor, false);
	}
	DrawOutline(ThumbnailX, ThumbnailY, ThumbnailSize, ThumbnailSize, FLinearColor(0.20f, 0.24f, 0.30f), 1.0f);

	// 第一行姓名，第二行主题曲：两行短字不会冲到
	// 参赛开关上，一行长字则会。
	const float TextX = ThumbnailX + ThumbnailSize + 10.0f;
	const float TextWidth = RowX + RowWidth - 66.f - TextX;
	DrawSharpText(FitText(GetEntryName(EntryIndex), TextWidth, BodyFontSize), TextX, RowY + 5.0f, BodyFontSize,
	              bEnabled ? TextColor : MutedTextColor, false, false);

	const FString ThemeName = Roster ? Roster->GetThemeDisplayName(EntryIndex) : FString(TEXT("-"));
	DrawSharpText(FitText(ThemeName, TextWidth, SmallFontSize), TextX, RowY + 32.f, SmallFontSize,
	              bEnabled ? MutedTextColor : FLinearColor(0.45f, 0.47f, 0.50f), false, false);

	// 参赛开关。浅色表示参加下一场，深色表示跳过。
	const float ToggleWidth = 54.0f;
	if (DrawButton(bEnabled ? TEXT("参赛") : TEXT("停用"), RowX + RowWidth - ToggleWidth - 6.0f, CenterY - 14.0f,
	               ToggleWidth, 28.0f, SmallFontSize, bEnabled))
	{
		if (Roster)
		{
			Roster->SetEnabled(EntryIndex, !bEnabled);
		}
	}
	else if (ConsumeClickInRect(RowX, RowY, RowWidth, RowHeightPixels))
	{
		// 选中另一行之前，先把正在编辑的内容写回去。
		CommitFocusedText();
		if (SelectedEntryIndex != EntryIndex) StopThemePreview();
		SelectedEntryIndex = EntryIndex;
		ThemePageIndex = 0;
		PortraitFilePageIndex = 0;
		PathEditBuffer = GetEntrySourcePath(EntryIndex);
		bScrollSelectionPending = true;
	}
}

void AMarbleRaceMenuHUD::DrawEntrySettings(float PanelX, float& PanelY, float PanelWidth)
{
	UMarbleRaceRosterSubsystem* Roster = GetRoster();
	if (!Roster || !Roster->IsValidIndex(SelectedEntryIndex))
	{
		DrawSharpText(TEXT("还没有选中任何小球"), PanelX, PanelY, BodyFontSize + 2, TextColor, false, false);
		DrawSharpText(TEXT("点击左侧列表中的一行，或用下方的「新建小球」创建一个"),
		              PanelX, PanelY + 28.0f, BodyFontSize, MutedTextColor, false, false);
		PanelY += 72.0f;
		return;
	}

	DrawSharpText(FitText(GetEntryName(SelectedEntryIndex), PanelWidth, HeadingFontSize),
		PanelX, PanelY, HeadingFontSize, TextColor, false, false);
	PanelY += 48.f;
	const TCHAR* Tabs[] = {TEXT("基本信息"), TEXT("头像"), TEXT("主题曲")};
	const float TabWidth = FMath::Min(140.f, (PanelWidth - 16.f) / 3.f);
	for (int32 Tab = 0; Tab < 3; ++Tab)
	{
		if (DrawButton(Tabs[Tab], PanelX + Tab * (TabWidth + 8.f), PanelY, TabWidth, 34.f, BodyFontSize, DetailTabIndex == Tab))
		{
			CommitFocusedText();
			if (Tab != 2) StopThemePreview();
			DetailTabIndex = Tab;
		}
	}
	PanelY += 56.f;
	if (DetailTabIndex == 0)
	{
		DrawNameSection(PanelX, PanelY, PanelWidth);
		DrawColorSection(PanelX, PanelY, PanelWidth);
	}
	else if (DetailTabIndex == 1) DrawPortraitSection(PanelX, PanelY, PanelWidth);
	else DrawThemeSection(PanelX, PanelY, PanelWidth);
}

void AMarbleRaceMenuHUD::DrawNameSection(float PanelX, float& PanelY, float PanelWidth)
{
	const bool bFocused = (FocusedTextField == EMarbleRaceMenuTextField::DisplayName);
	DrawSharpText(TEXT("名称（点击后输入，支持中文输入法）"), PanelX, PanelY, SmallFontSize + 1,
	              MutedTextColor, false, false);

	const float FieldHeight = 42.0f;
	const float FieldWidth = FMath::Min(PanelWidth, 420.0f);
	const FString ShownName = bFocused ? NameEditBuffer : GetEntryName(SelectedEntryIndex);
	if (DrawTextField(ShownName, PanelX, PanelY + 28.0f, FieldWidth, FieldHeight, bFocused))
	{
		FocusTextField(EMarbleRaceMenuTextField::DisplayName);
	}

	if (bFocused)
	{
		DrawSharpText(TEXT("Enter 保存　Esc 取消"), PanelX,
		              PanelY + 78.f, SmallFontSize, MutedTextColor, false,
		              false);
	}

	PanelY += 112.f;
}

void AMarbleRaceMenuHUD::DrawColorSection(float PanelX, float& PanelY, float PanelWidth)
{
	DrawSharpText(TEXT("弹珠颜色（保持不透明）"), PanelX, PanelY, SmallFontSize + 1, MutedTextColor, false, false);

	const float SwatchY = PanelY + 40.0f;
	const float Spacing = 34.0f;
	const int32 Columns = FMath::Max(1, FMath::FloorToInt((PanelWidth - 4.f) / Spacing));
	const FLinearColor CurrentColor = GetEntryColor(SelectedEntryIndex);

	for (int32 SwatchIndex = 0; SwatchIndex < PaletteColors.Num(); ++SwatchIndex)
	{
		const float SwatchX = PanelX + 16.0f + (SwatchIndex % Columns) * Spacing;
		const float RowY = SwatchY + (SwatchIndex / Columns) * Spacing;

		const FLinearColor SwatchColor = GetPaletteColorAt(SwatchIndex);
		const bool bHovered = IsMouseOverRect(SwatchX - 14.0f, RowY - 14.0f, 28.0f, 28.0f);

		DrawCircleBand(SwatchX, RowY, 0.0f, bHovered ? 13.0f : 11.0f, SwatchColor);
		if (SwatchColor.Equals(CurrentColor, 0.02f))
		{
			DrawCircleBand(SwatchX, RowY, 13.5f, 15.5f, AccentColor);
		}
		else if (bHovered)
		{
			DrawCircleBand(SwatchX, RowY, 13.0f, 15.0f, TextColor);
		}

		if (ConsumeClickInRect(SwatchX - 16.0f, RowY - 16.0f, 32.0f, 32.0f))
		{
			if (UMarbleRaceRosterSubsystem* Roster = GetRoster())
			{
				Roster->SetColor(SelectedEntryIndex, SwatchColor);
			}
		}
	}

	PanelY = SwatchY + FMath::DivideAndRoundUp(PaletteColors.Num(), Columns) * Spacing;
}

void AMarbleRaceMenuHUD::DrawPortraitSection(float PanelX, float& PanelY, float PanelWidth)
{
	UMarbleRaceRosterSubsystem* Roster = GetRoster();
	DrawSharpText(TEXT("头像图片"), PanelX, PanelY, SmallFontSize + 1, MutedTextColor, false, false);
	float CursorY = PanelY + 30.0f;

	// 当前头像，以及它从哪里来。
	const float PreviewSize = 54.0f;
	DrawRect(FLinearColor(0.05f, 0.06f, 0.09f, 1.0f), PanelX, CursorY, PreviewSize, PreviewSize);
	if (UTexture2D* Portrait = GetEntryPortrait(SelectedEntryIndex))
	{
		DrawTexture(Portrait, PanelX, CursorY, PreviewSize, PreviewSize, 0.0f, 0.0f, 1.0f, 1.0f,
		            FLinearColor::White, BLEND_Translucent);
	}
	DrawOutline(PanelX, CursorY, PreviewSize, PreviewSize, FLinearColor(0.20f, 0.24f, 0.30f), 1.0f);

	const FString SourcePath = GetEntrySourcePath(SelectedEntryIndex);
	DrawSharpText(TEXT("当前："), PanelX + PreviewSize + 10.0f, CursorY + 2.0f, SmallFontSize, MutedTextColor, false,
	              false);
	DrawSharpText(FitText(SourcePath.IsEmpty() ? TEXT("（没有图片，使用彩色圆盘＋首字）") : SourcePath,
	                      PanelWidth - PreviewSize - 10.f, SmallFontSize),
	              PanelX + PreviewSize + 10.0f, CursorY + 28.0f, SmallFontSize,
	              SourcePath.IsEmpty() ? MutedTextColor : TextColor, false, false);
	CursorY += PreviewSize + 10.0f;

	// Saved/Portraits 里找到的文件，分页显示，列表不会超出面板。
	DrawSharpText(TEXT("Saved/Portraits 里的图片（点击即用）"), PanelX, CursorY, SmallFontSize, MutedTextColor, false, false);
	CursorY += 28.0f;

	static const TArray<FString> EmptyFileList;
	const TArray<FString>& FolderFiles = Roster ? Roster->GetPortraitFolderFiles() : EmptyFileList;
	const int32 FileCount = FolderFiles.Num();
	const int32 FilesPerPage = FMath::Clamp(FMath::FloorToInt((DetailBottom - CursorY - 168.f) / 28.f), 1, FMath::Max(1, PortraitFilesPerPage));
	const int32 FilePageCount = FMath::Max(1, FMath::DivideAndRoundUp(FileCount, FilesPerPage));
	PortraitFilePageIndex = FMath::Clamp(PortraitFilePageIndex, 0, FilePageCount - 1);
	const int32 FirstFileIndex = PortraitFilePageIndex * FilesPerPage;

	if (FileCount == 0)
	{
		DrawSharpText(TEXT("文件夹为空：把 png/jpg 放进去后点「刷新列表」"), PanelX, CursorY, SmallFontSize,
		              MutedTextColor, false, false);
		CursorY += 30.0f;
	}

	for (int32 Slot = 0; Slot < FilesPerPage; ++Slot)
	{
		const int32 FileIndex = FirstFileIndex + Slot;
		if (!FolderFiles.IsValidIndex(FileIndex))
		{
			break;
		}
		if (DrawButton(FitText(FolderFiles[FileIndex], PanelWidth - 24.f, SmallFontSize), PanelX, CursorY + Slot * 28.0f, PanelWidth, 26.0f,
		               SmallFontSize, false))
		{
			ImportPortraitFromPath(FPaths::Combine(UMarbleRaceRosterSubsystem::GetPortraitFolderPath(),
			                                       FolderFiles[FileIndex]));
		}
	}
	CursorY += FMath::Clamp(FileCount - FirstFileIndex, 0, FilesPerPage) * 28.0f;

	if (FilePageCount > 1)
	{
		if (DrawButton(TEXT("上一页"), PanelX, CursorY, 78.0f, 26.0f, SmallFontSize, false))
		{
			--PortraitFilePageIndex;
		}
		if (DrawButton(TEXT("下一页"), PanelX + 84.0f, CursorY, 78.0f, 26.0f, SmallFontSize, false))
		{
			++PortraitFilePageIndex;
		}
		DrawSharpText(FString::Printf(TEXT("第 %d/%d 页"), PortraitFilePageIndex + 1, FilePageCount),
		              PanelX + 170.0f, CursorY + 5.0f, SmallFontSize, MutedTextColor, false, false);
		CursorY += 32.0f;
		PortraitFilePageIndex = FMath::Clamp(PortraitFilePageIndex, 0, FilePageCount - 1);
	}

	// 手填绝对路径，给不在上面那个文件夹里的图片用。
	DrawSharpText(TEXT("或输入图片的绝对路径"), PanelX, CursorY, SmallFontSize, MutedTextColor, false, false);
	CursorY += 28.0f;

	const float PathFieldHeight = 38.0f;
	const float ImportButtonWidth = 76.0f;
	const float PathFieldWidth = FMath::Max(120.0f, PanelWidth - ImportButtonWidth - 8.0f);
	if (DrawTextField(PathEditBuffer, PanelX, CursorY, PathFieldWidth, PathFieldHeight,
	                  FocusedTextField == EMarbleRaceMenuTextField::PortraitPath))
	{
		FocusTextField(EMarbleRaceMenuTextField::PortraitPath);
	}
	if (DrawButton(TEXT("导入"), PanelX + PathFieldWidth + 8.0f, CursorY, ImportButtonWidth, PathFieldHeight,
	               SmallFontSize, false))
	{
		ImportPortraitFromPath(PathEditBuffer);
	}
	CursorY += PathFieldHeight + 8.0f;

	if (DrawButton(TEXT("清除图片"), PanelX, CursorY, 96.0f, 28.0f, SmallFontSize, false))
	{
		if (Roster && Roster->ClearPortrait(SelectedEntryIndex))
		{
			PathEditBuffer.Reset();
			SetStatus(TEXT("头像已清除"), false);
		}
	}
	if (DrawButton(TEXT("刷新列表"), PanelX + 104.0f, CursorY, 96.0f, 28.0f, SmallFontSize, false))
	{
		if (Roster)
		{
			Roster->RefreshPortraitFolder();
		}
		PortraitFilePageIndex = 0;
		SetStatus(TEXT("已重新扫描 Saved/Portraits"), false);
	}
	CursorY += 34.0f;

	PanelY = CursorY + 20.0f;
}

void AMarbleRaceMenuHUD::StopThemePreview()
{
	PreviewPlayer->Stop();
	PreviewPlayer->SetSound(nullptr);
	PreviewMusicPath.Reset();
	PreviewPlayback = {};
	bPreviewPlaying = bDraggingThemeSeek = bResumeAfterThemeSeek = false;
}

void AMarbleRaceMenuHUD::EnsureThemePreview()
{
	UMarbleRaceRosterSubsystem* Roster = GetRoster();
	if (!Roster || !Roster->IsValidIndex(SelectedEntryIndex))
	{
		StopThemePreview();
		return;
	}
	const FSoftObjectPath Path = Roster->GetEntries()[SelectedEntryIndex].ThemeMusic.ToSoftObjectPath();
	if (PreviewPlayback.RosterIndex == SelectedEntryIndex && PreviewMusicPath == Path) return;
	StopThemePreview();
	PreviewMusicPath = Path;
	PreviewPlayback = {SelectedEntryIndex, 0.0, 0.0, Roster->GetThemeDuration(SelectedEntryIndex), 0.0};
	PreviewPlayer->SetSound(Roster->GetEntries()[SelectedEntryIndex].ThemeMusic.LoadSynchronous());
}

void AMarbleRaceMenuHUD::ToggleThemePreview(const double Now)
{
	if (!PreviewPlayer->Sound || PreviewPlayback.Duration <= 0.0) return;
	const double Position = PreviewPlayback.PositionAt(Now);
	bPreviewPlaying = !bPreviewPlaying;
	PreviewPlayback.StartedAt = Now;
	PreviewPlayback.StartedFrom = Position;
	PreviewPlayback.StopsAt = bPreviewPlaying ? TNumericLimits<double>::Max() : Now;
	if (bPreviewPlaying) PreviewPlayer->Play(static_cast<float>(Position));
	else PreviewPlayer->Stop();
}

void AMarbleRaceMenuHUD::SeekThemePreview(const double Seconds, const double Now)
{
	PreviewPlayback.StartedFrom = FMarbleThemePlayback::ClampStartTime(Seconds, PreviewPlayback.Duration);
	PreviewPlayback.StartedAt = Now;
	PreviewPlayback.StopsAt = bPreviewPlaying ? TNumericLimits<double>::Max() : Now;
	if (bPreviewPlaying) PreviewPlayer->Play(static_cast<float>(PreviewPlayback.StartedFrom));
}

void AMarbleRaceMenuHUD::DrawThemePreview(float PanelX, float& PanelY, float PanelWidth)
{
	EnsureThemePreview();
	if (const auto* Settings = GetGameInstance()->GetSubsystem<UMarbleRaceSettingsSubsystem>())
		PreviewPlayer->SetVolumeMultiplier(0.65f * Settings->GetThemeVolumeGain(PreviewMusicPath));
	const double Now = FPlatformTime::Seconds();
	const auto TimeLabel = [](double Seconds)
	{
		const int32 Whole = FMath::Max(0, FMath::FloorToInt(Seconds));
		return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
	};
	if (DrawButton(bPreviewPlaying ? TEXT("暂停试听") : TEXT("播放试听"), PanelX, PanelY,
		110.f, 32.f, SmallFontSize, bPreviewPlaying)) ToggleThemePreview(Now);
	DrawSharpText(TimeLabel(PreviewPlayback.PositionAt(Now)) + TEXT(" / ") + TimeLabel(PreviewPlayback.Duration),
		PanelX + 126.f, PanelY + 6.f, SmallFontSize, MutedTextColor, false, false);
	PanelY += 38.f;

	// 拖动时暂停，释放后从新位置继续，避免每帧重启音频产生杂音。
	APlayerController* Controller = GetOwningPlayerController();
	if (PreviewPlayback.Duration > 0.0 && ConsumeClickInRect(PanelX, PanelY, PanelWidth, 24.f))
	{
		bDraggingThemeSeek = true;
		bResumeAfterThemeSeek = bPreviewPlaying;
		if (bPreviewPlaying) ToggleThemePreview(Now);
	}
	if (bDraggingThemeSeek && Controller)
	{
		float MouseX = 0.f, MouseY = 0.f;
		if (Controller->GetMousePosition(MouseX, MouseY))
			SeekThemePreview(FMath::Clamp((MouseX - PanelX) / PanelWidth, 0.f, 1.f) * PreviewPlayback.Duration, Now);
		if (!Controller->IsInputKeyDown(EKeys::LeftMouseButton))
		{
			bDraggingThemeSeek = false;
			if (bResumeAfterThemeSeek) ToggleThemePreview(Now);
			bResumeAfterThemeSeek = false;
		}
	}
	DrawRect(ButtonColor, PanelX, PanelY + 9.f, PanelWidth, 6.f);
	const float Fraction = PreviewPlayback.Duration > 0.0 ? PreviewPlayback.PositionAt(Now) / PreviewPlayback.Duration : 0.f;
	DrawRect(AccentColor, PanelX, PanelY + 9.f, PanelWidth * Fraction, 6.f);
	DrawCircleBand(PanelX + PanelWidth * Fraction, PanelY + 12.f, 0.f, 6.f, TextColor);
	PanelY += 30.f;

	UMarbleRaceRosterSubsystem* Roster = GetRoster();
	if (Roster && Roster->IsValidIndex(SelectedEntryIndex))
	{
		DrawSharpText(TEXT("首次播放起点"), PanelX, PanelY + 7.f, SmallFontSize, TextColor, false, false);
		const bool bFocused = FocusedTextField == EMarbleRaceMenuTextField::MusicStartTime;
		const FString SecondsText = bFocused ? MusicStartEditBuffer
			: FString::Printf(TEXT("%.2f"), Roster->GetEntries()[SelectedEntryIndex].ThemeStartTimeSeconds);
		if (DrawTextField(SecondsText, PanelX + 140.f, PanelY, 88.f, 32.f, bFocused))
			FocusTextField(EMarbleRaceMenuTextField::MusicStartTime);
		DrawSharpText(TEXT("秒"), PanelX + 232.f, PanelY + 7.f, SmallFontSize, MutedTextColor, false, false);
		if (DrawButton(TEXT("保存"), PanelX + 258.f, PanelY, 54.f, 32.f, SmallFontSize, false)) CommitFocusedText();
		if (DrawButton(TEXT("设为当前时间"), PanelX + 320.f, PanelY, 120.f, 32.f, SmallFontSize, false))
		{
			CommitFocusedText();
			if (Roster->SetThemeStartTime(SelectedEntryIndex, PreviewPlayback.PositionAt(Now)))
				SetStatus(TEXT("首次播放起点已保存"), false);
		}
		if (DrawButton(TEXT("归零"), PanelX + 448.f, PanelY, 60.f, 32.f, SmallFontSize, false))
		{
			CommitFocusedText();
			if (Roster->SetThemeStartTime(SelectedEntryIndex, 0.f)) SetStatus(TEXT("首次播放从头开始"), false);
		}
	}
	PanelY += 48.f;
}

void AMarbleRaceMenuHUD::DrawThemeSection(float PanelX, float& PanelY, float PanelWidth)
{
	UMarbleRaceRosterSubsystem* Roster = GetRoster();
	DrawSharpText(TEXT("主题曲（该球领先时播放）"), PanelX, PanelY, SmallFontSize + 1, MutedTextColor, false, false);
	float CursorY = PanelY + 30.0f;

	DrawSharpText(FitText(FString::Printf(TEXT("当前：%s"),
	                              Roster ? *Roster->GetThemeDisplayName(SelectedEntryIndex) : TEXT("-")), PanelWidth, SmallFontSize),
	              PanelX, CursorY, SmallFontSize, AccentColor, false, false);
	CursorY += 28.0f;
	const float PreviewStartLabelY = CursorY + 68.f;
	DrawThemePreview(PanelX, CursorY, PanelWidth);

	const int32 ThemeCount = Roster ? Roster->GetAvailableThemes().Num() : 0;
	const int32 ThemesPerPageCount = FMath::Clamp(FMath::FloorToInt((DetailBottom - CursorY - 76.f) / 28.f), 1, FMath::Max(1, ThemesPerPage));
	const int32 ThemePageCount = FMath::Max(1, FMath::DivideAndRoundUp(ThemeCount, ThemesPerPageCount));
	ThemePageIndex = FMath::Clamp(ThemePageIndex, 0, ThemePageCount - 1);
	const int32 FirstThemeIndex = ThemePageIndex * ThemesPerPageCount;

	if (ThemeCount == 0)
	{
		DrawSharpText(TEXT("工程里还没有音频资源：先在编辑器中导入"), PanelX, CursorY, SmallFontSize,
		              MutedTextColor, false, false);
		CursorY += 22.0f;
	}

	if (Roster)
	{
		const TArray<TSoftObjectPtr<USoundBase>>& AvailableThemes = Roster->GetAvailableThemes();
		const TArray<FRaceCharacterEntry>& Entries = Roster->GetEntries();
		const FSoftObjectPath CurrentThemePath = Entries.IsValidIndex(SelectedEntryIndex)
			                                         ? Entries[SelectedEntryIndex].ThemeMusic.ToSoftObjectPath()
			                                         : FSoftObjectPath();

		for (int32 Slot = 0; Slot < ThemesPerPageCount; ++Slot)
		{
			const int32 ThemeIndex = FirstThemeIndex + Slot;
			if (!AvailableThemes.IsValidIndex(ThemeIndex))
			{
				break;
			}

			const TSoftObjectPtr<USoundBase>& Theme = AvailableThemes[ThemeIndex];
			const FString ThemeLabel = FitText(Roster->GetThemeLabel(Theme.ToSoftObjectPath()), PanelWidth - 24.f, SmallFontSize);
			const bool bIsCurrentTheme = Theme.ToSoftObjectPath() == CurrentThemePath;
			if (DrawButton(ThemeLabel, PanelX, CursorY + Slot * 28.0f, PanelWidth, 26.0f, SmallFontSize,
			               bIsCurrentTheme))
			{
				if (!bIsCurrentTheme)
				{
					CommitFocusedText();
					StopThemePreview();
					Roster->SetThemeMusicPath(SelectedEntryIndex, Theme.ToSoftObjectPath());
				}
				SetStatus(FString::Printf(TEXT("主题曲已设为：%s"), *ThemeLabel), false);
			}
		}
		CursorY += FMath::Clamp(ThemeCount - FirstThemeIndex, 0, ThemesPerPageCount) * 28.0f;
	}

	if (ThemePageCount > 1)
	{
		if (DrawButton(TEXT("上一页"), PanelX, CursorY, 78.0f, 26.0f, SmallFontSize, false))
		{
			--ThemePageIndex;
		}
		if (DrawButton(TEXT("下一页"), PanelX + 84.0f, CursorY, 78.0f, 26.0f, SmallFontSize, false))
		{
			++ThemePageIndex;
		}
		DrawSharpText(FString::Printf(TEXT("第 %d/%d 页"), ThemePageIndex + 1, ThemePageCount),
		              PanelX + 170.0f, CursorY + 5.0f, SmallFontSize, MutedTextColor, false, false);
		CursorY += 32.0f;
		ThemePageIndex = FMath::Clamp(ThemePageIndex, 0, ThemePageCount - 1);
	}

	if (DrawButton(TEXT("清除主题曲"), PanelX, CursorY, 160.0f, 28.0f, SmallFontSize, false))
	{
		if (Roster)
		{
			CommitFocusedText();
			StopThemePreview();
			Roster->SetThemeMusicPath(SelectedEntryIndex, FSoftObjectPath());
			SetStatus(TEXT("主题曲已清除"), false);
		}
	}
	PanelY = CursorY + 36.0f;
	if (IsMouseOverRect(PanelX, PreviewStartLabelY, 136.f, 32.f))
		DrawSettingsTooltip(TEXT("输入秒数并按回车，或使用试听当前位置。\n仅每场比赛首次播放使用；之后续播，循环从头开始。"),
			FVector2D(PanelX, PreviewStartLabelY + 36.f), Canvas->SizeX, Canvas->SizeY);
}

void AMarbleRaceMenuHUD::DrawBottomBar(float ScreenWidth, float ScreenHeight)
{
	UMarbleRaceRosterSubsystem* Roster = GetRoster();

	const float BarTop = ScreenHeight - MarbleRaceMenuLayout::BottomBarHeight + 10.0f;
	const float ButtonHeight = 40.0f;
	const float ButtonWidth = FMath::Min(132.f, (ScreenWidth - 48.f - 168.f - 50.f) / 4.f);
	const float ButtonGap = 10.0f;
	float ButtonX = MarbleRaceMenuLayout::Margin;

	if (DrawButton(TEXT("新建小球"), ButtonX, BarTop, ButtonWidth, ButtonHeight, BodyFontSize, false))
	{
		CommitFocusedText();
		if (Roster)
		{
			const int32 NewIndex = Roster->AddEntry();
			SelectedEntryIndex = NewIndex;
			ThemePageIndex = 0;
			PortraitFilePageIndex = 0;
			PathEditBuffer = GetEntrySourcePath(NewIndex);
			bScrollSelectionPending = true;
			SetStatus(FString::Printf(TEXT("已新建小球：%s"), *GetEntryName(NewIndex)), false);
		}
	}
	ButtonX += ButtonWidth + ButtonGap;

	if (DrawButton(TEXT("删除选中"), ButtonX, BarTop, ButtonWidth, ButtonHeight, BodyFontSize, false))
	{
		CommitFocusedText();
		if (Roster && Roster->IsValidIndex(SelectedEntryIndex))
		{
			const int32 RemovedIndex = SelectedEntryIndex;
			if (Roster->RemoveEntry(RemovedIndex))
			{
				const int32 RemainingCount = Roster->GetEntryCount();
				SelectedEntryIndex = (RemainingCount > 0)
					                     ? FMath::Clamp(RemovedIndex, 0, RemainingCount - 1)
					                     : INDEX_NONE;
				ThemePageIndex = 0;
				PortraitFilePageIndex = 0;
				PathEditBuffer = GetEntrySourcePath(SelectedEntryIndex);
				bScrollSelectionPending = true;
				SetStatus(FString::Printf(TEXT("已删除 1 个小球，剩余 %d 个"), RemainingCount), false);
			}
		}
	}
	ButtonX += ButtonWidth + ButtonGap;

	if (DrawButton(TEXT("上移"), ButtonX, BarTop, ButtonWidth, ButtonHeight, BodyFontSize, false))
	{
		CommitFocusedText();
		if (Roster && Roster->MoveEntry(SelectedEntryIndex, -1))
		{
			--SelectedEntryIndex;
			bScrollSelectionPending = true;
		}
	}
	ButtonX += ButtonWidth + ButtonGap;

	if (DrawButton(TEXT("下移"), ButtonX, BarTop, ButtonWidth, ButtonHeight, BodyFontSize, false))
	{
		CommitFocusedText();
		if (Roster && Roster->MoveEntry(SelectedEntryIndex, 1))
		{
			++SelectedEntryIndex;
			bScrollSelectionPending = true;
		}
	}

	const float BackButtonWidth = 168.0f;
	if (DrawButton(TEXT("返回主菜单"), ScreenWidth - MarbleRaceMenuLayout::Margin - BackButtonWidth, BarTop,
	               BackButtonWidth, ButtonHeight, BodyFontSize, false))
	{
		HandleEscapeToMainMenu();
	}
}

FString AMarbleRaceMenuHUD::FitText(const FString& Text, const float Width, const int32 FontSize) const
{
	if (!GEngine || !GEngine->GetMediumFont() || !FSlateApplication::IsInitialized()) return ShortenForRow(Text, FMath::Max(4, FMath::FloorToInt(Width / FontSize)));
	FSlateFontInfo Font = GEngine->GetMediumFont()->GetLegacySlateFontInfo();
	Font.Size = FMath::Max(6, FontSize);
	const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	if (Measure->Measure(Text, Font).X <= Width) return Text;
	FString Result = Text;
	while (!Result.IsEmpty() && Measure->Measure(Result + TEXT("…"), Font).X > Width) Result.LeftChopInline(1);
	if (!Result.IsEmpty() && Result[Result.Len() - 1] >= 0xD800 && Result[Result.Len() - 1] <= 0xDBFF) Result.LeftChopInline(1);
	return Result + TEXT("…");
}

FString AMarbleRaceMenuHUD::ShortenForRow(const FString& InText, int32 MaxCharacters)
{
	const int32 CharacterLimit = FMath::Max(4, MaxCharacters);
	if (InText.Len() <= CharacterLimit)
	{
		return InText;
	}

	FString Shortened = InText.Left(CharacterLimit - 3);
	// 不要把代理对切成两半，单独一半会画成坏字。
	if (!Shortened.IsEmpty())
	{
		const TCHAR LastGlyph = Shortened[Shortened.Len() - 1];
		if (LastGlyph >= 0xD800 && LastGlyph <= 0xDBFF)
		{
			Shortened = Shortened.Left(Shortened.Len() - 1);
		}
	}
	return Shortened + TEXT("...");
}
