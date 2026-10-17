#include "PromptEditorMcpServer.h"
#include "PromptEditorMcpTools.h"
#include "PromptEditorLog.h"

#include "Async/Async.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "IPAddress.h"
#include "ScopedTransaction.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString JsonRpcEnvelope(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result, const TSharedPtr<FJsonObject>& Error)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		if (Id.IsValid())
		{
			Root->SetField(TEXT("id"), Id);
		}
		else
		{
			Root->SetField(TEXT("id"), MakeShared<FJsonValueNull>());
		}
		if (Error.IsValid())
		{
			Root->SetObjectField(TEXT("error"), Error);
		}
		else
		{
			Root->SetObjectField(TEXT("result"), Result);
		}

		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Root, Writer);
		return Out;
	}

	TSharedRef<FJsonObject> RpcError(int32 Code, const FString& Message)
	{
		TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetNumberField(TEXT("code"), Code);
		Err->SetStringField(TEXT("message"), Message);
		return Err;
	}

	double JsonNumberOr(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, double Fallback)
	{
		if (!Obj.IsValid())
		{
			return Fallback;
		}
		double V = Fallback;
		if (Obj->TryGetNumberField(Field, V))
		{
			return V;
		}
		return Fallback;
	}

	bool IsLocalPortOpen(uint32 ProbePort)
	{
		ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (!Sockets)
		{
			return false;
		}

		bool bIsValid = false;
		TSharedRef<FInternetAddr> Addr = Sockets->CreateInternetAddr();
		Addr->SetIp(TEXT("127.0.0.1"), bIsValid);
		Addr->SetPort(static_cast<int32>(ProbePort));
		if (!bIsValid)
		{
			return false;
		}

		FSocket* Socket = Sockets->CreateSocket(NAME_Stream, TEXT("PromptEditorMcpProbe"), false);
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
}

bool FPromptEditorMcpServer::TryListen(uint32 Candidate)
{
	Port = Candidate;
	Router = FHttpServerModule::Get().GetHttpRouter(Candidate, true);
	if (!Router.IsValid())
	{
		Port = 0;
		UE_LOG(LogPromptEditor, Log, TEXT("MCP skip port %u (bind failed)"), Candidate);
		return false;
	}

	HealthRoute = Router->BindRoute(
		FHttpPath(TEXT("/health")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateSP(this, &FPromptEditorMcpServer::HandleHealth));

	McpPostRoute = Router->BindRoute(
		FHttpPath(TEXT("/mcp")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateSP(this, &FPromptEditorMcpServer::HandleMcp));

	McpOptionsRoute = Router->BindRoute(
		FHttpPath(TEXT("/mcp")),
		EHttpServerRequestVerbs::VERB_OPTIONS,
		FHttpRequestHandler::CreateSP(this, &FPromptEditorMcpServer::HandleMcpOptions));

	FHttpServerModule::Get().StartAllListeners();
	bRunning = true;
	UE_LOG(LogPromptEditor, Log, TEXT("MCP listening on http://127.0.0.1:%u/mcp"), Port);
	return true;
}

bool FPromptEditorMcpServer::Start()
{
	if (bRunning)
	{
		return true;
	}

	// Bind for real. Occupied ports are probed first; Windows may still accept 18765 with SO_REUSEADDR.
	FHttpServerModule::Get().StartAllListeners();

	for (uint32 Candidate = PromptEditor::McpPort; Candidate <= PromptEditor::McpPortMax; ++Candidate)
	{
		if (Candidate == PromptEditor::SidecarPort)
		{
			continue;
		}
		if (IsLocalPortOpen(Candidate))
		{
			UE_LOG(LogPromptEditor, Log, TEXT("MCP skip port %u (already listening)"), Candidate);
			continue;
		}
		if (TryListen(Candidate))
		{
			return true;
		}
	}

	UE_LOG(
		LogPromptEditor,
		Error,
		TEXT("MCP listen failed on 127.0.0.1:%u-%u (sidecar %u reserved)"),
		PromptEditor::McpPort,
		PromptEditor::McpPortMax,
		PromptEditor::SidecarPort);
	return false;
}

void FPromptEditorMcpServer::Stop()
{
	if (Router.IsValid())
	{
		if (HealthRoute.IsValid())
		{
			Router->UnbindRoute(HealthRoute);
		}
		if (McpPostRoute.IsValid())
		{
			Router->UnbindRoute(McpPostRoute);
		}
		if (McpOptionsRoute.IsValid())
		{
			Router->UnbindRoute(McpOptionsRoute);
		}
	}
	Router.Reset();
	Port = 0;
	bRunning = false;
}

void FPromptEditorMcpServer::AddCors(FHttpServerResponse& Response)
{
	Response.Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
	Response.Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, OPTIONS") });
	Response.Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Content-Type, Accept, Mcp-Session-Id") });
}

TUniquePtr<FHttpServerResponse> FPromptEditorMcpServer::JsonResponse(EHttpServerResponseCodes Code, const FString& Json)
{
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Json, TEXT("application/json"));
	Response->Code = Code;
	AddCors(*Response);
	return Response;
}

TUniquePtr<FHttpServerResponse> FPromptEditorMcpServer::EmptyCors(EHttpServerResponseCodes Code)
{
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Ok();
	Response->Code = Code;
	AddCors(*Response);
	return Response;
}

FString FPromptEditorMcpServer::RequestBodyToString(const FHttpServerRequest& Request)
{
	if (Request.Body.Num() <= 0)
	{
		return FString();
	}
	FUTF8ToTCHAR Convert(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
	return FString(Convert.Length(), Convert.Get());
}

bool FPromptEditorMcpServer::HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString Json = FString::Printf(
		TEXT("{\"ok\":true,\"mcp\":\"http://127.0.0.1:%u/mcp\",\"tools\":%d}"),
		Port,
		FPromptEditorMcpTools::ListTools().Num());
	OnComplete(JsonResponse(EHttpServerResponseCodes::Ok, Json));
	return true;
}

bool FPromptEditorMcpServer::HandleMcpOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	OnComplete(EmptyCors(EHttpServerResponseCodes::NoContent));
	return true;
}

