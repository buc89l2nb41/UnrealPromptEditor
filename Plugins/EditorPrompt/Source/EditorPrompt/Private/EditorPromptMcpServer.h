#pragma once

#include "CoreMinimal.h"
#include "HttpRequestHandler.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "HttpServerConstants.h"
#include "Templates/SharedPointer.h"

class IHttpRouter;
struct FHttpServerRequest;

class FEditorPromptMcpServer : public TSharedFromThis<FEditorPromptMcpServer>
{
public:
	bool Start();
	void Stop();
	uint32 GetPort() const { return Port; }
	bool IsRunning() const { return bRunning; }

private:
	bool TryListen(uint32 Candidate);
	bool HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMcp(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMcpOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	FString DispatchJsonRpc(const FString& Body) const;
	static TUniquePtr<struct FHttpServerResponse> JsonResponse(EHttpServerResponseCodes Code, const FString& Json);
	static TUniquePtr<struct FHttpServerResponse> EmptyCors(EHttpServerResponseCodes Code);
	static void AddCors(struct FHttpServerResponse& Response);
	static FString RequestBodyToString(const FHttpServerRequest& Request);

	TSharedPtr<IHttpRouter> Router;
	FHttpRouteHandle HealthRoute;
	FHttpRouteHandle McpPostRoute;
	FHttpRouteHandle McpOptionsRoute;
	uint32 Port = 0;
	bool bRunning = false;
};
