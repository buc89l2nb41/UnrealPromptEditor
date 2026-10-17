#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class FPromptEditorMcpTools
{
public:
	static TArray<TSharedPtr<FJsonValue>> ListTools();
	static FString Call(const FString& Name, const TSharedPtr<FJsonObject>& Args, bool& bOk);
	static void PurgeRestoreTemps(bool bSavedTemps = true);
};