bool FPromptEditorMcpServer::HandleMcp(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString Body = RequestBodyToString(Request);
	if (Body.IsEmpty())
	{
		OnComplete(EmptyCors(EHttpServerResponseCodes::Accepted));
		return true;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OnComplete(JsonResponse(EHttpServerResponseCodes::BadRequest, TEXT("{\"error\":\"invalid json\"}")));
		return true;
	}

	// Notification: no id
	if (!Root->HasTypedField<EJson::Number>(TEXT("id"))
		&& !Root->HasTypedField<EJson::String>(TEXT("id")))
	{
		OnComplete(EmptyCors(EHttpServerResponseCodes::Accepted));
		return true;
	}

	OnComplete(JsonResponse(EHttpServerResponseCodes::Ok, DispatchJsonRpc(Body)));
	return true;
}

FString FPromptEditorMcpServer::DispatchJsonRpc(const FString& Body) const
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	FJsonSerializer::Deserialize(Reader, Root);
	if (!Root.IsValid())
	{
		return JsonRpcEnvelope(nullptr, nullptr, RpcError(-32700, TEXT("Parse error")));
	}

	const TSharedPtr<FJsonValue> Id = Root->TryGetField(TEXT("id"));
	FString Method;
	Root->TryGetStringField(TEXT("method"), Method);

	if (Method == TEXT("initialize"))
	{
		TSharedRef<FJsonObject> Caps = MakeShared<FJsonObject>();
		Caps->SetObjectField(TEXT("tools"), MakeShared<FJsonObject>());

		TSharedRef<FJsonObject> Info = MakeShared<FJsonObject>();
		Info->SetStringField(TEXT("name"), TEXT("editor-prompt"));
		Info->SetStringField(TEXT("version"), TEXT("0.1.0"));

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		FString Protocol = TEXT("2024-11-05");
		const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
		if (Root->TryGetObjectField(TEXT("params"), ParamsPtr) && ParamsPtr && (*ParamsPtr).IsValid())
		{
			(*ParamsPtr)->TryGetStringField(TEXT("protocolVersion"), Protocol);
		}
		Result->SetStringField(TEXT("protocolVersion"), Protocol.IsEmpty() ? TEXT("2024-11-05") : Protocol);
		Result->SetObjectField(TEXT("capabilities"), Caps);
		Result->SetObjectField(TEXT("serverInfo"), Info);
		return JsonRpcEnvelope(Id, Result, nullptr);
	}

	if (Method == TEXT("ping") || Method == TEXT("notifications/initialized"))
	{
		return JsonRpcEnvelope(Id, MakeShared<FJsonObject>(), nullptr);
	}

	if (Method == TEXT("tools/list"))
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("tools"), FPromptEditorMcpTools::ListTools());
		return JsonRpcEnvelope(Id, Result, nullptr);
	}

	if (Method == TEXT("tools/call"))
	{
		const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
		TSharedPtr<FJsonObject> Params;
		if (Root->TryGetObjectField(TEXT("params"), ParamsPtr) && ParamsPtr)
		{
			Params = *ParamsPtr;
		}
		FString Name;
		if (Params.IsValid())
		{
			Params->TryGetStringField(TEXT("name"), Name);
		}
		TSharedPtr<FJsonObject> CallArgs;
		const TSharedPtr<FJsonObject>* ArgsPtr = nullptr;
		if (Params.IsValid() && Params->TryGetObjectField(TEXT("arguments"), ArgsPtr) && ArgsPtr)
		{
			CallArgs = *ArgsPtr;
		}

		bool bOk = false;
		const FString Text = FPromptEditorMcpTools::Call(Name, CallArgs, bOk);

		TSharedRef<FJsonObject> ContentItem = MakeShared<FJsonObject>();
		ContentItem->SetStringField(TEXT("type"), TEXT("text"));
		ContentItem->SetStringField(TEXT("text"), Text);
		TArray<TSharedPtr<FJsonValue>> Content;
		Content.Add(MakeShared<FJsonValueObject>(ContentItem));

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("content"), Content);
		if (!bOk)
		{
			Result->SetBoolField(TEXT("isError"), true);
		}
		return JsonRpcEnvelope(Id, Result, nullptr);
	}

	return JsonRpcEnvelope(Id, nullptr, RpcError(-32601, FString::Printf(TEXT("Method not found: %s"), *Method)));
}
