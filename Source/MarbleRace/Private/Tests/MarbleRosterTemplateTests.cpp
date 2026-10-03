#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Engine/GameInstance.h"
#include "Roster/MarbleRaceRosterSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMarbleRosterTemplateTest, "MarbleRace.Roster.TemplateWithoutMedia",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMarbleRosterTemplateTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>();
	auto* Roster = NewObject<UMarbleRaceRosterSubsystem>(Instance);
	TestTrue(TEXT("Generic template loads without a media pack"),
		Roster->LoadRosterFromManifest(FPaths::ProjectConfigDir() / TEXT("Roster/default_roster.json")));
	TestEqual(TEXT("Template contains twelve participants"), Roster->GetEnabledCount(), 12);
	TSet<FString> Ids;
	TSet<uint32> Colors;
	for (int32 Index = 0; Index < Roster->GetEntryCount(); ++Index)
	{
		const auto& Entry = Roster->GetEntries()[Index];
		Ids.Add(Entry.CharacterId);
		Colors.Add(Entry.Color.ToFColorSRGB().ToPackedARGB());
		TestEqual(TEXT("Marble remains opaque"), Entry.Color.A, 1.f);
		TestFalse(TEXT("No portrait dependency"), Entry.HasPortrait());
		TestNull(TEXT("No portrait loads"), Roster->GetPortraitTexture(Index));
		TestTrue(TEXT("No audio dependency"), Entry.ThemeMusic.IsNull());
	}
	TestEqual(TEXT("IDs are distinct"), Ids.Num(), 12);
	TestEqual(TEXT("Colors are distinct"), Colors.Num(), 12);
	TestFalse(TEXT("Missing optional pack is rejected"),
		Roster->LoadRosterFromManifest(FPaths::ProjectSavedDir() / TEXT("Automation/NonexistentRoster.json")));
	TestEqual(TEXT("Failed pack load preserves the working roster"), Roster->GetEntryCount(), 12);
	return true;
}
#endif
