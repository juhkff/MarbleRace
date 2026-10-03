// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class MarbleRace : ModuleRules
{
	public MarbleRace(ReadOnlyTargetRules target) : base(target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(["Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Slate", "SlateCore", "AssetRegistry", "PhysicsCore"]);

		PrivateDependencyModuleNames.AddRange(["Json", "AudioMixer", "ImageCore", "ImageWrapper", "RenderCore", "RHI"]);
		RuntimeDependencies.Add("$(ProjectDir)/Config/Roster/default_roster.json", StagedFileType.UFS);
		RuntimeDependencies.Add("$(ProjectDir)/Config/Roster/music_levels.json", StagedFileType.UFS);
		// Theme packs are local and optional. A clean checkout still builds and stages.
		foreach (string config in new[] { "Roster/local_roster.json", "Roster/local_music_levels.json", "GGST/roster.json", "GGST/music_levels.json" })
		{
			if (File.Exists(Path.Combine(ModuleDirectory, "../../Config", config)))
				RuntimeDependencies.Add("$(ProjectDir)/Config/" + config, StagedFileType.UFS);
		}
		if (target.Platform == UnrealTargetPlatform.Win64)
		{
			if (File.Exists(Path.Combine(ModuleDirectory, "../../Tools/FFmpeg/ffmpeg.exe")))
				RuntimeDependencies.Add("$(ProjectDir)/Tools/FFmpeg/ffmpeg.exe", StagedFileType.NonUFS);
			RuntimeDependencies.Add("$(ProjectDir)/Tools/FFmpeg/LICENSE", StagedFileType.NonUFS);
			RuntimeDependencies.Add("$(ProjectDir)/Tools/FFmpeg/README.md", StagedFileType.NonUFS);
		}

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
