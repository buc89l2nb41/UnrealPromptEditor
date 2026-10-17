#pragma once

#include "Delegates/IDelegateInstance.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"

class FPromptEditorMcpServer;
class SWindow;

class FPromptEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	TSharedPtr<FPromptEditorMcpServer> GetMcpServer() const { return McpServer; }

	static FPromptEditorModule& Get();
	static void OpenPromptTab();

private:
	TSharedRef<class SDockTab> SpawnPromptTab(const class FSpawnTabArgs& Args);
	void RegisterTabSpawner();
	void AddWindowAndToolsMenus();
	void OnMainFrameReady(TSharedPtr<SWindow> InRootWindow, bool bIsRunningStartupDialog);

	TSharedPtr<FPromptEditorMcpServer> McpServer;
	FDelegateHandle PostEngineInitHandle;
	FDelegateHandle EnginePreExitHandle;
	bool bTabRegistered = false;
	bool bMenusAdded = false;
};
