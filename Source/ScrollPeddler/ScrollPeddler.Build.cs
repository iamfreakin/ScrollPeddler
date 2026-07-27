// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ScrollPeddler : ModuleRules
{
	public ScrollPeddler(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreOnline",
			"CoreUObject",
			"Engine",
			"EnhancedInput",
			"GameplayTags",
			"InputCore",
			"OnlineSubsystem",
			"OnlineSubsystemUtils"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		DynamicallyLoadedModuleNames.Add("OnlineSubsystemSteam");

	}
}
