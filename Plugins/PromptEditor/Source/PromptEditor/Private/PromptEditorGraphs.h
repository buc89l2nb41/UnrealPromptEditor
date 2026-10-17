#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FPromptEditorGraphs
{
public:
	static FString ListMaterialExpressions(const FString& MaterialPath);
	static FString AddMaterialExpression(const FString& MaterialPath, const TSharedPtr<FJsonObject>& Args);
	static FString ConnectMaterial(const FString& MaterialPath, const TSharedPtr<FJsonObject>& Args);

	static FString ListBlueprintNodes(const FString& Path, const FString& GraphName);
	static FString AddBlueprintNode(const FString& Path, const TSharedPtr<FJsonObject>& Args);
	static FString ConnectBlueprint(const FString& Path, const TSharedPtr<FJsonObject>& Args);
	static FString SetBlueprintPin(const FString& Path, const TSharedPtr<FJsonObject>& Args);
	static FString AddBlueprintVariable(const FString& Path, const TSharedPtr<FJsonObject>& Args);
	static FString ListBlueprintVariables(const FString& Path);
	static FString AddBlueprintComponent(const FString& Path, const TSharedPtr<FJsonObject>& Args);
	static FString AddBlueprintFunction(const FString& Path, const TSharedPtr<FJsonObject>& Args);
	static FString ListLibraryAssets();
	static FString OpenAssetLibrary();

	static FString AddSequenceKey(const FString& Path, const TSharedPtr<FJsonObject>& Args);
	static FString AddCameraCut(const FString& Path, const TSharedPtr<FJsonObject>& Args);
};
