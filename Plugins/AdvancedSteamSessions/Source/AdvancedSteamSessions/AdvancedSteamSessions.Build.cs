using UnrealBuildTool;
using System.IO;

public class AdvancedSteamSessions : ModuleRules
{
	public AdvancedSteamSessions(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDefinitions.Add("WITH_ADVANCED_STEAM_SESSIONS=1");

		// What this module publicly depends on
		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core","CoreUObject","Engine","InputCore",
			"OnlineSubsystem","OnlineSubsystemUtils",
			"Networking","Sockets",
			"Slate","SlateCore","UMG",          // <-- fixes CoreStyleConstants unresolveds
			"AdvancedSessions"                  // uses BlueprintDataDefinitions.h, etc
		});

		// Private-only links (safe/common)
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"ApplicationCore","RenderCore","RHI","Serialization"
		});

		// Expose AdvancedSessions' Public (and Classes if your copy has it)
		PublicIncludePaths.AddRange(new[]
		{
			Path.Combine(ModuleDirectory, "../AdvancedSessions/Public"),
			Path.Combine(ModuleDirectory, "../AdvancedSessions/Classes") // ok if missing
		});

		if (Target.Platform == UnrealTargetPlatform.Win64
		    || Target.Platform == UnrealTargetPlatform.Linux
		    || Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicDependencyModuleNames.AddRange(new[] { "SteamShared", "Steamworks", "OnlineSubsystemSteam" });
			AddEngineThirdPartyPrivateStaticDependencies(Target, "Steamworks");
		}
	}
}