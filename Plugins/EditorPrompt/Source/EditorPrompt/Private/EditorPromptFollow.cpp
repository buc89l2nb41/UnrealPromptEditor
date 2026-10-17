#include "EditorPromptFollow.h"
#include "EditorPromptLog.h"

#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Components/SceneComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	const TCHAR* FollowTagPrefix = TEXT("EPFollow_");
	TArray<FEditorPromptFollowBinding> Bindings;
	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle PieStartHandle;
	FDelegateHandle PieEndHandle;

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

	FString MakeId(const FEditorPromptFollowBinding& Binding)
	{
		if (!Binding.Id.IsEmpty())
		{
			return SanitizeId(Binding.Id);
		}
		const FString Asset = FPaths::GetBaseFilename(Binding.Path);
		return SanitizeId(Binding.Actor + TEXT("_") + Asset);
	}

	FString StorePath()
	{
		return FPaths::ProjectSavedDir() / TEXT("EditorPrompt/follows.json");
	}

	void Save()
	{
		TArray<TSharedPtr<FJsonValue>> Rows;
		for (const FEditorPromptFollowBinding& Binding : Bindings)
		{
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("id"), Binding.Id);
			Row->SetStringField(TEXT("path"), Binding.Path);
			Row->SetStringField(TEXT("actor"), Binding.Actor);
			Row->SetStringField(TEXT("overlap"), Binding.Overlap);
			Row->SetNumberField(TEXT("x"), Binding.Offset.X);
			Row->SetNumberField(TEXT("y"), Binding.Offset.Y);
			Row->SetNumberField(TEXT("z"), Binding.Offset.Z);
			Row->SetNumberField(TEXT("pad"), Binding.OverlapPad);
			Row->SetBoolField(TEXT("kill"), Binding.bKillOnStop);
			Row->SetStringField(TEXT("component"), Binding.Component);
			Row->SetStringField(TEXT("socket"), Binding.Socket);
			Row->SetStringField(TEXT("overlap_component"), Binding.OverlapComponent);
			Row->SetStringField(TEXT("user_float"), Binding.UserFloat);
			Row->SetStringField(TEXT("user_position"), Binding.UserPosition);
			if (Binding.bLockSurfaceZ)
			{
				Row->SetNumberField(TEXT("surface_z"), Binding.SurfaceZ);
			}
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
			FEditorPromptFollowBinding Binding;
			Binding.Id = Row->GetStringField(TEXT("id"));
			Binding.Path = Row->GetStringField(TEXT("path"));
			Binding.Actor = Row->GetStringField(TEXT("actor"));
			Binding.Overlap = Row->GetStringField(TEXT("overlap"));
			Binding.Offset = FVector(
				Row->GetNumberField(TEXT("x")),
				Row->GetNumberField(TEXT("y")),
				Row->GetNumberField(TEXT("z")));
			double SurfaceZ = 0.0;
			Binding.bLockSurfaceZ = Row->TryGetNumberField(TEXT("surface_z"), SurfaceZ);
			Binding.SurfaceZ = static_cast<float>(SurfaceZ);
			double Pad = 0.0;
			Row->TryGetNumberField(TEXT("pad"), Pad);
			Binding.OverlapPad = static_cast<float>(Pad);
			bool bKill = true;
			Binding.bKillOnStop = !Row->TryGetBoolField(TEXT("kill"), bKill) || bKill;
			Row->TryGetStringField(TEXT("component"), Binding.Component);
			Row->TryGetStringField(TEXT("socket"), Binding.Socket);
			Row->TryGetStringField(TEXT("overlap_component"), Binding.OverlapComponent);
			Row->TryGetStringField(TEXT("user_float"), Binding.UserFloat);
			Row->TryGetStringField(TEXT("user_position"), Binding.UserPosition);
			if (!Binding.Id.IsEmpty() && !Binding.Path.IsEmpty() && !Binding.Actor.IsEmpty())
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

	bool TryNamedPoint(AActor* Actor, const FString& Socket, const FString& ComponentName, FVector& Out)
	{
		if (!Actor)
		{
			return false;
		}
		TInlineComponentArray<USceneComponent*> Components(Actor);
		if (!Socket.IsEmpty())
		{
			for (USceneComponent* Component : Components)
			{
				if (Component && Component->DoesSocketExist(*Socket))
				{
					Out = Component->GetSocketLocation(*Socket);
					return true;
				}
			}
			return false;
		}
		if (!ComponentName.IsEmpty())
		{
			for (USceneComponent* Component : Components)
			{
				if (Component && Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
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

	void WriteUser(UNiagaraComponent* Component, const FString& Name, float Value)
	{
		if (!Component || Name.IsEmpty())
		{
			return;
		}
		Component->SetVariableFloat(*Name, Value);
		if (!Name.Contains(TEXT(".")))
		{
			Component->SetVariableFloat(*(TEXT("User.") + Name), Value);
		}
	}

	void WriteUser(UNiagaraComponent* Component, const FString& Name, const FVector& Value)
	{
		if (!Component || Name.IsEmpty())
		{
			return;
		}
		Component->SetVariableVec3(*Name, Value);
		if (!Name.Contains(TEXT(".")))
		{
			Component->SetVariableVec3(*(TEXT("User.") + Name), Value);
		}
	}

	void StopFollow(UNiagaraComponent* Component, bool bKill)
	{
		if (!Component || !Component->IsActive())
		{
			return;
		}
		if (bKill)
		{
			Component->DeactivateImmediate();
		}
		else
		{
			Component->Deactivate();
		}
	}

	FName FollowTag(const FString& Id)
	{
		return FName(*(FString(FollowTagPrefix) + Id));
	}

	UNiagaraComponent* FindFollowComponent(AActor* Actor, const FString& Id)
	{
		if (!Actor)
		{
			return nullptr;
		}
		const FName Tag = FollowTag(Id);
		TInlineComponentArray<UNiagaraComponent*> Components(Actor);
		for (UNiagaraComponent* Component : Components)
		{
			if (Component && Component->ComponentTags.Contains(Tag))
			{
				return Component;
			}
		}
		return nullptr;
	}

	void DestroyFollowComponent(UWorld* World, const FString& Id)
	{
		if (!World)
		{
			return;
		}
		const FName Tag = FollowTag(Id);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<UNiagaraComponent*> Components(*It);
			for (UNiagaraComponent* Component : Components)
			{
				if (Component && Component->ComponentTags.Contains(Tag))
				{
					Component->DestroyComponent();
				}
			}
		}
	}

	void ApplyBinding(UWorld* World, const FEditorPromptFollowBinding& Binding, bool bPlayWorld)
	{
		AActor* Actor = ResolveActor(World, Binding.Actor, bPlayWorld);
		if (!Actor)
		{
			return;
		}
		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *Binding.Path);
		if (!System)
		{
			return;
		}
		UNiagaraComponent* Component = FindFollowComponent(Actor, Binding.Id);
		if (!Component)
		{
			Component = NewObject<UNiagaraComponent>(Actor);
			if (!Component)
			{
				return;
			}
			Component->ComponentTags.Add(FollowTag(Binding.Id));
			Component->bIsEditorOnly = !bPlayWorld;
			Component->SetAutoActivate(false);
			const bool bAnchor = Binding.bLockSurfaceZ || !Binding.Socket.IsEmpty() || !Binding.Component.IsEmpty();
			if (!bAnchor)
			{
				if (USceneComponent* Root = Actor->GetRootComponent())
				{
					Component->SetupAttachment(Root);
				}
			}
			Component->RegisterComponent();
		}
		if (Component->GetAsset() != System)
		{
			Component->SetAsset(System);
		}
		FVector Origin = FVector::ZeroVector;
		if (!TryNamedPoint(Actor, Binding.Socket, Binding.Component, Origin))
		{
			StopFollow(Component, Binding.bKillOnStop);
			return;
		}
		const bool bAnchor = Binding.bLockSurfaceZ || !Binding.Socket.IsEmpty() || !Binding.Component.IsEmpty();
		FVector WorldPoint = Origin + Binding.Offset;
		if (bAnchor)
		{
			if (Component->GetAttachParent())
			{
				Component->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
			}
			if (Binding.bLockSurfaceZ)
			{
				WorldPoint.Z = Binding.SurfaceZ + Binding.Offset.Z;
			}
			Component->SetWorldLocation(WorldPoint);
		}
		else
		{
			if (Component->GetAttachParent() == nullptr)
			{
				if (USceneComponent* Root = Actor->GetRootComponent())
				{
					Component->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
				}
			}
			if (Component->GetAttachParent() != nullptr)
			{
				Component->SetRelativeLocation(Binding.Offset);
				WorldPoint = Component->GetComponentLocation();
			}
			else
			{
				Component->SetWorldLocation(WorldPoint);
			}
		}
		const float UserZ = Binding.bLockSurfaceZ ? Binding.SurfaceZ : static_cast<float>(WorldPoint.Z);
		WriteUser(Component, Binding.UserFloat, UserZ);
		WriteUser(Component, Binding.UserPosition, WorldPoint);
		bool bPlay = true;
		if (!Binding.Overlap.IsEmpty())
		{
			bPlay = BoundsOverlap(
				Actor,
				Binding.Component,
				FindLabeled(World, Binding.Overlap),
				Binding.OverlapComponent,
				Binding.OverlapPad);
		}
		if (bPlay)
		{
			if (!Component->IsActive())
			{
				Component->Activate(true);
			}
		}
		else
		{
			StopFollow(Component, Binding.bKillOnStop);
		}
	}

	void ApplyWorld(UWorld* World, bool bPlayWorld)
	{
		if (!World)
		{
			return;
		}
		for (const FEditorPromptFollowBinding& Binding : Bindings)
		{
			ApplyBinding(World, Binding, bPlayWorld);
		}
	}

	void ApplyAll()
	{
		if (!GEditor)
		{
			return;
		}
		ApplyWorld(GEditor->GetEditorWorldContext().World(), false);
		ApplyWorld(GEditor->PlayWorld, true);
	}

	bool Tick(float)
	{
		ApplyAll();
		return true;
	}
}

void FEditorPromptFollow::Startup()
{
	Load();
	if (!TickHandle.IsValid())
	{
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), 0.f);
	}
	if (!PieStartHandle.IsValid())
	{
		PieStartHandle = FEditorDelegates::PostPIEStarted.AddLambda([](bool)
		{
			ApplyAll();
		});
	}
	if (!PieEndHandle.IsValid())
	{
		PieEndHandle = FEditorDelegates::EndPIE.AddLambda([](bool)
		{
			ApplyAll();
		});
	}
	ApplyAll();
}

