#pragma once

#include "Delegates/IDelegateInstance.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"

class FEditorPromptMcpServer;
class SWindow;

class FEditorPromptModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	TSharedPtr<FEditorPromptMcpServer> GetMcpServer() const { return McpServer; }

	static FEditorPromptModule& Get();
	static void OpenPromptTab();

private:
	TSharedRef<class SDockTab> SpawnPromptTab(const class FSpawnTabArgs& Args);
	void RegisterTabSpawner();
	void AddWindowAndToolsMenus();
	void OnMainFrameReady(TSharedPtr<SWindow> InRootWindow, bool bIsRunningStartupDialog);

	TSharedPtr<FEditorPromptMcpServer> McpServer;
	FDelegateHandle PostEngineInitHandle;
	FDelegateHandle EnginePreExitHandle;
	bool bTabRegistered = false;
	bool bMenusAdded = false;
};
