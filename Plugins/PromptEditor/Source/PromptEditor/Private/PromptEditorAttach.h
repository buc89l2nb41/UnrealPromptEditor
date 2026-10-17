#pragma once

#include "CoreMinimal.h"

namespace PromptEditorAttach
{
	constexpr int32 MaxCount = 8;
	constexpr int64 MaxBytes = 12 * 1024 * 1024;

	bool IsImageFile(const FString& Path);
	FString AttachDir(const FString& ChatId);
	bool ImportFile(const FString& Src, const FString& ChatId, FString& OutPath, FString& OutError);
	bool ImportBytes(
		TArrayView<const uint8> Bytes,
		const FString& ChatId,
		const FString& Stem,
		const FString& ExtensionWithDot,
		FString& OutPath,
		FString& OutError);
	bool ImportClipboard(const FString& ChatId, TArray<FString>& OutPaths, FString& OutError);
	TSharedPtr<struct FSlateDynamicImageBrush> MakeThumbBrush(
		const FString& Path,
		int32 MaxEdge = 80,
		bool bCheckerboard = false);
}
