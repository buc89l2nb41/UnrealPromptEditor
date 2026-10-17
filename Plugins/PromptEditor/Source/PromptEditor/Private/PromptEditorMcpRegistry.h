#pragma once

#include "PromptEditorMcpUtil.h"

class UWorld;

namespace PromptEditorMcp
{
	using FMcpToolFn = TFunction<FString(const TSharedPtr<FJsonObject>& Args, UWorld* World, bool& bOk)>;

	void RegisterTool(const TCHAR* Name, const TCHAR* Description, TSharedRef<FJsonObject> Properties, FMcpToolFn Fn);
	void AppendRegisteredTools(TArray<TSharedPtr<FJsonValue>>& OutTools);
	bool TryCallRegistered(const FString& Name, const TSharedPtr<FJsonObject>& Args, UWorld* World, FString& Message, bool& bOk);
	void EnsureRegistered();
}
