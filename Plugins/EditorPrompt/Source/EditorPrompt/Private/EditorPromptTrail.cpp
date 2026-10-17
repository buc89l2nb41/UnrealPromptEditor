#include "EditorPromptTrail.h"
#include "EditorPromptLog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SceneComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TextureResource.h"

namespace
{
	TArray<FEditorPromptTrailBinding> Bindings;
	FTSTicker::FDelegateHandle TickHandle;
	UTexture2D* WhiteTex = nullptr;
	UTexture2D* RadialTex = nullptr;

	FString SanitizeId(const FString& Text)
	{
		FString Out;
		for (const TCHAR Ch : Text)
		{
			if (FChar::IsAlnum(Ch) || Ch == TEXT('_') || Ch == TEXT('-'))
			{
				Out.AppendChar(Ch);
			}
		}
		return Out.Left(48);
	}

	FString StorePath()
	{
		return FPaths::ProjectSavedDir() / TEXT("EditorPrompt/trails.json");
	}

	void Save()
	{
		TArray<TSharedPtr<FJsonValue>> Rows;
		for (const FEditorPromptTrailBinding& Binding : Bindings)
		{
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("id"), Binding.Id);
			Row->SetStringField(TEXT("actor"), Binding.Actor);
			Row->SetStringField(TEXT("socket"), Binding.Socket);
			Row->SetStringField(TEXT("component"), Binding.Component);
			Row->SetStringField(TEXT("overlap"), Binding.Overlap);
			Row->SetStringField(TEXT("overlap_component"), Binding.OverlapComponent);
			Row->SetStringField(TEXT("bounds_actor"), Binding.BoundsActor);
			Row->SetStringField(TEXT("material"), Binding.MaterialPath);
			Row->SetStringField(TEXT("parameter"), Binding.Parameter);
			Row->SetNumberField(TEXT("center_x"), Binding.CenterX);
			Row->SetNumberField(TEXT("center_y"), Binding.CenterY);
			Row->SetNumberField(TEXT("size"), Binding.Size);
			Row->SetNumberField(TEXT("radius"), Binding.Radius);
			Row->SetNumberField(TEXT("fade"), Binding.Fade);
			Row->SetNumberField(TEXT("pad"), Binding.OverlapPad);
			Row->SetNumberField(TEXT("resolution"), Binding.Resolution);
			Row->SetBoolField(TEXT("has_center"), Binding.bHasCenter);
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("bindings"), Rows);
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Root, Writer);
		const FString Path = StorePath();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveStringToFile(Text, *Path);
	}

	void Load()
	{
		Bindings.Reset();
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
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Root->TryGetArrayField(TEXT("bindings"), Rows) || !Rows)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Rows)
		{
			const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Row.IsValid())
			{
				continue;
			}
			FEditorPromptTrailBinding Binding;
			Binding.Id = Row->GetStringField(TEXT("id"));
			Binding.Actor = Row->GetStringField(TEXT("actor"));
			Row->TryGetStringField(TEXT("socket"), Binding.Socket);
			Row->TryGetStringField(TEXT("component"), Binding.Component);
			Row->TryGetStringField(TEXT("overlap"), Binding.Overlap);
			Row->TryGetStringField(TEXT("overlap_component"), Binding.OverlapComponent);
			Row->TryGetStringField(TEXT("bounds_actor"), Binding.BoundsActor);
			Row->TryGetStringField(TEXT("material"), Binding.MaterialPath);
			Row->TryGetStringField(TEXT("parameter"), Binding.Parameter);
			Binding.CenterX = static_cast<float>(Row->GetNumberField(TEXT("center_x")));
			Binding.CenterY = static_cast<float>(Row->GetNumberField(TEXT("center_y")));
			Binding.Size = static_cast<float>(Row->GetNumberField(TEXT("size")));
			Binding.Radius = static_cast<float>(Row->GetNumberField(TEXT("radius")));
			Binding.Fade = static_cast<float>(Row->GetNumberField(TEXT("fade")));
			double Pad = 0.0;
			Row->TryGetNumberField(TEXT("pad"), Pad);
			Binding.OverlapPad = static_cast<float>(Pad);
			Binding.Resolution = static_cast<int32>(Row->GetNumberField(TEXT("resolution")));
			Row->TryGetBoolField(TEXT("has_center"), Binding.bHasCenter);
			if (!Binding.Id.IsEmpty() && !Binding.Actor.IsEmpty())
			{
				Bindings.Add(Binding);
			}
		}
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
		if (!World || Actor.IsEmpty())
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

	bool TryPoint(AActor* Actor, const FEditorPromptTrailBinding& Binding, FVector& Out)
	{
		if (!Actor)
		{
			return false;
		}
		TInlineComponentArray<USceneComponent*> Components(Actor);
		if (!Binding.Socket.IsEmpty())
		{
			for (USceneComponent* Component : Components)
			{
				if (Component && Component->DoesSocketExist(*Binding.Socket))
				{
					Out = Component->GetSocketLocation(*Binding.Socket);
					return true;
				}
			}
			return false;
		}
		if (!Binding.Component.IsEmpty())
		{
			for (USceneComponent* Component : Components)
			{
				if (Component && Component->GetName().Equals(Binding.Component, ESearchCase::IgnoreCase))
				{
					Out = Component->GetComponentLocation();
					return true;
				}
			}
			return false;
		}
		Out = Actor->GetActorLocation();
		return true;
	}

	UTexture2D* Solid(int32 N, bool bRadial)
	{
		UTexture2D* Tex = UTexture2D::CreateTransient(N, N, PF_B8G8R8A8);
		if (!Tex || !Tex->GetPlatformData() || Tex->GetPlatformData()->Mips.Num() == 0)
		{
			return nullptr;
		}
		FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
		uint8* Data = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				uint8 Alpha = 255;
				if (bRadial)
				{
					const float Dx = (static_cast<float>(X) + 0.5f) / static_cast<float>(N) - 0.5f;
					const float Dy = (static_cast<float>(Y) + 0.5f) / static_cast<float>(N) - 0.5f;
					const float D = FMath::Sqrt(Dx * Dx + Dy * Dy) * 2.f;
					Alpha = D >= 1.f ? 0 : static_cast<uint8>((1.f - D) * (1.f - D) * 255.f);
				}
				const int32 I = (Y * N + X) * 4;
				Data[I + 0] = 255;
				Data[I + 1] = 255;
				Data[I + 2] = 255;
				Data[I + 3] = Alpha;
			}
		}
		Mip.BulkData.Unlock();
		Tex->SRGB = false;
		Tex->UpdateResource();
		Tex->AddToRoot();
		return Tex;
	}

	void EnsureBrushes()
	{
		if (!WhiteTex)
		{
			WhiteTex = Solid(4, false);
		}
		if (!RadialTex)
		{
			RadialTex = Solid(64, true);
		}
	}

	FString TargetPath(const FString& Id)
	{
		return FString::Printf(TEXT("/Game/EditorPrompt/RT_%s.RT_%s"), *Id, *Id);
	}

	UTextureRenderTarget2D* EnsureTarget(const FEditorPromptTrailBinding& Binding)
	{
		const FString Full = TargetPath(Binding.Id);
		if (UTextureRenderTarget2D* Existing = LoadObject<UTextureRenderTarget2D>(nullptr, *Full))
		{
			return Existing;
		}
		const FString PackageName = FString::Printf(TEXT("/Game/EditorPrompt/RT_%s"), *Binding.Id);
		UPackage* Package = CreatePackage(*PackageName);
		UTextureRenderTarget2D* Created = NewObject<UTextureRenderTarget2D>(
			Package, *FString::Printf(TEXT("RT_%s"), *Binding.Id), RF_Public | RF_Standalone | RF_Transactional);
		if (!Created)
		{
			return nullptr;
		}
		Created->RenderTargetFormat = RTF_RGBA8;
		Created->ClearColor = FLinearColor::Black;
		Created->InitAutoFormat(FMath::Clamp(Binding.Resolution, 64, 1024), FMath::Clamp(Binding.Resolution, 64, 1024));
		Created->UpdateResourceImmediate(true);
		FAssetRegistryModule::AssetCreated(Created);
		Created->MarkPackageDirty();
		TArray<UPackage*> Packages;
		Packages.Add(Package);
		UEditorLoadingAndSavingUtils::SavePackages(Packages, true);
		return Created;
	}

	void EnsureTextureParam(const FEditorPromptTrailBinding& Binding, UTexture* Texture)
	{
		if (Binding.MaterialPath.IsEmpty() || Binding.Parameter.IsEmpty() || !Texture)
		{
			return;
		}
		UMaterialInterface* Interface = LoadObject<UMaterialInterface>(nullptr, *Binding.MaterialPath);
		if (!Interface)
		{
			return;
		}
		UMaterial* Material = Interface->GetMaterial();
		if (!Material || Material->GetPathName().StartsWith(TEXT("/Engine")))
		{
			return;
		}
		bool bFound = false;
		for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
		{
			if (UMaterialExpressionTextureSampleParameter2D* Sample = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression))
			{
				if (Sample->ParameterName.ToString().Equals(Binding.Parameter, ESearchCase::IgnoreCase))
				{
					bFound = true;
					break;
				}
			}
		}
		if (!bFound)
		{
			UMaterialExpression* Created = UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionTextureSampleParameter2D::StaticClass(), -400, 200);
			if (UMaterialExpressionTextureSampleParameter2D* Sample = Cast<UMaterialExpressionTextureSampleParameter2D>(Created))
			{
				Sample->ParameterName = *Binding.Parameter;
				Sample->Texture = Texture;
				Sample->Desc = Binding.Parameter;
				UMaterialEditingLibrary::RecompileMaterial(Material);
			}
		}
		if (UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Interface))
		{
			Instance->SetTextureParameterValueEditorOnly(*Binding.Parameter, Texture);
			Instance->MarkPackageDirty();
		}
	}

	void Paint(UWorld* World, const FEditorPromptTrailBinding& Binding, bool bStamp, const FVector& Point)
	{
		EnsureBrushes();
		UTextureRenderTarget2D* Target = EnsureTarget(Binding);
		if (!World || !Target || !WhiteTex)
		{
			return;
		}
		UCanvas* Canvas = nullptr;
		FVector2D Size(ForceInit);
		FDrawToRenderTargetContext Context;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, Target, Canvas, Size, Context);
		if (Canvas && Size.X > 1.f && Size.Y > 1.f)
		{
			const float FadeAlpha = FMath::Clamp(Binding.Fade * 0.05f, 0.f, 0.5f);
			if (FadeAlpha > 0.f)
			{
				Canvas->K2_DrawTexture(
					WhiteTex, FVector2D::ZeroVector, Size, FVector2D::ZeroVector, FVector2D::UnitVector,
					FLinearColor(0.f, 0.f, 0.f, FadeAlpha), BLEND_Translucent);
			}
			if (bStamp && RadialTex && Binding.Size > 1.f)
			{
				const float U = (Point.X - Binding.CenterX) / Binding.Size + 0.5f;
				const float V = (Point.Y - Binding.CenterY) / Binding.Size + 0.5f;
				const float Diam = (Binding.Radius * 2.f / Binding.Size) * Size.X;
				Canvas->K2_DrawTexture(
					RadialTex,
					FVector2D(U * Size.X - Diam * 0.5f, V * Size.Y - Diam * 0.5f),
					FVector2D(Diam, Diam),
					FVector2D::ZeroVector,
					FVector2D::UnitVector,
					FLinearColor::White,
					BLEND_Translucent);
			}
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
	}

	void ApplyWorld(UWorld* World, bool bPlayWorld)
	{
		if (!World)
		{
			return;
		}
		for (FEditorPromptTrailBinding& Binding : Bindings)
		{
			if (!Binding.BoundsActor.IsEmpty() && !Binding.bHasCenter)
			{
				if (AActor* BoundsActor = ResolveActor(World, Binding.BoundsActor, bPlayWorld))
				{
					const FBox Box = BoundsActor->GetComponentsBoundingBox(true);
					if (Box.IsValid)
					{
						Binding.CenterX = static_cast<float>(Box.GetCenter().X);
						Binding.CenterY = static_cast<float>(Box.GetCenter().Y);
						Binding.Size = static_cast<float>(FMath::Max(Box.GetSize().X, Box.GetSize().Y));
						Binding.bHasCenter = Binding.Size > 1.f;
					}
				}
			}
			AActor* Actor = ResolveActor(World, Binding.Actor, bPlayWorld);
			FVector Point = FVector::ZeroVector;
			bool bStamp = Actor && TryPoint(Actor, Binding, Point);
			if (bStamp && !Binding.Overlap.IsEmpty())
			{
				bStamp = BoundsOverlap(
					Actor, Binding.Component, FindLabeled(World, Binding.Overlap), Binding.OverlapComponent, Binding.OverlapPad);
			}
			Paint(World, Binding, bStamp, Point);
		}
	}

	bool Tick(float)
	{
		if (!GEditor || Bindings.Num() == 0)
		{
			return true;
		}
		if (GEditor->PlayWorld)
		{
			ApplyWorld(GEditor->PlayWorld, true);
		}
		else
		{
			ApplyWorld(GEditor->GetEditorWorldContext().World(), false);
		}
		return true;
	}
}

