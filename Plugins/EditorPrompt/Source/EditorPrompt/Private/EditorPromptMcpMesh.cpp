#include "EditorPromptMcpMesh.h"
#include "EditorPromptMcpRegistry.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "AutomatedAssetImportData.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

using namespace EditorPromptMcp;

namespace
{
	const FName PreviewTag(TEXT("EditorPromptPreview"));
	const TCHAR* PreviewRoot = TEXT("/Game/Meshes/Preview/");

	bool FileUnderProject(const FString& File)
	{
		const FString Full = FPaths::ConvertRelativePathToFull(File);
		const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		return Full.StartsWith(Root);
	}

	bool IsMeshFile(const FString& File)
	{
		const FString Ext = FPaths::GetExtension(File).ToLower();
		return Ext == TEXT("glb") || Ext == TEXT("gltf");
	}

	bool IsAssetName(const FString& Name)
	{
		if (Name.IsEmpty() || FChar::IsDigit(Name[0]))
		{
			return false;
		}
		for (const TCHAR Ch : Name)
		{
			if (!FChar::IsAlnum(Ch) && Ch != TEXT('_'))
			{
				return false;
			}
		}
		return true;
	}

	bool AssetExists(const FString& Dest, const FString& Name)
	{
		FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		const FString ObjectPath = Dest / Name + TEXT(".") + Name;
		return Registry.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid();
	}

	TArray<UObject*> LoadAssetsUnder(const FString& Folder)
	{
		FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		Registry.Get().GetAssetsByPath(FName(*Folder), Assets, true);
		TArray<UObject*> Objects;
		for (const FAssetData& Asset : Assets)
		{
			if (UObject* Obj = Asset.GetAsset())
			{
				Objects.Add(Obj);
			}
		}
		return Objects;
	}

	void DestroyTaggedPreview(UWorld* World, const FString& Label)
	{
		if (!World)
		{
			return;
		}
		TArray<AActor*> Doomed;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(PreviewTag) && (Label.IsEmpty() || It->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase)))
			{
				Doomed.Add(*It);
			}
		}
		for (AActor* Actor : Doomed)
		{
			World->DestroyActor(Actor);
		}
	}

	FString ImportStaticMesh(const TSharedPtr<FJsonObject>& Args, UWorld* World, bool& bOk)
	{
		const FString File = ArgStr(Args, TEXT("file"));
		if (File.IsEmpty() || !FPaths::FileExists(File) || !FileUnderProject(File) || !IsMeshFile(File))
		{
			return TEXT("file must be a .glb/.gltf under the project");
		}
		const FString Dest = ArgStr(Args, TEXT("dest"));
		if (!Dest.StartsWith(TEXT("/Game/")))
		{
			return TEXT("dest must start with /Game/");
		}
		const FString Name = ArgStr(Args, TEXT("name"));
		if (!Name.IsEmpty() && !IsAssetName(Name))
		{
			return TEXT("name must be letters, digits, underscore");
		}
		const bool bPreview = ArgBool(Args, TEXT("preview"), false);
		const bool bReplace = ArgBool(Args, TEXT("replace"), false);
		if (bPreview && !Dest.StartsWith(PreviewRoot))
		{
			return FString::Printf(TEXT("preview dest must be under %s"), PreviewRoot);
		}
		if (!Name.IsEmpty() && !bReplace && AssetExists(Dest, Name))
		{
			return FString::Printf(TEXT("exists=%s/%s"), *Dest, *Name);
		}
		const FString Label = ArgStr(Args, TEXT("label"), Name);
		if (bPreview)
		{
			DestroyTaggedPreview(World, Label);
		}

		UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
		ImportData->Filenames.Add(FPaths::ConvertRelativePathToFull(File));
		ImportData->DestinationPath = Dest;
		ImportData->bReplaceExisting = bReplace;
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		const TArray<UObject*> Imported = AssetTools.ImportAssetsAutomated(ImportData);

		UStaticMesh* Mesh = nullptr;
		for (UObject* Obj : Imported)
		{
			if (UStaticMesh* Candidate = Cast<UStaticMesh>(Obj))
			{
				Mesh = Candidate;
				break;
			}
		}
		if (!Mesh)
		{
			return FString::Printf(TEXT("Import produced no static mesh (%d objects)"), Imported.Num());
		}
		if (!Name.IsEmpty() && Mesh->GetName() != Name)
		{
			TArray<FAssetRenameData> Renames;
			Renames.Emplace(Mesh, Dest, Name);
			if (!AssetTools.RenameAssets(Renames))
			{
				return TEXT("Imported, but rename failed: ") + Mesh->GetPathName();
			}
		}

		FVector Loc = FVector::ZeroVector;
		FRotator Rot = FRotator::ZeroRotator;
		FVector Scale(1.0);
		const FBox Bounds = Mesh->GetBoundingBox();
		const FString CopyFrom = ArgStr(Args, TEXT("copy_from"));
		AActor* Source = CopyFrom.IsEmpty() ? nullptr : FindLabel(World, CopyFrom);
		if (Source)
		{
			Loc = Source->GetActorLocation();
			Rot = Source->GetActorRotation();
			Scale = Source->GetActorScale3D();
		}
		else
		{
			const double Height = ArgNum(Args, TEXT("height"), 0);
			const double MeshHeight = Bounds.GetSize().Z;
			if (Height > 1.0 && MeshHeight > 1.0)
			{
				Scale = FVector(Height / MeshHeight);
			}
			const double Extent = (Bounds.GetSize() * Scale).GetMax();
			Loc = ArgsHaveLoc(Args) ? ArgLoc(Args) : CameraFrontLocation(static_cast<float>(FMath::Max(400.0, Extent * 1.6)));
			Loc.Z -= Bounds.Min.Z * Scale.Z;
		}

		if (!World)
		{
			return TEXT("mesh=") + Mesh->GetPathName() + TEXT(" (no editor world to spawn)");
		}
		const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "ImportStaticMesh", "Editor Prompt import_static_mesh"));
		World->Modify();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Loc, Rot, Params);
		if (!Actor)
		{
			return TEXT("mesh=") + Mesh->GetPathName() + TEXT(" spawn failed");
		}
		TouchActor(Actor);
		Actor->SetActorScale3D(Scale);
		if (!Label.IsEmpty())
		{
			Actor->SetActorLabel(Label);
		}
		if (bPreview)
		{
			Actor->Tags.AddUnique(PreviewTag);
		}
		if (UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent())
		{
			Comp->SetFlags(RF_Transactional);
			Comp->Modify();
			Comp->SetStaticMesh(Mesh);
		}
		if (!bPreview)
		{
			TArray<UPackage*> Packages;
			for (UObject* Obj : Imported)
			{
				if (Obj && Obj->GetOutermost())
				{
					Packages.AddUnique(Obj->GetOutermost());
				}
			}
			Packages.AddUnique(Mesh->GetOutermost());
			UEditorLoadingAndSavingUtils::SavePackages(Packages, true);
		}
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(Actor, true, true, true);
		if (bPreview)
		{
			GEditor->MoveViewportCamerasToActor(*Actor, false);
		}
		Redraw();
		bOk = true;
		return FString::Printf(
			TEXT("mesh=%s actor=%s scale=%.3f height=%.0f"),
			*Mesh->GetPathName(),
			*Actor->GetActorLabel(),
			Scale.Z,
			Bounds.GetSize().Z * Scale.Z);
	}

	FString DiscardMeshPreview(const TSharedPtr<FJsonObject>& Args, UWorld* World, bool& bOk)
	{
		const FString Folder = ArgStr(Args, TEXT("folder"));
		if (!Folder.StartsWith(PreviewRoot))
		{
			return FString::Printf(TEXT("folder must be under %s"), PreviewRoot);
		}
		const FString Label = ArgStr(Args, TEXT("label"));
		DestroyTaggedPreview(World, Label);
		TArray<UObject*> Objects = LoadAssetsUnder(Folder);
		int32 Deleted = 0;
		if (Objects.Num() > 0)
		{
			Deleted = ObjectTools::ForceDeleteObjects(Objects, false);
		}
		Redraw();
		bOk = true;
		return FString::Printf(TEXT("discarded actor=%s assets=%d folder=%s"), *Label, Deleted, *Folder);
	}
}

