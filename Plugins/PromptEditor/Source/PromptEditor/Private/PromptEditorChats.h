#pragma once

#include "CoreMinimal.h"

struct FPromptEditorChatInfo
{
	FString Id;
	FString Title;
};

struct FPromptEditorTurn
{
	FString User;
	FString Reply;
	TArray<FString> UserImages;
	TArray<FString> GenImages;
};

class FPromptEditorChats
{
public:
	static FString RootDir();
	static FString ChatDir(const FString& ChatId);
	static FString SanitizeId(const FString& Raw);
	static FString NewChatId();
	static void EnsureChat(const FString& ChatId, const FString& Title);
	static TArray<FPromptEditorChatInfo> List();
	static FString TitleOf(const FString& ChatId);
	static void ListTurns(const FString& ChatId, TArray<FPromptEditorTurn>& OutTurns);
	static void RebuildLog(const FString& ChatId, FString& OutLog);
	static void ListGenImages(const FString& ChatId, TArray<FString>& OutPaths);
	static bool DeleteChat(const FString& ChatId);
};
