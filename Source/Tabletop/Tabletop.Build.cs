// Copyright …

using UnrealBuildTool;

public class Tabletop : ModuleRules
{
	public Tabletop(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Public deps your headers expose
		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"UMG",
			"Slate",
			"SlateCore",
			"Niagara",
			"NetCore",
			"GameplayTags",
			"OnlineSubsystem",
			"OnlineSubsystemUtils",
			// If you include AdvancedSessions headers in your game code:
			"AdvancedSessions",
			"AdvancedSteamSessions"
		});

		// Private deps your .cpps link against
		PrivateDependencyModuleNames.AddRange(new[]
		{
			"ApplicationCore",
			"RenderCore",
			"RHI",
			"Slate",
			"SlateCore",
			"UMG",
			"AudioMixer",
			"AudioExtensions",   // fixes Audio::NAME_* if they pop up
			"Serialization",     // fixes FCompositeBuffer::Null / FCompressedBuffer::Null
			"Projects",          // safe/common
		});

		// If you reference plugin headers directly, make sure the include paths are visible
		PublicIncludePaths.AddRange(new[]
		{
			// AdvancedSessions (guard these; they’re fine if the folders exist)
			"Plugins/AdvancedSessions/Source/AdvancedSessions/Public",
			"Plugins/AdvancedSteamSessions/Source/AdvancedSteamSessions/Public",
		});

		// If you ship with Steam in editor builds:
		DynamicallyLoadedModuleNames.AddRange(new[]
		{
			"OnlineSubsystemSteam"
		});
	}
}