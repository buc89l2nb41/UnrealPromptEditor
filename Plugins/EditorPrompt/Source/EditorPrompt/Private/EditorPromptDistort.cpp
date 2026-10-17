#include "EditorPromptDistort.h"
#include "EditorPromptLog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SceneComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	const TCHAR* Marker = TEXT("EPNormalDistort");
	const TCHAR* MpcPath = TEXT("/Game/EditorPrompt/MPC_NormalDistort.MPC_NormalDistort");
	const TCHAR* MpcPackage = TEXT("/Game/EditorPrompt/MPC_NormalDistort");
	const FName OriginName(TEXT("Origin"));
	const FName StrengthName(TEXT("Strength"));

	FEditorPromptDistortBinding Driver;
	bool bHasDriver = false;
	FTSTicker::FDelegateHandle TickHandle;

	FString StorePath()
	{
		return FPaths::ProjectSavedDir() / TEXT("EditorPrompt/distort.json");
	}

	void Save()
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("enabled"), bHasDriver);
		Root->SetStringField(TEXT("material"), Driver.MaterialPath);
		Root->SetStringField(TEXT("actor"), Driver.Actor);
		Root->SetStringField(TEXT("overlap"), Driver.Overlap);
		Root->SetStringField(TEXT("component"), Driver.Component);
		Root->SetStringField(TEXT("overlap_component"), Driver.OverlapComponent);
		Root->SetNumberField(TEXT("radius"), Driver.Radius);
		Root->SetNumberField(TEXT("strength"), Driver.Strength);
		Root->SetNumberField(TEXT("pad"), Driver.OverlapPad);
		if (Driver.bLockSurfaceZ)
		{
			Root->SetNumberField(TEXT("surface_z"), Driver.SurfaceZ);
		}
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Root, Writer);
		const FString Path = StorePath();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveStringToFile(Text, *Path);
	}

	void Load()
	{
		bHasDriver = false;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *StorePath()))
		{
			return;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return;
		}
		bool bEnabled = false;
		Root->TryGetBoolField(TEXT("enabled"), bEnabled);
		if (!bEnabled)
		{
			return;
		}
		Driver.MaterialPath = Root->GetStringField(TEXT("material"));
		Driver.Actor = Root->GetStringField(TEXT("actor"));
		Driver.Overlap = Root->GetStringField(TEXT("overlap"));
		Root->TryGetStringField(TEXT("component"), Driver.Component);
		Root->TryGetStringField(TEXT("overlap_component"), Driver.OverlapComponent);
		Driver.Radius = static_cast<float>(Root->GetNumberField(TEXT("radius")));
		Driver.Strength = static_cast<float>(Root->GetNumberField(TEXT("strength")));
		double Pad = 0.0;
		Root->TryGetNumberField(TEXT("pad"), Pad);
		Driver.OverlapPad = static_cast<float>(Pad);
		double SurfaceZ = 0.0;
		Driver.bLockSurfaceZ = Root->TryGetNumberField(TEXT("surface_z"), SurfaceZ);
		Driver.SurfaceZ = static_cast<float>(SurfaceZ);
		if (Driver.Actor.IsEmpty() || Driver.MaterialPath.IsEmpty())
		{
			return;
		}
		if (Driver.Radius < 1.f)
		{
			Driver.Radius = 1.f;
		}
		bHasDriver = true;
	}

	bool IsPlayerToken(const FString& Actor)
	{
		return Actor.Equals(TEXT("player"), ESearchCase::IgnoreCase)
			|| Actor.Equals(TEXT("pawn"), ESearchCase::IgnoreCase);
	}

	AActor* FindLabeled(UWorld* World, const FString& Label)
	{
		if (!World || Label.IsEmpty() || IsPlayerToken(Label))
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

	AActor* ResolveActor(UWorld* World, const FString& Actor, bool bPlayWorld)
	{
		if (!World)
		{
			return nullptr;
		}
		if (IsPlayerToken(Actor))
		{
			if (!bPlayWorld)
			{
				return nullptr;
			}
			APlayerController* Controller = World->GetFirstPlayerController();
			return Controller ? Controller->GetPawn() : nullptr;
		}
		return FindLabeled(World, Actor);
	}

	FBox ComponentOrActorBox(const AActor* Actor, const FString& ComponentName)
	{
		if (!Actor)
		{
			return FBox(ForceInit);
		}
		if (!ComponentName.IsEmpty())
		{
			TInlineComponentArray<USceneComponent*> Components(const_cast<AActor*>(Actor));
			for (USceneComponent* Component : Components)
			{
				if (Component && Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
				{
					return Component->CalcBounds(Component->GetComponentTransform()).GetBox();
				}
			}
			return FBox(ForceInit);
		}
		return Actor->GetComponentsBoundingBox(true);
	}

	bool BoundsOverlap(const AActor* A, const FString& CompA, const AActor* B, const FString& CompB, float Pad)
	{
		if (!A || !B)
		{
			return false;
		}
		const FBox BoxA = ComponentOrActorBox(A, CompA).ExpandBy(Pad);
		const FBox BoxB = ComponentOrActorBox(B, CompB).ExpandBy(Pad);
		return BoxA.IsValid && BoxB.IsValid && BoxA.Intersect(BoxB);
	}

	int32 FindVectorParam(const UMaterialParameterCollection* Collection, FName Name)
	{
		if (!Collection)
		{
			return INDEX_NONE;
		}
		for (int32 Index = 0; Index < Collection->VectorParameters.Num(); ++Index)
		{
			if (Collection->VectorParameters[Index].ParameterName == Name)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	int32 FindScalarParam(const UMaterialParameterCollection* Collection, FName Name)
	{
		if (!Collection)
		{
			return INDEX_NONE;
		}
		for (int32 Index = 0; Index < Collection->ScalarParameters.Num(); ++Index)
		{
			if (Collection->ScalarParameters[Index].ParameterName == Name)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	void FinalizeCollection(UMaterialParameterCollection* Collection)
	{
		// NewObject never runs PostLoad, so UniformBufferStruct stays null until this.
		// Compiling a material that references the collection asserts without it.
		FPropertyChangedEvent Event(nullptr, EPropertyChangeType::ValueSet);
		Collection->PostEditChangeProperty(Event);
	}

	UMaterialParameterCollection* LoadOrCreateCollection(FString& OutError)
	{
		if (UMaterialParameterCollection* Existing = LoadObject<UMaterialParameterCollection>(nullptr, MpcPath))
		{
			bool bAdded = false;
			if (FindVectorParam(Existing, OriginName) == INDEX_NONE)
			{
				FCollectionVectorParameter Origin;
				Origin.ParameterName = OriginName;
				Origin.DefaultValue = FLinearColor(0.f, 0.f, 0.f, 180.f);
				Existing->VectorParameters.Add(Origin);
				bAdded = true;
			}
			if (FindScalarParam(Existing, StrengthName) == INDEX_NONE)
			{
				FCollectionScalarParameter Strength;
				Strength.ParameterName = StrengthName;
				Strength.DefaultValue = 0.f;
				Existing->ScalarParameters.Add(Strength);
				bAdded = true;
			}
			if (bAdded)
			{
				Existing->StateId = FGuid::NewGuid();
				Existing->MarkPackageDirty();
				FinalizeCollection(Existing);
			}
			return Existing;
		}
		UPackage* Package = CreatePackage(MpcPackage);
		UMaterialParameterCollection* Created = NewObject<UMaterialParameterCollection>(
			Package, TEXT("MPC_NormalDistort"), RF_Public | RF_Standalone | RF_Transactional);
		if (!Created)
		{
			OutError = TEXT("Create parameter collection failed");
			return nullptr;
		}
		FCollectionVectorParameter Origin;
		Origin.ParameterName = OriginName;
		Origin.DefaultValue = FLinearColor(0.f, 0.f, 0.f, 180.f);
		Created->VectorParameters.Add(Origin);
		FCollectionScalarParameter Strength;
		Strength.ParameterName = StrengthName;
		Strength.DefaultValue = 0.f;
		Created->ScalarParameters.Add(Strength);
		Created->StateId = FGuid::NewGuid();
		FAssetRegistryModule::AssetCreated(Created);
		Created->MarkPackageDirty();
		FinalizeCollection(Created);
		return Created;
	}

	FGuid ParameterId(const UMaterialParameterCollection* Collection, FName Name, bool bVector)
	{
		if (!Collection)
		{
			return FGuid();
		}
		if (bVector)
		{
			const int32 Index = FindVectorParam(Collection, Name);
			return Index == INDEX_NONE ? FGuid() : Collection->VectorParameters[Index].Id;
		}
		const int32 Index = FindScalarParam(Collection, Name);
		return Index == INDEX_NONE ? FGuid() : Collection->ScalarParameters[Index].Id;
	}

	bool AlreadyPatched(const UMaterial* Material)
	{
		if (!Material)
		{
			return false;
		}
		for (const UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
		{
			if (Expression && Expression->Desc == Marker)
			{
				return true;
			}
		}
		return false;
	}

	UMaterialExpression* Expr(UMaterial* Material, UClass* Class, int32 X, int32 Y)
	{
		return UMaterialEditingLibrary::CreateMaterialExpression(Material, Class, X, Y);
	}

	bool Link(UMaterialExpression* From, const TCHAR* FromPin, UMaterialExpression* To, const TCHAR* ToPin, FString& OutError)
	{
		if (From && To && UMaterialEditingLibrary::ConnectMaterialExpressions(From, FromPin, To, ToPin))
		{
			return true;
		}
		OutError = FString::Printf(TEXT("material link failed (%s -> %s)"), FromPin, ToPin);
		return false;
	}

	bool PatchMaterial(UMaterial* Material, UMaterialParameterCollection* Collection, FString& OutNote, FString& OutError)
	{
		if (AlreadyPatched(Material))
		{
			OutNote = TEXT("graph already patched");
			return true;
		}
		const FExpressionInput* ExistingNormal = Material->GetExpressionInputForProperty(MP_Normal);
		UMaterialExpression* BaseNormal = ExistingNormal ? ExistingNormal->Expression : nullptr;
		if (BaseNormal == nullptr && !Material->bTangentSpaceNormal)
		{
			OutError = TEXT("material outputs a world-space normal with nothing on the Normal pin");
			return false;
		}
		const bool bWasWorldNormal = !Material->bTangentSpaceNormal && BaseNormal == nullptr;
		const bool bMakeTranslucent = Material->BlendMode == BLEND_Opaque || Material->BlendMode == BLEND_Masked;
		const FExpressionInput* ExistingOpacity = Material->GetExpressionInputForProperty(MP_Opacity);
		const bool bNeedOpacity = bMakeTranslucent && (!ExistingOpacity || !ExistingOpacity->Expression);
		const FExpressionInput* ExistingRefraction = Material->GetExpressionInputForProperty(MP_Refraction);
		const bool bNeedRefraction = !ExistingRefraction || !ExistingRefraction->Expression;

		UMaterialExpressionCollectionParameter* Origin = Cast<UMaterialExpressionCollectionParameter>(
			Expr(Material, UMaterialExpressionCollectionParameter::StaticClass(), -900, -80));
		UMaterialExpressionCollectionParameter* Strength = Cast<UMaterialExpressionCollectionParameter>(
			Expr(Material, UMaterialExpressionCollectionParameter::StaticClass(), -900, 80));
		UMaterialExpressionWorldPosition* WorldPos = Cast<UMaterialExpressionWorldPosition>(
			Expr(Material, UMaterialExpressionWorldPosition::StaticClass(), -900, -240));
		UMaterialExpressionComponentMask* OriginXYZ = Cast<UMaterialExpressionComponentMask>(
			Expr(Material, UMaterialExpressionComponentMask::StaticClass(), -680, -80));
		UMaterialExpressionComponentMask* Radius = Cast<UMaterialExpressionComponentMask>(
			Expr(Material, UMaterialExpressionComponentMask::StaticClass(), -680, 40));
		UMaterialExpressionSubtract* Delta = Cast<UMaterialExpressionSubtract>(
			Expr(Material, UMaterialExpressionSubtract::StaticClass(), -480, -200));
		UMaterialExpressionDistance* Distance = Cast<UMaterialExpressionDistance>(
			Expr(Material, UMaterialExpressionDistance::StaticClass(), -480, -40));
		UMaterialExpressionDivide* Ratio = Cast<UMaterialExpressionDivide>(
			Expr(Material, UMaterialExpressionDivide::StaticClass(), -280, -40));
		UMaterialExpressionSaturate* Saturated = Cast<UMaterialExpressionSaturate>(
			Expr(Material, UMaterialExpressionSaturate::StaticClass(), -80, -40));
		UMaterialExpressionOneMinus* Falloff = Cast<UMaterialExpressionOneMinus>(
			Expr(Material, UMaterialExpressionOneMinus::StaticClass(), 80, -40));
		UMaterialExpressionNormalize* Radial = Cast<UMaterialExpressionNormalize>(
			Expr(Material, UMaterialExpressionNormalize::StaticClass(), -280, -200));
		UMaterialExpressionTransform* ToTangent = Cast<UMaterialExpressionTransform>(
			Expr(Material, UMaterialExpressionTransform::StaticClass(), -80, -200));
		UMaterialExpressionComponentMask* TangentXY = Cast<UMaterialExpressionComponentMask>(
			Expr(Material, UMaterialExpressionComponentMask::StaticClass(), 80, -200));
		UMaterialExpressionConstant* Zero = Cast<UMaterialExpressionConstant>(
			Expr(Material, UMaterialExpressionConstant::StaticClass(), 80, -80));
		UMaterialExpressionAppendVector* Tilt = Cast<UMaterialExpressionAppendVector>(
			Expr(Material, UMaterialExpressionAppendVector::StaticClass(), 260, -200));
		UMaterialExpressionMultiply* TiltFalloff = Cast<UMaterialExpressionMultiply>(
			Expr(Material, UMaterialExpressionMultiply::StaticClass(), 440, -160));
		UMaterialExpressionMultiply* TiltStrength = Cast<UMaterialExpressionMultiply>(
			Expr(Material, UMaterialExpressionMultiply::StaticClass(), 620, -80));
		UMaterialExpressionAdd* Added = Cast<UMaterialExpressionAdd>(
			Expr(Material, UMaterialExpressionAdd::StaticClass(), 800, -200));
		UMaterialExpressionNormalize* Result = Cast<UMaterialExpressionNormalize>(
			Expr(Material, UMaterialExpressionNormalize::StaticClass(), 980, -200));
		UMaterialExpression* Base = BaseNormal;
		UMaterialExpressionConstant3Vector* Flat = nullptr;
		if (!Base)
		{
			Flat = Cast<UMaterialExpressionConstant3Vector>(
				Expr(Material, UMaterialExpressionConstant3Vector::StaticClass(), 620, -280));
			Base = Flat;
		}
		if (!Origin || !Strength || !WorldPos || !OriginXYZ || !Radius || !Delta || !Distance || !Ratio
			|| !Saturated || !Falloff || !Radial || !ToTangent || !TangentXY || !Zero || !Tilt
			|| !TiltFalloff || !TiltStrength || !Added || !Result || !Base)
		{
			OutError = TEXT("create material expressions failed");
			return false;
		}

		Origin->Collection = Collection;
		Origin->ParameterName = OriginName;
		Origin->ParameterId = ParameterId(Collection, OriginName, true);
		Strength->Collection = Collection;
		Strength->ParameterName = StrengthName;
		Strength->ParameterId = ParameterId(Collection, StrengthName, false);
		OriginXYZ->R = 1;
		OriginXYZ->G = 1;
		OriginXYZ->B = 1;
		Radius->A = 1;
		ToTangent->TransformSourceType = TRANSFORMSOURCE_World;
		ToTangent->TransformType = TRANSFORM_Tangent;
		TangentXY->R = 1;
		TangentXY->G = 1;
		Zero->R = 0.f;
		if (Flat)
		{
			Flat->Constant = FLinearColor(0.f, 0.f, 1.f);
		}
		Result->Desc = Marker;

		const bool bLinked =
			Link(Origin, TEXT(""), OriginXYZ, TEXT(""), OutError)
			&& Link(Origin, TEXT(""), Radius, TEXT(""), OutError)
			&& Link(WorldPos, TEXT(""), Delta, TEXT("A"), OutError)
			&& Link(OriginXYZ, TEXT(""), Delta, TEXT("B"), OutError)
			&& Link(WorldPos, TEXT(""), Distance, TEXT("A"), OutError)
			&& Link(OriginXYZ, TEXT(""), Distance, TEXT("B"), OutError)
			&& Link(Distance, TEXT(""), Ratio, TEXT("A"), OutError)
			&& Link(Radius, TEXT(""), Ratio, TEXT("B"), OutError)
			&& Link(Ratio, TEXT(""), Saturated, TEXT(""), OutError)
			&& Link(Saturated, TEXT(""), Falloff, TEXT(""), OutError)
			&& Link(Delta, TEXT(""), Radial, TEXT(""), OutError)
			&& Link(Radial, TEXT(""), ToTangent, TEXT(""), OutError)
			&& Link(ToTangent, TEXT(""), TangentXY, TEXT(""), OutError)
			&& Link(TangentXY, TEXT(""), Tilt, TEXT("A"), OutError)
			&& Link(Zero, TEXT(""), Tilt, TEXT("B"), OutError)
			&& Link(Tilt, TEXT(""), TiltFalloff, TEXT("A"), OutError)
			&& Link(Falloff, TEXT(""), TiltFalloff, TEXT("B"), OutError)
			&& Link(TiltFalloff, TEXT(""), TiltStrength, TEXT("A"), OutError)
			&& Link(Strength, TEXT(""), TiltStrength, TEXT("B"), OutError)
			&& Link(Base, TEXT(""), Added, TEXT("A"), OutError)
			&& Link(TiltStrength, TEXT(""), Added, TEXT("B"), OutError)
			&& Link(Added, TEXT(""), Result, TEXT(""), OutError);
		if (!bLinked)
		{
			return false;
		}
		if (!UMaterialEditingLibrary::ConnectMaterialProperty(Result, TEXT(""), MP_Normal))
		{
			OutError = TEXT("connect Normal pin failed");
			return false;
		}

		if (bWasWorldNormal)
		{
			Material->bTangentSpaceNormal = true;
		}
		if (bMakeTranslucent)
		{
			Material->BlendMode = BLEND_Translucent;
			Material->TranslucencyLightingMode = TLM_Surface;
		}
		if (bNeedOpacity)
		{
			UMaterialExpressionConstant* Opacity = Cast<UMaterialExpressionConstant>(
				Expr(Material, UMaterialExpressionConstant::StaticClass(), 980, 40));
			if (!Opacity)
			{
				OutError = TEXT("create opacity constant failed");
				return false;
			}
			Opacity->R = 0.65f;
			Opacity->Desc = TEXT("EPNormalOpacity");
			if (!UMaterialEditingLibrary::ConnectMaterialProperty(Opacity, TEXT(""), MP_Opacity))
			{
				OutError = TEXT("connect Opacity pin failed");
				return false;
			}
		}
		if (bNeedRefraction)
		{
			Material->RefractionMethod = RM_PixelNormalOffset;
			UMaterialExpressionConstant* Refraction = Cast<UMaterialExpressionConstant>(
				Expr(Material, UMaterialExpressionConstant::StaticClass(), 980, 160));
			if (!Refraction)
			{
				OutError = TEXT("create refraction constant failed");
				return false;
			}
			Refraction->R = 1.7f;
			Refraction->Desc = TEXT("EPNormalRefract");
			if (!UMaterialEditingLibrary::ConnectMaterialProperty(Refraction, TEXT(""), MP_Refraction))
			{
				OutError = TEXT("connect Refraction pin failed");
				return false;
			}
		}

		UMaterialEditingLibrary::RecompileMaterial(Material);
		TArray<FString> Notes;
		Notes.Add(TEXT("patched tangent normal"));
		if (bMakeTranslucent)
		{
			Notes.Add(TEXT("blend=Translucent"));
		}
		if (bNeedOpacity)
		{
			Notes.Add(TEXT("opacity=0.65"));
		}
		if (bNeedRefraction)
		{
			Notes.Add(TEXT("refraction=PixelNormalOffset"));
		}
		OutNote = FString::Join(Notes, TEXT(", "));
		return true;
	}

	void WriteCollection(UWorld* World, const FVector& Origin, float Radius, float Strength)
	{
		UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(nullptr, MpcPath);
		if (!World || !Collection)
		{
			return;
		}
		UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(Collection);
		if (!Instance)
		{
			return;
		}
		Instance->SetVectorParameterValue(OriginName, FLinearColor(Origin.X, Origin.Y, Origin.Z, FMath::Max(Radius, 1.f)));
		Instance->SetScalarParameterValue(StrengthName, Strength);
	}

	void ApplyWorld(UWorld* World, bool bPlayWorld)
	{
		if (!World)
		{
			return;
		}
		if (!bHasDriver)
		{
			WriteCollection(World, FVector::ZeroVector, Driver.Radius, 0.f);
			return;
		}
		AActor* Actor = ResolveActor(World, Driver.Actor, bPlayWorld);
		if (!Actor)
		{
			WriteCollection(World, FVector::ZeroVector, Driver.Radius, 0.f);
			return;
		}
		FVector Origin = Actor->GetActorLocation();
		if (Driver.bLockSurfaceZ)
		{
			Origin.Z = Driver.SurfaceZ;
		}
		float Strength = Driver.Strength;
		if (!Driver.Overlap.IsEmpty())
		{
			const bool bHit = BoundsOverlap(
				Actor,
				Driver.Component,
				FindLabeled(World, Driver.Overlap),
				Driver.OverlapComponent,
				Driver.OverlapPad);
			if (!bHit)
			{
				Strength = 0.f;
			}
		}
		WriteCollection(World, Origin, Driver.Radius, Strength);
	}

	void ApplyAll()
	{
		if (!GEditor)
		{
			return;
		}
		ApplyWorld(GEditor->GetEditorWorldContext().World(), false);
		if (GEditor->PlayWorld)
		{
			ApplyWorld(GEditor->PlayWorld, true);
		}
	}

	bool Tick(float)
	{
		ApplyAll();
		return true;
	}

	void SavePackages(UObject* A, UObject* B)
	{
		TArray<UPackage*> Packages;
		if (A)
		{
			Packages.Add(A->GetOutermost());
		}
		if (B && B->GetOutermost() != (A ? A->GetOutermost() : nullptr))
		{
			Packages.Add(B->GetOutermost());
		}
		if (Packages.Num() > 0)
		{
			UEditorLoadingAndSavingUtils::SavePackages(Packages, true);
		}
	}
}

void FEditorPromptDistort::Startup()
{
	Load();
	if (!TickHandle.IsValid())
	{
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), 0.f);
	}
	ApplyAll();
}

void FEditorPromptDistort::Shutdown()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
}

FString FEditorPromptDistort::Apply(const FEditorPromptDistortBinding& Binding)
{
	if (!Binding.MaterialPath.StartsWith(TEXT("/Game")))
	{
		return TEXT("material path must start with /Game");
	}
	if (Binding.Actor.IsEmpty())
	{
		return TEXT("actor is empty");
	}
	UMaterialInterface* Interface = LoadObject<UMaterialInterface>(nullptr, *Binding.MaterialPath);
	UMaterial* Material = Interface ? Interface->GetMaterial() : nullptr;
	if (!Material)
	{
		return TEXT("material not found");
	}
	if (Material->GetPathName().StartsWith(TEXT("/Engine")))
	{
		return TEXT("engine material cannot be edited; duplicate it into /Game first");
	}
	FString CollectionError;
	UMaterialParameterCollection* Collection = LoadOrCreateCollection(CollectionError);
	if (!Collection)
	{
		return CollectionError.IsEmpty() ? TEXT("parameter collection missing") : CollectionError;
	}

	FScopedTransaction Transaction(NSLOCTEXT("EditorPrompt", "DistortNormals", "Editor Prompt distort_normals"));
	Material->Modify();
	Collection->Modify();
	FString Note;
	FString Error;
	if (!PatchMaterial(Material, Collection, Note, Error))
	{
		Transaction.Cancel();
		return Error.IsEmpty() ? TEXT("patch material failed") : Error;
	}
	SavePackages(Material, Collection);

	Driver = Binding;
	Driver.MaterialPath = Material->GetPathName();
	Driver.Radius = FMath::Max(Binding.Radius, 1.f);
	bHasDriver = true;
	Save();
	ApplyAll();

	FString Message = FString::Printf(
		TEXT("distort material=%s actor=%s radius=%.0f strength=%.2f"),
		*Driver.MaterialPath,
		*Driver.Actor,
		Driver.Radius,
		Driver.Strength);
	if (Driver.bLockSurfaceZ)
	{
		Message += FString::Printf(TEXT(" surface_z=%.1f"), Driver.SurfaceZ);
	}
	if (!Driver.Overlap.IsEmpty())
	{
		Message += TEXT(" overlap=") + Driver.Overlap;
		Message += FString::Printf(TEXT(" pad=%.0f"), Driver.OverlapPad);
	}
	Message += TEXT("\n") + Note;
	Message += TEXT("\nXY of the actor drives a tangent normal tilt. Strength is 0 when overlap fails.");
	if (Interface != Material)
	{
		Message += TEXT("\nEdited the parent material; instances of it pick this up.");
	}
	return Message;
}

FString FEditorPromptDistort::Clear()
{
	bHasDriver = false;
	Driver.Actor.Empty();
	Save();
	ApplyAll();
	return TEXT("normal distort driver cleared. Graph nodes stay in the material and strength is 0.");
}
