#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/MarbleRaceMenuHUD.h"
#include "Roster/MarbleRaceRosterSubsystem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "AssetCompilingManager.h"
#include "Engine/World.h"
#include "CanvasTypes.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/BufferArchive.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleMenuRenderingTest, "MarbleRace.UI.MenuRendering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FMarbleMenuRenderingTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	AMarbleRaceMenuHUD* HUD = World->SpawnActor<AMarbleRaceMenuHUD>();
	HUD->SelectedEntryIndex = 0;
	HUD->GetRoster()->RefreshAvailableThemes();
	HUD->PathEditBuffer = HUD->GetEntrySourcePath(0);
	for (int32 Index = 0; Index < HUD->GetRoster()->GetEntryCount(); ++Index) HUD->GetEntryPortrait(Index);
	FAssetCompilingManager::Get().FinishAllCompilation();
	const FString Folder = FPaths::ProjectSavedDir() / TEXT("Automation/MenuPreview");
	IFileManager::Get().MakeDirectory(*Folder, true);
	for (const FIntPoint Size : {FIntPoint(1280, 720), FIntPoint(1920, 1080)})
	{
		for (int32 Page = 0; Page < 12; ++Page)
		{
			UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>();
			Target->RenderTargetFormat = RTF_RGBA8;
			Target->InitAutoFormat(Size.X, Size.Y);
			Target->UpdateResourceImmediate(true);
			FCanvas RenderCanvas(Target->GameThread_GetRenderTargetResource(), nullptr, World, World->GetFeatureLevel());
			UCanvas* Canvas = NewObject<UCanvas>();
			Canvas->Init(Size.X, Size.Y, nullptr, &RenderCanvas);
			Canvas->Update();
			HUD->SetCanvas(Canvas, Canvas);
			RenderCanvas.Clear(HUD->BackgroundColor);
			if (Page == 0) HUD->DrawMainMenuPage(Size.X, Size.Y);
			else if (Page == 11) HUD->DrawRecordingExitPage(Size.X, Size.Y);
			else if (Page == 1 || Page == 6 || Page == 7 || Page >= 9)
			{
				HUD->SettingsTabIndex = Page >= 9 ? Page - 7 : Page == 6 ? 0 : 1;
				HUD->DrawGameSettingsPage(Size.X, Size.Y);
				if (Page == 7) HUD->DrawSettingsTooltip(TEXT("临近终点时放慢场景，冲线阶段约 2.4 秒。\nBGM 仍以正常速度播放。"),
					FVector2D(Size.X * 0.6f, Size.Y * 0.48f), Size.X, Size.Y);
			}
			else
			{
				HUD->DetailTabIndex = Page == 8 ? 2 : Page == 5 ? 1 : Page - 2;
				HUD->FocusedTextField = Page == 8 ? EMarbleRaceMenuTextField::MusicStartTime
					: Page == 5 ? EMarbleRaceMenuTextField::PortraitPath : EMarbleRaceMenuTextField::None;
				if (Page == 8)
				{
					HUD->MusicStartEditBuffer = TEXT("45.50");
					HUD->EnsureThemePreview();
					HUD->SeekThemePreview(45.5, FPlatformTime::Seconds());
				}
				HUD->PathEditBuffer = Page == 5
					? TEXT("D:/很长的角色头像素材文件夹/用于确认输入框文字不会盖住导入按钮的测试图片名称/very-long-portrait-filename.png")
					: HUD->GetEntrySourcePath(0);
				HUD->DrawCharacterSetupPage(Size.X, Size.Y);
			}
			RenderCanvas.Flush_GameThread();
			FBufferArchive Png;
			TestTrue(TEXT("Menu renders and exports as PNG"), FImageUtils::ExportRenderTarget2DAsPNG(Target, Png));
			const FString Path = Folder / FString::Printf(TEXT("%dx%d-page%d.png"), Size.X, Size.Y, Page);
			TestTrue(TEXT("Preview is saved for visual inspection"), FFileHelper::SaveArrayToFile(Png, *Path));
			HUD->SetCanvas(nullptr, nullptr);
		}
	}
	Instance->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}
#endif
