using UnrealBuildTool;

public class AdvancedSessions : ModuleRules
{
	public AdvancedSessions(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDefinitions.Add("WITH_ADVANCED_SESSIONS=1");

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core","CoreUObject","Engine","InputCore",
			"OnlineSubsystem","OnlineSubsystemUtils",
			"Networking","Sockets",
			"Slate","SlateCore","UMG"   // <-- needed by UHT-generated code that references CoreStyle
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"ApplicationCore","RenderCore","RHI","Serialization"
		});
	}
}