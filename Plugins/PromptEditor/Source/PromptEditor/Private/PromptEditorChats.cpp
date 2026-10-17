#include "PromptEditorChats.h"
#include "PromptEditorLog.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/Char.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString ReadFile(const FString& Path)
	{
		FString Text;
		FFileHelper::LoadFileToString(Text, *Path);
		return Text;
	}

	TSharedPtr<FJsonObject> ReadJson(const FString& Path)
	{
		TSharedPtr<FJsonObject> Root;
		const FString Text = ReadFile(Path);
		if (Text.IsEmpty())
		{
			return Root;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Root);
		return Root;
	}

	void WriteJson(const FString& Path, const TSharedRef<FJsonObject>& Root)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Root, Writer);
		FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}

FString FPromptEditorChats::RootDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("PromptEditor") / TEXT("chats"));
}

FString FPromptEditorChats::SanitizeId(const FString& Raw)
{
	FString Out;
	Out.Reserve(Raw.Len());
	for (const TCHAR Ch : Raw)
	{
		if (FChar::IsAlnum(Ch) || Ch == TEXT('-') || Ch == TEXT('_'))
		{
			Out.AppendChar(Ch);
		}
	}
	Out = Out.Left(80);
	return Out.IsEmpty() ? FString(PromptEditor::DefaultChatId) : Out;
}

FString FPromptEditorChats::ChatDir(const FString& ChatId)
{
	return RootDir() / SanitizeId(ChatId);
}

FString FPromptEditorChats::NewChatId()
{
	return SanitizeId(FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
}

void FPromptEditorChats::EnsureChat(const FString& ChatId, const FString& Title)
{
	const FString Id = SanitizeId(ChatId);
	const FString Dir = ChatDir(Id);
	IFileManager::Get().MakeDirectory(*(Dir / TEXT("turns")), true);

	const FString MetaPath = Dir / TEXT("meta.json");
	TSharedPtr<FJsonObject> Meta = ReadJson(MetaPath);
	if (!Meta.IsValid())
	{
		Meta = MakeShared<FJsonObject>();
		Meta->SetStringField(TEXT("id"), Id);
		Meta->SetStringField(TEXT("created"), FDateTime::UtcNow().ToIso8601());
	}
	FString ExistingTitle;
	Meta->TryGetStringField(TEXT("title"), ExistingTitle);
	if (ExistingTitle.IsEmpty())
	{
		Meta->SetStringField(TEXT("title"), Title.IsEmpty() ? TEXT("New Agent") : Title);
	}
	Meta->SetStringField(TEXT("modified"), FDateTime::UtcNow().ToIso8601());
	WriteJson(MetaPath, Meta.ToSharedRef());
}

FString FPromptEditorChats::TitleOf(const FString& ChatId)
{
	const TSharedPtr<FJsonObject> Meta = ReadJson(ChatDir(ChatId) / TEXT("meta.json"));
	FString Title;
	if (Meta.IsValid())
	{
		Meta->TryGetStringField(TEXT("title"), Title);
	}
	Title.TrimStartAndEndInline();
	return Title.IsEmpty() ? SanitizeId(ChatId) : Title;
}

TArray<FPromptEditorChatInfo> FPromptEditorChats::List()
{
	TArray<FPromptEditorChatInfo> Out;
	TArray<FString> Folders;
	IFileManager::Get().FindFiles(Folders, *(RootDir() / TEXT("*")), false, true);
	Folders.Sort();
	for (const FString& Folder : Folders)
	{
		FPromptEditorChatInfo Info;
		Info.Id = Folder;
		Info.Title = TitleOf(Folder);
		Out.Add(Info);
	}
	return Out;
}

void FPromptEditorChats::ListTurns(const FString& ChatId, TArray<FPromptEditorTurn>& OutTurns)
{
	OutTurns.Reset();
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(ChatDir(ChatId) / TEXT("turns") / TEXT("*.json")), true, false);
	Files.Sort();

	auto AddPath = [](TArray<FString>& Dest, const FString& Path)
	{
		if (Path.IsEmpty() || Dest.Contains(Path))
		{
			return;
		}
		if (IFileManager::Get().FileExists(*Path))
		{
			Dest.Add(Path);
		}
	};

	auto CollectPngEquals = [&AddPath](TArray<FString>& Dest, const FString& Text)
	{
		int32 Start = 0;
		while (Start < Text.Len())
		{
			const int32 At = Text.Find(TEXT("png="), ESearchCase::IgnoreCase, ESearchDir::FromStart, Start);
			if (At == INDEX_NONE)
			{
				break;
			}
			int32 End = At + 4;
			while (End < Text.Len() && !FChar::IsWhitespace(Text[End]))
			{
				++End;
			}
			AddPath(Dest, Text.Mid(At + 4, End - (At + 4)));
			Start = End;
		}
	};

	for (const FString& File : Files)
	{
		const TSharedPtr<FJsonObject> TurnObj = ReadJson(ChatDir(ChatId) / TEXT("turns") / File);
		if (!TurnObj.IsValid())
		{
			continue;
		}
		FPromptEditorTurn Turn;
		FString RawUser;
		TurnObj->TryGetStringField(TEXT("user"), RawUser);
		TurnObj->TryGetStringField(TEXT("reply"), Turn.Reply);

		TArray<FString> Lines;
		RawUser.ParseIntoArrayLines(Lines, false);
		TArray<FString> Keep;
		for (const FString& Line : Lines)
		{
			if (Line.StartsWith(TEXT("image=")))
			{
				AddPath(Turn.UserImages, Line.Mid(6).TrimStartAndEnd());
			}
			else
			{
				Keep.Add(Line);
			}
		}
		Turn.User.Reset();
		for (int32 Index = 0; Index < Keep.Num(); ++Index)
		{
			if (Index > 0)
			{
				Turn.User += TEXT("\n");
			}
			Turn.User += Keep[Index];
		}

		const TArray<TSharedPtr<FJsonValue>>* Images = nullptr;
		if (TurnObj->TryGetArrayField(TEXT("gen_images"), Images) && Images)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Images)
			{
				if (Value.IsValid())
				{
					AddPath(Turn.GenImages, Value->AsString());
				}
			}
		}
		CollectPngEquals(Turn.GenImages, Turn.Reply);
		const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
		if (TurnObj->TryGetArrayField(TEXT("tools"), Tools) && Tools)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Tools)
			{
				const TSharedPtr<FJsonObject> Tool = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Tool.IsValid())
				{
					continue;
				}
				FString Result;
				Tool->TryGetStringField(TEXT("result"), Result);
				CollectPngEquals(Turn.GenImages, Result);
			}
		}
		if (!Turn.User.IsEmpty() || !Turn.Reply.IsEmpty() || Turn.UserImages.Num() > 0 || Turn.GenImages.Num() > 0)
		{
			OutTurns.Add(MoveTemp(Turn));
		}
	}
}

