#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Race/MarbleRaceLevelGameMode.h"
#include "UI/MarbleRaceHUD.h"
#include "UI/MarbleRaceResultsLayout.h"
#include "Roster/MarbleRaceRosterSubsystem.h"
#include "Components/SceneComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "CanvasTypes.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/BufferArchive.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleRaceResultsTest, "MarbleRace.Results.CrossingOrderAndClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleRaceResultsTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	auto* Mode = World->SpawnActor<AMarbleRaceLevelGameMode>();
	const auto* Roster = Instance->GetSubsystem<UMarbleRaceRosterSubsystem>();
	const auto MakeMarble = [World](double Z)
	{
		AActor* Marble = World->SpawnActor<AActor>();
		auto* Root = NewObject<USceneComponent>(Marble);
		Marble->SetRootComponent(Root);
		Root->RegisterComponent();
		Marble->SetActorLocation(FVector(0., 0., Z));
		return Marble;
	};
	AActor* Later = MakeMarble(100.0);
	AActor* Earlier = MakeMarble(200.0);
	AActor* Queued = MakeMarble(-500.0);
	Queued->SetActorHiddenInGame(true);
	Mode->Marbles = {Later, Earlier, Queued};
	Mode->MarbleRosterIndices.Add(Later, 0);
	Mode->MarbleRosterIndices.Add(Earlier, 1);
	Mode->MarbleRosterIndices.Add(Queued, 2);
	Mode->bHasFinishLine = true;
	Mode->FinishLineZ = 0.f;
	Mode->RaceStartedAt = 100.0;
	Mode->MarkFinishedMarbles(110.0);
	World->GetWorldSettings()->SetTimeDilation(.1f);
	Later->SetActorLocation(FVector(0., 0., -100.0));
	Earlier->SetActorLocation(FVector(0., 0., -600.0));
	Mode->MarkFinishedMarbles(111.0);
	const auto& Results = Mode->GetFinishResults();
	TestEqual(TEXT("Hidden queued actor below the finish is excluded"), Results.Num(), 2);
	if (Results.Num() == 2)
	{
		TestEqual(TEXT("Same-frame crossings use crossing time, not spawn/list order"), Results[0].RosterIndex, 1);
		TestEqual(TEXT("Ranking starts at one"), Results[0].Rank, 1);
		TestEqual(TEXT("Time excludes countdown and uses real seconds under slow motion"), Results[0].ElapsedSeconds, 10.25);
		TestEqual(TEXT("Second crossing time is interpolated"), Results[1].ElapsedSeconds, 10.5);
		TestEqual(TEXT("Character name is included"), Results[0].CharacterName, Roster->GetEntries()[1].DisplayName);
		TestEqual(TEXT("Song title or no-music placeholder is included"), Results[0].ThemeTitle,
			Roster->GetEntries()[1].ThemeTitle.IsEmpty() ? FString(TEXT("未设置 BGM")) : Roster->GetEntries()[1].ThemeTitle);
	}
	Mode->MarkFinishedMarbles(111.5);
	TestEqual(TEXT("Repeated checks never duplicate finishes"), Results.Num(), 2);
	TestNotNull(TEXT("Notification lasts two real seconds"), Mode->GetFinishNotification(112.99));
	TestNull(TEXT("Notification expires without game time dilation"), Mode->GetFinishNotification(113.0));
	TestFalse(TEXT("Results wait for the remaining participant"), Mode->UpdateRaceCompletion(111.5));
	Queued->SetActorHiddenInGame(false);
	Queued->SetActorLocation(FVector(0., 0., 100.0));
	Mode->MarkFinishedMarbles(112.0);
	Queued->SetActorLocation(FVector(0., 0., -100.0));
	Mode->MarkFinishedMarbles(113.0);
	TestEqual(TEXT("Released queued marble is ranked on its actual finish"), Results.Num(), 3);
	TestEqual(TEXT("Queued interval is not used as a crossing sample"), Results.Last().ElapsedSeconds, 12.5);
	TestTrue(TEXT("Final crossing completes the race immediately"), Mode->UpdateRaceCompletion(113.0));
	TestTrue(TEXT("HUD can read completion"), Mode->IsRaceComplete());
	const FString SavedName = Results[0].CharacterName;
	Earlier->Destroy();
	TestEqual(TEXT("Result survives finished actor destruction"), Results[0].CharacterName, SavedName);
	TestEqual(TEXT("Hundredths carry correctly into the next minute"), MarbleRace::FormatFinishTime(59.999), FString(TEXT("01:00.00")));
	TestEqual(TEXT("Reference-style time display"), MarbleRace::FormatFinishTime(235.38), FString(TEXT("03:55.38")));
	TestEqual(TEXT("Negative times are clamped"), MarbleRace::FormatFinishTime(-1.), FString(TEXT("00:00.00")));
	for (const FVector2D Size : {FVector2D(270, 600), FVector2D(360, 900), FVector2D(1280, 720)})
	{
		const auto Layout = MarbleRace::MakeResultsLayout(Size);
		TestTrue(TEXT("Rows cannot overlap navigation"), Layout.RowsY + Layout.VisibleRows * Layout.RowHeight < Layout.NavigationY);
		TestTrue(TEXT("Continue stays inside the viewport"), Layout.ContinueY + 56.f * Layout.Scale < Size.Y);
	}
	const FVector2D CanvasSize(360, 900), Viewport(1600, 900);
	const auto Layout = MarbleRace::MakeResultsLayout(CanvasSize);
	const FVector2D ButtonCentre(180, Layout.ContinueY + 28);
	TestTrue(TEXT("Continue hit-testing compensates for pillarbox bars"),
		MarbleRace::MouseToRaceCanvas(ButtonCentre + (Viewport - CanvasSize) * .5, Viewport, CanvasSize).Equals(ButtonCentre));
	Instance->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleRaceResultsRenderingTest, "MarbleRace.Results.Rendering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FMarbleRaceResultsRenderingTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	auto* HUD = World->SpawnActor<AMarbleRaceHUD>();
	TArray<FMarbleRaceFinishResult> Results;
	const auto* Roster = Instance->GetSubsystem<UMarbleRaceRosterSubsystem>();
	for (const auto& Entry : Roster->GetEntries())
	{
		FMarbleRaceFinishResult& Result = Results.AddDefaulted_GetRef();
		Result.Rank = Results.Num();
		Result.CharacterName = Entry.DisplayName;
		Result.ThemeTitle = Entry.ThemeTitle;
		Result.ElapsedSeconds = 235.38 + Results.Num() * 6.19;
	}
	const FString Folder = FPaths::ProjectSavedDir() / TEXT("Automation/ResultsPreview");
	IFileManager::Get().MakeDirectory(*Folder, true);
	for (const FIntPoint Size : {FIntPoint(270, 600), FIntPoint(360, 900), FIntPoint(1280, 720)})
	{
		for (int32 Page = 0; Page <= MarbleRace::ResultsPageCount(Results.Num()); ++Page)
		{
			auto* Target = NewObject<UTextureRenderTarget2D>();
			Target->RenderTargetFormat = RTF_RGBA8;
			Target->InitAutoFormat(Size.X, Size.Y);
			Target->UpdateResourceImmediate(true);
			FCanvas RenderCanvas(Target->GameThread_GetRenderTargetResource(), nullptr, World, World->GetFeatureLevel());
			auto* Canvas = NewObject<UCanvas>();
			Canvas->Init(Size.X, Size.Y, nullptr, &RenderCanvas);
			Canvas->Update();
			HUD->SetCanvas(Canvas, Canvas);
			RenderCanvas.Clear(FLinearColor(.24f, .65f, .86f));
			if (Page == 0) HUD->DrawFinishNotification(Results.Last());
			else
			{
				HUD->DrawResults(Results, Page - 1);
				if (Page == 1) HUD->DrawFinishNotification(Results.Last(), true);
			}
			RenderCanvas.Flush_GameThread();
			FBufferArchive Png;
			TestTrue(TEXT("Results render to PNG"), FImageUtils::ExportRenderTarget2DAsPNG(Target, Png));
			const FString Path = Folder / FString::Printf(TEXT("%dx%d-page%d.png"), Size.X, Size.Y, Page);
			TestTrue(TEXT("Results preview is saved"), FFileHelper::SaveArrayToFile(Png, *Path));
			HUD->SetCanvas(nullptr, nullptr);
		}
	}
	Instance->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleRaceFlowTest, "MarbleRace.Results.AutomaticPagesAndAbort",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMarbleRaceFlowTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("34 racers produce four pages"), MarbleRace::ResultsPageCount(34), 4);
	TestEqual(TEXT("Exactly ten racers need one page"), MarbleRace::ResultsPageCount(10), 1);
	TestEqual(TEXT("Eleven racers need two pages"), MarbleRace::ResultsPageCount(11), 2);
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	auto* Mode = World->SpawnActor<AMarbleRaceLevelGameMode>();
	Mode->bFollowLeader = true;
	Mode->bRaceComplete = true;
	Mode->bPostRaceMusicFinished = true;
	Mode->ResultsStartedAt = 100.;
	Mode->FinishResults.SetNum(34);
	for (int32 Page = 0; Page < 4; ++Page)
	{
		TestEqual(TEXT("Page starts at its exact five-second boundary"), Mode->GetResultsPage(100. + Page * 5.), Page);
		TestEqual(TEXT("Page is retained for the full five seconds"), Mode->GetResultsPage(104.999 + Page * 5.), Page);
	}
	TestFalse(TEXT("Last partial page is still displayed at 19.999 seconds"), MarbleRace::ResultsDisplayFinished(19.999, 34));
	TestTrue(TEXT("All 34 racers have been displayed after twenty seconds"), MarbleRace::ResultsDisplayFinished(20., 34));
	TestFalse(TEXT("Auto return is not requested before display completes"), Mode->bReturningToMainMenu);
	Mode->ResultsStartedAt = World->GetRealTimeSeconds() - 20.;
	World->GetWorldSettings()->SetTimeDilation(.1f);
	Mode->Tick(.016f);
	TestTrue(TEXT("Final page timeout queues menu travel even with slow motion"), Mode->bReturningToMainMenu);
	TestFalse(TEXT("Normal results timeout preserves the recording"), Mode->bRaceAborted);
	Mode->Tick(.016f);
	TestFalse(TEXT("Further ticks cannot turn normal return into an abort"), Mode->bRaceAborted);
	auto* Aborted = World->SpawnActor<AMarbleRaceLevelGameMode>();
	Aborted->AbortRaceAndReturnToMainMenu();
	TestTrue(TEXT("ESC action works during countdown before race completion"), Aborted->bRaceAborted);
	TestTrue(TEXT("ESC action requests the menu immediately"), Aborted->bReturningToMainMenu);
	Aborted->AbortRaceAndReturnToMainMenu();
	TestFalse(TEXT("An abort never needs to fabricate a completed race"), Aborted->bRaceComplete);
	Instance->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}
#endif