void RegisterEditorPromptMeshTools()
{
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("file"), Prop(TEXT("string"), TEXT(".glb/.gltf under the project")));
		P->SetObjectField(TEXT("dest"), Prop(TEXT("string"), TEXT("/Game folder for the mesh, materials, textures")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Static mesh asset name, e.g. SM_Tree")));
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Actor label")));
		P->SetObjectField(TEXT("preview"), Prop(TEXT("boolean"), TEXT("Tag EditorPromptPreview; dest must be under /Game/Meshes/Preview/")));
		P->SetObjectField(TEXT("replace"), Prop(TEXT("boolean"), TEXT("Overwrite an existing asset of that name, default false")));
		P->SetObjectField(TEXT("height"), Prop(TEXT("number"), TEXT("Scale so the mesh is this tall in cm")));
		P->SetObjectField(TEXT("copy_from"), Prop(TEXT("string"), TEXT("Spawn at this actor's transform")));
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("Bottom center X")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Bottom center Y")));
		P->SetObjectField(TEXT("z"), Prop(TEXT("number"), TEXT("Bottom center Z")));
		RegisterTool(
			TEXT("import_static_mesh"),
			TEXT("Sidecar mesh flow only: import a TRELLIS GLB as a static mesh and spawn it. Agents do not call this."),
			P,
			[](const TSharedPtr<FJsonObject>& Args, UWorld* World, bool& bOk) -> FString
			{
				return ImportStaticMesh(Args, World, bOk);
			});
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Preview actor label")));
		P->SetObjectField(TEXT("folder"), Prop(TEXT("string"), TEXT("Preview asset folder under /Game/Meshes/Preview/")));
		RegisterTool(
			TEXT("discard_mesh_preview"),
			TEXT("Sidecar mesh flow only: remove the preview actor and its preview assets."),
			P,
			[](const TSharedPtr<FJsonObject>& Args, UWorld* World, bool& bOk) -> FString
			{
				return DiscardMeshPreview(Args, World, bOk);
			});
	}
}