void FEditorPromptTrail::Startup()
{
	Load();
	if (!TickHandle.IsValid())
	{
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), 0.05f);
	}
}

void FEditorPromptTrail::Shutdown()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	if (WhiteTex)
	{
		WhiteTex->RemoveFromRoot();
		WhiteTex = nullptr;
	}
	if (RadialTex)
	{
		RadialTex->RemoveFromRoot();
		RadialTex = nullptr;
	}
}

FString FEditorPromptTrail::Apply(const FEditorPromptTrailBinding& InBinding)
{
	if (InBinding.Actor.IsEmpty())
	{
		return TEXT("actor is empty");
	}
	FEditorPromptTrailBinding Binding = InBinding;
	if (Binding.Id.IsEmpty())
	{
		Binding.Id = SanitizeId(Binding.Actor + TEXT("_trail"));
	}
	else
	{
		Binding.Id = SanitizeId(Binding.Id);
	}
	if (Binding.Id.IsEmpty())
	{
		return TEXT("id is empty");
	}
	Binding.Size = FMath::Max(Binding.Size, 10.f);
	Binding.Radius = FMath::Max(Binding.Radius, 1.f);
	Binding.Resolution = FMath::Clamp(Binding.Resolution, 64, 1024);
	if (!Binding.BoundsActor.IsEmpty())
	{
		Binding.bHasCenter = false;
	}
	bool bReplaced = false;
	for (FEditorPromptTrailBinding& Existing : Bindings)
	{
		if (Existing.Id == Binding.Id)
		{
			Existing = Binding;
			bReplaced = true;
			break;
		}
	}
	if (!bReplaced)
	{
		Bindings.Add(Binding);
	}
	Save();
	UTextureRenderTarget2D* Target = EnsureTarget(Binding);
	if (!Target)
	{
		return TEXT("render target create failed");
	}
	EnsureTextureParam(Binding, Target);
	FString Message = FString::Printf(
		TEXT("trail id=%s actor=%s target=%s size=%.0f radius=%.0f fade=%.2f"),
		*Binding.Id,
		*Binding.Actor,
		*Target->GetPathName(),
		Binding.Size,
		Binding.Radius,
		Binding.Fade);
	Message += TEXT("\nUV x=(worldX-centerX)/size+0.5, UV y=(worldY-centerY)/size+0.5. The texture fades toward black.");
	if (!Binding.Parameter.IsEmpty())
	{
		Message += TEXT("\nTexture parameter ") + Binding.Parameter + TEXT(" is on the material. Connect it with connect_material.");
	}
	return Message;
}

FString FEditorPromptTrail::Clear(const FString& Id)
{
	const FString Clean = SanitizeId(Id);
	const int32 Before = Bindings.Num();
	if (Clean.IsEmpty())
	{
		Bindings.Reset();
	}
	else
	{
		Bindings.RemoveAll([&](const FEditorPromptTrailBinding& Binding) { return Binding.Id == Clean; });
	}
	Save();
	if (Bindings.Num() == Before && !Clean.IsEmpty())
	{
		return TEXT("no trail");
	}
	return Clean.IsEmpty() ? TEXT("trails cleared") : TEXT("trail cleared ") + Clean;
}
