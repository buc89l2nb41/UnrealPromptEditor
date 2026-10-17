#include "EditorPromptModule.h"
#include "EditorPromptDistort.h"
#include "EditorPromptFollow.h"
#include "EditorPromptTrail.h"
#include "EditorPromptLog.h"
#include "EditorPromptMcpServer.h"
#include "EditorPromptMcpTools.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IMainFrameModule.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "SEditorPromptPanel.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FEditorPromptModule"

DEFINE_LOG_CATEGORY(LogEditorPrompt);

static const FName EditorPromptTabName(TEXT("EditorPrompt"));

FEditorPromptModule& FEditorPromptModule::Get()
{
	return FModuleManager::LoadModuleChecked<FEditorPromptModule>(TEXT("EditorPrompt"));
}

void FEditorPromptModule::OpenPromptTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(EditorPromptTabName);
}

void FEditorPromptModule::StartupModule()
{
	McpServer = MakeShared<FEditorPromptMcpServer>();
	if (!McpServer->Start())
	{
		UE_LOG(LogEditorPrompt, Error, TEXT("MCP listen failed on 127.0.0.1:%u-%u"), EditorPrompt::McpPort, EditorPrompt::McpPortMax);
	}

	RegisterTabSpawner();

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("EditorPrompt.Open"),
		TEXT("Open Window → Editor Prompt"),
		FConsoleCommandDelegate::CreateStatic(&FEditorPromptModule::OpenPromptTab),
		ECVF_Default);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FEditorPromptModule::AddWindowAndToolsMenus));

	PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FEditorPromptModule::AddWindowAndToolsMenus);
	FEditorPromptMcpTools::PurgeRestoreTemps(false);
	FEditorPromptFollow::Startup();
	FEditorPromptDistort::Startup();
	FEditorPromptTrail::Startup();
	EnginePreExitHandle = FCoreDelegates::OnEnginePreExit.AddLambda([]()
	{
		FEditorPromptMcpTools::PurgeRestoreTemps(true);
	});

	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		MainFrame.OnMainFrameCreationFinished().AddRaw(this, &FEditorPromptModule::OnMainFrameReady);
		if (MainFrame.IsWindowInitialized())
		{
			AddWindowAndToolsMenus();
		}
	}
	else
	{
		FModuleManager::Get().OnModulesChanged().AddLambda([this](FName ModuleName, EModuleChangeReason Reason)
		{
			if (ModuleName == TEXT("MainFrame") && Reason == EModuleChangeReason::ModuleLoaded)
			{
				IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
				MainFrame.OnMainFrameCreationFinished().AddRaw(this, &FEditorPromptModule::OnMainFrameReady);
			}
		});
	}

	if (UToolMenus::IsToolMenuUIEnabled())
	{
		AddWindowAndToolsMenus();
	}
}

void FEditorPromptModule::OnMainFrameReady(TSharedPtr<SWindow> InRootWindow, bool bIsRunningStartupDialog)
{
	AddWindowAndToolsMenus();
}

void FEditorPromptModule::RegisterTabSpawner()
{
	if (bTabRegistered)
	{
		return;
	}
	bTabRegistered = true;

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		EditorPromptTabName,
		FOnSpawnTab::CreateRaw(this, &FEditorPromptModule::SpawnPromptTab))
		.SetDisplayName(LOCTEXT("TabTitle", "Editor Prompt"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Prompt the editor. MCP spawn_cube."))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());
}

void FEditorPromptModule::AddWindowAndToolsMenus()
{
	if (!UToolMenus::IsToolMenuUIEnabled())
	{
		return;
	}

	RegisterTabSpawner();

	FToolMenuOwnerScoped OwnerScoped(this);

	auto AddOpenEntry = [](UToolMenu* Menu, const FName SectionName)
	{
		if (!Menu)
		{
			return;
		}
		FToolMenuSection& Section = Menu->FindOrAddSection(SectionName, LOCTEXT("EditorPromptSection", "Editor Prompt"));
		if (Section.FindEntry("OpenEditorPrompt"))
		{
			return;
		}
		Section.AddMenuEntry(
			"OpenEditorPrompt",
			LOCTEXT("OpenEditorPrompt", "Editor Prompt"),
			LOCTEXT("OpenEditorPromptTip", "Open the Editor Prompt panel"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&FEditorPromptModule::OpenPromptTab)));
	};

	AddOpenEntry(UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools"), "EditorPrompt");

	if (!bMenusAdded)
	{
		bMenusAdded = true;
		UE_LOG(LogEditorPrompt, Log, TEXT("Editor Prompt menu entry added (Tools)"));
	}
}

void FEditorPromptModule::ShutdownModule()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}
	if (EnginePreExitHandle.IsValid())
	{
		FCoreDelegates::OnEnginePreExit.Remove(EnginePreExitHandle);
		EnginePreExitHandle.Reset();
	}

	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule::Get().OnMainFrameCreationFinished().RemoveAll(this);
	}

	IConsoleManager::Get().UnregisterConsoleObject(TEXT("EditorPrompt.Open"));

	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

	if (bTabRegistered)
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(EditorPromptTabName);
		bTabRegistered = false;
	}

	FEditorPromptTrail::Shutdown();
	FEditorPromptDistort::Shutdown();
	FEditorPromptFollow::Shutdown();
	if (McpServer.IsValid())
	{
		McpServer->Stop();
		McpServer.Reset();
	}
}

TSharedRef<SDockTab> FEditorPromptModule::SpawnPromptTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("TabTitle", "Editor Prompt"))
		[
			SNew(SEditorPromptPanel)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FEditorPromptModule, EditorPrompt)
