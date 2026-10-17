using UnrealBuildTool;

public class PromptEditor : ModuleRules
{
	public PromptEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"RenderCore",
			"RHI",
			"Slate",
			"SlateCore",
			"InputCore",
			"UnrealEd",
			"LevelEditor",
			"MaterialEditor",
			"AssetRegistry",
			"AssetTools",
			"Kismet",
			"KismetCompiler",
			"Niagara",
			"NiagaraCore",
			"NiagaraEditor",
			"NiagaraShader",
			"Sequencer",
			"SequencerCore",
			"CinematicCamera",
			"LevelSequence",
			"MovieScene",
			"MovieSceneTracks",
			"BlueprintGraph",
			"WorkspaceMenuStructure",
			"ToolMenus",
			"MainFrame",
			"HTTP",
			"HTTPServer",
			"Json",
			"JsonUtilities",
			"Projects",
			"Sockets",
			"DesktopPlatform",
			"ImageWrapper",
			"ImageCore",
			"Landscape",
			"Foliage",
			"MeshDescription",
			"StaticMeshDescription",
		});
	}
}
