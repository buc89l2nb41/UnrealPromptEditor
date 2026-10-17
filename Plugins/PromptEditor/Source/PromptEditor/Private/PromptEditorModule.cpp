#include "PromptEditorModule.h"
#include "PromptEditorDistort.h"
#include "PromptEditorFollow.h"
#include "PromptEditorTrail.h"
#include "PromptEditorLog.h"
#include "PromptEditorMcpServer.h"
#include "PromptEditorMcpTools.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IMainFrameModule.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "SPromptEditorPanel.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FPromptEditorModule"

DEFINE_LOG_CATEGORY(LogPromptEditor);

static const FName PromptEditorTabName(TEXT("PromptEditor"));

FPromptEditorModule& FPromptEditorModule::Get()
{
	return FModuleManager::LoadModuleChecked<FPromptEditorModule>(TEXT("PromptEditor"));
}

void FPromptEditorModule::OpenPromptTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(PromptEditorTabName);
}

void FPromptEditorModule::StartupModule()
{
	McpServer = MakeShared<FPromptEditorMcpServer>();
	if (!McpServer->Start())
	{
		UE_LOG(LogPromptEditor, Error, TEXT("MCP listen failed on 127.0.0.1:%u-%u"), PromptEditor::McpPort, PromptEditor::McpPortMax);
	}

	RegisterTabSpawner();

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PromptEditor.Open"),
		TEXT("Open Tools → PromptEditor"),
		FConsoleCommandDelegate::CreateStatic(&FPromptEditorModule::OpenPromptTab),
		ECVF_Default);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPromptEditorModule::AddWindowAndToolsMenus));

	PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FPromptEditorModule::AddWindowAndToolsMenus);
	FPromptEditorMcpTools::PurgeRestoreTemps(false);
	FPromptEditorFollow::Startup();
	FPromptEditorDistort::Startup();
	FPromptEditorTrail::Startup();
	EnginePreExitHandle = FCoreDelegates::OnEnginePreExit.AddLambda([]()
	{
		FPromptEditorMcpTools::PurgeRestoreTemps(true);
	});

	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		MainFrame.OnMainFrameCreationFinished().AddRaw(this, &FPromptEditorModule::OnMainFrameReady);
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
				MainFrame.OnMainFrameCreationFinished().AddRaw(this, &FPromptEditorModule::OnMainFrameReady);
			}
		});
	}

	if (UToolMenus::IsToolMenuUIEnabled())
	{
		AddWindowAndToolsMenus();
	}
}

void FPromptEditorModule::OnMainFrameReady(TSharedPtr<SWindow> InRootWindow, bool bIsRunningStartupDialog)
{
	AddWindowAndToolsMenus();
}

void FPromptEditorModule::RegisterTabSpawner()
{
	if (bTabRegistered)
	{
		return;
	}
	bTabRegistered = true;

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		PromptEditorTabName,
		FOnSpawnTab::CreateRaw(this, &FPromptEditorModule::SpawnPromptTab))
		.SetDisplayName(LOCTEXT("TabTitle", "PromptEditor"))
		.SetTooltipText(LOCTEXT("TabTooltip", "Prompt the editor. MCP spawn_cube."))
		.SetMenuType(ETabSpawnerMenuType::Hidden)
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());
}

void FPromptEditorModule::AddWindowAndToolsMenus()
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
		FToolMenuSection& Section = Menu->FindOrAddSection(SectionName, LOCTEXT("PromptEditorSection", "PromptEditor"));
		if (Section.FindEntry("OpenPromptEditor"))
		{
			return;
		}
		Section.AddMenuEntry(
			"OpenPromptEditor",
			LOCTEXT("OpenPromptEditor", "PromptEditor"),
			LOCTEXT("OpenPromptEditorTip", "Open the PromptEditor panel"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&FPromptEditorModule::OpenPromptTab)));
	};

	AddOpenEntry(UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools"), "PromptEditor");

	if (!bMenusAdded)
	{
		bMenusAdded = true;
		UE_LOG(LogPromptEditor, Log, TEXT("PromptEditor menu entry added (Tools)"));
	}
}

void FPromptEditorModule::ShutdownModule()
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

	IConsoleManager::Get().UnregisterConsoleObject(TEXT("PromptEditor.Open"));

	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

	if (bTabRegistered)
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(PromptEditorTabName);
		bTabRegistered = false;
	}

	FPromptEditorTrail::Shutdown();
	FPromptEditorDistort::Shutdown();
	FPromptEditorFollow::Shutdown();
	if (McpServer.IsValid())
	{
		McpServer->Stop();
		McpServer.Reset();
	}
}

TSharedRef<SDockTab> FPromptEditorModule::SpawnPromptTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("TabTitle", "PromptEditor"))
		[
			SNew(SPromptEditorPanel)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPromptEditorModule, PromptEditor)
