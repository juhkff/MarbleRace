// Import source data is only available in editor builds.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Roster/MarbleRaceRosterSubsystem.h"
#include "Sound/SoundWave.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGGSTBuiltInRosterTest,
	"MarbleRace.GGST.CompleteRosterAndAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGGSTBuiltInRosterTest::RunTest(const FString& Parameters)
{
	if (!FPaths::FileExists(FPaths::ProjectConfigDir() / TEXT("GGST/roster.json")) ||
		!FPaths::FileExists(FPaths::ProjectContentDir() / TEXT("GGST/Portraits/T_sol.uasset")))
	{
		AddInfo(TEXT("Optional GGST media pack is not installed; skipping its asset integration test."));
		return true;
	}
	UGameInstance* Instance = NewObject<UGameInstance>();
	UMarbleRaceRosterSubsystem* Roster = NewObject<UMarbleRaceRosterSubsystem>(Instance);
	TestTrue(TEXT("GGST manifest loads explicitly"), Roster->LoadRosterFromManifest(FPaths::ProjectConfigDir() / TEXT("GGST/roster.json")));
	TestEqual(TEXT("All officially released characters are included"), Roster->GetEntryCount(), 34);
	TestEqual(TEXT("Every character participates"), Roster->GetEnabledCount(), 34);
	TSet<FString> Ids, Names;
	TSet<uint32> Colors;
	for (int32 Index = 0; Index < Roster->GetEntryCount(); ++Index)
	{
		const FRaceCharacterEntry& Entry = Roster->GetEntries()[Index];
		Ids.Add(Entry.CharacterId);
		Names.Add(Entry.DisplayName);
		Colors.Add(Entry.Color.ToFColorSRGB().ToPackedARGB());
		TestEqual(*FString::Printf(TEXT("%s is opaque"), *Entry.CharacterId), Entry.Color.A, 1.f);
		TestFalse(TEXT("Theme title is present"), Entry.ThemeTitle.IsEmpty());
		UTexture2D* Portrait = Roster->GetPortraitTexture(Index);
		TestNotNull(*FString::Printf(TEXT("%s portrait loads"), *Entry.CharacterId), Portrait);
		if (Portrait)
		{
			// NullRHI does not create the runtime platform texture; inspect import source dimensions.
			const int32 Width = Portrait->Source.GetSizeX();
			const int32 Height = Portrait->Source.GetSizeY();
			TestTrue(TEXT("Official portrait is approximately square and detailed enough"), FMath::Abs(Width - Height) <= 1 && FMath::Min(Width, Height) >= 256);
		}
		USoundWave* Theme = Cast<USoundWave>(Entry.ThemeMusic.LoadSynchronous());
		TestNotNull(*FString::Printf(TEXT("%s full BGM loads"), *Entry.CharacterId), Theme);
		if (Theme)
		{
			TestTrue(TEXT("BGM is the full track, not a sample"), Theme->Duration > 180.f);
			TestTrue(TEXT("BGM loops"), Theme->bLooping);
		}
	}
	TestEqual(TEXT("Character IDs are unique"), Ids.Num(), 34);
	TestEqual(TEXT("Character names are unique"), Names.Num(), 34);
	TestEqual(TEXT("Every character has a distinct color"), Colors.Num(), 34);
	UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/Game/GGST/Materials/M_GGSTMarble.M_GGSTMarble"));
	TestNotNull(TEXT("Portrait material loads"), Material);
	if (Material)
	{
		TestEqual(TEXT("Portrait alpha cannot make marbles translucent"), Material->GetBlendMode(), BLEND_Opaque);
	}
	return true;
}

#endif
