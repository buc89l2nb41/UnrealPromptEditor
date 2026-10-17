#include "PromptEditorSidecar.h"
#include "PromptEditorLog.h"
#include "PromptEditorMcpServer.h"
#include "PromptEditorModule.h"

#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "IPAddress.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace
{
	FProcHandle GSidecarProc;

	bool IsLocalPortOpen(uint32 Port)
	{
		ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (!Sockets)
		{
			return false;
		}

		bool bIsValid = false;
		TSharedRef<FInternetAddr> Addr = Sockets->CreateInternetAddr();
		Addr->SetIp(TEXT("127.0.0.1"), bIsValid);
		Addr->SetPort(static_cast<int32>(Port));
		if (!bIsValid)
		{
			return false;
		}

		FSocket* Socket = Sockets->CreateSocket(NAME_Stream, TEXT("PromptEditorSidecarProbe"), false);
		if (!Socket)
		{
			return false;
		}

		Socket->SetLinger(false, 0);
		const bool bConnected = Socket->Connect(*Addr);
		Socket->Close();
		Sockets->DestroySocket(Socket);
		return bConnected;
	}

	FString PythonIn(const FString& Dir)
	{
		return Dir / TEXT(".venv/Scripts/python.exe");
	}
}

FString FPromptEditorSidecar::HealthUrl()
{
	return FString::Printf(TEXT("http://127.0.0.1:%u/health"), PromptEditor::SidecarPort);
}

FString FPromptEditorSidecar::PromptUrl()
{
	return FString::Printf(TEXT("http://127.0.0.1:%u/prompt"), PromptEditor::SidecarPort);
}

FString FPromptEditorSidecar::ModelsUrl()
{
	return FString::Printf(TEXT("http://127.0.0.1:%u/models"), PromptEditor::SidecarPort);
}

FString FPromptEditorSidecar::CancelUrl()
{
	return FString::Printf(TEXT("http://127.0.0.1:%u/cancel"), PromptEditor::SidecarPort);
}

FString FPromptEditorSidecar::StatusUrl()
{
	return FString::Printf(TEXT("http://127.0.0.1:%u/status"), PromptEditor::SidecarPort);
}

FString FPromptEditorSidecar::McpUrl()
{
	uint32 Port = PromptEditor::McpPort;
	if (FModuleManager::Get().IsModuleLoaded(TEXT("PromptEditor")))
	{
		if (const TSharedPtr<FPromptEditorMcpServer> Server = FPromptEditorModule::Get().GetMcpServer())
		{
			if (Server->GetPort() != 0)
			{
				Port = Server->GetPort();
			}
		}
	}
	return FString::Printf(TEXT("http://127.0.0.1:%u/mcp"), Port);
}

FString FPromptEditorSidecar::SidecarDir()
{
	TArray<FString> Candidates;
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("PromptEditor")))
	{
		Candidates.Add(FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("sidecar")));
		Candidates.Add(FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("..") / TEXT("..") / TEXT("sidecar")));
	}
	Candidates.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("sidecar")));

	for (FString& Dir : Candidates)
	{
		FPaths::CollapseRelativeDirectories(Dir);
		FPaths::NormalizeDirectoryName(Dir);
	}

	for (const FString& Dir : Candidates)
	{
		if (FPaths::FileExists(PythonIn(Dir)))
		{
			return Dir;
		}
	}
	for (const FString& Dir : Candidates)
	{
		if (FPaths::DirectoryExists(Dir))
		{
			return Dir;
		}
	}
	return Candidates.Num() > 0 ? Candidates[0] : FString();
}

bool FPromptEditorSidecar::EnsureStarted(FString& OutError)
{
	if (GSidecarProc.IsValid() && FPlatformProcess::IsProcRunning(GSidecarProc))
	{
		return true;
	}

	if (IsLocalPortOpen(PromptEditor::SidecarPort))
	{
		UE_LOG(LogPromptEditor, Log, TEXT("Sidecar already listening on %u"), PromptEditor::SidecarPort);
		return true;
	}

	const FString Dir = SidecarDir();
	const FString Python = PythonIn(Dir);
	if (!FPaths::FileExists(Python))
	{
		OutError = FString::Printf(
			TEXT("Cursor 사이드카가 없습니다. 한 번만 %s 에서 python -m venv .venv 후 pip install -r requirements.txt, copy .env.example .env 후 CURSOR_API_KEY 입력. 게임 프로젝트마다 할 필요 없음."),
			*FPaths::ConvertRelativePathToFull(Dir));
		return false;
	}

	const FString Args = FString::Printf(
		TEXT("-m uvicorn --app-dir \"%s\" server:app --host 127.0.0.1 --port %u"),
		*Dir,
		PromptEditor::SidecarPort);

	GSidecarProc = FPlatformProcess::CreateProc(
		*Python,
		*Args,
		true,
		true,
		true,
		nullptr,
		0,
		*Dir,
		nullptr);

	if (!GSidecarProc.IsValid())
	{
		OutError = TEXT("사이드카 프로세스 시작 실패");
		return false;
	}

	UE_LOG(LogPromptEditor, Log, TEXT("Sidecar started: %s %s"), *Python, *Args);
	return true;
}
