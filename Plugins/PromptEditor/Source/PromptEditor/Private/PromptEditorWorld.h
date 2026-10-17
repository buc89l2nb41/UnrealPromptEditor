#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FPromptEditorWorld
{
public:
	/** Returns true when Name is a landscape or foliage tool. */
	static bool Handle(const FString& Name, const TSharedPtr<FJsonObject>& Args, FString& OutMessage, bool& bOutOk);
};
