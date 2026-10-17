#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "IAssetViewport.h"
#include "LevelEditor.h"

namespace EditorPromptMcp
{
	inline FString ArgStr(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, const FString& Fallback = FString())
	{
		if (!Args.IsValid())
		{
			return Fallback;
		}
		FString V;
		return Args->TryGetStringField(Key, V) ? V : Fallback;
	}

	inline double ArgNum(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, double Fallback = 0)
	{
		if (!Args.IsValid())
		{
			return Fallback;
		}
		double V = Fallback;
		if (Args->TryGetNumberField(Key, V))
		{
			return V;
		}
		return Fallback;
	}

	inline bool ArgBool(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, bool Fallback = false)
	{
		if (!Args.IsValid())
		{
			return Fallback;
		}
		bool V = Fallback;
		if (Args->TryGetBoolField(Key, V))
		{
			return V;
		}
		return Fallback;
	}

	inline int32 ArgInt(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, int32 Fallback = 0)
	{
		return static_cast<int32>(ArgNum(Args, Key, Fallback));
	}

	inline FLinearColor ArgColor(const TSharedPtr<FJsonObject>& Args)
	{
		double R = ArgNum(Args, TEXT("r"), 1);
		double G = ArgNum(Args, TEXT("g"), 1);
		double B = ArgNum(Args, TEXT("b"), 1);
		double A = ArgNum(Args, TEXT("a"), 1);
		if (R > 1.0 || G > 1.0 || B > 1.0)
		{
			R /= 255.0;
			G /= 255.0;
			B /= 255.0;
		}
		return FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), static_cast<float>(A));
	}

	inline FVector ArgLoc(const TSharedPtr<FJsonObject>& Args)
	{
		return FVector(ArgNum(Args, TEXT("x")), ArgNum(Args, TEXT("y")), ArgNum(Args, TEXT("z")));
	}

	inline bool ArgsHaveLoc(const TSharedPtr<FJsonObject>& Args)
	{
		return Args.IsValid() && (Args->HasField(TEXT("x")) || Args->HasField(TEXT("y")) || Args->HasField(TEXT("z")));
	}

	inline TSharedRef<FJsonObject> Prop(const TCHAR* Type, const TCHAR* Desc)
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), Type);
		P->SetStringField(TEXT("description"), Desc);
		return P;
	}

	inline TSharedPtr<FJsonValue> MakeTool(const TCHAR* Name, const TCHAR* Desc, TSharedRef<FJsonObject> Properties)
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		Schema->SetObjectField(TEXT("properties"), Properties);
		TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
		Tool->SetStringField(TEXT("name"), Name);
		Tool->SetStringField(TEXT("description"), Desc);
		Tool->SetObjectField(TEXT("inputSchema"), Schema);
		return MakeShared<FJsonValueObject>(Tool);
	}

	inline TSharedRef<FJsonObject> EmptyProps()
	{
		return MakeShared<FJsonObject>();
	}

	inline TSharedRef<FJsonObject> LabelProps()
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Actor label in the outliner")));
		return P;
	}

	inline bool SplitGameAssetPath(const FString& Path, FString& OutPackagePath, FString& OutAssetName, FString& OutError)
	{
		if (!Path.StartsWith(TEXT("/Game")))
		{
			OutError = TEXT("path must start with /Game");
			return false;
		}
		Path.Split(TEXT("/"), &OutPackagePath, &OutAssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (OutAssetName.IsEmpty())
		{
			OutError = TEXT("invalid path");
			return false;
		}
		return true;
	}

	inline UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	inline AActor* FindLabel(UWorld* World, const FString& Label)
	{
		if (!World || Label.IsEmpty())
		{
			return nullptr;
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
		return nullptr;
	}

	inline void TouchActor(AActor* Actor)
	{
		if (!Actor)
		{
			return;
		}
		Actor->SetFlags(RF_Transactional);
		Actor->Modify();
		Actor->MarkPackageDirty();
	}

	inline void Redraw()
	{
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
	}

	inline FEditorViewportClient* ActiveViewport()
	{
		FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
		if (!LevelEditor)
		{
			return nullptr;
		}
		const TSharedPtr<IAssetViewport> Viewport = LevelEditor->GetFirstActiveViewport();
		return Viewport.IsValid() ? &Viewport->GetAssetViewportClient() : nullptr;
	}

	inline void EnableFxViewport()
	{
		if (FEditorViewportClient* Client = ActiveViewport())
		{
			Client->SetRealtime(true);
		}
		Redraw();
	}

	inline FVector CameraFrontLocation(float Distance)
	{
		if (FEditorViewportClient* Client = ActiveViewport())
		{
			return Client->GetViewLocation() + Client->GetViewRotation().Vector() * Distance;
		}
		return FVector::ZeroVector;
	}
}
