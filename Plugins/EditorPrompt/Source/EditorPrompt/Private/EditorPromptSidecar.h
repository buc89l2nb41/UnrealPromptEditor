#pragma once

#include "CoreMinimal.h"

class FEditorPromptSidecar
{
public:
	static FString HealthUrl();
	static FString PromptUrl();
	static FString ModelsUrl();
	static FString CancelUrl();
	static FString StatusUrl();
	static FString McpUrl();
	static FString SidecarDir();
	static bool EnsureStarted(FString& OutError);
};