void FPromptEditorChats::ListGenImages(const FString& ChatId, TArray<FString>& OutPaths)
{
	OutPaths.Reset();
	TArray<FPromptEditorTurn> Turns;
	ListTurns(ChatId, Turns);
	for (const FPromptEditorTurn& Turn : Turns)
	{
		for (const FString& Path : Turn.UserImages)
		{
			if (!OutPaths.Contains(Path))
			{
				OutPaths.Add(Path);
			}
		}
		for (const FString& Path : Turn.GenImages)
		{
			if (!OutPaths.Contains(Path))
			{
				OutPaths.Add(Path);
			}
		}
	}
}

void FPromptEditorChats::RebuildLog(const FString& ChatId, FString& OutLog)
{
	OutLog.Reset();
	TArray<FPromptEditorTurn> Turns;
	ListTurns(ChatId, Turns);
	for (const FPromptEditorTurn& Turn : Turns)
	{
		if (!Turn.User.IsEmpty())
		{
			if (!OutLog.IsEmpty())
			{
				OutLog += TEXT("\n");
			}
			OutLog += FString::Printf(TEXT("[user] %s"), *Turn.User);
		}
		if (!Turn.Reply.IsEmpty())
		{
			if (!OutLog.IsEmpty())
			{
				OutLog += TEXT("\n");
			}
			OutLog += FString::Printf(TEXT("[model] %s"), *Turn.Reply);
		}
	}
}

bool FPromptEditorChats::DeleteChat(const FString& ChatId)
{
	const FString Id = SanitizeId(ChatId);
	FString Dir = ChatDir(Id);
	FString Root = RootDir();
	FPaths::NormalizeDirectoryName(Dir);
	FPaths::NormalizeDirectoryName(Root);
	if (Dir.IsEmpty() || Root.IsEmpty() || Dir == Root || !FPaths::IsUnderDirectory(Dir, Root))
	{
		return false;
	}
	if (!FPaths::DirectoryExists(Dir))
	{
		return true;
	}
	return IFileManager::Get().DeleteDirectory(*Dir, false, true);
}