void FEditorPromptFollow::Shutdown()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	if (PieStartHandle.IsValid())
	{
		FEditorDelegates::PostPIEStarted.Remove(PieStartHandle);
		PieStartHandle.Reset();
	}
	if (PieEndHandle.IsValid())
	{
		FEditorDelegates::EndPIE.Remove(PieEndHandle);
		PieEndHandle.Reset();
	}
}

FString FEditorPromptFollow::Add(const FEditorPromptFollowBinding& InBinding)
{
	FEditorPromptFollowBinding Binding = InBinding;
	Binding.Path.TrimStartAndEndInline();
	Binding.Actor.TrimStartAndEndInline();
	Binding.Overlap.TrimStartAndEndInline();
	if (Binding.Path.IsEmpty() || Binding.Actor.IsEmpty())
	{
		return TEXT("path and actor are required");
	}
	if (!LoadObject<UNiagaraSystem>(nullptr, *Binding.Path))
	{
		return TEXT("Niagara system not found");
	}
	Binding.Id = MakeId(Binding);
	if (Binding.Id.IsEmpty())
	{
		return TEXT("id is empty");
	}
	bool bReplaced = false;
	for (FEditorPromptFollowBinding& Existing : Bindings)
	{
		if (Existing.Id == Binding.Id
			|| (Existing.Actor.Equals(Binding.Actor, ESearchCase::IgnoreCase)
				&& Existing.Path == Binding.Path
				&& Existing.Overlap.Equals(Binding.Overlap, ESearchCase::IgnoreCase)))
		{
			Binding.Id = Existing.Id;
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
	ApplyAll();
	FString Message = FString::Printf(
		TEXT("follow id=%s actor=%s path=%s"),
		*Binding.Id,
		*Binding.Actor,
		*Binding.Path);
	if (Binding.bLockSurfaceZ)
	{
		Message += FString::Printf(TEXT(" surface_z=%.1f"), Binding.SurfaceZ);
		Message += TEXT("\nXY follows the actor. Z stays on that world height.");
	}
	if (!Binding.Overlap.IsEmpty())
	{
		Message += TEXT(" overlap=") + Binding.Overlap;
		Message += FString::Printf(
			TEXT("\nPlays only while XYZ bounds overlap (pad=%.0f)."),
			Binding.OverlapPad);
		if (Binding.bKillOnStop)
		{
			Message += TEXT(" Particles already spawned are cleared when overlap ends.");
		}
	}
	else
	{
		Message += TEXT("\nPlays the whole time the actor exists.");
	}
	if (!Binding.Component.IsEmpty())
	{
		Message += TEXT("\ncomponent=") + Binding.Component;
	}
	if (!Binding.Socket.IsEmpty())
	{
		Message += TEXT("\nsocket=") + Binding.Socket;
	}
	if (!Binding.OverlapComponent.IsEmpty())
	{
		Message += TEXT("\noverlap_component=") + Binding.OverlapComponent;
	}
	if (!Binding.UserFloat.IsEmpty() || !Binding.UserPosition.IsEmpty())
	{
		Message += TEXT("\nWrites user parameters each tick. Add them with add_niagara_user first.");
	}
	if (IsPlayerToken(Binding.Actor))
	{
		Message += TEXT("\nactor=player attaches to the possessed pawn while playing.");
	}
	return Message;
}

FString FEditorPromptFollow::Remove(const FString& ActorOrId)
{
	const FString Key = ActorOrId.TrimStartAndEnd();
	TArray<FString> Removed;
	if (Key.IsEmpty())
	{
		for (const FEditorPromptFollowBinding& Binding : Bindings)
		{
			Removed.Add(Binding.Id);
		}
		Bindings.Reset();
	}
	else
	{
		Bindings.RemoveAll([&](const FEditorPromptFollowBinding& Binding)
		{
			const bool bMatch = Binding.Id.Equals(Key, ESearchCase::IgnoreCase)
				|| Binding.Actor.Equals(Key, ESearchCase::IgnoreCase);
			if (bMatch)
			{
				Removed.Add(Binding.Id);
			}
			return bMatch;
		});
	}
	if (GEditor)
	{
		for (const FString& Id : Removed)
		{
			DestroyFollowComponent(GEditor->GetEditorWorldContext().World(), Id);
			DestroyFollowComponent(GEditor->PlayWorld, Id);
		}
	}
	Save();
	if (Removed.Num() == 0)
	{
		return TEXT("no follow binding matched");
	}
	return TEXT("cleared ") + FString::Join(Removed, TEXT(", "));
}

FString FEditorPromptFollow::List()
{
	if (Bindings.Num() == 0)
	{
		return TEXT("(none)");
	}
	FString Out;
	for (const FEditorPromptFollowBinding& Binding : Bindings)
	{
		Out += FString::Printf(
			TEXT("id=%s actor=%s path=%s overlap=%s offset=%s surface_z=%s pad=%.0f\n"),
			*Binding.Id,
			*Binding.Actor,
			*Binding.Path,
			*Binding.Overlap,
			*Binding.Offset.ToCompactString(),
			Binding.bLockSurfaceZ ? *FString::SanitizeFloat(Binding.SurfaceZ) : TEXT("off"),
			Binding.OverlapPad);
	}
	return Out.TrimEnd();
}
