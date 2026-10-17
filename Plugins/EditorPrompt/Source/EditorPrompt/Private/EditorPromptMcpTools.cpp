#include "EditorPromptMcpTools.h"
#include "EditorPromptDistort.h"
#include "EditorPromptFollow.h"
#include "EditorPromptGraphs.h"
#include "EditorPromptTrail.h"
#include "EditorPromptWorld.h"
#include "EditorPromptLog.h"
#include "EditorPromptMcpRegistry.h"
#include "EditorPromptMcpNiagaraGraph.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "AutomatedAssetImportData.h"
#include "Async/Async.h"
#include "Camera/CameraActor.h"
#include "CineCameraActor.h"
#include "Components/ActorComponent.h"
#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/MeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "UnrealClient.h"
#include "EditorActorFolders.h"
#include "EditorFramework/AssetImportData.h"
#include "EditorReimportHandler.h"
#include "EditorViewportClient.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PointLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/RectLight.h"
#include "Engine/SkyLight.h"
#include "Engine/SpotLight.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/EngineTypes.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "FileHelpers.h"
#include "Folder.h"
#include "GameFramework/Actor.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "IAssetViewport.h"
#include "ImageUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LevelEditor.h"
#include "LevelSequence.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Misc/DateTime.h"
#include "Misc/FrameNumber.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "MovieScene.h"
#include "NiagaraActor.h"
#include "NiagaraCommon.h"
#include "NiagaraComponent.h"
#include "NiagaraConstants.h"
#include "NiagaraModule.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceGrid2DCollection.h"
#include "NiagaraEditorModule.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraMeshRendererProperties.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraNode.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraDataInterfaceCurve.h"
#include "NiagaraDataInterfaceCurveBase.h"
#include "NiagaraDataInterfaceColorCurve.h"
#include "NiagaraDataInterfaceVector2DCurve.h"
#include "Curves/RichCurve.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraShared.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraGPUSortInfo.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraTypes.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ScopedTransaction.h"
#include "Sound/AmbientSound.h"
#include "Sound/SoundBase.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Templates/Function.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"

namespace
{
	FString ArgStr(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, const FString& Fallback = FString())
	{
		if (!Args.IsValid())
		{
			return Fallback;
		}
		FString V;
		return Args->TryGetStringField(Key, V) ? V : Fallback;
	}

	double ArgNum(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, double Fallback = 0)
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

	bool ArgBool(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, bool Fallback = false)
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

	int32 ArgInt(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, int32 Fallback = 0)
	{
		return static_cast<int32>(ArgNum(Args, Key, Fallback));
	}

	FLinearColor ArgColor(const TSharedPtr<FJsonObject>& Args)
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

	FVector ArgLoc(const TSharedPtr<FJsonObject>& Args)
	{
		return FVector(ArgNum(Args, TEXT("x")), ArgNum(Args, TEXT("y")), ArgNum(Args, TEXT("z")));
	}

	FRotator ArgRot(const TSharedPtr<FJsonObject>& Args)
	{
		return FRotator(ArgNum(Args, TEXT("pitch")), ArgNum(Args, TEXT("yaw")), ArgNum(Args, TEXT("roll")));
	}

	FVector ArgScale(const TSharedPtr<FJsonObject>& Args)
	{
		const double S = ArgNum(Args, TEXT("scale"), 1);
		const double SX = ArgNum(Args, TEXT("sx"), S);
		const double SY = ArgNum(Args, TEXT("sy"), S);
		const double SZ = ArgNum(Args, TEXT("sz"), S);
		return FVector(SX, SY, SZ);
	}

	void RunGame(TFunction<void()> Work)
	{
		if (IsInGameThread())
		{
			Work();
			return;
		}
		FEvent* Done = FPlatformProcess::GetSynchEventFromPool(true);
		AsyncTask(ENamedThreads::GameThread, [Work, Done]()
		{
			Work();
			Done->Trigger();
		});
		Done->Wait();
		FPlatformProcess::ReturnSynchEventToPool(Done);
	}

	UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	AActor* FindLabel(UWorld* World, const FString& Label)
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

	void TouchActor(AActor* Actor)
	{
		if (!Actor)
		{
			return;
		}
		Actor->SetFlags(RF_Transactional);
		Actor->Modify();
		Actor->MarkPackageDirty();
	}

	void Redraw()
	{
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports();
		}
	}

	FEditorViewportClient* ActiveViewport()
	{
		FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
		if (!LevelEditor)
		{
			return nullptr;
		}
		const TSharedPtr<IAssetViewport> Viewport = LevelEditor->GetFirstActiveViewport();
		return Viewport.IsValid() ? &Viewport->GetAssetViewportClient() : nullptr;
	}

	void EnableFxViewport()
	{
		if (FEditorViewportClient* Client = ActiveViewport())
		{
			Client->SetRealtime(true);
		}
		Redraw();
	}

	FVector CameraFrontLocation(float Distance)
	{
		if (FEditorViewportClient* Client = ActiveViewport())
		{
			return Client->GetViewLocation() + Client->GetViewRotation().Vector() * Distance;
		}
		return FVector::ZeroVector;
	}

	bool ArgsHaveLoc(const TSharedPtr<FJsonObject>& Args)
	{
		return Args.IsValid() && (Args->HasField(TEXT("x")) || Args->HasField(TEXT("y")) || Args->HasField(TEXT("z")));
	}

	bool ArgsHaveScale(const TSharedPtr<FJsonObject>& Args)
	{
		return Args.IsValid()
			&& (Args->HasField(TEXT("scale")) || Args->HasField(TEXT("sx"))
				|| Args->HasField(TEXT("sy")) || Args->HasField(TEXT("sz")));
	}

	bool IsUnderProject(const FString& File)
	{
		const FString Full = FPaths::ConvertRelativePathToFull(File);
		const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		return Full.StartsWith(Root);
	}

	TSharedRef<FJsonObject> Prop(const TCHAR* Type, const TCHAR* Desc)
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), Type);
		P->SetStringField(TEXT("description"), Desc);
		return P;
	}

	TSharedPtr<FJsonValue> MakeTool(const TCHAR* Name, const TCHAR* Desc, TSharedRef<FJsonObject> Properties)
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

	TSharedRef<FJsonObject> EmptyProps()
	{
		return MakeShared<FJsonObject>();
	}

	TSharedRef<FJsonObject> LabelProps()
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Actor label in the outliner")));
		return P;
	}

	TSharedRef<FJsonObject> XyzProps()
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("X cm")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Y cm")));
		P->SetObjectField(TEXT("z"), Prop(TEXT("number"), TEXT("Z cm")));
		return P;
	}

	FString ListActorLine(AActor* Actor)
	{
		const FVector L = Actor->GetActorLocation();
		return FString::Printf(TEXT("%s | %s | %.0f,%.0f,%.0f"),
			*Actor->GetActorLabel(),
			*Actor->GetClass()->GetName(),
			L.X, L.Y, L.Z);
	}

	FString ListAssetsOf(UClass* Class, const FString& PathPrefix, int32 MaxN)
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		FARFilter Filter;
		if (Class)
		{
			Filter.ClassPaths.Add(Class->GetClassPathName());
			Filter.bRecursiveClasses = true;
		}
		Filter.PackagePaths.Add(FName(*PathPrefix));
		Filter.bRecursivePaths = true;
		TArray<FAssetData> Assets;
		ARM.Get().GetAssets(Filter, Assets);
		MaxN = FMath::Clamp(MaxN, 1, 80);
		FString Out;
		int32 Count = 0;
		for (const FAssetData& A : Assets)
		{
			if (Count >= MaxN)
			{
				Out += TEXT("\n...");
				break;
			}
			Out += A.GetObjectPathString() + TEXT("\n");
			++Count;
		}
		return Out.IsEmpty() ? TEXT("(none)") : Out;
	}

	bool IsGamePath(const FString& Path)
	{
		return Path.StartsWith(TEXT("/Game")) || Path.StartsWith(TEXT("/Engine"));
	}

	FString SpawnMeshAt(const FVector& Loc, const FRotator& Rot, const FString& MeshPath, const FString& Label)
	{
		UWorld* World = EditorWorld();
		if (!World)
		{
			return TEXT("No editor world");
		}
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath.IsEmpty()
			? TEXT("/Engine/BasicShapes/Cube.Cube")
			: *MeshPath);
		if (!Mesh)
		{
			return TEXT("Failed to load static mesh");
		}
		World->Modify();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Loc, Rot, Params);
		if (!Actor)
		{
			return TEXT("SpawnActor failed");
		}
		TouchActor(Actor);
		if (!Label.IsEmpty())
		{
			Actor->SetActorLabel(Label);
		}
		Actor->SetMobility(EComponentMobility::Movable);
		if (UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent())
		{
			Comp->SetFlags(RF_Transactional);
			Comp->Modify();
			Comp->SetStaticMesh(Mesh);
			Comp->SetMobility(EComponentMobility::Movable);
		}
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(Actor, true, true, true);
		Redraw();
		return FString::Printf(TEXT("Spawned '%s'"), *Actor->GetActorLabel());
	}

	template <typename TLight>
	FString SpawnTypedLight(const TSharedPtr<FJsonObject>& Args, const FString& DefaultLabel)
	{
		UWorld* World = EditorWorld();
		if (!World)
		{
			return TEXT("No editor world");
		}
		World->Modify();
		TLight* Light = World->SpawnActor<TLight>(ArgLoc(Args), ArgRot(Args));
		if (!Light)
		{
			return TEXT("Spawn light failed");
		}
		TouchActor(Light);
		const FString Label = ArgStr(Args, TEXT("label"), DefaultLabel);
		if (!Label.IsEmpty())
		{
			Light->SetActorLabel(Label);
		}
		if (ULightComponent* Comp = Light->GetLightComponent())
		{
			Comp->Modify();
			Comp->SetLightColor(ArgColor(Args));
			const double Intensity = ArgNum(Args, TEXT("intensity"), -1);
			if (Intensity >= 0)
			{
				Comp->SetIntensity(static_cast<float>(Intensity));
			}
		}
		Redraw();
		return FString::Printf(TEXT("Spawned light '%s'"), *Light->GetActorLabel());
	}

	ULightComponent* LightComp(AActor* Actor)
	{
		return Actor ? Actor->FindComponentByClass<ULightComponent>() : nullptr;
	}

	bool SplitGameAssetPath(const FString& Path, FString& OutPackagePath, FString& OutAssetName, FString& OutError)
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

	ENiagaraScriptUsage UsageFromStack(const FString& Stack)
	{
		const FString S = Stack.Replace(TEXT(" "), TEXT("_")).ToLower();
		if (S.Contains(TEXT("emitter_spawn")))
		{
			return ENiagaraScriptUsage::EmitterSpawnScript;
		}
		if (S.Contains(TEXT("emitter_update")))
		{
			return ENiagaraScriptUsage::EmitterUpdateScript;
		}
		if (S.Contains(TEXT("particle_spawn")))
		{
			return ENiagaraScriptUsage::ParticleSpawnScript;
		}
		if (S.Contains(TEXT("simulation")))
		{
			return ENiagaraScriptUsage::ParticleSimulationStageScript;
		}
		if (S.Contains(TEXT("system_spawn")))
		{
			return ENiagaraScriptUsage::SystemSpawnScript;
		}
		if (S.Contains(TEXT("system_update")))
		{
			return ENiagaraScriptUsage::SystemUpdateScript;
		}
		return ENiagaraScriptUsage::ParticleUpdateScript;
	}

	const TCHAR* UsageLabel(ENiagaraScriptUsage Usage)
	{
		switch (Usage)
		{
		case ENiagaraScriptUsage::EmitterSpawnScript: return TEXT("emitter_spawn");
		case ENiagaraScriptUsage::EmitterUpdateScript: return TEXT("emitter_update");
		case ENiagaraScriptUsage::ParticleSpawnScript: return TEXT("particle_spawn");
		case ENiagaraScriptUsage::ParticleUpdateScript: return TEXT("particle_update");
		case ENiagaraScriptUsage::ParticleSimulationStageScript: return TEXT("simulation_stage");
		case ENiagaraScriptUsage::SystemSpawnScript: return TEXT("system_spawn");
		case ENiagaraScriptUsage::SystemUpdateScript: return TEXT("system_update");
		default: return TEXT("other");
		}
	}

	UNiagaraGraph* EmitterGraph(FVersionedNiagaraEmitterData* Data)
	{
		if (!Data)
		{
			return nullptr;
		}
		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Data->GraphSource);
		return Source ? Source->NodeGraph.Get() : nullptr;
	}

	UNiagaraNodeOutput* FindOutputNode(UNiagaraGraph* Graph, ENiagaraScriptUsage Usage, const FGuid& UsageId = FGuid())
	{
		if (!Graph)
		{
			return nullptr;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UNiagaraNodeOutput* Out = Cast<UNiagaraNodeOutput>(Node))
			{
				if (Out->GetUsage() == Usage && (!UsageId.IsValid() || Out->GetUsageId() == UsageId))
				{
					return Out;
				}
			}
		}
		return nullptr;
	}

	UNiagaraDataInterface* GetInputNodeDataInterface(UNiagaraNodeInput* Node)
	{
		if (!Node)
		{
			return nullptr;
		}
		const FObjectProperty* Prop = FindFProperty<FObjectProperty>(UNiagaraNodeInput::StaticClass(), TEXT("DataInterface"));
		return Prop ? Cast<UNiagaraDataInterface>(Prop->GetObjectPropertyValue_InContainer(Node)) : nullptr;
	}

	void SetInputNodeDataInterface(UNiagaraNodeInput* Node, UNiagaraDataInterface* DataInterface)
	{
		if (!Node)
		{
			return;
		}
		if (FObjectProperty* Prop = FindFProperty<FObjectProperty>(UNiagaraNodeInput::StaticClass(), TEXT("DataInterface")))
		{
			Prop->SetObjectPropertyValue_InContainer(Node, DataInterface);
		}
	}

	UEdGraphPin* FirstPin(UNiagaraNode* Node, EEdGraphPinDirection Dir)
	{
		if (!Node)
		{
			return nullptr;
		}
		TArray<UEdGraphPin*> Pins;
		if (Dir == EGPD_Input)
		{
			Node->GetInputPins(Pins);
		}
		else
		{
			Node->GetOutputPins(Pins);
		}
		return Pins.Num() > 0 ? Pins[0] : nullptr;
	}

	UNiagaraNodeOutput* EnsureStackMap(UNiagaraGraph* Graph, ENiagaraScriptUsage Usage, const FGuid& UsageId)
	{
		if (!Graph)
		{
			return nullptr;
		}
		if (UNiagaraNodeOutput* Existing = FindOutputNode(Graph, Usage, UsageId))
		{
			return Existing;
		}
		Graph->Modify();
		FGraphNodeCreator<UNiagaraNodeOutput> OutputCreator(*Graph);
		UNiagaraNodeOutput* OutputNode = OutputCreator.CreateNode(false);
		OutputNode->SetUsage(Usage);
		OutputNode->SetUsageId(UsageId);
		OutputNode->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Out")));
		OutputCreator.Finalize();

		FGraphNodeCreator<UNiagaraNodeInput> InputCreator(*Graph);
		UNiagaraNodeInput* InputNode = InputCreator.CreateNode(false);
		InputNode->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("InputMap"));
		InputNode->Usage = ENiagaraInputNodeUsage::Parameter;
		InputCreator.Finalize();

		if (UEdGraphPin* OutPin = FirstPin(OutputNode, EGPD_Input))
		{
			if (UEdGraphPin* InPin = FirstPin(InputNode, EGPD_Output))
			{
				OutPin->MakeLinkTo(InPin);
			}
		}
		return OutputNode;
	}

	UNiagaraSimulationStageBase* FindSimStage(FVersionedNiagaraEmitterData* Data, const FString& StageName)
	{
		if (!Data)
		{
			return nullptr;
		}
		const TArray<UNiagaraSimulationStageBase*>& Stages = Data->GetSimulationStages();
		if (StageName.IsEmpty())
		{
			return Stages.Num() > 0 ? Stages[0] : nullptr;
		}
		for (UNiagaraSimulationStageBase* Stage : Stages)
		{
			if (Stage && Stage->SimulationStageName.ToString().Equals(StageName, ESearchCase::IgnoreCase))
			{
				return Stage;
			}
		}
		return nullptr;
	}

	UNiagaraNodeFunctionCall* TryAddLibraryModule(UNiagaraNodeOutput* Output, const TCHAR* PathOrName)
	{
		if (!Output || !PathOrName)
		{
			return nullptr;
		}
		UNiagaraScript* Module = LoadObject<UNiagaraScript>(nullptr, PathOrName);
		if (!Module)
		{
			FNiagaraEditorUtilities::FGetFilteredScriptAssetsOptions Filter;
			Filter.ScriptUsageToInclude = ENiagaraScriptUsage::Module;
			Filter.TargetUsageToMatch = Output->GetUsage();
			TArray<FAssetData> Assets;
			FNiagaraEditorUtilities::GetFilteredScriptAssets(Filter, Assets);
			const FString Want(PathOrName);
			for (const FAssetData& A : Assets)
			{
				if (A.AssetName.ToString().Equals(Want, ESearchCase::IgnoreCase)
					|| A.GetObjectPathString().Contains(Want))
				{
					Module = Cast<UNiagaraScript>(A.GetAsset());
					if (Module)
					{
						break;
					}
				}
			}
		}
		return Module ? FNiagaraStackGraphUtilities::AddScriptModuleToStack(Module, *Output) : nullptr;
	}

	FNiagaraVariable MakeGrid2DVariable()
	{
		const FNiagaraParameterHandle Handle(FNiagaraConstants::EmitterNamespace, FName(TEXT("Grid")));
		return FNiagaraVariable(
			FNiagaraTypeDefinition(UNiagaraDataInterfaceGrid2DCollection::StaticClass()),
			Handle.GetParameterHandleString());
	}

	UNiagaraNodeInput* FindOrAddGrid2DInput(UNiagaraGraph* Graph, const FNiagaraVariable& GridVar)
	{
		if (!Graph)
		{
			return nullptr;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UNiagaraNodeInput* Input = Cast<UNiagaraNodeInput>(Node);
			if (Input && Input->Usage == ENiagaraInputNodeUsage::Parameter && Input->Input == GridVar)
			{
				return Input;
			}
		}
		Graph->Modify();
		FGraphNodeCreator<UNiagaraNodeInput> Creator(*Graph);
		UNiagaraNodeInput* InputNode = Creator.CreateNode(false);
		InputNode->Input = GridVar;
		InputNode->Usage = ENiagaraInputNodeUsage::Parameter;
		InputNode->ExposureOptions.bRequired = false;
		InputNode->Input.AllocateData();
		Creator.Finalize();
		UNiagaraDataInterfaceGrid2DCollection* Grid = NewObject<UNiagaraDataInterfaceGrid2DCollection>(
			InputNode, TEXT("Grid"), RF_Transactional | RF_Public);
		SetInputNodeDataInterface(InputNode, Grid);
		return InputNode;
	}

	UNiagaraSimulationStageGeneric* AddGridSimStage(
		UNiagaraEmitter* Emitter,
		const FGuid& Version,
		UNiagaraGraph* Graph,
		UNiagaraScriptSource* Source,
		const FNiagaraVariable& GridVar,
		FName StageName)
	{
		if (!Emitter || !Graph || !Source)
		{
			return nullptr;
		}
		UNiagaraSimulationStageGeneric* Stage = NewObject<UNiagaraSimulationStageGeneric>(Emitter, StageName, RF_Transactional);
		Stage->SimulationStageName = StageName;
		Stage->IterationSource = ENiagaraIterationSource::DataInterface;
		Stage->DataInterface = FNiagaraVariableDataInterfaceBinding(GridVar);
		Stage->Script = NewObject<UNiagaraScript>(Stage, TEXT("SimulationStage"), RF_Transactional);
		Stage->Script->SetUsage(ENiagaraScriptUsage::ParticleSimulationStageScript);
		Stage->Script->SetUsageId(Stage->GetMergeId());
		Stage->Script->SetLatestSource(Source);
		Emitter->AddSimulationStage(Stage, Version);
		EnsureStackMap(Graph, ENiagaraScriptUsage::ParticleSimulationStageScript, Stage->Script->GetUsageId());
		return Stage;
	}

	FString ConfigureGrid2DEmitter(UNiagaraEmitter* Emitter, const FGuid& Version, int32 Cells, float WorldSize, int32 Attributes)
	{
		if (!Emitter)
		{
			return TEXT("No emitter");
		}
		FVersionedNiagaraEmitterData* Data = Emitter->GetEmitterData(Version);
		if (!Data)
		{
			Data = Emitter->GetLatestEmitterData();
		}
		UNiagaraGraph* Graph = EmitterGraph(Data);
		UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
		if (!Data || !Graph || !Source)
		{
			return TEXT("Emitter graph missing");
		}
		Cells = FMath::Clamp(Cells, 8, 512);
		WorldSize = FMath::Clamp(WorldSize, 10.f, 10000.f);
		Attributes = FMath::Clamp(Attributes, 1, 16);

		Data->SimTarget = ENiagaraSimTarget::GPUComputeSim;
		Data->bLocalSpace = true;
		Data->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
		const float Half = WorldSize * 0.5f;
		Data->FixedBounds = FBox(FVector(-Half, -Half, -Half), FVector(Half, Half, Half));

		const FNiagaraVariable GridVar = MakeGrid2DVariable();
		UNiagaraNodeInput* GridInput = FindOrAddGrid2DInput(Graph, GridVar);
		UNiagaraDataInterfaceGrid2DCollection* Grid = Cast<UNiagaraDataInterfaceGrid2DCollection>(GetInputNodeDataInterface(GridInput));
		if (!Grid)
		{
			return TEXT("Grid2D Collection create failed");
		}
		Grid->NumCellsX = Cells;
		Grid->NumCellsY = Cells;
		Grid->NumCellsMaxAxis = Cells;
		Grid->SetGridFromMaxAxis = false;
		Grid->NumAttributes = Attributes;
		Grid->WorldBBoxSize = FVector2D(WorldSize, WorldSize);
		Grid->bPreviewGrid = true;
		Grid->PreviewAttribute = FName(TEXT("Attribute At Index 0"));

		if (Data->GetSimulationStages().Num() == 0)
		{
			AddGridSimStage(Emitter, Version, Graph, Source, GridVar, FName(TEXT("GridIterate")));
		}
		if (UNiagaraSimulationStageBase* Stage = FindSimStage(Data, TEXT("GridIterate")))
		{
			UNiagaraNodeOutput* StageOut = Stage->Script
				? FindOutputNode(Graph, ENiagaraScriptUsage::ParticleSimulationStageScript, Stage->Script->GetUsageId())
				: nullptr;
			EditorPromptEnsureDiOnSimStage(Graph, StageOut, GridVar);
			TryAddLibraryModule(StageOut, TEXT("SetFloat"));
		}
		if (UNiagaraNodeOutput* EmitterUpdate = FindOutputNode(Graph, ENiagaraScriptUsage::EmitterUpdateScript))
		{
			TryAddLibraryModule(EmitterUpdate, TEXT("/Niagara/Modules/Emitter/EmitterState.EmitterState"));
		}
		Graph->NotifyGraphChanged();
		return FString::Printf(
			TEXT("grid2d cells=%d world=%.0f attributes=%d di=%s stages=%d"),
			Cells, WorldSize, Attributes, *GridVar.GetName().ToString(), Data->GetSimulationStages().Num());
	}

	FNiagaraEmitterHandle* FindEmitterHandle(UNiagaraSystem* Sys, const FString& EmitterName)
	{
		if (!Sys)
		{
			return nullptr;
		}
		TArray<FNiagaraEmitterHandle>& Handles = Sys->GetEmitterHandles();
		if (Handles.Num() == 0)
		{
			return nullptr;
		}
		if (EmitterName.IsEmpty())
		{
			return &Handles[0];
		}
		for (FNiagaraEmitterHandle& Handle : Handles)
		{
			if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase)
				|| Handle.GetUniqueInstanceName().Equals(EmitterName, ESearchCase::IgnoreCase))
			{
				return &Handle;
			}
		}
		return nullptr;
	}

	UMaterialInterface* NiagaraSpriteMaterialForCutout()
	{
		static const TCHAR* Paths[] = {
			TEXT("/Niagara/DefaultMaterial/M_NiagaraDefaultSpriteMaterial.M_NiagaraDefaultSpriteMaterial"),
			TEXT("/Niagara/DefaultMaterial/M_NiagaraDefaultSpriteMaterialOpaque.M_NiagaraDefaultSpriteMaterialOpaque"),
			TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"),
		};
		for (const TCHAR* Path : Paths)
		{
			if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, Path))
			{
				return Mat;
			}
		}
		return nullptr;
	}

	const TCHAR* EditorPromptSpriteMatPath()
	{
		return TEXT("/Game/FX/M_EditorPromptSprite");
	}

	UMaterial* EnsureEditorPromptSpriteMaterial(FString& OutError)
	{
		const TCHAR* Path = EditorPromptSpriteMatPath();
		if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, Path))
		{
			return Existing;
		}
		FString PackagePath;
		FString AssetName;
		if (!SplitGameAssetPath(Path, PackagePath, AssetName, OutError))
		{
			return nullptr;
		}
		UPackage* Pkg = CreatePackage(*(PackagePath / AssetName));
		UMaterial* Mat = NewObject<UMaterial>(Pkg, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!Mat)
		{
			OutError = TEXT("Create sprite material failed");
			return nullptr;
		}
		Mat->SetShadingModel(MSM_Unlit);
		Mat->BlendMode = BLEND_Translucent;
		Mat->TwoSided = true;
		Mat->bUsedWithNiagaraSprites = true;
		bool bNeedsRecompile = false;
		UMaterialEditingLibrary::SetMaterialUsage(Mat, MATUSAGE_NiagaraSprites, bNeedsRecompile);
		UMaterialEditingLibrary::SetMaterialUsage(Mat, MATUSAGE_ParticleSprites, bNeedsRecompile);

		auto Expr = [Mat](UClass* Class, int32 X, int32 Y) -> UMaterialExpression*
		{
			return UMaterialEditingLibrary::CreateMaterialExpression(Mat, Class, X, Y);
		};
		UMaterialExpressionTextureSampleParameter2D* Tex = Cast<UMaterialExpressionTextureSampleParameter2D>(
			Expr(UMaterialExpressionTextureSampleParameter2D::StaticClass(), -560, 0));
		UClass* ParticleColorClass = LoadClass<UMaterialExpression>(
			nullptr, TEXT("/Script/Engine.MaterialExpressionParticleColor"));
		UMaterialExpression* PCol = ParticleColorClass
			? Expr(ParticleColorClass, -560, 280)
			: nullptr;
		UMaterialExpressionScalarParameter* MaskFromRed = Cast<UMaterialExpressionScalarParameter>(
			Expr(UMaterialExpressionScalarParameter::StaticClass(), -560, 480));
		UMaterialExpressionConstant3Vector* White = Cast<UMaterialExpressionConstant3Vector>(
			Expr(UMaterialExpressionConstant3Vector::StaticClass(), -280, 160));
		UMaterialExpressionLinearInterpolate* LerpRgb = Cast<UMaterialExpressionLinearInterpolate>(
			Expr(UMaterialExpressionLinearInterpolate::StaticClass(), -280, 0));
		UMaterialExpressionLinearInterpolate* LerpA = Cast<UMaterialExpressionLinearInterpolate>(
			Expr(UMaterialExpressionLinearInterpolate::StaticClass(), -280, 320));
		UMaterialExpressionMultiply* MulRgb = Cast<UMaterialExpressionMultiply>(
			Expr(UMaterialExpressionMultiply::StaticClass(), -40, 0));
		UMaterialExpressionMultiply* MulA = Cast<UMaterialExpressionMultiply>(
			Expr(UMaterialExpressionMultiply::StaticClass(), -40, 320));
		if (!Tex || !PCol || !MaskFromRed || !White || !LerpRgb || !LerpA || !MulRgb || !MulA)
		{
			OutError = TEXT("Sprite material expressions failed");
			return nullptr;
		}
		Tex->ParameterName = TEXT("SpriteTexture");
		MaskFromRed->ParameterName = TEXT("MaskFromRed");
		MaskFromRed->DefaultValue = 0.f;
		White->Constant = FLinearColor::White;
		UMaterialEditingLibrary::ConnectMaterialExpressions(Tex, TEXT(""), LerpRgb, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(White, TEXT(""), LerpRgb, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(MaskFromRed, TEXT(""), LerpRgb, TEXT("Alpha"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(LerpRgb, TEXT(""), MulRgb, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(PCol, TEXT(""), MulRgb, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Tex, TEXT("A"), LerpA, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Tex, TEXT("R"), LerpA, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(MaskFromRed, TEXT(""), LerpA, TEXT("Alpha"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(LerpA, TEXT(""), MulA, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(PCol, TEXT("A"), MulA, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialProperty(MulRgb, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(MulA, TEXT(""), MP_Opacity);
		UMaterialEditingLibrary::RecompileMaterial(Mat);
		FAssetRegistryModule::AssetCreated(Mat);
		Pkg->MarkPackageDirty();
		return Mat;
	}

	FString SpriteMicPathFor(UNiagaraSystem* Sys)
	{
		FString Safe = Sys ? Sys->GetName() : TEXT("NS");
		Safe = Safe.Replace(TEXT("."), TEXT("_"));
		return FString::Printf(TEXT("/Game/FX/Materials/MI_%s_Sprite"), *Safe);
	}

	UMaterialInstanceConstant* EnsureSystemSpriteMic(UNiagaraSystem* Sys, UTexture2D* Tex, FString& OutError)
	{
		UMaterial* Parent = EnsureEditorPromptSpriteMaterial(OutError);
		if (!Parent || !Sys || !Tex)
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("Sprite MIC inputs missing");
			}
			return nullptr;
		}
		const FString MicPath = SpriteMicPathFor(Sys);
		UMaterialInstanceConstant* MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *MicPath);
		if (!MIC)
		{
			FString PackagePath;
			FString AssetName;
			if (!SplitGameAssetPath(MicPath, PackagePath, AssetName, OutError))
			{
				return nullptr;
			}
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
			UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
			Factory->InitialParent = Parent;
			UObject* Created = AssetTools.CreateAsset(AssetName, PackagePath, UMaterialInstanceConstant::StaticClass(), Factory);
			MIC = Cast<UMaterialInstanceConstant>(Created);
		}
		if (!MIC)
		{
			OutError = TEXT("Create sprite MIC failed");
			return nullptr;
		}
		MIC->Modify();
		if (MIC->Parent != Parent)
		{
			MIC->SetParentEditorOnly(Parent);
		}
		MIC->SetTextureParameterValueEditorOnly(FName(TEXT("SpriteTexture")), Tex);
		const float MaskFromRed = (Tex->CompressionSettings == TC_Grayscale) ? 1.f : 0.f;
		MIC->SetScalarParameterValueEditorOnly(FName(TEXT("MaskFromRed")), MaskFromRed);
		MIC->PostEditChange();
		MIC->MarkPackageDirty();
		return MIC;
	}

	bool ApplySpriteImageTexture(UNiagaraSpriteRendererProperties* Sprite, UNiagaraSystem* Sys, UTexture2D* Tex, FString& OutError)
	{
		UMaterialInstanceConstant* MIC = EnsureSystemSpriteMic(Sys, Tex, OutError);
		if (!MIC || !Sprite)
		{
			return false;
		}
		Sprite->Material = MIC;
		Sprite->bUseMaterialCutoutTexture = false;
		Sprite->CutoutTexture = nullptr;
		return true;
	}

	void ApplyImportedTextureKind(UTexture2D* Tex, const FString& Kind)
	{
		if (!Tex)
		{
			return;
		}
		Tex->Modify();
		if (Kind.Contains(TEXT("mask")))
		{
			Tex->SRGB = false;
			Tex->CompressionSettings = TC_Grayscale;
		}
		else if (Kind.Contains(TEXT("noise")) || Kind.Contains(TEXT("flow")) || Kind.Contains(TEXT("vector")))
		{
			Tex->SRGB = false;
			Tex->CompressionSettings = TC_Default;
			Tex->LODGroup = TEXTUREGROUP_Effects;
			Tex->MipGenSettings = TMGS_NoMipmaps;
			Tex->Filter = TF_Bilinear;
			Tex->AddressX = TA_Wrap;
			Tex->AddressY = TA_Wrap;
		}
		else if (Kind.Contains(TEXT("atlas")) || Kind.Contains(TEXT("flipbook")) || Kind.Contains(TEXT("sprite")) || Kind.Contains(TEXT("albedo")))
		{
			Tex->SRGB = true;
			Tex->CompressionSettings = TC_Default;
			Tex->LODGroup = TEXTUREGROUP_Effects;
			Tex->MipGenSettings = TMGS_NoMipmaps;
			Tex->Filter = TF_Bilinear;
			Tex->AddressX = TA_Clamp;
			Tex->AddressY = TA_Clamp;
		}
		Tex->PostEditChange();
		Tex->UpdateResource();
		Tex->MarkPackageDirty();
	}

	UTexture2D* FindTextureNamedUnder(const FString& Dest, const FString& AssetName)
	{
		if (Dest.IsEmpty() || AssetName.IsEmpty())
		{
			return nullptr;
		}
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		ARM.Get().GetAssetsByPath(FName(*Dest), Assets, true);
		const FName Want(*AssetName);
		for (const FAssetData& A : Assets)
		{
			if (A.AssetName == Want)
			{
				return Cast<UTexture2D>(A.GetAsset());
			}
		}
		return nullptr;
	}

	bool RenameObjectAvoidingCollision(UObject* Obj, const FString& NewName, FString& OutError)
	{
		if (!Obj || NewName.IsEmpty() || Obj->GetName() == NewName)
		{
			return true;
		}
		if (UObject* Hit = StaticFindObject(UObject::StaticClass(), Obj->GetOuter(), *NewName, true))
		{
			if (Hit != Obj)
			{
				const FName Trash = MakeUniqueObjectName(GetTransientPackage(), Hit->GetClass(), TEXT("EP_Discard"));
				if (!Hit->Rename(*Trash.ToString(), GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty))
				{
					OutError = TEXT("Could not vacate existing name");
					return false;
				}
				Hit->ClearFlags(RF_Public | RF_Standalone);
				Hit->MarkAsGarbage();
			}
		}
		const ERenameFlags Flags = REN_DontCreateRedirectors;
		if (!Obj->Rename(*NewName, nullptr, Flags | REN_Test))
		{
			OutError = TEXT("Rename not allowed");
			return false;
		}
		if (!Obj->Rename(*NewName, nullptr, Flags))
		{
			OutError = TEXT("Rename failed");
			return false;
		}
		return true;
	}

	UMaterialExpression* ExprNamed(UMaterial* Mat, const TCHAR* ClassName, int32 X, int32 Y)
	{
		const FString Path = FString::Printf(TEXT("/Script/Engine.%s"), ClassName);
		UClass* Cls = LoadClass<UMaterialExpression>(nullptr, *Path);
		return Cls ? UMaterialEditingLibrary::CreateMaterialExpression(Mat, Cls, X, Y) : nullptr;
	}

	void SetExprBool(UObject* Obj, const TCHAR* PropName, bool bValue)
	{
		if (!Obj)
		{
			return;
		}
		if (FBoolProperty* Prop = FindFProperty<FBoolProperty>(Obj->GetClass(), PropName))
		{
			Prop->SetPropertyValue_InContainer(Obj, bValue);
		}
	}

	void SetExprFloat(UObject* Obj, const TCHAR* PropName, float Value)
	{
		if (!Obj)
		{
			return;
		}
		if (FFloatProperty* Prop = FindFProperty<FFloatProperty>(Obj->GetClass(), PropName))
		{
			Prop->SetPropertyValue_InContainer(Obj, Value);
		}
	}

	void SetExprInt(UObject* Obj, const TCHAR* PropName, int32 Value)
	{
		if (!Obj)
		{
			return;
		}
		if (FIntProperty* Prop = FindFProperty<FIntProperty>(Obj->GetClass(), PropName))
		{
			Prop->SetPropertyValue_InContainer(Obj, Value);
			return;
		}
		if (FByteProperty* Prop = FindFProperty<FByteProperty>(Obj->GetClass(), PropName))
		{
			Prop->SetPropertyValue_InContainer(Obj, static_cast<uint8>(Value));
			return;
		}
		if (FEnumProperty* Prop = FindFProperty<FEnumProperty>(Obj->GetClass(), PropName))
		{
			Prop->GetUnderlyingProperty()->SetIntPropertyValue(
				Prop->ContainerPtrToValuePtr<void>(Obj), static_cast<int64>(Value));
		}
	}

	FRotator BillboardToViewport(const FVector& Loc)
	{
		FVector CamLoc = Loc + FVector(200.f, 0.f, 80.f);
		if (FEditorViewportClient* Client = ActiveViewport())
		{
			CamLoc = Client->GetViewLocation();
		}
		const FVector ToCam = CamLoc - Loc;
		if (ToCam.IsNearlyZero())
		{
			return FRotator::ZeroRotator;
		}
		return FRotationMatrix::MakeFromZ(ToCam).Rotator();
	}

	const TCHAR* EditorPromptFlipbookMatPath()
	{
		return TEXT("/Game/FX/M_EditorPromptFlipbook");
	}

	UMaterial* EnsureEditorPromptFlipbookMaterial(FString& OutError)
	{
		const TCHAR* Path = EditorPromptFlipbookMatPath();
		if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, Path))
		{
			return Existing;
		}
		FString PackagePath;
		FString AssetName;
		if (!SplitGameAssetPath(Path, PackagePath, AssetName, OutError))
		{
			return nullptr;
		}
		UPackage* Pkg = CreatePackage(*(PackagePath / AssetName));
		UMaterial* Mat = NewObject<UMaterial>(Pkg, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!Mat)
		{
			OutError = TEXT("Create flipbook material failed");
			return nullptr;
		}
		Mat->SetShadingModel(MSM_Unlit);
		Mat->BlendMode = BLEND_Translucent;
		Mat->TwoSided = true;
		Mat->bUsedWithNiagaraSprites = true;
		bool bNeedsRecompile = false;
		UMaterialEditingLibrary::SetMaterialUsage(Mat, MATUSAGE_SkeletalMesh, bNeedsRecompile);
		UMaterialEditingLibrary::SetMaterialUsage(Mat, MATUSAGE_NiagaraSprites, bNeedsRecompile);
		UMaterialEditingLibrary::SetMaterialUsage(Mat, MATUSAGE_ParticleSprites, bNeedsRecompile);

		auto Bin = [Mat](UMaterialExpression* A, const TCHAR* APin, UMaterialExpression* B, const TCHAR* BPin,
			const TCHAR* ClassName, int32 X, int32 Y) -> UMaterialExpression*
		{
			UMaterialExpression* Node = ExprNamed(Mat, ClassName, X, Y);
			if (!Node || !A || !B)
			{
				return nullptr;
			}
			UMaterialEditingLibrary::ConnectMaterialExpressions(A, APin, Node, TEXT("A"));
			UMaterialEditingLibrary::ConnectMaterialExpressions(B, BPin, Node, TEXT("B"));
			return Node;
		};

		UMaterialExpression* Uv = ExprNamed(Mat, TEXT("MaterialExpressionTextureCoordinate"), -1400, 0);
		UMaterialExpression* MaskU = ExprNamed(Mat, TEXT("MaterialExpressionComponentMask"), -1220, -80);
		UMaterialExpression* MaskV = ExprNamed(Mat, TEXT("MaterialExpressionComponentMask"), -1220, 40);
		UMaterialExpressionScalarParameter* Fps = Cast<UMaterialExpressionScalarParameter>(
			ExprNamed(Mat, TEXT("MaterialExpressionScalarParameter"), -1400, 220));
		UMaterialExpression* Time = ExprNamed(Mat, TEXT("MaterialExpressionTime"), -1400, 300);
		UMaterialExpressionScalarParameter* Cols = Cast<UMaterialExpressionScalarParameter>(
			ExprNamed(Mat, TEXT("MaterialExpressionScalarParameter"), -1040, 360));
		UMaterialExpressionScalarParameter* Rows = Cast<UMaterialExpressionScalarParameter>(
			ExprNamed(Mat, TEXT("MaterialExpressionScalarParameter"), -1040, 430));
		UMaterialExpressionScalarParameter* Strength = Cast<UMaterialExpressionScalarParameter>(
			ExprNamed(Mat, TEXT("MaterialExpressionScalarParameter"), 40, 240));
		UMaterialExpressionScalarParameter* OpacityAmp = Cast<UMaterialExpressionScalarParameter>(
			ExprNamed(Mat, TEXT("MaterialExpressionScalarParameter"), 280, 200));
		UMaterialExpressionTextureSampleParameter2D* Sample = Cast<UMaterialExpressionTextureSampleParameter2D>(
			ExprNamed(Mat, TEXT("MaterialExpressionTextureSampleParameter2D"), 40, 0));
		UMaterialExpression* One = ExprNamed(Mat, TEXT("MaterialExpressionConstant"), -500, 200);
		if (!Uv || !MaskU || !MaskV || !Fps || !Time || !Cols || !Rows || !Strength || !OpacityAmp || !Sample || !One)
		{
			OutError = TEXT("Flipbook material expressions failed");
			return nullptr;
		}
		SetExprBool(MaskU, TEXT("R"), true);
		SetExprBool(MaskU, TEXT("G"), false);
		SetExprBool(MaskU, TEXT("B"), false);
		SetExprBool(MaskU, TEXT("A"), false);
		SetExprBool(MaskV, TEXT("R"), false);
		SetExprBool(MaskV, TEXT("G"), true);
		SetExprBool(MaskV, TEXT("B"), false);
		SetExprBool(MaskV, TEXT("A"), false);
		UMaterialEditingLibrary::ConnectMaterialExpressions(Uv, TEXT(""), MaskU, TEXT(""));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Uv, TEXT(""), MaskV, TEXT(""));
		Fps->ParameterName = TEXT("FPS");
		Fps->DefaultValue = 10.f;
		Cols->ParameterName = TEXT("Columns");
		Cols->DefaultValue = 4.f;
		Rows->ParameterName = TEXT("Rows");
		Rows->DefaultValue = 4.f;
		Strength->ParameterName = TEXT("EmissiveStrength");
		Strength->DefaultValue = 2.4f;
		OpacityAmp->ParameterName = TEXT("OpacityStrength");
		OpacityAmp->DefaultValue = 1.f;
		Sample->ParameterName = TEXT("FlipbookTexture");
		SetExprFloat(One, TEXT("R"), 1.f);

		UMaterialExpression* TFrames = Bin(Time, TEXT(""), Fps, TEXT(""), TEXT("MaterialExpressionMultiply"), -1220, 260);
		UMaterialExpression* Frame = ExprNamed(Mat, TEXT("MaterialExpressionFloor"), -1040, 260);
		UMaterialExpression* Total = Bin(Cols, TEXT(""), Rows, TEXT(""), TEXT("MaterialExpressionMultiply"), -1040, 500);
		if (!TFrames || !Frame || !Total)
		{
			OutError = TEXT("Flipbook frame math failed");
			return nullptr;
		}
		UMaterialEditingLibrary::ConnectMaterialExpressions(TFrames, TEXT(""), Frame, TEXT(""));
		UMaterialExpression* Wrapped = Bin(Frame, TEXT(""), Total, TEXT(""), TEXT("MaterialExpressionFmod"), -860, 260);
		UMaterialExpression* Col = Bin(Wrapped, TEXT(""), Cols, TEXT(""), TEXT("MaterialExpressionFmod"), -680, 220);
		UMaterialExpression* DivRow = Bin(Wrapped, TEXT(""), Cols, TEXT(""), TEXT("MaterialExpressionDivide"), -860, 340);
		UMaterialExpression* Row = ExprNamed(Mat, TEXT("MaterialExpressionFloor"), -680, 320);
		if (!Wrapped || !Col || !DivRow || !Row)
		{
			OutError = TEXT("Flipbook cell math failed");
			return nullptr;
		}
		UMaterialEditingLibrary::ConnectMaterialExpressions(DivRow, TEXT(""), Row, TEXT(""));
		UMaterialExpression* UCell = Bin(MaskU, TEXT(""), Col, TEXT(""), TEXT("MaterialExpressionAdd"), -500, -40);
		UMaterialExpression* VCell = Bin(MaskV, TEXT(""), Row, TEXT(""), TEXT("MaterialExpressionAdd"), -500, 80);
		UMaterialExpression* InvC = Bin(One, TEXT(""), Cols, TEXT(""), TEXT("MaterialExpressionDivide"), -500, 260);
		UMaterialExpression* InvR = Bin(One, TEXT(""), Rows, TEXT(""), TEXT("MaterialExpressionDivide"), -500, 340);
		UMaterialExpression* UAtlas = Bin(UCell, TEXT(""), InvC, TEXT(""), TEXT("MaterialExpressionMultiply"), -320, -40);
		UMaterialExpression* VAtlas = Bin(VCell, TEXT(""), InvR, TEXT(""), TEXT("MaterialExpressionMultiply"), -320, 80);
		UMaterialExpression* AtlasUv = ExprNamed(Mat, TEXT("MaterialExpressionAppendVector"), -140, 20);
		if (!UCell || !VCell || !InvC || !InvR || !UAtlas || !VAtlas || !AtlasUv)
		{
			OutError = TEXT("Flipbook UV failed");
			return nullptr;
		}
		UMaterialEditingLibrary::ConnectMaterialExpressions(UAtlas, TEXT(""), AtlasUv, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(VAtlas, TEXT(""), AtlasUv, TEXT("B"));
		if (!UMaterialEditingLibrary::ConnectMaterialExpressions(AtlasUv, TEXT(""), Sample, TEXT("UVs")))
		{
			UMaterialEditingLibrary::ConnectMaterialExpressions(AtlasUv, TEXT(""), Sample, TEXT("Coordinates"));
		}
		UMaterialExpression* Emissive = Bin(Sample, TEXT(""), Strength, TEXT(""), TEXT("MaterialExpressionMultiply"), 280, 0);
		UMaterialExpression* Opacity = ExprNamed(Mat, TEXT("MaterialExpressionMultiply"), 460, 140);
		if (!Emissive || !Opacity)
		{
			OutError = TEXT("Flipbook output failed");
			return nullptr;
		}
		UMaterialEditingLibrary::ConnectMaterialExpressions(Sample, TEXT("A"), Opacity, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(OpacityAmp, TEXT(""), Opacity, TEXT("B"));
		UMaterialEditingLibrary::ConnectMaterialProperty(Emissive, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Opacity, TEXT(""), MP_Opacity);
		UMaterialEditingLibrary::RecompileMaterial(Mat);
		FAssetRegistryModule::AssetCreated(Mat);
		Pkg->MarkPackageDirty();
		return Mat;
	}

	UMaterialInstanceConstant* CreateFlipbookMic(const FString& MicPath, UTexture2D* Tex, float Columns, float Rows, float Fps, float Emissive, float Opacity, FString& OutError)
	{
		UMaterial* Parent = EnsureEditorPromptFlipbookMaterial(OutError);
		if (!Parent || !Tex)
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("Flipbook MIC inputs missing");
			}
			return nullptr;
		}
		UMaterialInstanceConstant* MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *MicPath);
		if (!MIC)
		{
			FString PackagePath;
			FString AssetName;
			if (!SplitGameAssetPath(MicPath, PackagePath, AssetName, OutError))
			{
				return nullptr;
			}
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
			UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
			Factory->InitialParent = Parent;
			MIC = Cast<UMaterialInstanceConstant>(AssetTools.CreateAsset(
				AssetName, PackagePath, UMaterialInstanceConstant::StaticClass(), Factory));
		}
		if (!MIC)
		{
			OutError = TEXT("Create flipbook MIC failed");
			return nullptr;
		}
		MIC->Modify();
		if (MIC->Parent != Parent)
		{
			MIC->SetParentEditorOnly(Parent);
		}
		Tex->Modify();
		Tex->AddressX = TA_Clamp;
		Tex->AddressY = TA_Clamp;
		Tex->PostEditChange();
		Tex->MarkPackageDirty();
		MIC->SetTextureParameterValueEditorOnly(FName(TEXT("FlipbookTexture")), Tex);
		MIC->SetScalarParameterValueEditorOnly(FName(TEXT("Columns")), Columns);
		MIC->SetScalarParameterValueEditorOnly(FName(TEXT("Rows")), Rows);
		MIC->SetScalarParameterValueEditorOnly(FName(TEXT("FPS")), Fps);
		MIC->SetScalarParameterValueEditorOnly(FName(TEXT("EmissiveStrength")), Emissive);
		MIC->SetScalarParameterValueEditorOnly(FName(TEXT("OpacityStrength")), Opacity);
		MIC->PostEditChange();
		MIC->MarkPackageDirty();
		return MIC;
	}

	const TCHAR* EditorPromptNoiseMatPath()
	{
		return TEXT("/Game/FX/M_EditorPromptNoiseSprite");
	}

	UTexture2D* EnsureLinearStubTexture(FString& OutError)
	{
		const TCHAR* Path = TEXT("/Game/FX/Textures/T_EditorPromptLinearStub");
		if (UTexture2D* Existing = LoadObject<UTexture2D>(nullptr, Path))
		{
			Existing->Modify();
			Existing->SRGB = false;
			Existing->CompressionSettings = TC_Default;
			Existing->MipGenSettings = TMGS_NoMipmaps;
			Existing->Filter = TF_Bilinear;
			Existing->AddressX = TA_Wrap;
			Existing->AddressY = TA_Wrap;
			Existing->LODGroup = TEXTUREGROUP_Effects;
			Existing->PostEditChange();
			Existing->UpdateResource();
			return Existing;
		}
		FString PackagePath;
		FString AssetName;
		if (!SplitGameAssetPath(Path, PackagePath, AssetName, OutError))
		{
			return nullptr;
		}
		UPackage* Pkg = CreatePackage(*(PackagePath / AssetName));
		TArray<FColor> Pixels;
		Pixels.Init(FColor(128, 128, 128, 255), 16);
		FCreateTexture2DParameters Params;
		Params.bSRGB = false;
		Params.bDeferCompression = true;
		Params.CompressionSettings = TC_Default;
		Params.MipGenSettings = TMGS_NoMipmaps;
		UTexture2D* Tex = FImageUtils::CreateTexture2D(
			4, 4, Pixels, Pkg, AssetName, RF_Public | RF_Standalone | RF_Transactional, Params);
		if (!Tex)
		{
			OutError = TEXT("Linear stub texture failed");
			return nullptr;
		}
		Tex->SRGB = false;
		Tex->LODGroup = TEXTUREGROUP_Effects;
		Tex->AddressX = TA_Wrap;
		Tex->AddressY = TA_Wrap;
		Tex->Filter = TF_Bilinear;
		Tex->UpdateResource();
		FAssetRegistryModule::AssetCreated(Tex);
		Pkg->MarkPackageDirty();
		return Tex;
	}

	bool BindNoiseParentSamplers(UMaterial* Mat, FString& OutError)
	{
		UTexture2D* Stub = EnsureLinearStubTexture(OutError);
		if (!Mat || !Stub)
		{
			return false;
		}
		TArray<const UMaterialExpressionTextureSampleParameter2D*> Samples;
		Mat->GetAllExpressionsOfType(Samples);
		bool bChanged = false;
		for (const UMaterialExpressionTextureSampleParameter2D* ConstSample : Samples)
		{
			UMaterialExpressionTextureSampleParameter2D* Sample =
				const_cast<UMaterialExpressionTextureSampleParameter2D*>(ConstSample);
			if (!Sample)
			{
				continue;
			}
			if (Sample->ParameterName == FName(TEXT("NoiseTexture")))
			{
				if (Sample->Texture != Stub || Sample->SamplerType != SAMPLERTYPE_LinearColor)
				{
					Sample->Modify();
					Sample->Texture = Stub;
					Sample->SamplerType = SAMPLERTYPE_LinearColor;
					bChanged = true;
				}
			}
			else if (Sample->ParameterName == FName(TEXT("SpriteTexture"))
				&& Sample->SamplerType != SAMPLERTYPE_Color)
			{
				Sample->Modify();
				Sample->SamplerType = SAMPLERTYPE_Color;
				bChanged = true;
			}
		}
		if (bChanged)
		{
			Mat->Modify();
			UMaterialEditingLibrary::RecompileMaterial(Mat);
			Mat->MarkPackageDirty();
		}
		return true;
	}

	UMaterial* EnsureEditorPromptNoiseMaterial(FString& OutError)
	{
		const TCHAR* Path = EditorPromptNoiseMatPath();
		if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, Path))
		{
			if (!BindNoiseParentSamplers(Existing, OutError))
			{
				return nullptr;
			}
			return Existing;
		}
		FString PackagePath;
		FString AssetName;
		if (!SplitGameAssetPath(Path, PackagePath, AssetName, OutError))
		{
			return nullptr;
		}
		UPackage* Pkg = CreatePackage(*(PackagePath / AssetName));
		UMaterial* Mat = NewObject<UMaterial>(Pkg, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!Mat)
		{
			OutError = TEXT("Create noise sprite material failed");
			return nullptr;
		}
		Mat->SetShadingModel(MSM_Unlit);
		Mat->BlendMode = BLEND_Translucent;
		Mat->TwoSided = true;
		Mat->bUsedWithNiagaraSprites = true;
		bool bNeedsRecompile = false;
		UMaterialEditingLibrary::SetMaterialUsage(Mat, MATUSAGE_NiagaraSprites, bNeedsRecompile);
		UMaterialEditingLibrary::SetMaterialUsage(Mat, MATUSAGE_ParticleSprites, bNeedsRecompile);

		auto Bin = [Mat](UMaterialExpression* A, const TCHAR* APin, UMaterialExpression* B, const TCHAR* BPin,
			const TCHAR* ClassName, int32 X, int32 Y) -> UMaterialExpression*
		{
			UMaterialExpression* Node = ExprNamed(Mat, ClassName, X, Y);
			if (!Node || !A || !B)
			{
				return nullptr;
			}
			UMaterialEditingLibrary::ConnectMaterialExpressions(A, APin, Node, TEXT("A"));
			UMaterialEditingLibrary::ConnectMaterialExpressions(B, BPin, Node, TEXT("B"));
			return Node;
		};

		UMaterialExpression* Uv = ExprNamed(Mat, TEXT("MaterialExpressionTextureCoordinate"), -1400, 0);
		UMaterialExpression* Time = ExprNamed(Mat, TEXT("MaterialExpressionTime"), -1400, 220);
		UMaterialExpressionScalarParameter* Tiling = Cast<UMaterialExpressionScalarParameter>(
			ExprNamed(Mat, TEXT("MaterialExpressionScalarParameter"), -1400, 300));
		UMaterialExpressionScalarParameter* SpeedU = Cast<UMaterialExpressionScalarParameter>(
			ExprNamed(Mat, TEXT("MaterialExpressionScalarParameter"), -1400, 380));
		UMaterialExpressionScalarParameter* SpeedV = Cast<UMaterialExpressionScalarParameter>(
			ExprNamed(Mat, TEXT("MaterialExpressionScalarParameter"), -1400, 460));
		UMaterialExpressionScalarParameter* Strength = Cast<UMaterialExpressionScalarParameter>(
			ExprNamed(Mat, TEXT("MaterialExpressionScalarParameter"), -200, 360));
		UMaterialExpressionScalarParameter* Emissive = Cast<UMaterialExpressionScalarParameter>(
			ExprNamed(Mat, TEXT("MaterialExpressionScalarParameter"), 280, 240));
		UMaterialExpressionScalarParameter* OpacityAmp = Cast<UMaterialExpressionScalarParameter>(
			ExprNamed(Mat, TEXT("MaterialExpressionScalarParameter"), 460, 280));
		UMaterialExpression* Half = ExprNamed(Mat, TEXT("MaterialExpressionConstant2Vector"), -200, 200);
		UMaterialExpression* Panner = ExprNamed(Mat, TEXT("MaterialExpressionPanner"), -860, 40);
		UMaterialExpression* Speed2 = ExprNamed(Mat, TEXT("MaterialExpressionAppendVector"), -1100, 400);
		UMaterialExpressionTextureSampleParameter2D* Noise = Cast<UMaterialExpressionTextureSampleParameter2D>(
			ExprNamed(Mat, TEXT("MaterialExpressionTextureSampleParameter2D"), -560, 0));
		UMaterialExpressionTextureSampleParameter2D* Sprite = Cast<UMaterialExpressionTextureSampleParameter2D>(
			ExprNamed(Mat, TEXT("MaterialExpressionTextureSampleParameter2D"), 40, 0));
		UMaterialExpression* MaskRG = ExprNamed(Mat, TEXT("MaterialExpressionComponentMask"), -200, 0);
		if (!Uv || !Time || !Tiling || !SpeedU || !SpeedV || !Strength || !Emissive || !OpacityAmp
			|| !Half || !Panner || !Speed2 || !Noise || !Sprite || !MaskRG)
		{
			OutError = TEXT("Noise material expressions failed");
			return nullptr;
		}
		Tiling->ParameterName = TEXT("NoiseTiling");
		Tiling->DefaultValue = 1.f;
		SpeedU->ParameterName = TEXT("NoiseSpeedU");
		SpeedU->DefaultValue = 0.05f;
		SpeedV->ParameterName = TEXT("NoiseSpeedV");
		SpeedV->DefaultValue = 0.035f;
		Strength->ParameterName = TEXT("NoiseStrength");
		Strength->DefaultValue = 0.06f;
		Emissive->ParameterName = TEXT("EmissiveStrength");
		Emissive->DefaultValue = 2.4f;
		OpacityAmp->ParameterName = TEXT("OpacityStrength");
		OpacityAmp->DefaultValue = 1.f;
		SetExprFloat(Half, TEXT("R"), 0.5f);
		SetExprFloat(Half, TEXT("G"), 0.5f);
		SetExprBool(Panner, TEXT("bFractionalPart"), true);
		Noise->ParameterName = TEXT("NoiseTexture");
		Sprite->ParameterName = TEXT("SpriteTexture");
		SetExprBool(MaskRG, TEXT("R"), true);
		SetExprBool(MaskRG, TEXT("G"), true);
		SetExprBool(MaskRG, TEXT("B"), false);
		SetExprBool(MaskRG, TEXT("A"), false);

		UMaterialExpression* Tiled = Bin(Uv, TEXT(""), Tiling, TEXT(""), TEXT("MaterialExpressionMultiply"), -1100, 0);
		UMaterialEditingLibrary::ConnectMaterialExpressions(SpeedU, TEXT(""), Speed2, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(SpeedV, TEXT(""), Speed2, TEXT("B"));
		if (!Tiled)
		{
			OutError = TEXT("Noise UV tiling failed");
			return nullptr;
		}
		UMaterialEditingLibrary::ConnectMaterialExpressions(Tiled, TEXT(""), Panner, TEXT("Coordinate"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Time, TEXT(""), Panner, TEXT("Time"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(Speed2, TEXT(""), Panner, TEXT("Speed"));
		if (!UMaterialEditingLibrary::ConnectMaterialExpressions(Panner, TEXT(""), Noise, TEXT("UVs")))
		{
			UMaterialEditingLibrary::ConnectMaterialExpressions(Panner, TEXT(""), Noise, TEXT("Coordinates"));
		}
		UMaterialEditingLibrary::ConnectMaterialExpressions(Noise, TEXT(""), MaskRG, TEXT(""));
		UMaterialExpression* Centered = Bin(MaskRG, TEXT(""), Half, TEXT(""), TEXT("MaterialExpressionSubtract"), -40, 80);
		UMaterialExpression* Offset = Bin(Centered, TEXT(""), Strength, TEXT(""), TEXT("MaterialExpressionMultiply"), -40, 200);
		UMaterialExpression* Warped = Bin(Uv, TEXT(""), Offset, TEXT(""), TEXT("MaterialExpressionAdd"), -40, 0);
		if (!Centered || !Offset || !Warped)
		{
			OutError = TEXT("Noise UV warp failed");
			return nullptr;
		}
		if (!UMaterialEditingLibrary::ConnectMaterialExpressions(Warped, TEXT(""), Sprite, TEXT("UVs")))
		{
			UMaterialEditingLibrary::ConnectMaterialExpressions(Warped, TEXT(""), Sprite, TEXT("Coordinates"));
		}

		UClass* ParticleColorClass = LoadClass<UMaterialExpression>(
			nullptr, TEXT("/Script/Engine.MaterialExpressionParticleColor"));
		UMaterialExpression* PCol = ParticleColorClass
			? UMaterialEditingLibrary::CreateMaterialExpression(Mat, ParticleColorClass, 40, 320)
			: nullptr;
		UMaterialExpression* MulRgb = Bin(Sprite, TEXT(""), Emissive, TEXT(""), TEXT("MaterialExpressionMultiply"), 280, 0);
		UMaterialExpression* Opacity = ExprNamed(Mat, TEXT("MaterialExpressionMultiply"), 460, 140);
		if (!MulRgb || !Opacity)
		{
			OutError = TEXT("Noise output failed");
			return nullptr;
		}
		UMaterialEditingLibrary::ConnectMaterialExpressions(Sprite, TEXT("A"), Opacity, TEXT("A"));
		UMaterialEditingLibrary::ConnectMaterialExpressions(OpacityAmp, TEXT(""), Opacity, TEXT("B"));
		if (PCol)
		{
			UMaterialExpression* Tint = Bin(MulRgb, TEXT(""), PCol, TEXT(""), TEXT("MaterialExpressionMultiply"), 460, 0);
			UMaterialExpression* OpA = Bin(Opacity, TEXT(""), PCol, TEXT("A"), TEXT("MaterialExpressionMultiply"), 640, 140);
			if (Tint)
			{
				MulRgb = Tint;
			}
			if (OpA)
			{
				Opacity = OpA;
			}
		}
		UMaterialEditingLibrary::ConnectMaterialProperty(MulRgb, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Opacity, TEXT(""), MP_Opacity);
		FAssetRegistryModule::AssetCreated(Mat);
		Pkg->MarkPackageDirty();
		if (!BindNoiseParentSamplers(Mat, OutError))
		{
			return nullptr;
		}
		return Mat;
	}

	UMaterialInstanceConstant* CreateNoiseMic(
		const FString& MicPath,
		UTexture2D* SpriteTex,
		UTexture2D* NoiseTex,
		float Strength,
		float Tiling,
		float SpeedU,
		float SpeedV,
		float Emissive,
		float Opacity,
		FString& OutError)
	{
		UMaterial* Parent = EnsureEditorPromptNoiseMaterial(OutError);
		if (!Parent || !SpriteTex || !NoiseTex)
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("Noise MIC inputs missing");
			}
			return nullptr;
		}
		UMaterialInstanceConstant* MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *MicPath);
		if (!MIC)
		{
			FString PackagePath;
			FString AssetName;
			if (!SplitGameAssetPath(MicPath, PackagePath, AssetName, OutError))
			{
				return nullptr;
			}
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
			UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
			Factory->InitialParent = Parent;
			MIC = Cast<UMaterialInstanceConstant>(AssetTools.CreateAsset(
				AssetName, PackagePath, UMaterialInstanceConstant::StaticClass(), Factory));
		}
		if (!MIC)
		{
			OutError = TEXT("Create noise MIC failed");
			return nullptr;
		}
		MIC->Modify();
		if (MIC->Parent != Parent)
		{
			MIC->SetParentEditorOnly(Parent);
		}
		NoiseTex->Modify();
		NoiseTex->SRGB = false;
		NoiseTex->CompressionSettings = TC_Default;
		NoiseTex->MipGenSettings = TMGS_NoMipmaps;
		NoiseTex->Filter = TF_Bilinear;
		NoiseTex->AddressX = TA_Wrap;
		NoiseTex->AddressY = TA_Wrap;
		NoiseTex->PostEditChange();
		NoiseTex->MarkPackageDirty();
		SpriteTex->Modify();
		SpriteTex->AddressX = TA_Clamp;
		SpriteTex->AddressY = TA_Clamp;
		SpriteTex->PostEditChange();
		SpriteTex->MarkPackageDirty();
		MIC->SetTextureParameterValueEditorOnly(FName(TEXT("SpriteTexture")), SpriteTex);
		MIC->SetTextureParameterValueEditorOnly(FName(TEXT("NoiseTexture")), NoiseTex);
		MIC->SetScalarParameterValueEditorOnly(FName(TEXT("NoiseStrength")), Strength);
		MIC->SetScalarParameterValueEditorOnly(FName(TEXT("NoiseTiling")), Tiling);
		MIC->SetScalarParameterValueEditorOnly(FName(TEXT("NoiseSpeedU")), SpeedU);
		MIC->SetScalarParameterValueEditorOnly(FName(TEXT("NoiseSpeedV")), SpeedV);
		MIC->SetScalarParameterValueEditorOnly(FName(TEXT("EmissiveStrength")), Emissive);
		MIC->SetScalarParameterValueEditorOnly(FName(TEXT("OpacityStrength")), Opacity);
		MIC->PostEditChange();
		MIC->MarkPackageDirty();
		return MIC;
	}

	UNiagaraSpriteRendererProperties* FindSpriteRenderer(UNiagaraSystem* Sys, const FString& EmitterName)
	{
		FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, EmitterName);
		FVersionedNiagaraEmitterData* Data = Handle ? Handle->GetEmitterData() : nullptr;
		if (!Data)
		{
			return nullptr;
		}
		for (UNiagaraRendererProperties* R : Data->GetRenderers())
		{
			if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(R))
			{
				return Sprite;
			}
		}
		return nullptr;
	}

	ENiagaraSpriteAlignment ParseSpriteAlignment(const FString& Text)
	{
		const FString S = Text.Replace(TEXT(" "), TEXT("_")).ToLower();
		if (S.Contains(TEXT("velocity")))
		{
			return ENiagaraSpriteAlignment::VelocityAligned;
		}
		if (S.Contains(TEXT("custom")))
		{
			return ENiagaraSpriteAlignment::CustomAlignment;
		}
		if (S.Contains(TEXT("unaligned")))
		{
			return ENiagaraSpriteAlignment::Unaligned;
		}
		return ENiagaraSpriteAlignment::Automatic;
	}

	ENiagaraSpriteFacingMode ParseSpriteFacing(const FString& Text)
	{
		const FString S = Text.Replace(TEXT(" "), TEXT("_")).ToLower();
		if (S.Contains(TEXT("plane")))
		{
			return ENiagaraSpriteFacingMode::FaceCameraPlane;
		}
		if (S.Contains(TEXT("position")))
		{
			return ENiagaraSpriteFacingMode::FaceCameraPosition;
		}
		if (S.Contains(TEXT("custom")))
		{
			return ENiagaraSpriteFacingMode::CustomFacingVector;
		}
		if (S.Contains(TEXT("camera")) || S.Contains(TEXT("billboard")))
		{
			return ENiagaraSpriteFacingMode::FaceCamera;
		}
		return ENiagaraSpriteFacingMode::Automatic;
	}

	ENiagaraSortMode ParseSpriteSort(const FString& Text)
	{
		const FString S = Text.Replace(TEXT(" "), TEXT("_")).ToLower();
		if (S.Contains(TEXT("distance")))
		{
			return ENiagaraSortMode::ViewDistance;
		}
		if (S.Contains(TEXT("depth")))
		{
			return ENiagaraSortMode::ViewDepth;
		}
		if (S.Contains(TEXT("custom")))
		{
			return ENiagaraSortMode::CustomAscending;
		}
		return ENiagaraSortMode::None;
	}

	const TCHAR* AlignmentLabel(ENiagaraSpriteAlignment V)
	{
		switch (V)
		{
		case ENiagaraSpriteAlignment::VelocityAligned: return TEXT("velocity");
		case ENiagaraSpriteAlignment::CustomAlignment: return TEXT("custom");
		case ENiagaraSpriteAlignment::Unaligned: return TEXT("unaligned");
		default: return TEXT("automatic");
		}
	}

	const TCHAR* FacingLabel(ENiagaraSpriteFacingMode V)
	{
		switch (V)
		{
		case ENiagaraSpriteFacingMode::FaceCamera: return TEXT("camera");
		case ENiagaraSpriteFacingMode::FaceCameraPlane: return TEXT("camera_plane");
		case ENiagaraSpriteFacingMode::CustomFacingVector: return TEXT("custom");
		case ENiagaraSpriteFacingMode::FaceCameraPosition: return TEXT("camera_position");
		default: return TEXT("automatic");
		}
	}

	UNiagaraSpriteRendererProperties* EnsureSpriteRenderer(UNiagaraSystem* Sys, const FString& EmitterName, FString& OutError)
	{
		if (UNiagaraSpriteRendererProperties* Existing = FindSpriteRenderer(Sys, EmitterName))
		{
			return Existing;
		}
		FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, EmitterName);
		UNiagaraEmitter* Emitter = Handle ? Handle->GetInstance().Emitter : nullptr;
		if (!Emitter || !Handle)
		{
			OutError = TEXT("Emitter not found");
			return nullptr;
		}
		UNiagaraSpriteRendererProperties* Sprite = NewObject<UNiagaraSpriteRendererProperties>(Emitter, TEXT("Renderer"));
		Emitter->AddRenderer(Sprite, Handle->GetInstance().Version);
		return Sprite;
	}

	UNiagaraNodeFunctionCall* FindModuleAmong(const TArray<UNiagaraNodeFunctionCall*>& Calls, const FString& ModuleQuery)
	{
		if (ModuleQuery.IsEmpty())
		{
			return nullptr;
		}
		UNiagaraNodeFunctionCall* Fallback = nullptr;
		for (UNiagaraNodeFunctionCall* Call : Calls)
		{
			if (!Call)
			{
				continue;
			}
			const FString Fn = Call->GetFunctionName();
			const FString Path = Call->FunctionScript ? Call->FunctionScript->GetPathName() : FString();
			if (Fn.Equals(ModuleQuery, ESearchCase::IgnoreCase) || Path.Equals(ModuleQuery, ESearchCase::IgnoreCase))
			{
				return Call;
			}
			if (!Fallback && (Fn.Contains(ModuleQuery) || Path.Contains(ModuleQuery)))
			{
				Fallback = Call;
			}
		}
		return Fallback;
	}

	UNiagaraScript* ScriptForUsage(FVersionedNiagaraEmitterData* Data, ENiagaraScriptUsage Usage)
	{
		if (!Data)
		{
			return nullptr;
		}
		if (UNiagaraScript* Script = Data->GetScript(Usage, FGuid()))
		{
			return Script;
		}
		if (Usage == ENiagaraScriptUsage::ParticleSpawnScript)
		{
			return Data->GetScript(ENiagaraScriptUsage::ParticleSpawnScriptInterpolated, FGuid());
		}
		return nullptr;
	}

	FString UniqueEmitterName(const FNiagaraEmitterHandle& Handle)
	{
		return Handle.GetUniqueInstanceName();
	}

	void CollectFunctionCalls(UNiagaraNodeOutput* Output, TArray<UNiagaraNodeFunctionCall*>& OutCalls)
	{
		if (!Output)
		{
			return;
		}
		TSet<const UEdGraphNode*> Visited;
		TArray<UEdGraphNode*> Stack;
		TArray<UEdGraphPin*> Pins;
		Output->GetInputPins(Pins);
		for (UEdGraphPin* Pin : Pins)
		{
			if (!Pin)
			{
				continue;
			}
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (Linked && Linked->GetOwningNode())
				{
					Stack.Add(Linked->GetOwningNode());
				}
			}
		}
		while (Stack.Num() > 0)
		{
			UEdGraphNode* Node = Stack.Pop(EAllowShrinking::No);
			if (!Node || Visited.Contains(Node))
			{
				continue;
			}
			Visited.Add(Node);
			if (UNiagaraNodeFunctionCall* Call = Cast<UNiagaraNodeFunctionCall>(Node))
			{
				OutCalls.Add(Call);
			}
			if (UNiagaraNode* NiagaraNode = Cast<UNiagaraNode>(Node))
			{
				TArray<UEdGraphPin*> InPins;
				NiagaraNode->GetInputPins(InPins);
				for (UEdGraphPin* Pin : InPins)
				{
					if (!Pin)
					{
						continue;
					}
					for (UEdGraphPin* Linked : Pin->LinkedTo)
					{
						if (Linked && Linked->GetOwningNode())
						{
							Stack.Add(Linked->GetOwningNode());
						}
					}
				}
			}
		}
	}

	FString ShortModuleInputName(const FNiagaraVariable& Var)
	{
		FString Name = Var.GetName().ToString();
		Name.RemoveFromStart(TEXT("Module."));
		return Name;
	}

	bool IsModuleNamespaceInput(const FNiagaraVariable& Var)
	{
		const FString Name = Var.GetName().ToString();
		if (!Name.StartsWith(TEXT("Module.")) || Name.Equals(TEXT("Module.InputMap")))
		{
			return false;
		}
		if (Var.GetType() == FNiagaraTypeDefinition::GetParameterMapDef() || Var.IsDataInterface())
		{
			return false;
		}
		return true;
	}

	void CollectModuleInputs(UNiagaraNodeFunctionCall* Call, TArray<FNiagaraVariable>& Out)
	{
		Out.Reset();
		if (!Call)
		{
			return;
		}
		UNiagaraGraph* Graph = Call->GetCalledGraph();
		if (!Graph)
		{
			return;
		}
		for (const TPair<FNiagaraVariable, TObjectPtr<UNiagaraScriptVariable>>& Pair : Graph->GetAllMetaData())
		{
			if (IsModuleNamespaceInput(Pair.Key))
			{
				Out.AddUnique(Pair.Key);
			}
		}
		if (Out.Num() > 0)
		{
			return;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UNiagaraNodeInput* InputNode = Cast<UNiagaraNodeInput>(Node);
			if (!InputNode)
			{
				continue;
			}
			const FNiagaraVariable& Input = InputNode->Input;
			if (IsModuleNamespaceInput(Input))
			{
				Out.AddUnique(Input);
				continue;
			}
			if (InputNode->Usage == ENiagaraInputNodeUsage::Parameter
				&& Input.GetType() != FNiagaraTypeDefinition::GetParameterMapDef()
				&& !Input.GetName().ToString().Equals(TEXT("InputMap")))
			{
				Out.AddUnique(Input);
			}
		}
	}

	const FNiagaraVariable* FindListedModuleInput(const TArray<FNiagaraVariable>& Inputs, const FString& Query)
	{
		FString Want = Query;
		Want.RemoveFromStart(TEXT("Module."));
		for (const FNiagaraVariable& Var : Inputs)
		{
			if (ShortModuleInputName(Var).Equals(Want, ESearchCase::IgnoreCase)
				|| Var.GetName().ToString().Equals(Query, ESearchCase::IgnoreCase))
			{
				return &Var;
			}
		}
		return nullptr;
	}

	void CollectModuleDiInputs(UNiagaraNodeFunctionCall* Call, TArray<FNiagaraVariable>& Out)
	{
		Out.Reset();
		if (!Call)
		{
			return;
		}
		UNiagaraGraph* Graph = Call->GetCalledGraph();
		if (!Graph)
		{
			return;
		}
		auto Consider = [&](const FNiagaraVariable& Var)
		{
			const FString Name = Var.GetName().ToString();
			if (!Name.StartsWith(TEXT("Module.")) || Name.Equals(TEXT("Module.InputMap")))
			{
				return;
			}
			if (Var.IsDataInterface())
			{
				Out.AddUnique(Var);
			}
		};
		for (const TPair<FNiagaraVariable, TObjectPtr<UNiagaraScriptVariable>>& Pair : Graph->GetAllMetaData())
		{
			Consider(Pair.Key);
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UNiagaraNodeInput* InputNode = Cast<UNiagaraNodeInput>(Node))
			{
				Consider(InputNode->Input);
			}
		}
	}

	FString ModuleInputLines(UNiagaraNodeFunctionCall* Call)
	{
		FString Lines;
		TArray<FNiagaraVariable> Inputs;
		CollectModuleInputs(Call, Inputs);
		for (const FNiagaraVariable& Var : Inputs)
		{
			Lines += FString::Printf(
				TEXT("    set name=%s type=%s\n"),
				*ShortModuleInputName(Var),
				*Var.GetType().GetName());
		}
		TArray<FNiagaraVariable> Dis;
		CollectModuleDiInputs(Call, Dis);
		for (const FNiagaraVariable& Var : Dis)
		{
			Lines += FString::Printf(
				TEXT("    curve name=%s type=%s\n"),
				*ShortModuleInputName(Var),
				*Var.GetType().GetName());
		}
		return Lines;
	}

	bool ResolveModuleCall(
		UNiagaraGraph* Graph,
		const FString& ModuleQuery,
		const FString& StackHint,
		UNiagaraNodeFunctionCall*& OutCall,
		ENiagaraScriptUsage& OutUsage,
		FString& Message)
	{
		OutCall = nullptr;
		static const ENiagaraScriptUsage Stacks[] = {
			ENiagaraScriptUsage::EmitterSpawnScript,
			ENiagaraScriptUsage::EmitterUpdateScript,
			ENiagaraScriptUsage::ParticleSpawnScript,
			ENiagaraScriptUsage::ParticleUpdateScript,
			ENiagaraScriptUsage::ParticleSimulationStageScript,
		};
		auto CollectOn = [&](ENiagaraScriptUsage Usage) -> UNiagaraNodeFunctionCall*
		{
			TArray<UNiagaraNodeFunctionCall*> Calls;
			if (Usage == ENiagaraScriptUsage::ParticleSimulationStageScript && Graph)
			{
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (UNiagaraNodeOutput* Out = Cast<UNiagaraNodeOutput>(Node))
					{
						if (Out->GetUsage() == Usage)
						{
							CollectFunctionCalls(Out, Calls);
						}
					}
				}
			}
			else
			{
				CollectFunctionCalls(FindOutputNode(Graph, Usage), Calls);
			}
			return FindModuleAmong(Calls, ModuleQuery);
		};
		if (!StackHint.IsEmpty())
		{
			OutUsage = UsageFromStack(StackHint);
			OutCall = CollectOn(OutUsage);
			if (OutCall)
			{
				return true;
			}
			for (ENiagaraScriptUsage Usage : Stacks)
			{
				if (Usage == OutUsage)
				{
					continue;
				}
				if (UNiagaraNodeFunctionCall* Other = CollectOn(Usage))
				{
					Message = FString::Printf(
						TEXT("Module %s is on %s, not %s"),
						*ModuleQuery, UsageLabel(Usage), *StackHint);
					return false;
				}
			}
			Message = TEXT("Module node not found");
			return false;
		}
		int32 Hits = 0;
		for (ENiagaraScriptUsage Usage : Stacks)
		{
			if (UNiagaraNodeFunctionCall* Call = CollectOn(Usage))
			{
				++Hits;
				OutCall = Call;
				OutUsage = Usage;
			}
		}
		if (Hits == 1)
		{
			return true;
		}
		if (Hits > 1)
		{
			Message = TEXT("Module found on multiple stacks; pass stack=");
			return false;
		}
		Message = TEXT("Module node not found");
		return false;
	}

	bool IsParamMapPin(const UEdGraphPin* Pin)
	{
		return Pin && UEdGraphSchema_Niagara::PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef();
	}

	UEdGraphPin* NodeParamMapPin(UNiagaraNode* Node, EEdGraphPinDirection Dir)
	{
		if (!Node)
		{
			return nullptr;
		}
		TArray<UEdGraphPin*> Pins;
		if (Dir == EGPD_Input)
		{
			Node->GetInputPins(Pins);
		}
		else
		{
			Node->GetOutputPins(Pins);
		}
		for (UEdGraphPin* Pin : Pins)
		{
			if (IsParamMapPin(Pin))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	bool NodeClassNamed(const UEdGraphNode* Node, const TCHAR* Name)
	{
		return Node && Node->GetClass() && Node->GetClass()->GetName().Equals(Name);
	}

	void DeleteGraphNode(UEdGraphNode* Node)
	{
		if (!Node || !Node->GetGraph())
		{
			return;
		}
		Node->Modify();
		Node->BreakAllNodeLinks();
		Node->GetGraph()->RemoveNode(Node);
	}

	bool UnbindModuleInput(UNiagaraNodeFunctionCall& Call, const FString& InputName, FString& OutLog)
	{
		TArray<FNiagaraVariable> Inputs;
		CollectModuleInputs(&Call, Inputs);
		const FNiagaraVariable* Matched = FindListedModuleInput(Inputs, InputName);
		if (!Matched)
		{
			OutLog = TEXT("Unknown input on module");
			return false;
		}
		const FNiagaraParameterHandle ModuleHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(*ShortModuleInputName(*Matched));
		const FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(ModuleHandle, &Call);
		UEdGraphPin& Pin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
			Call, Aliased, Matched->GetType(), FGuid(), FGuid());
		TArray<UEdGraphNode*> Side;
		for (UEdGraphPin* Linked : Pin.LinkedTo)
		{
			if (Linked && Linked->GetOwningNode())
			{
				Side.AddUnique(Linked->GetOwningNode());
			}
		}
		if (Pin.LinkedTo.Num() == 0 && Side.Num() == 0)
		{
			OutLog = TEXT("Unbound ") + ShortModuleInputName(*Matched) + TEXT(" (already)");
			return true;
		}
		Pin.BreakAllPinLinks();
		for (UEdGraphNode* Node : Side)
		{
			if (NodeClassNamed(Node, TEXT("NiagaraNodeParameterMapGet")) || Node->IsA<UNiagaraNodeInput>())
			{
				DeleteGraphNode(Node);
			}
		}
		OutLog = TEXT("Unbound ") + ShortModuleInputName(*Matched);
		return true;
	}

	bool RemoveStackModule(UNiagaraSystem& Sys, FNiagaraEmitterHandle& Handle, UNiagaraNodeFunctionCall& Call, ENiagaraScriptUsage Usage, FString& OutLog)
	{
		(void)Sys;
		(void)Handle;
		(void)Usage;
		UNiagaraGraph* Graph = Call.GetNiagaraGraph();
		const FString Removed = Call.GetFunctionName();
		UEdGraphPin* InMap = NodeParamMapPin(&Call, EGPD_Input);
		UEdGraphPin* OutMap = NodeParamMapPin(&Call, EGPD_Output);
		if (!Graph || !InMap || !OutMap || OutMap->LinkedTo.Num() != 1)
		{
			OutLog = TEXT("Remove module failed (not in a stack chain)");
			return false;
		}
		UEdGraphPin* NextIn = OutMap->LinkedTo[0];
		UEdGraphPin* ChainIn = InMap->LinkedTo.Num() == 1 ? InMap->LinkedTo[0] : nullptr;
		UEdGraphNode* MaybeSet = ChainIn ? ChainIn->GetOwningNode() : nullptr;
		TArray<UEdGraphNode*> Side;
		UEdGraphPin* PrevOut = ChainIn;
		if (NodeClassNamed(MaybeSet, TEXT("NiagaraNodeParameterMapSet")))
		{
			UEdGraphPin* SetIn = NodeParamMapPin(Cast<UNiagaraNode>(MaybeSet), EGPD_Input);
			PrevOut = (SetIn && SetIn->LinkedTo.Num() == 1) ? SetIn->LinkedTo[0] : nullptr;
			for (UEdGraphPin* Pin : MaybeSet->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Input || IsParamMapPin(Pin))
				{
					continue;
				}
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (Linked && Linked->GetOwningNode() && Linked->GetOwningNode() != MaybeSet)
					{
						Side.AddUnique(Linked->GetOwningNode());
					}
				}
			}
			Side.AddUnique(MaybeSet);
			if (SetIn)
			{
				SetIn->BreakAllPinLinks();
			}
		}
		InMap->BreakAllPinLinks();
		OutMap->BreakAllPinLinks();
		if (PrevOut && NextIn && PrevOut != NextIn)
		{
			PrevOut->MakeLinkTo(NextIn);
		}
		for (UEdGraphNode* Node : Side)
		{
			if (NodeClassNamed(Node, TEXT("NiagaraNodeParameterMapGet"))
				|| NodeClassNamed(Node, TEXT("NiagaraNodeParameterMapSet"))
				|| Node->IsA<UNiagaraNodeInput>())
			{
				DeleteGraphNode(Node);
			}
		}
		DeleteGraphNode(&Call);
		Graph->NotifyGraphChanged();
		OutLog = TEXT("Removed ") + Removed;
		return true;
	}

	void AppendScriptCompileLog(UNiagaraScript* Script, const FString& Label, TArray<FString>& Errors, TArray<FString>& Warnings)
	{
		if (!Script)
		{
			return;
		}
		const FNiagaraVMExecutableData& VM = Script->GetVMExecutableData();
		if (!VM.ErrorMsg.IsEmpty())
		{
			Errors.Add(Label + TEXT(": ") + VM.ErrorMsg);
		}
		for (const FNiagaraCompileEvent& Event : VM.LastCompileEvents)
		{
			FString Line = Label + TEXT(": ") + Event.Message;
			if (!Event.ShortDescription.IsEmpty())
			{
				Line += TEXT(" (") + Event.ShortDescription + TEXT(")");
			}
			if (Event.Severity == FNiagaraCompileEventSeverity::Error)
			{
				Errors.Add(Line);
			}
			else if (Event.Severity == FNiagaraCompileEventSeverity::Warning)
			{
				Warnings.Add(Line);
			}
		}
		const ENiagaraScriptCompileStatus Status = Script->GetLastCompileStatus();
		if (Status == ENiagaraScriptCompileStatus::NCS_Error && Errors.Num() == 0)
		{
			Errors.Add(Label + TEXT(": compile status error"));
		}
	}

	template <typename ValueType>
	bool WriteRapidInput(const FNiagaraEmitterHandle& Handle, UNiagaraNodeFunctionCall& Call, ENiagaraScriptUsage Usage, FName InputName, FNiagaraTypeDefinition InputType, const ValueType& Value)
	{
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		UNiagaraScript* Script = ScriptForUsage(Data, Usage);
		if (!Script)
		{
			return false;
		}
		FNiagaraParameterHandle InputHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(InputName);
		FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, &Call);
		FNiagaraVariable InputVar(InputType, Aliased.GetParameterHandleString());
		FNiagaraVariable Rapid = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(
			InputVar, *UniqueEmitterName(Handle), Script->GetUsage());
		Rapid.SetValue(Value);
		return Script->RapidIterationParameters.SetParameterData(Rapid.GetData(), Rapid, true);
	}

	bool WriteListedInput(FNiagaraEmitterHandle& Handle, UNiagaraNodeFunctionCall& Call, ENiagaraScriptUsage Usage, const FNiagaraVariable& Var, const TSharedPtr<FJsonObject>& Args, FString& OutError)
	{
		const FNiagaraTypeDefinition Type = Var.GetType();
		const FName InputName(*ShortModuleInputName(Var));
		const bool bColorArgs = Args.IsValid() && (Args->HasField(TEXT("r")) || Args->HasField(TEXT("g")) || Args->HasField(TEXT("b")));
		const bool bVecArgs = Args.IsValid() && (Args->HasField(TEXT("x")) || Args->HasField(TEXT("y")) || Args->HasField(TEXT("z")));
		const bool bSizeArgs = Args.IsValid() && (Args->HasField(TEXT("size")) || Args->HasField(TEXT("sx")) || Args->HasField(TEXT("sy")));
		if (Type == FNiagaraTypeDefinition::GetFloatDef())
		{
			return WriteRapidInput(Handle, Call, Usage, InputName, Type, static_cast<float>(ArgNum(Args, TEXT("value"))));
		}
		if (Type == FNiagaraTypeDefinition::GetColorDef())
		{
			if (!bColorArgs)
			{
				OutError = FString::Printf(TEXT("Input %s is color; use r/g/b"), *InputName.ToString());
				return false;
			}
			return WriteRapidInput(Handle, Call, Usage, InputName, Type, ArgColor(Args));
		}
		if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef())
		{
			if (!bVecArgs)
			{
				OutError = FString::Printf(TEXT("Input %s is vec3; use x/y/z"), *InputName.ToString());
				return false;
			}
			const FVector3f V(
				static_cast<float>(ArgNum(Args, TEXT("x"))),
				static_cast<float>(ArgNum(Args, TEXT("y"))),
				static_cast<float>(ArgNum(Args, TEXT("z"))));
			if (Type == FNiagaraTypeDefinition::GetPositionDef())
			{
				return WriteRapidInput(Handle, Call, Usage, InputName, Type, FNiagaraPosition(V));
			}
			return WriteRapidInput(Handle, Call, Usage, InputName, Type, V);
		}
		if (Type == FNiagaraTypeDefinition::GetVec2Def())
		{
			if (!bSizeArgs && !bVecArgs)
			{
				OutError = FString::Printf(TEXT("Input %s is vec2; use size or sx/sy"), *InputName.ToString());
				return false;
			}
			const float Uniform = static_cast<float>(ArgNum(Args, TEXT("size"), ArgNum(Args, TEXT("value"))));
			const double SXDef = Args.IsValid() && Args->HasField(TEXT("x")) ? ArgNum(Args, TEXT("x")) : Uniform;
			const double SYDef = Args.IsValid() && Args->HasField(TEXT("y")) ? ArgNum(Args, TEXT("y")) : Uniform;
			const float SX = static_cast<float>(ArgNum(Args, TEXT("sx"), SXDef));
			const float SY = static_cast<float>(ArgNum(Args, TEXT("sy"), SYDef));
			return WriteRapidInput(Handle, Call, Usage, InputName, Type, FVector2f(SX, SY));
		}
		if (Type == FNiagaraTypeDefinition::GetIntDef())
		{
			return WriteRapidInput(Handle, Call, Usage, InputName, Type, ArgInt(Args, TEXT("value")));
		}
		OutError = FString::Printf(TEXT("Input %s type %s is not float/color/vec2/vec3/int"), *InputName.ToString(), *Type.GetName());
		return false;
	}

	bool SetFirstNamedInput(FNiagaraEmitterHandle& Handle, UNiagaraGraph* Graph, const FString& InputQuery, const TSharedPtr<FJsonObject>& Args, FString& OutLog)
	{
		if (!Graph)
		{
			return false;
		}
		static const ENiagaraScriptUsage Stacks[] = {
			ENiagaraScriptUsage::EmitterSpawnScript,
			ENiagaraScriptUsage::EmitterUpdateScript,
			ENiagaraScriptUsage::ParticleSpawnScript,
			ENiagaraScriptUsage::ParticleUpdateScript,
		};
		for (ENiagaraScriptUsage Usage : Stacks)
		{
			TArray<UNiagaraNodeFunctionCall*> Calls;
			CollectFunctionCalls(FindOutputNode(Graph, Usage), Calls);
			for (UNiagaraNodeFunctionCall* Call : Calls)
			{
				if (!Call)
				{
					continue;
				}
				TArray<FNiagaraVariable> Inputs;
				CollectModuleInputs(Call, Inputs);
				const FNiagaraVariable* Matched = FindListedModuleInput(Inputs, InputQuery);
				if (!Matched)
				{
					continue;
				}
				FString Error;
				if (!WriteListedInput(Handle, *Call, Usage, *Matched, Args, Error))
				{
					if (!Error.IsEmpty())
					{
						OutLog += Error + TEXT("\n");
					}
					return false;
				}
				OutLog += FString::Printf(TEXT("set %s.%s\n"), *Call->GetFunctionName(), *ShortModuleInputName(*Matched));
				return true;
			}
		}
		return false;
	}

	bool SetInputOnModule(
		FNiagaraEmitterHandle& Handle,
		UNiagaraGraph* Graph,
		const FString& ModuleQuery,
		const FString& StackHint,
		const FString& InputQuery,
		const TSharedPtr<FJsonObject>& Args,
		FString& OutLog)
	{
		UNiagaraNodeFunctionCall* Call = nullptr;
		ENiagaraScriptUsage Usage = ENiagaraScriptUsage::ParticleSpawnScript;
		FString Message;
		if (!ResolveModuleCall(Graph, ModuleQuery, StackHint, Call, Usage, Message) || !Call)
		{
			OutLog += ModuleQuery + TEXT(": ") + Message + TEXT("\n");
			return false;
		}
		TArray<FNiagaraVariable> Inputs;
		CollectModuleInputs(Call, Inputs);
		const FNiagaraVariable* Matched = FindListedModuleInput(Inputs, InputQuery);
		if (!Matched)
		{
			OutLog += FString::Printf(TEXT("missing %s.%s\n"), *ModuleQuery, *InputQuery);
			return false;
		}
		FString Error;
		if (!WriteListedInput(Handle, *Call, Usage, *Matched, Args, Error))
		{
			if (!Error.IsEmpty())
			{
				OutLog += Error + TEXT("\n");
			}
			return false;
		}
		OutLog += FString::Printf(TEXT("set %s.%s\n"), *Call->GetFunctionName(), *ShortModuleInputName(*Matched));
		return true;
	}

	TSharedRef<FJsonObject> JsonFloat(double Value)
	{
		TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetNumberField(TEXT("value"), Value);
		return Args;
	}

	TSharedRef<FJsonObject> JsonVec(double X, double Y, double Z)
	{
		TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetNumberField(TEXT("x"), X);
		Args->SetNumberField(TEXT("y"), Y);
		Args->SetNumberField(TEXT("z"), Z);
		return Args;
	}

	UNiagaraNodeFunctionCall* FindCallOnStacks(UNiagaraGraph* Graph, const FString& Query);

	bool SetFirstFloatOnModule(
		FNiagaraEmitterHandle& Handle,
		UNiagaraGraph* Graph,
		const FString& ModuleQuery,
		const FString& StackHint,
		const TCHAR* NameA,
		const TCHAR* NameB,
		const TCHAR* NameC,
		double Value,
		FString& OutLog)
	{
		const TCHAR* Names[] = { NameA, NameB, NameC };
		for (const TCHAR* Name : Names)
		{
			if (!Name || !*Name)
			{
				continue;
			}
			const int32 Before = OutLog.Len();
			if (SetInputOnModule(Handle, Graph, ModuleQuery, StackHint, Name, JsonFloat(Value), OutLog))
			{
				return true;
			}
			OutLog.LeftInline(Before);
		}
		return false;
	}

	void ApplyParticleSpread(
		UNiagaraSystem* Sys,
		FNiagaraEmitterHandle& Handle,
		UNiagaraGraph* Graph,
		float Radius,
		float UpSpeed,
		float GravityZ,
		FString& Log)
	{
		if (!Graph)
		{
			return;
		}
		UNiagaraNodeOutput* SpawnOut = FindOutputNode(Graph, ENiagaraScriptUsage::ParticleSpawnScript);
		UNiagaraNodeOutput* UpdateOut = FindOutputNode(Graph, ENiagaraScriptUsage::ParticleUpdateScript);
		SetInputOnModule(
			Handle, Graph, TEXT("InitializeParticle"), TEXT("particle_spawn"), TEXT("Velocity"),
			JsonVec(0.0, 0.0, UpSpeed), Log);

		if (SpawnOut && !FindCallOnStacks(Graph, TEXT("SphereLocation")))
		{
			TryAddLibraryModule(SpawnOut, TEXT("SphereLocation"));
		}
		SetFirstFloatOnModule(
			Handle, Graph, TEXT("SphereLocation"), TEXT("particle_spawn"),
			TEXT("Sphere Radius"), TEXT("SphereRadius"), TEXT("Radius"),
			Radius, Log);

		if (SpawnOut && !FindCallOnStacks(Graph, TEXT("AddVelocity")))
		{
			TryAddLibraryModule(SpawnOut, TEXT("AddVelocity"));
		}
		SetInputOnModule(
			Handle, Graph, TEXT("AddVelocity"), TEXT("particle_spawn"), TEXT("Velocity"),
			JsonVec(0.0, 0.0, UpSpeed), Log);
		SetFirstFloatOnModule(
			Handle, Graph, TEXT("AddVelocity"), TEXT("particle_spawn"),
			TEXT("Cone Angle"), TEXT("ConeAngle"), TEXT("Angle"),
			38.0, Log);
		SetFirstFloatOnModule(
			Handle, Graph, TEXT("AddVelocity"), TEXT("particle_spawn"),
			TEXT("Velocity Speed"), TEXT("Speed"), TEXT("Max Velocity Speed"),
			UpSpeed, Log);

		if (UpdateOut && !FindCallOnStacks(Graph, TEXT("GravityForce")))
		{
			TryAddLibraryModule(UpdateOut, TEXT("GravityForce"));
		}
		if (!SetInputOnModule(
			Handle, Graph, TEXT("GravityForce"), TEXT("particle_update"), TEXT("Gravity"),
			JsonVec(0.0, 0.0, GravityZ), Log))
		{
			SetFirstNamedInput(Handle, Graph, TEXT("Gravity"), JsonVec(0.0, 0.0, GravityZ), Log);
		}

		if (UNiagaraSpriteRendererProperties* Sprite = FindSpriteRenderer(Sys, FString()))
		{
			Sprite->Modify();
			Sprite->Alignment = ENiagaraSpriteAlignment::Unaligned;
			Sprite->FacingMode = ENiagaraSpriteFacingMode::FaceCamera;
			Log += TEXT("sprite unaligned camera\n");
		}
	}

	FString ParticleAttrName(const FString& Name)
	{
		if (Name.StartsWith(TEXT("Particles.")) || Name.StartsWith(TEXT("Emitter.")) || Name.StartsWith(TEXT("Engine.")))
		{
			return Name;
		}
		return TEXT("Particles.") + Name;
	}

	FNiagaraTypeDefinition TypeFromToken(const FString& Token)
	{
		const FString T = Token.Replace(TEXT(" "), TEXT("_")).ToLower();
		if (T.Contains(TEXT("color")))
		{
			return FNiagaraTypeDefinition::GetColorDef();
		}
		if (T.Contains(TEXT("vec2")) || T.Contains(TEXT("float2")))
		{
			return FNiagaraTypeDefinition::GetVec2Def();
		}
		if (T.Contains(TEXT("pos")))
		{
			return FNiagaraTypeDefinition::GetPositionDef();
		}
		if (T.Contains(TEXT("vec3")) || T.Contains(TEXT("float3")))
		{
			return FNiagaraTypeDefinition::GetVec3Def();
		}
		if (T.Contains(TEXT("int")) || T.Contains(TEXT("bool")))
		{
			return FNiagaraTypeDefinition::GetIntDef();
		}
		return FNiagaraTypeDefinition::GetFloatDef();
	}

	FNiagaraVariable ResolveParticleVariable(const FString& Name, const FString& TypeToken)
	{
		const FString Low = Name.Replace(TEXT(" "), TEXT("")).ToLower();
		if (Low.Contains(TEXT("lifetime")))
		{
			return SYS_PARAM_PARTICLES_LIFETIME;
		}
		if (Low.Contains(TEXT("spritesize")) || Low.Equals(TEXT("size")) || Low.Equals(TEXT("particles.size")))
		{
			return SYS_PARAM_PARTICLES_SPRITE_SIZE;
		}
		if (Low.Contains(TEXT("color")))
		{
			return SYS_PARAM_PARTICLES_COLOR;
		}
		if (Low.Contains(TEXT("velocity")))
		{
			return SYS_PARAM_PARTICLES_VELOCITY;
		}
		if (Low.Contains(TEXT("ribbonwidth")))
		{
			return SYS_PARAM_PARTICLES_RIBBONWIDTH;
		}
		if (Low.Contains(TEXT("ribbontwist")))
		{
			return SYS_PARAM_PARTICLES_RIBBONTWIST;
		}
		if (Low.Contains(TEXT("ribbonfacing")))
		{
			return SYS_PARAM_PARTICLES_RIBBONFACING;
		}
		if (Low.Contains(TEXT("scale")) && !Low.Contains(TEXT("uv")))
		{
			return SYS_PARAM_PARTICLES_SCALE;
		}
		if (Low.Contains(TEXT("subimage")))
		{
			return SYS_PARAM_PARTICLES_SUB_IMAGE_INDEX;
		}
		if (Low.Contains(TEXT("meshorient")))
		{
			return SYS_PARAM_PARTICLES_MESH_ORIENTATION;
		}
		FNiagaraTypeDefinition Type = TypeToken.IsEmpty()
			? FNiagaraTypeDefinition::GetFloatDef()
			: TypeFromToken(TypeToken);
		return FNiagaraVariable(Type, *ParticleAttrName(Name));
	}

	FString DefaultStringFor(const FNiagaraVariable& Var, const TSharedPtr<FJsonObject>& Args)
	{
		FString Known = FNiagaraConstants::GetAttributeDefaultValue(Var);
		const FNiagaraTypeDefinition Type = Var.GetType();
		if (Type == FNiagaraTypeDefinition::GetColorDef()
			&& Args.IsValid() && (Args->HasField(TEXT("r")) || Args->HasField(TEXT("g")) || Args->HasField(TEXT("b"))))
		{
			return ArgColor(Args).ToString();
		}
		if ((Type == FNiagaraTypeDefinition::GetVec2Def())
			&& Args.IsValid() && (Args->HasField(TEXT("size")) || Args->HasField(TEXT("sx")) || Args->HasField(TEXT("sy"))))
		{
			const float Uniform = static_cast<float>(ArgNum(Args, TEXT("size"), 50));
			const float SX = static_cast<float>(ArgNum(Args, TEXT("sx"), Uniform));
			const float SY = static_cast<float>(ArgNum(Args, TEXT("sy"), Uniform));
			return FString::Printf(TEXT("X=%g Y=%g"), SX, SY);
		}
		if ((Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef())
			&& Args.IsValid() && (Args->HasField(TEXT("x")) || Args->HasField(TEXT("y")) || Args->HasField(TEXT("z"))))
		{
			return FString::Printf(TEXT("%g,%g,%g"), ArgNum(Args, TEXT("x")), ArgNum(Args, TEXT("y")), ArgNum(Args, TEXT("z")));
		}
		if (Args.IsValid() && Args->HasField(TEXT("value")))
		{
			if (Type == FNiagaraTypeDefinition::GetVec2Def())
			{
				const float V = static_cast<float>(ArgNum(Args, TEXT("value")));
				return FString::Printf(TEXT("X=%g Y=%g"), V, V);
			}
			return FString::Printf(TEXT("%g"), ArgNum(Args, TEXT("value")));
		}
		if (!Known.IsEmpty())
		{
			return Known;
		}
		if (Type == FNiagaraTypeDefinition::GetColorDef())
		{
			return FLinearColor::White.ToString();
		}
		if (Type == FNiagaraTypeDefinition::GetVec2Def())
		{
			return TEXT("X=1.0 Y=1.0");
		}
		if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef())
		{
			return TEXT("0.0,0.0,0.0");
		}
		return TEXT("0.0");
	}

	bool ParseFloatKeys(const FString& Text, TArray<TPair<float, float>>& Out)
	{
		Out.Reset();
		FString Work = Text.Replace(TEXT("|"), TEXT(";"));
		TArray<FString> Parts;
		Work.ParseIntoArray(Parts, TEXT(";"), true);
		for (FString& Part : Parts)
		{
			Part.ReplaceInline(TEXT(":"), TEXT(","));
			TArray<FString> Nums;
			Part.ParseIntoArray(Nums, TEXT(","), true);
			if (Nums.Num() < 2)
			{
				continue;
			}
			Out.Add(TPair<float, float>(FCString::Atof(*Nums[0]), FCString::Atof(*Nums[1])));
		}
		return Out.Num() >= 2;
	}

	void ApplyFloatCurve(FRichCurve& Curve, const TArray<TPair<float, float>>& Keys)
	{
		Curve.Reset();
		for (const TPair<float, float>& Key : Keys)
		{
			Curve.AddKey(Key.Key, Key.Value);
		}
		Curve.AutoSetTangents(0.f);
	}

	UNiagaraNodeFunctionCall* FindCallOnStacks(UNiagaraGraph* Graph, const FString& Query)
	{
		if (!Graph)
		{
			return nullptr;
		}
		static const ENiagaraScriptUsage Stacks[] = {
			ENiagaraScriptUsage::EmitterSpawnScript,
			ENiagaraScriptUsage::EmitterUpdateScript,
			ENiagaraScriptUsage::ParticleSpawnScript,
			ENiagaraScriptUsage::ParticleUpdateScript,
		};
		TArray<UNiagaraNodeFunctionCall*> All;
		for (ENiagaraScriptUsage Usage : Stacks)
		{
			CollectFunctionCalls(FindOutputNode(Graph, Usage), All);
		}
		return FindModuleAmong(All, Query);
	}

	UNiagaraNodeAssignment* FindAssignmentFor(UNiagaraGraph* Graph, const FNiagaraVariable& Var)
	{
		if (!Graph)
		{
			return nullptr;
		}
		static const ENiagaraScriptUsage Stacks[] = {
			ENiagaraScriptUsage::ParticleSpawnScript,
			ENiagaraScriptUsage::ParticleUpdateScript,
			ENiagaraScriptUsage::EmitterSpawnScript,
			ENiagaraScriptUsage::EmitterUpdateScript,
		};
		for (ENiagaraScriptUsage Usage : Stacks)
		{
			TArray<UNiagaraNodeFunctionCall*> Calls;
			CollectFunctionCalls(FindOutputNode(Graph, Usage), Calls);
			for (UNiagaraNodeFunctionCall* Call : Calls)
			{
				if (UNiagaraNodeAssignment* Assign = Cast<UNiagaraNodeAssignment>(Call))
				{
					if (Assign->FindAssignmentTarget(Var.GetName()) != INDEX_NONE)
					{
						return Assign;
					}
				}
			}
		}
		return nullptr;
	}

	bool SetOrAddAttribute(FNiagaraEmitterHandle& Handle, const FNiagaraVariable& Var, const FString& Default, const FString& StackHint, FString& OutLog)
	{
		UNiagaraGraph* Graph = EmitterGraph(Handle.GetEmitterData());
		if (!Graph)
		{
			OutLog = TEXT("Emitter graph missing");
			return false;
		}
		if (UNiagaraNodeAssignment* Existing = FindAssignmentFor(Graph, Var))
		{
			const int32 Idx = Existing->FindAssignmentTarget(Var.GetName());
			Existing->Modify();
			Existing->SetAssignmentTarget(Idx, Var, &Default);
			Existing->RefreshFromExternalChanges();
			OutLog = TEXT("Updated attribute ") + Var.GetName().ToString();
			return true;
		}
		const ENiagaraScriptUsage Usage = UsageFromStack(StackHint.IsEmpty() ? TEXT("particle_spawn") : StackHint);
		UNiagaraNodeOutput* Output = FindOutputNode(Graph, Usage);
		if (!Output)
		{
			OutLog = TEXT("Stack output missing");
			return false;
		}
		TArray<FNiagaraVariable> Vars;
		TArray<FString> Defaults;
		Vars.Add(Var);
		Defaults.Add(Default);
		UNiagaraNodeAssignment* Added = FNiagaraStackGraphUtilities::AddParameterModuleToStack(Vars, *Output, INDEX_NONE, Defaults);
		if (!Added)
		{
			OutLog = TEXT("Add attribute failed");
			return false;
		}
		OutLog = TEXT("Added attribute ") + Var.GetName().ToString();
		return true;
	}

	UNiagaraDataInterface* ExistingOverrideDI(UNiagaraNodeFunctionCall& Call, const FNiagaraVariable& InputVar)
	{
		const FNiagaraParameterHandle ModuleHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(*ShortModuleInputName(InputVar));
		const FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(ModuleHandle, &Call);
		UEdGraphPin& Pin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
			Call, Aliased, InputVar.GetType(), FGuid(), FGuid());
		if (Pin.LinkedTo.Num() == 0)
		{
			return nullptr;
		}
		if (UNiagaraNodeInput* Input = Cast<UNiagaraNodeInput>(Pin.LinkedTo[0]->GetOwningNode()))
			{
				return GetInputNodeDataInterface(Input);
			}
		return nullptr;
	}

	UNiagaraDataInterface* EnsureCurveDI(UNiagaraNodeFunctionCall& Call, const FNiagaraVariable& InputVar, UClass* CurveClass, FString& OutError)
	{
		if (UNiagaraDataInterface* Existing = ExistingOverrideDI(Call, InputVar))
		{
			return Existing;
		}
		const FNiagaraParameterHandle ModuleHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(*ShortModuleInputName(InputVar));
		const FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(ModuleHandle, &Call);
		UEdGraphPin& Pin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
			Call, Aliased, InputVar.GetType(), FGuid(), FGuid());
		if (Pin.LinkedTo.Num() > 0)
		{
			OutError = TEXT("Input already bound");
			return nullptr;
		}
		UNiagaraDataInterface* Created = nullptr;
		FNiagaraStackGraphUtilities::SetDataInterfaceValueForFunctionInput(
			Pin, CurveClass, Aliased.GetParameterHandleString().ToString(), Created);
		if (!Created)
		{
			OutError = TEXT("Create curve DI failed");
		}
		return Created;
	}

	bool WriteCurveKeysToDI(UNiagaraDataInterface* DI, const TArray<TPair<float, float>>& Keys, FString& OutError)
	{
		if (UNiagaraDataInterfaceCurve* Curve = Cast<UNiagaraDataInterfaceCurve>(DI))
		{
			Curve->Modify();
			ApplyFloatCurve(Curve->Curve, Keys);
			return true;
		}
		if (UNiagaraDataInterfaceVector2DCurve* Curve2 = Cast<UNiagaraDataInterfaceVector2DCurve>(DI))
		{
			Curve2->Modify();
			ApplyFloatCurve(Curve2->XCurve, Keys);
			ApplyFloatCurve(Curve2->YCurve, Keys);
			return true;
		}
		if (UNiagaraDataInterfaceColorCurve* Color = Cast<UNiagaraDataInterfaceColorCurve>(DI))
		{
			Color->Modify();
			ApplyFloatCurve(Color->RedCurve, Keys);
			ApplyFloatCurve(Color->GreenCurve, Keys);
			ApplyFloatCurve(Color->BlueCurve, Keys);
			ApplyFloatCurve(Color->AlphaCurve, Keys);
			return true;
		}
		OutError = TEXT("Input is not a float/vec2/color curve");
		return false;
	}

	bool ApplyCurveToModule(UNiagaraNodeFunctionCall& Call, const FString& InputQuery, const TArray<TPair<float, float>>& Keys, FString& OutLog)
	{
		TArray<FNiagaraVariable> Inputs;
		CollectModuleDiInputs(&Call, Inputs);
		for (const FNiagaraVariable& Var : Inputs)
		{
			if (!Var.IsDataInterface())
			{
				continue;
			}
			if (!InputQuery.IsEmpty()
				&& !ShortModuleInputName(Var).Equals(InputQuery, ESearchCase::IgnoreCase)
				&& !Var.GetName().ToString().Contains(InputQuery))
			{
				continue;
			}
			UClass* Class = Var.GetType().GetClass();
			if (!Class || !Class->IsChildOf(UNiagaraDataInterfaceCurveBase::StaticClass()))
			{
				continue;
			}
			FString Error;
			UNiagaraDataInterface* DI = EnsureCurveDI(Call, Var, Class, Error);
			if (!DI)
			{
				OutLog += Error;
				return false;
			}
			if (!WriteCurveKeysToDI(DI, Keys, Error))
			{
				OutLog += Error;
				return false;
			}
			OutLog += TEXT("curve ") + Call.GetFunctionName() + TEXT(".") + ShortModuleInputName(Var);
			return true;
		}
		OutLog += TEXT("No curve data-interface input on ") + Call.GetFunctionName();
		return false;
	}

	UNiagaraMeshRendererProperties* FindMeshRenderer(UNiagaraSystem* Sys, const FString& EmitterName)
	{
		FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, EmitterName);
		FVersionedNiagaraEmitterData* Data = Handle ? Handle->GetEmitterData() : nullptr;
		if (!Data)
		{
			return nullptr;
		}
		for (UNiagaraRendererProperties* R : Data->GetRenderers())
		{
			if (UNiagaraMeshRendererProperties* Mesh = Cast<UNiagaraMeshRendererProperties>(R))
			{
				return Mesh;
			}
		}
		return nullptr;
	}

	UNiagaraRibbonRendererProperties* FindRibbonRenderer(UNiagaraSystem* Sys, const FString& EmitterName)
	{
		FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, EmitterName);
		FVersionedNiagaraEmitterData* Data = Handle ? Handle->GetEmitterData() : nullptr;
		if (!Data)
		{
			return nullptr;
		}
		for (UNiagaraRendererProperties* R : Data->GetRenderers())
		{
			if (UNiagaraRibbonRendererProperties* Ribbon = Cast<UNiagaraRibbonRendererProperties>(R))
			{
				return Ribbon;
			}
		}
		return nullptr;
	}

	void DisableSpriteRenderers(UNiagaraSystem* Sys)
	{
		if (!Sys)
		{
			return;
		}
		for (FNiagaraEmitterHandle& Handle : Sys->GetEmitterHandles())
		{
			FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			if (!Data)
			{
				continue;
			}
			for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
			{
				UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer);
				if (Sprite && Sprite->GetIsEnabled())
				{
					Sprite->Modify();
					Sprite->SetIsEnabled(false);
				}
			}
		}
	}

	void EnsureRibbonBindings(FNiagaraEmitterHandle& Handle, FString& OutLog)
	{
		UNiagaraGraph* Graph = EmitterGraph(Handle.GetEmitterData());
		UNiagaraNodeOutput* Spawn = Graph ? FindOutputNode(Graph, ENiagaraScriptUsage::ParticleSpawnScript) : nullptr;
		if (Spawn && !FindCallOnStacks(Graph, TEXT("InitializeRibbon")))
		{
			if (TryAddLibraryModule(Spawn, TEXT("InitializeRibbon")))
			{
				OutLog += TEXT("added InitializeRibbon\n");
			}
		}
		FString AttrLog;
		SetOrAddAttribute(Handle, SYS_PARAM_PARTICLES_RIBBONID, TEXT("0"), TEXT("particle_spawn"), AttrLog);
		OutLog += AttrLog + TEXT("\n");
		SetOrAddAttribute(Handle, SYS_PARAM_PARTICLES_RIBBONLINKORDER, TEXT("0"), TEXT("particle_spawn"), AttrLog);
		OutLog += AttrLog + TEXT("\n");
		SetOrAddAttribute(Handle, SYS_PARAM_PARTICLES_RIBBONWIDTH, TEXT("12"), TEXT("particle_spawn"), AttrLog);
		OutLog += AttrLog + TEXT("\n");
	}

	void OrderedStackModules(UNiagaraNodeOutput* Output, TArray<UNiagaraNodeFunctionCall*>& Out)
	{
		Out.Reset();
		if (!Output)
		{
			return;
		}
		UNiagaraNode* Previous = Output;
		while (Previous)
		{
			UEdGraphPin* InPin = NodeParamMapPin(Previous, EGPD_Input);
			if (InPin && InPin->LinkedTo.Num() == 1 && InPin->LinkedTo[0])
			{
				UNiagaraNode* Current = Cast<UNiagaraNode>(InPin->LinkedTo[0]->GetOwningNode());
				if (UNiagaraNodeFunctionCall* Module = Cast<UNiagaraNodeFunctionCall>(Current))
				{
					Out.Insert(Module, 0);
				}
				Previous = Current;
			}
			else
			{
				Previous = nullptr;
			}
		}
	}

	bool DependencyAppliesToUsage(const FNiagaraModuleDependency& Dep, ENiagaraScriptUsage Usage)
	{
		ENiagaraModuleDependencyUsage Slot = ENiagaraModuleDependencyUsage::Update;
		switch (Usage)
		{
		case ENiagaraScriptUsage::ParticleSpawnScript:
		case ENiagaraScriptUsage::ParticleSpawnScriptInterpolated:
		case ENiagaraScriptUsage::EmitterSpawnScript:
		case ENiagaraScriptUsage::SystemSpawnScript:
			Slot = ENiagaraModuleDependencyUsage::Spawn;
			break;
		case ENiagaraScriptUsage::ParticleEventScript:
			Slot = ENiagaraModuleDependencyUsage::Event;
			break;
		case ENiagaraScriptUsage::ParticleSimulationStageScript:
			Slot = ENiagaraModuleDependencyUsage::SimulationStage;
			break;
		default:
			break;
		}
		return (Dep.OnlyEvaluateInScriptUsage & (1 << static_cast<int32>(Slot))) != 0;
	}

	bool ModuleProvidesDependency(UNiagaraNodeFunctionCall* Call, FName Id)
	{
		if (!Call)
		{
			return false;
		}
		if (const FVersionedNiagaraScriptData* Data = Call->GetScriptData())
		{
			return Data->ProvidedDependencies.Contains(Id);
		}
		return false;
	}

	bool FindModuleGroupPins(
		UNiagaraNodeFunctionCall& Call,
		UEdGraphPin*& OutPrevOut,
		UEdGraphPin*& OutNextIn,
		UEdGraphPin*& OutGroupIn)
	{
		UEdGraphPin* InMap = NodeParamMapPin(&Call, EGPD_Input);
		UEdGraphPin* OutMap = NodeParamMapPin(&Call, EGPD_Output);
		if (!InMap || !OutMap || OutMap->LinkedTo.Num() != 1 || !OutMap->LinkedTo[0])
		{
			return false;
		}
		OutNextIn = OutMap->LinkedTo[0];
		UEdGraphPin* ChainIn = InMap->LinkedTo.Num() == 1 ? InMap->LinkedTo[0] : nullptr;
		UEdGraphNode* MaybeSet = ChainIn ? ChainIn->GetOwningNode() : nullptr;
		if (NodeClassNamed(MaybeSet, TEXT("NiagaraNodeParameterMapSet")))
		{
			UEdGraphPin* SetIn = NodeParamMapPin(Cast<UNiagaraNode>(MaybeSet), EGPD_Input);
			OutPrevOut = (SetIn && SetIn->LinkedTo.Num() == 1) ? SetIn->LinkedTo[0] : nullptr;
			OutGroupIn = SetIn;
		}
		else
		{
			OutPrevOut = ChainIn;
			OutGroupIn = InMap;
		}
		return OutPrevOut && OutNextIn && OutGroupIn;
	}

	bool MoveStackModuleBefore(UNiagaraNodeFunctionCall& MoveCall, UNiagaraNodeFunctionCall& BeforeCall)
	{
		if (&MoveCall == &BeforeCall)
		{
			return false;
		}
		UEdGraphPin* MovePrev = nullptr;
		UEdGraphPin* MoveNext = nullptr;
		UEdGraphPin* MoveGroupIn = nullptr;
		UEdGraphPin* BeforePrev = nullptr;
		UEdGraphPin* BeforeNext = nullptr;
		UEdGraphPin* BeforeGroupIn = nullptr;
		if (!FindModuleGroupPins(MoveCall, MovePrev, MoveNext, MoveGroupIn)
			|| !FindModuleGroupPins(BeforeCall, BeforePrev, BeforeNext, BeforeGroupIn))
		{
			return false;
		}
		(void)BeforeNext;
		UNiagaraGraph* Graph = MoveCall.GetNiagaraGraph();
		if (!Graph)
		{
			return false;
		}
		Graph->Modify();
		UEdGraphPin* MoveOut = NodeParamMapPin(&MoveCall, EGPD_Output);
		MoveGroupIn->BreakAllPinLinks();
		if (MoveOut)
		{
			MoveOut->BreakAllPinLinks();
		}
		if (MovePrev && MoveNext && MovePrev != MoveNext)
		{
			MovePrev->MakeLinkTo(MoveNext);
		}
		BeforeGroupIn->BreakAllPinLinks();
		if (BeforePrev)
		{
			BeforePrev->MakeLinkTo(MoveGroupIn);
		}
		if (MoveOut)
		{
			MoveOut->MakeLinkTo(BeforeGroupIn);
		}
		Graph->NotifyGraphChanged();
		return true;
	}

	void FindScriptsProvidingDependency(
		FName DepId,
		ENiagaraScriptUsage Usage,
		TArray<FAssetData>& Out)
	{
		FNiagaraEditorUtilities::FGetFilteredScriptAssetsOptions Filter;
		Filter.ScriptUsageToInclude = ENiagaraScriptUsage::Module;
		Filter.TargetUsageToMatch = Usage;
		Filter.bIncludeDeprecatedScripts = false;
		Filter.bIncludeNonLibraryScripts = false;
		TArray<FAssetData> Assets;
		FNiagaraEditorUtilities::GetFilteredScriptAssets(Filter, Assets);
		const FString Want = DepId.ToString();
		for (const FAssetData& Asset : Assets)
		{
			FString Provided;
			if (!Asset.GetTagValue(
					GET_MEMBER_NAME_CHECKED(FVersionedNiagaraScriptData, ProvidedDependencies), Provided)
				|| Provided.IsEmpty())
			{
				continue;
			}
			TArray<FString> Parts;
			Provided.ParseIntoArray(Parts, TEXT(","));
			for (FString& Part : Parts)
			{
				Part.TrimStartAndEndInline();
				if (Part == Want)
				{
					Out.Add(Asset);
					break;
				}
			}
		}
	}

	void ForEachEmitterStack(
		UNiagaraSystem* Sys,
		TFunctionRef<void(FNiagaraEmitterHandle&, UNiagaraNodeOutput*)> Fn)
	{
		if (!Sys)
		{
			return;
		}
		static const ENiagaraScriptUsage Stacks[] = {
			ENiagaraScriptUsage::EmitterSpawnScript,
			ENiagaraScriptUsage::EmitterUpdateScript,
			ENiagaraScriptUsage::ParticleSpawnScript,
			ENiagaraScriptUsage::ParticleUpdateScript,
		};
		for (FNiagaraEmitterHandle& Handle : Sys->GetEmitterHandles())
		{
			FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			UNiagaraGraph* Graph = EmitterGraph(Data);
			if (!Graph)
			{
				continue;
			}
			for (ENiagaraScriptUsage Usage : Stacks)
			{
				if (UNiagaraNodeOutput* Output = FindOutputNode(Graph, Usage))
				{
					Fn(Handle, Output);
				}
			}
			if (!Data)
			{
				continue;
			}
			for (UNiagaraSimulationStageBase* Stage : Data->GetSimulationStages())
			{
				if (!Stage || !Stage->Script)
				{
					continue;
				}
				if (UNiagaraNodeOutput* StageOut = FindOutputNode(
						Graph, ENiagaraScriptUsage::ParticleSimulationStageScript, Stage->Script->GetUsageId()))
				{
					Fn(Handle, StageOut);
				}
			}
		}
	}

	bool OutputProvidesDependency(UNiagaraNodeOutput* Output, FName Id)
	{
		if (!Output || Id.IsNone())
		{
			return false;
		}
		TArray<UNiagaraNodeFunctionCall*> Ordered;
		OrderedStackModules(Output, Ordered);
		for (UNiagaraNodeFunctionCall* Call : Ordered)
		{
			if (ModuleProvidesDependency(Call, Id))
			{
				return true;
			}
		}
		return false;
	}

	UNiagaraNodeOutput* SystemStackOutput(UNiagaraSystem* Sys, ENiagaraScriptUsage Usage)
	{
		if (!Sys)
		{
			return nullptr;
		}
		UNiagaraScript* Script = nullptr;
		if (Usage == ENiagaraScriptUsage::SystemSpawnScript)
		{
			Script = Sys->GetSystemSpawnScript();
		}
		else if (Usage == ENiagaraScriptUsage::SystemUpdateScript)
		{
			Script = Sys->GetSystemUpdateScript();
		}
		if (!Script)
		{
			return nullptr;
		}
		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
		return Source ? FindOutputNode(Source->NodeGraph, Usage) : nullptr;
	}

	// EmitterState's SystemState dependency is AllScripts: the provider lives on the
	// system spawn/update scripts, not on the emitter stack that reports it.
	bool DependencyProvidedOnOtherScript(UNiagaraSystem* Sys, UNiagaraNodeOutput* Self, FName Id)
	{
		if (!Sys)
		{
			return false;
		}
		const ENiagaraScriptUsage SystemUsages[] = {
			ENiagaraScriptUsage::SystemSpawnScript,
			ENiagaraScriptUsage::SystemUpdateScript,
		};
		for (ENiagaraScriptUsage Usage : SystemUsages)
		{
			UNiagaraNodeOutput* Output = SystemStackOutput(Sys, Usage);
			if (Output && Output != Self && OutputProvidesDependency(Output, Id))
			{
				return true;
			}
		}
		bool bFound = false;
		ForEachEmitterStack(Sys, [&](FNiagaraEmitterHandle&, UNiagaraNodeOutput* Output)
		{
			if (!bFound && Output != Self && OutputProvidesDependency(Output, Id))
			{
				bFound = true;
			}
		});
		return bFound;
	}

	bool DependencyCanBeSatisfiedAcrossScripts(const FNiagaraModuleDependency& Dep)
	{
		return Dep.ScriptConstraint == ENiagaraModuleDependencyScriptConstraint::AllScripts
			|| Dep.Id == FName(TEXT("SystemState"));
	}

	int32 EnsureDependencyOnSystemScripts(UNiagaraSystem* Sys, FName DepId, FString& Log)
	{
		int32 Added = 0;
		if (!Sys || DepId.IsNone())
		{
			return 0;
		}
		const ENiagaraScriptUsage Usages[] = {
			ENiagaraScriptUsage::SystemSpawnScript,
			ENiagaraScriptUsage::SystemUpdateScript,
		};
		for (ENiagaraScriptUsage Usage : Usages)
		{
			UNiagaraNodeOutput* Output = SystemStackOutput(Sys, Usage);
			if (!Output || OutputProvidesDependency(Output, DepId))
			{
				continue;
			}
			TArray<FAssetData> Assets;
			FindScriptsProvidingDependency(DepId, Usage, Assets);
			UNiagaraScript* Module = nullptr;
			const FString Want = DepId.ToString();
			for (const FAssetData& Asset : Assets)
			{
				if (Asset.AssetName.ToString().Contains(Want))
				{
					Module = Cast<UNiagaraScript>(Asset.GetAsset());
					break;
				}
			}
			if (!Module && Assets.Num() > 0)
			{
				Module = Cast<UNiagaraScript>(Assets[0].GetAsset());
			}
			if (!Module && DepId == FName(TEXT("SystemState")))
			{
				Module = LoadObject<UNiagaraScript>(
					nullptr, TEXT("/Niagara/Modules/System/SystemState.SystemState"));
			}
			UNiagaraNodeFunctionCall* Node = Module
				? FNiagaraStackGraphUtilities::AddScriptModuleToStack(Module, *Output)
				: nullptr;
			if (Node)
			{
				Log += FString::Printf(
					TEXT("added %s on %s for %s\n"),
					*Node->GetFunctionName(),
					UsageLabel(Usage),
					*DepId.ToString());
				++Added;
			}
		}
		return Added;
	}

	void CollectUnmetStackDependencies(
		UNiagaraSystem* Sys,
		UNiagaraNodeOutput* Output,
		TArray<FString>& OutLines)
	{
		TArray<UNiagaraNodeFunctionCall*> Ordered;
		OrderedStackModules(Output, Ordered);
		for (int32 I = 0; I < Ordered.Num(); ++I)
		{
			UNiagaraNodeFunctionCall* Call = Ordered[I];
			const FVersionedNiagaraScriptData* ScriptData = Call ? Call->GetScriptData() : nullptr;
			if (!ScriptData)
			{
				continue;
			}
			for (const FNiagaraModuleDependency& Dep : ScriptData->RequiredDependencies)
			{
				if (!DependencyAppliesToUsage(Dep, Output->GetUsage()))
				{
					continue;
				}
				TArray<int32> Providers;
				for (int32 J = 0; J < Ordered.Num(); ++J)
				{
					if (J != I && ModuleProvidesDependency(Ordered[J], Dep.Id))
					{
						Providers.Add(J);
					}
				}
				bool bMet = false;
				if (Dep.Type == ENiagaraModuleDependencyType::PostDependency)
				{
					for (int32 P : Providers)
					{
						if (P > I)
						{
							bMet = true;
							break;
						}
					}
				}
				else
				{
					for (int32 P : Providers)
					{
						if (P < I)
						{
							bMet = true;
							break;
						}
					}
				}
				if (!bMet
					&& DependencyCanBeSatisfiedAcrossScripts(Dep)
					&& DependencyProvidedOnOtherScript(Sys, Output, Dep.Id))
				{
					bMet = true;
				}
				if (!bMet)
				{
					OutLines.Add(FString::Printf(
						TEXT("%s: %s %s unmet"),
						*Call->GetFunctionName(),
						Dep.Type == ENiagaraModuleDependencyType::PostDependency
							? TEXT("post-dependency")
							: TEXT("pre-dependency"),
						*Dep.Id.ToString()));
				}
			}
		}
	}

	int32 HealNiagaraStackIssues(UNiagaraSystem* Sys, FString& Log)
	{
		int32 Fixed = 0;
		if (!Sys)
		{
			return 0;
		}
		TSet<FName> EnsuredOnSystem;
		ForEachEmitterStack(Sys, [&](FNiagaraEmitterHandle&, UNiagaraNodeOutput* Output)
		{
			for (int32 Pass = 0; Pass < 8; ++Pass)
			{
				TArray<UNiagaraNodeFunctionCall*> Ordered;
				OrderedStackModules(Output, Ordered);
				bool bDid = false;
				for (int32 I = 0; I < Ordered.Num() && !bDid; ++I)
				{
					UNiagaraNodeFunctionCall* Call = Ordered[I];
					const FVersionedNiagaraScriptData* ScriptData = Call ? Call->GetScriptData() : nullptr;
					if (!ScriptData)
					{
						continue;
					}
					for (const FNiagaraModuleDependency& Dep : ScriptData->RequiredDependencies)
					{
						if (!DependencyAppliesToUsage(Dep, Output->GetUsage()))
						{
							continue;
						}
						TArray<int32> Providers;
						for (int32 J = 0; J < Ordered.Num(); ++J)
						{
							if (J != I && ModuleProvidesDependency(Ordered[J], Dep.Id))
							{
								Providers.Add(J);
							}
						}
						bool bMet = false;
						if (Dep.Type == ENiagaraModuleDependencyType::PostDependency)
						{
							for (int32 P : Providers)
							{
								if (P > I)
								{
									bMet = true;
									break;
								}
							}
						}
						else
						{
							for (int32 P : Providers)
							{
								if (P < I)
								{
									bMet = true;
									break;
								}
							}
						}
						if (!bMet
							&& DependencyCanBeSatisfiedAcrossScripts(Dep)
							&& DependencyProvidedOnOtherScript(Sys, Output, Dep.Id))
						{
							bMet = true;
						}
						if (bMet)
						{
							continue;
						}
						if (DependencyCanBeSatisfiedAcrossScripts(Dep) && !EnsuredOnSystem.Contains(Dep.Id))
						{
							EnsuredOnSystem.Add(Dep.Id);
							const int32 AddedOnSystem = EnsureDependencyOnSystemScripts(Sys, Dep.Id, Log);
							if (AddedOnSystem > 0)
							{
								Fixed += AddedOnSystem;
								bDid = true;
								break;
							}
						}
						if (Providers.Num() > 0)
						{
							const int32 ProviderIndex =
								Dep.Type == ENiagaraModuleDependencyType::PostDependency
									? Providers[0]
									: Providers.Last();
							UNiagaraNodeFunctionCall* Provider = Ordered[ProviderIndex];
							bool bMoved = false;
							if (Dep.Type == ENiagaraModuleDependencyType::PostDependency)
							{
								bMoved = MoveStackModuleBefore(*Call, *Provider);
							}
							else
							{
								bMoved = MoveStackModuleBefore(*Provider, *Call);
							}
							if (bMoved)
							{
								Log += FString::Printf(
									TEXT("fixed %s %s %s\n"),
									*Call->GetFunctionName(),
									Dep.Type == ENiagaraModuleDependencyType::PostDependency
										? TEXT("before")
										: TEXT("after"),
									*Provider->GetFunctionName());
								++Fixed;
								bDid = true;
								break;
							}
						}
						TArray<FAssetData> Assets;
						FindScriptsProvidingDependency(Dep.Id, Output->GetUsage(), Assets);
						if (Assets.Num() == 0)
						{
							continue;
						}
						FAssetData Pick = Assets[0];
						const FString Want = Dep.Id.ToString();
						for (const FAssetData& Asset : Assets)
						{
							if (Asset.AssetName.ToString().Contains(Want))
							{
								Pick = Asset;
								break;
							}
						}
						UNiagaraScript* Module = Cast<UNiagaraScript>(Pick.GetAsset());
						const int32 InsertIndex =
							Dep.Type == ENiagaraModuleDependencyType::PostDependency ? I + 1 : I;
						UNiagaraNodeFunctionCall* Added = Module
							? FNiagaraStackGraphUtilities::AddScriptModuleToStack(Module, *Output, InsertIndex)
							: nullptr;
						if (Added)
						{
							Log += FString::Printf(
								TEXT("added %s for %s %s\n"),
								*Added->GetFunctionName(),
								*Call->GetFunctionName(),
								*Dep.Id.ToString());
							++Fixed;
							bDid = true;
							break;
						}
					}
				}
				if (!bDid)
				{
					break;
				}
			}
		});
		return Fixed;
	}

	int32 ApplyFixableRendererFeedback(UNiagaraSystem* Sys)
	{
		int32 Fixed = 0;
		if (!Sys)
		{
			return 0;
		}
		for (FNiagaraEmitterHandle& Handle : Sys->GetEmitterHandles())
		{
			const FVersionedNiagaraEmitter Versioned = Handle.GetInstance();
			FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			if (!Data)
			{
				continue;
			}
			for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
			{
				if (!Renderer)
				{
					continue;
				}
				TArray<FNiagaraRendererFeedback> Errors;
				TArray<FNiagaraRendererFeedback> Warnings;
				TArray<FNiagaraRendererFeedback> Infos;
				Renderer->GetRendererFeedback(Versioned, Errors, Warnings, Infos);
				auto FixItems = [&Fixed](const TArray<FNiagaraRendererFeedback>& Items)
				{
					for (const FNiagaraRendererFeedback& Item : Items)
					{
						if (Item.IsFixable())
						{
							Item.TryFix();
							++Fixed;
						}
					}
				};
				FixItems(Errors);
				FixItems(Warnings);
			}
		}
		return Fixed;
	}

	void CollectRendererIssues(UNiagaraSystem* Sys, TArray<FString>& Errors, TArray<FString>& Warnings)
	{
		if (!Sys)
		{
			return;
		}
		for (FNiagaraEmitterHandle& Handle : Sys->GetEmitterHandles())
		{
			const FVersionedNiagaraEmitter Versioned = Handle.GetInstance();
			FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			if (!Data)
			{
				continue;
			}
			for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
			{
				if (!Renderer || !Renderer->GetIsEnabled())
				{
					continue;
				}
				TArray<FNiagaraRendererFeedback> FeedbackErrors;
				TArray<FNiagaraRendererFeedback> FeedbackWarnings;
				TArray<FNiagaraRendererFeedback> Infos;
				Renderer->GetRendererFeedback(Versioned, FeedbackErrors, FeedbackWarnings, Infos);
				for (const FNiagaraRendererFeedback& Item : FeedbackErrors)
				{
					Errors.Add(Renderer->GetName() + TEXT(": ") + Item.GetSummaryText().ToString());
				}
				for (const FNiagaraRendererFeedback& Item : FeedbackWarnings)
				{
					Warnings.Add(Renderer->GetName() + TEXT(": ") + Item.GetSummaryText().ToString());
				}
			}
		}
	}

	UNiagaraSystem* LoadOrFailSystem(const FString& Path, FString& Message)
	{
		UNiagaraSystem* Sys = LoadObject<UNiagaraSystem>(nullptr, *Path);
		if (!Sys)
		{
			Message = TEXT("Niagara system not found");
		}
		return Sys;
	}

	void CollectNiagaraIssues(UNiagaraSystem* Sys, TArray<FString>& Errors, TArray<FString>& Warnings)
	{
		Errors.Reset();
		Warnings.Reset();
		if (!Sys)
		{
			return;
		}
		AppendScriptCompileLog(Sys->GetSystemSpawnScript(), TEXT("system_spawn"), Errors, Warnings);
		AppendScriptCompileLog(Sys->GetSystemUpdateScript(), TEXT("system_update"), Errors, Warnings);
		for (const FNiagaraEmitterHandle& Handle : Sys->GetEmitterHandles())
		{
			FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			if (!Data)
			{
				continue;
			}
			TArray<UNiagaraScript*> Scripts;
			Data->GetScripts(Scripts, false, false);
			for (UNiagaraScript* Script : Scripts)
			{
				if (!Script)
				{
					continue;
				}
				AppendScriptCompileLog(
					Script,
					Handle.GetName().ToString() + TEXT("/") + UsageLabel(Script->GetUsage()),
					Errors,
					Warnings);
			}
		}
		CollectRendererIssues(Sys, Errors, Warnings);
		ForEachEmitterStack(Sys, [&](FNiagaraEmitterHandle& Handle, UNiagaraNodeOutput* Output)
		{
			TArray<FString> StackLines;
			CollectUnmetStackDependencies(Sys, Output, StackLines);
			for (const FString& Line : StackLines)
			{
				Errors.Add(Handle.GetName().ToString() + TEXT("/") + UsageLabel(Output->GetUsage()) + TEXT("/") + Line);
			}
		});
	}

	FString FormatNiagaraCompileLog(UNiagaraSystem* Sys, const TArray<FString>& Errors, const TArray<FString>& Warnings)
	{
		FString Out;
		if (Errors.Num() > 0)
		{
			Out = TEXT("Compiled FAILED\n");
			const int32 MaxN = FMath::Min(Errors.Num(), 20);
			for (int32 I = 0; I < MaxN; ++I)
			{
				Out += TEXT("error: ") + Errors[I] + TEXT("\n");
			}
			if (Errors.Num() > MaxN)
			{
				Out += TEXT("...\n");
			}
			return Out.TrimEnd();
		}
		if (!Sys || !Sys->IsReadyToRun())
		{
			Out = TEXT("Compiled not ready to run\n");
			if (Sys && Sys->HasOutstandingCompilationRequests(true))
			{
				Out += TEXT("outstanding compilation (incl GPU)\n");
			}
			if (Sys)
			{
				for (const FNiagaraEmitterHandle& Handle : Sys->GetEmitterHandles())
				{
					FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
					UNiagaraScript* Gpu = Data ? Data->GetGPUComputeScript() : nullptr;
					if (Gpu && Gpu->IsScriptCompilationPending(true))
					{
						Out += Handle.GetName().ToString() + TEXT(": GPU shader pending\n");
					}
					else if (Gpu && !Gpu->DidScriptCompilationSucceed(true))
					{
						Out += Handle.GetName().ToString() + TEXT(": GPU shader failed\n");
					}
				}
			}
		}
		else
		{
			Out = TEXT("Compiled ok\n");
		}
		const int32 MaxW = FMath::Min(Warnings.Num(), 12);
		for (int32 I = 0; I < MaxW; ++I)
		{
			Out += TEXT("warning: ") + Warnings[I] + TEXT("\n");
		}
		return Out.TrimEnd();
	}

	FString CompileNiagaraSystem(UNiagaraSystem* Sys)
	{
		if (!Sys)
		{
			return TEXT("No system");
		}
		EditorPromptCompileAndWait(Sys);
		FString HealLog;
		int32 Fixed = ApplyFixableRendererFeedback(Sys);
		Fixed += HealNiagaraStackIssues(Sys, HealLog);
		if (Fixed > 0)
		{
			Sys->MarkPackageDirty();
			EditorPromptCompileAndWait(Sys);
			Fixed += ApplyFixableRendererFeedback(Sys);
			Fixed += HealNiagaraStackIssues(Sys, HealLog);
			if (Fixed > 0)
			{
				Sys->MarkPackageDirty();
				EditorPromptCompileAndWait(Sys);
			}
		}
		TArray<FString> Errors;
		TArray<FString> Warnings;
		CollectNiagaraIssues(Sys, Errors, Warnings);
		FString Out = FormatNiagaraCompileLog(Sys, Errors, Warnings);
		if (!HealLog.IsEmpty())
		{
			Out = HealLog + Out;
		}
		return Out;
	}

	bool NiagaraCompiledOk(const FString& Log)
	{
		return Log.Contains(TEXT("Compiled ok"));
	}

	FString NiagaraTemplateHaystack(const FAssetData& Data)
	{
		FString Desc;
		Data.GetTagValue(TEXT("TemplateAssetDescription"), Desc);
		FString Category;
		Data.GetTagValue(TEXT("Category"), Category);
		return (Data.AssetName.ToString() + TEXT(" ") + Data.GetObjectPathString() + TEXT(" ") + Desc + TEXT(" ") + Category).ToLower();
	}

	bool IsNiagaraTemplateAsset(const FAssetData& Data)
	{
		ENiagaraScriptTemplateSpecification Spec = ENiagaraScriptTemplateSpecification::None;
		if (FNiagaraEditorUtilities::GetTemplateSpecificationFromTag(Data, Spec)
			&& (Spec == ENiagaraScriptTemplateSpecification::Template
				|| Spec == ENiagaraScriptTemplateSpecification::Behavior))
		{
			return true;
		}
		FString Desc;
		if (Data.GetTagValue(TEXT("TemplateAssetDescription"), Desc) && !Desc.TrimStartAndEnd().IsEmpty())
		{
			return true;
		}
		const FString Path = Data.GetObjectPathString();
		return Path.Contains(TEXT("/Template")) || Path.Contains(TEXT("Templates/"));
	}

	int32 ScoreNiagaraTemplate(const FAssetData& Data, const FString& Look)
	{
		const FString Hay = NiagaraTemplateHaystack(Data);
		if (Hay.Contains(TEXT("grid")) || Hay.Contains(TEXT("fluid")) || Hay.Contains(TEXT("simcache")))
		{
			return -1000;
		}
		int32 Score = 1;
		if (Data.GetClass() == UNiagaraSystem::StaticClass())
		{
			Score += 8;
		}
		if (Hay.Contains(TEXT("simple")))
		{
			Score += 20;
		}
		if (Hay.Contains(TEXT("empty")) || Hay.Contains(TEXT("blank")))
		{
			Score -= 30;
		}
		if (Hay.Contains(TEXT("advanced")) || Hay.Contains(TEXT("example")) || Hay.Contains(TEXT("demo")))
		{
			Score -= 15;
		}
		if (Look == TEXT("ribbon"))
		{
			if (Hay.Contains(TEXT("ribbon")) || Hay.Contains(TEXT("trail")))
			{
				Score += 80;
			}
			else
			{
				Score -= 40;
			}
		}
		else if (Look == TEXT("mesh"))
		{
			if (Hay.Contains(TEXT("mesh")))
			{
				Score += 80;
			}
			else
			{
				Score -= 40;
			}
		}
		else
		{
			if (Hay.Contains(TEXT("ribbon")) || Hay.Contains(TEXT("mesh")) || Hay.Contains(TEXT("beam")))
			{
				Score -= 40;
			}
			if (Hay.Contains(TEXT("sprite")) || Hay.Contains(TEXT("fountain")) || Hay.Contains(TEXT("burst")))
			{
				Score += 50;
			}
		}
		return Score;
	}

	void CollectNiagaraTemplateAssets(TArray<FAssetData>& Out)
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		FARFilter Filter;
		Filter.ClassPaths.Add(UNiagaraSystem::StaticClass()->GetClassPathName());
		Filter.ClassPaths.Add(UNiagaraEmitter::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		ARM.Get().GetAssets(Filter, Assets);
		for (const FAssetData& Data : Assets)
		{
			if (IsNiagaraTemplateAsset(Data))
			{
				Out.Add(Data);
			}
		}
	}

	FString ListNiagaraTemplates(const FString& Look, int32 MaxN)
	{
		TArray<FAssetData> Templates;
		CollectNiagaraTemplateAssets(Templates);
		Templates.Sort([&Look](const FAssetData& A, const FAssetData& B)
		{
			return ScoreNiagaraTemplate(A, Look) > ScoreNiagaraTemplate(B, Look);
		});
		MaxN = FMath::Clamp(MaxN, 1, 40);
		FString Out;
		int32 Count = 0;
		for (const FAssetData& Data : Templates)
		{
			if (Look != TEXT("all") && ScoreNiagaraTemplate(Data, Look) < 10)
			{
				continue;
			}
			if (Count >= MaxN)
			{
				Out += TEXT("...\n");
				break;
			}
			const TCHAR* Kind = Data.GetClass() == UNiagaraSystem::StaticClass() ? TEXT("system") : TEXT("emitter");
			Out += FString::Printf(
				TEXT("%s  [%s] score=%d\n"),
				*Data.GetObjectPathString(),
				Kind,
				ScoreNiagaraTemplate(Data, Look));
			++Count;
		}
		return Out.IsEmpty() ? TEXT("(none)") : Out;
	}

	FString PickNiagaraTemplatePath(const FString& Look)
	{
		TArray<FAssetData> Templates;
		CollectNiagaraTemplateAssets(Templates);
		int32 BestScore = 0;
		FString Best;
		for (const FAssetData& Data : Templates)
		{
			const int32 Score = ScoreNiagaraTemplate(Data, Look);
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = Data.GetObjectPathString();
			}
		}
		return Best;
	}

	void StripNiagaraTemplateMeta(UNiagaraSystem* Sys)
	{
		if (!Sys)
		{
			return;
		}
		Sys->TemplateAssetDescription = FText();
		Sys->Category = FText();
		Sys->AssetTags.Empty();
	}

	void RefreshAssetUsers(UObject* Asset);

	void DetachNiagaraUsers(UNiagaraSystem* Sys, TArray<TWeakObjectPtr<UNiagaraComponent>>* OutDetached = nullptr)
	{
		if (!Sys)
		{
			return;
		}
		const FString PkgName = Sys->GetOutermost() ? Sys->GetOutermost()->GetName() : FString();
		if (PkgName.IsEmpty())
		{
			return;
		}
		for (TObjectIterator<UNiagaraComponent> It; It; ++It)
		{
			UNiagaraComponent* Comp = *It;
			if (!IsValid(Comp) || Comp->IsTemplate())
			{
				continue;
			}
			UNiagaraSystem* Cur = Comp->GetAsset();
			if (!Cur)
			{
				continue;
			}
			if (Cur != Sys && !(Cur->GetOutermost() && Cur->GetOutermost()->GetName() == PkgName))
			{
				continue;
			}
			Comp->DeactivateImmediate();
			Comp->SetAsset(nullptr);
			if (OutDetached)
			{
				OutDetached->Add(Comp);
			}
		}
	}

	void ReattachNiagaraUsers(UNiagaraSystem* Sys, const TArray<TWeakObjectPtr<UNiagaraComponent>>& Detached)
	{
		if (!Sys)
		{
			return;
		}
		for (const TWeakObjectPtr<UNiagaraComponent>& Weak : Detached)
		{
			UNiagaraComponent* Comp = Weak.Get();
			if (!IsValid(Comp))
			{
				continue;
			}
			Comp->SetAsset(Sys);
			Comp->Activate(true);
			Comp->ResetSystem();
		}
	}

	bool CopyFileReplaceRetry(const FString& Dst, const FString& Src, FString& Error)
	{
		IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
		if (Platform.FileExists(*Dst))
		{
			Platform.SetReadOnly(*Dst, false);
		}
		for (int32 Try = 0; Try < 4; ++Try)
		{
			if (IFileManager::Get().Copy(*Dst, *Src, true, true) == COPY_OK)
			{
				return true;
			}
			FPlatformProcess::Sleep(0.05f * static_cast<float>(Try + 1));
		}
		Error = TEXT("restore copy failed: ") + FPaths::ConvertRelativePathToFull(Dst);
		return false;
	}

	bool ReplaceExistingNiagaraAsset(UNiagaraSystem* Existing, FString& OutError)
	{
		if (!Existing)
		{
			return true;
		}
		const FString PkgName = Existing->GetOutermost() ? Existing->GetOutermost()->GetName() : FString();
		const FString ObjectPath = Existing->GetPathName();
		if (GEditor)
		{
			if (UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				Editors->CloseAllEditorsForAsset(Existing);
			}
		}
		DetachNiagaraUsers(Existing);
		TArray<UObject*> ToDelete;
		ToDelete.Add(Existing);
		ObjectTools::ForceDeleteObjects(ToDelete, false);
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		if (UPackage* Leftover = FindPackage(nullptr, *PkgName))
		{
			if (IsValid(Leftover))
			{
				TArray<UPackage*> ToUnload;
				ToUnload.Add(Leftover);
				FText UnloadError;
				UPackageTools::UnloadPackages(ToUnload, UnloadError);
				CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			}
		}
		if (LoadObject<UNiagaraSystem>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn))
		{
			OutError = TEXT("Could not replace existing Niagara system (still referenced): ") + ObjectPath;
			return false;
		}
		return true;
	}

	UNiagaraSystem* DuplicateNiagaraTemplateTo(UObject* Source, UPackage* Pkg, const FString& AssetName, FString& OutError)
	{
		if (!Source || !Pkg)
		{
			OutError = TEXT("Template source missing");
			return nullptr;
		}
		if (UNiagaraSystem* SrcSys = Cast<UNiagaraSystem>(Source))
		{
			if (!SrcSys->IsReadyToRun())
			{
				SrcSys->WaitForCompilationComplete();
			}
			UNiagaraSystem* Dup = Cast<UNiagaraSystem>(StaticDuplicateObject(SrcSys, Pkg, *AssetName));
			if (!Dup)
			{
				OutError = TEXT("Duplicate system failed");
				return nullptr;
			}
			StripNiagaraTemplateMeta(Dup);
			return Dup;
		}
		UNiagaraEmitter* SrcEmitter = Cast<UNiagaraEmitter>(Source);
		if (!SrcEmitter)
		{
			OutError = TEXT("Template is not a Niagara system or emitter");
			return nullptr;
		}
		UNiagaraSystem* Sys = NewObject<UNiagaraSystem>(Pkg, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!Sys)
		{
			OutError = TEXT("Create system failed");
			return nullptr;
		}
		UNiagaraSystemFactoryNew::InitializeSystem(Sys, true);
		FNiagaraEditorUtilities::AddEmitterToSystem(*Sys, *SrcEmitter, SrcEmitter->GetExposedVersion().VersionGuid, true);
		StripNiagaraTemplateMeta(Sys);
		return Sys;
	}

	FString CreateNiagaraSystemAt(const FString& Path, bool bDefaults, const FString& TemplatePath = FString())
	{
		FString PackagePath;
		FString AssetName;
		FString Error;
		if (!SplitGameAssetPath(Path, PackagePath, AssetName, Error))
		{
			return Error;
		}
		{
			const FString ObjectPath = PackagePath / AssetName + TEXT(".") + AssetName;
			UNiagaraSystem* Existing = LoadObject<UNiagaraSystem>(nullptr, *Path);
			if (!Existing)
			{
				Existing = LoadObject<UNiagaraSystem>(nullptr, *ObjectPath);
			}
			if (Existing)
			{
				FString ReplaceError;
				if (!ReplaceExistingNiagaraAsset(Existing, ReplaceError))
				{
					return ReplaceError;
				}
			}
		}
		UPackage* Pkg = CreatePackage(*(PackagePath / AssetName));
		UNiagaraSystem* Sys = nullptr;
		FString From;
		if (!TemplatePath.IsEmpty())
		{
			UObject* Source = LoadObject<UObject>(nullptr, *TemplatePath);
			Sys = DuplicateNiagaraTemplateTo(Source, Pkg, AssetName, Error);
			if (!Sys)
			{
				return Error.IsEmpty() ? (TEXT("Template not found: ") + TemplatePath) : Error;
			}
			From = TEXT("from_template=") + TemplatePath;
		}
		else
		{
			Sys = NewObject<UNiagaraSystem>(Pkg, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
			if (!Sys)
			{
				return TEXT("Create system failed");
			}
			UNiagaraSystemFactoryNew::InitializeSystem(Sys, true);
			UNiagaraEmitter* Emitter = NewObject<UNiagaraEmitter>(GetTransientPackage(), NAME_None, RF_Transactional);
			UNiagaraEmitterFactoryNew::InitializeEmitter(Emitter, bDefaults);
			FNiagaraEditorUtilities::AddEmitterToSystem(*Sys, *Emitter, Emitter->GetExposedVersion().VersionGuid, true);
			From = TEXT("from_scratch");
		}
		if (Sys->GetEmitterHandles().Num() > 0)
		{
			Sys->GetEmitterHandles()[0].SetName(*AssetName, *Sys);
		}
		FString Compile = CompileNiagaraSystem(Sys);
		FAssetRegistryModule::AssetCreated(Sys);
		Pkg->MarkPackageDirty();
		RefreshAssetUsers(Sys);
		return Sys->GetPathName() + TEXT("\n") + From + TEXT("\n") + Compile;
	}

	FString CreateNiagaraGrid2DAt(const FString& Path, int32 Cells, float WorldSize, int32 Attributes)
	{
		FString PackagePath;
		FString AssetName;
		FString Error;
		if (!SplitGameAssetPath(Path, PackagePath, AssetName, Error))
		{
			return Error;
		}
		if (LoadObject<UNiagaraSystem>(nullptr, *Path) != nullptr
			|| LoadObject<UNiagaraSystem>(nullptr, *(PackagePath / AssetName + TEXT(".") + AssetName)) != nullptr)
		{
			return TEXT("Asset already exists");
		}
		UPackage* Pkg = CreatePackage(*(PackagePath / AssetName));
		UNiagaraSystem* Sys = NewObject<UNiagaraSystem>(Pkg, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!Sys)
		{
			return TEXT("Create system failed");
		}
		UNiagaraSystemFactoryNew::InitializeSystem(Sys, true);
		UNiagaraEmitter* Emitter = NewObject<UNiagaraEmitter>(GetTransientPackage(), NAME_None, RF_Transactional);
		UNiagaraEmitterFactoryNew::InitializeEmitter(Emitter, false);
		FNiagaraEditorUtilities::AddEmitterToSystem(*Sys, *Emitter, Emitter->GetExposedVersion().VersionGuid, true);
		if (Sys->GetEmitterHandles().Num() == 0)
		{
			return TEXT("Add emitter failed");
		}
		FNiagaraEmitterHandle& Handle = Sys->GetEmitterHandles()[0];
		Handle.SetName(*AssetName, *Sys);
		UNiagaraEmitter* Live = Handle.GetInstance().Emitter;
		FString Setup = ConfigureGrid2DEmitter(Live, Handle.GetInstance().Version, Cells, WorldSize, Attributes);
		FString Compile = CompileNiagaraSystem(Sys);
		FAssetRegistryModule::AssetCreated(Sys);
		Pkg->MarkPackageDirty();
		return Sys->GetPathName() + TEXT("\n") + Setup + TEXT("\n") + Compile;
	}

	UTexture2D* ImportTextureFile(const FString& File, const FString& Kind, const FString& NewName, FString& OutError)
	{
		if (File.StartsWith(TEXT("/Game")) || File.StartsWith(TEXT("/Engine")))
		{
			UTexture2D* Loaded = LoadObject<UTexture2D>(nullptr, *File);
			if (!Loaded)
			{
				OutError = TEXT("Texture not found: ") + File;
			}
			return Loaded;
		}
		if (File.IsEmpty() || !FPaths::FileExists(File) || !IsUnderProject(File))
		{
			OutError = TEXT("file must exist under the project");
			return nullptr;
		}
		const FString FullFile = FPaths::ConvertRelativePathToFull(File);
		const FString Dest = TEXT("/Game/FX/Textures");
		if (!NewName.IsEmpty())
		{
			if (UTexture2D* Existing = FindTextureNamedUnder(Dest, NewName))
			{
				Existing->Modify();
				if (Existing->AssetImportData)
				{
					Existing->AssetImportData->Update(FullFile);
				}
				FReimportManager::Instance()->Reimport(
					Existing, false, false, FullFile, nullptr, INDEX_NONE, false, true);
				ApplyImportedTextureKind(Existing, Kind);
				return Existing;
			}
		}
		UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
		ImportData->Filenames.Add(FullFile);
		ImportData->DestinationPath = Dest;
		ImportData->bReplaceExisting = true;
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		TArray<UObject*> Imported = AssetTools.ImportAssetsAutomated(ImportData);
		if (Imported.Num() == 0)
		{
			OutError = TEXT("Import failed");
			return nullptr;
		}
		UTexture2D* Tex = Cast<UTexture2D>(Imported[0]);
		if (!Tex)
		{
			OutError = TEXT("Import produced no texture");
			return nullptr;
		}
		if (!NewName.IsEmpty() && Tex->GetName() != NewName)
		{
			if (!RenameObjectAvoidingCollision(Tex, NewName, OutError))
			{
				return nullptr;
			}
		}
		ApplyImportedTextureKind(Tex, Kind);
		return Tex;
	}

	AStaticMeshActor* SpawnRecipePlane(UWorld* World, UMaterialInterface* Mat, const FString& Label)
	{
		if (!World)
		{
			return nullptr;
		}
		const FVector Loc = CameraFrontLocation(280.f);
		const FRotator Rot = BillboardToViewport(Loc);
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
		if (!Mesh)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Loc, Rot, Params);
		if (!Actor)
		{
			return nullptr;
		}
		TouchActor(Actor);
		Actor->SetActorLabel(Label.IsEmpty() ? TEXT("VfxCard") : Label);
		Actor->SetMobility(EComponentMobility::Movable);
		Actor->SetActorScale3D(FVector(1.8f, -2.6f, 1.f));
		if (UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent())
		{
			Comp->SetFlags(RF_Transactional);
			Comp->Modify();
			Comp->SetStaticMesh(Mesh);
			Comp->SetMobility(EComponentMobility::Movable);
			Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Comp->SetCastShadow(false);
			if (Mat)
			{
				Comp->SetMaterial(0, Mat);
			}
		}
		return Actor;
	}

	void FocusRecipeActor(AActor* Actor)
	{
		if (!Actor || !GEditor)
		{
			return;
		}
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(Actor, true, true, true);
		GEditor->MoveViewportCamerasToActor(*Actor, false);
		Redraw();
	}

	FString InferLookToken(const FString& Look, const FString& Description)
	{
		FString Token = Look.Replace(TEXT("-"), TEXT("_")).Replace(TEXT(" "), TEXT("_")).ToLower();
		if (Token.Contains(TEXT("noise")) || Token.Contains(TEXT("warp")))
		{
			return TEXT("card_noise");
		}
		if (Token.Contains(TEXT("flip")) || Token.Contains(TEXT("atlas")) || Token.Contains(TEXT("card")))
		{
			return TEXT("card_flipbook");
		}
		if (Token.Contains(TEXT("ribbon")) || Token.Contains(TEXT("trail")))
		{
			return TEXT("ribbon");
		}
		if (Token.Contains(TEXT("mesh")) || Token.Contains(TEXT("debris")))
		{
			return TEXT("mesh");
		}
		if (Token.Contains(TEXT("particle")))
		{
			return TEXT("particles");
		}
		const FString Low = Description.ToLower();
		if (Low.Contains(TEXT("ribbon")) || Low.Contains(TEXT("trail")))
		{
			return TEXT("ribbon");
		}
		if (Low.Contains(TEXT("warp")) || Low.Contains(TEXT("noise")))
		{
			return TEXT("card_noise");
		}
		if (Low.Contains(TEXT("particle")) || Low.Contains(TEXT("ember")))
		{
			return TEXT("particles");
		}
		return TEXT("card_flipbook");
	}

	FString UniqueRecipeSystemPath(const FString& Look)
	{
		FString Safe = Look;
		Safe.ReplaceInline(TEXT(" "), TEXT("_"));
		if (Safe.IsEmpty())
		{
			Safe = TEXT("Look");
		}
		const FString Tag = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
		return FString::Printf(TEXT("/Game/FX/NS_%s_%s"), *Safe, *Tag);
	}

	FString RestoreTempRoot()
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("EditorPrompt") / TEXT("restore"));
	}

	FString RestoreStackPath()
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("EditorPrompt") / TEXT("restore_stack.json"));
	}

	void DeletePathIfExists(const FString& Path)
	{
		IFileManager& Files = IFileManager::Get();
		if (Files.DirectoryExists(*Path))
		{
			Files.DeleteDirectory(*Path, false, true);
		}
		else if (Files.FileExists(*Path))
		{
			Files.Delete(*Path, false, true, true);
		}
	}

	FString ObjectPackageName(UObject* Obj)
	{
		return (Obj && Obj->GetOutermost()) ? Obj->GetOutermost()->GetName() : FString();
	}

	TArray<TPair<FString, FString>> LoadRestoreStack()
	{
		TArray<TPair<FString, FString>> Out;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *RestoreStackPath()) || Text.IsEmpty())
		{
			return Out;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return Out;
		}
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!Root->TryGetArrayField(TEXT("entries"), Entries) || !Entries)
		{
			return Out;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Entries)
		{
			const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Row.IsValid())
			{
				continue;
			}
			const FString Source = Row->GetStringField(TEXT("source"));
			const FString Snapshot = Row->GetStringField(TEXT("snapshot"));
			if (!Source.IsEmpty() && !Snapshot.IsEmpty())
			{
				Out.Emplace(Source, Snapshot);
			}
		}
		return Out;
	}

	void SaveRestoreStack(const TArray<TPair<FString, FString>>& Stack)
	{
		TArray<TSharedPtr<FJsonValue>> Entries;
		for (const TPair<FString, FString>& Row : Stack)
		{
			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("source"), Row.Key);
			Obj->SetStringField(TEXT("snapshot"), Row.Value);
			Entries.Add(MakeShared<FJsonValueObject>(Obj));
		}
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("entries"), Entries);
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Root, Writer);
		FFileHelper::SaveStringToFile(Out, *RestoreStackPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	bool CopyPackageToFolder(const FString& FromPkg, const FString& DestDir, const FString& DestStem, FString& Error)
	{
		const FString FromFile = FPaths::ConvertRelativePathToFull(
			FPackageName::LongPackageNameToFilename(FromPkg, FPackageName::GetAssetPackageExtension()));
		if (!FPaths::FileExists(FromFile))
		{
			Error = TEXT("temp package file missing");
			return false;
		}
		const FString FromDir = FPaths::GetPath(FromFile);
		const FString FromStem = FPaths::GetBaseFilename(FromFile);
		IFileManager::Get().MakeDirectory(*DestDir, true);
		const TCHAR* Exts[] = { TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl") };
		for (const TCHAR* Ext : Exts)
		{
			const FString Src = FromDir / (FromStem + TEXT(".") + Ext);
			if (FPaths::FileExists(Src))
			{
				const FString Dst = DestDir / (DestStem + TEXT(".") + Ext);
				if (!CopyFileReplaceRetry(Dst, Src, Error))
				{
					if (Error.StartsWith(TEXT("restore copy failed")))
					{
						Error = TEXT("temp copy failed");
					}
					return false;
				}
			}
			else if (FCString::Stricmp(Ext, TEXT("uasset")) == 0)
			{
				Error = TEXT("temp uasset missing");
				return false;
			}
		}
		return true;
	}

	bool CopyFolderToPackage(const FString& SrcDir, const FString& ToPkg, FString& Error)
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(SrcDir / TEXT("*.uasset")), true, false);
		if (Found.Num() == 0)
		{
			Error = TEXT("temp snapshot missing");
			return false;
		}
		const FString FromStem = FPaths::GetBaseFilename(Found[0]);
		const FString ToFile = FPaths::ConvertRelativePathToFull(
			FPackageName::LongPackageNameToFilename(ToPkg, FPackageName::GetAssetPackageExtension()));
		const FString ToDir = FPaths::GetPath(ToFile);
		const FString ToStem = FPaths::GetBaseFilename(ToFile);
		IFileManager::Get().MakeDirectory(*ToDir, true);
		const TCHAR* Exts[] = { TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl") };
		for (const TCHAR* Ext : Exts)
		{
			const FString Src = SrcDir / (FromStem + TEXT(".") + Ext);
			const FString Dst = ToDir / (ToStem + TEXT(".") + Ext);
			if (FPaths::FileExists(Src))
			{
				if (!CopyFileReplaceRetry(Dst, Src, Error))
				{
					return false;
				}
			}
			else if (FCString::Stricmp(Ext, TEXT("uasset")) == 0)
			{
				Error = TEXT("temp uasset missing");
				return false;
			}
			else if (FPaths::FileExists(Dst))
			{
				IFileManager::Get().Delete(*Dst, false, true, true);
			}
		}
		return true;
	}

	void DeleteGameRestoreAssets()
	{
		TArray<FAssetData> Assets;
		FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		Registry.Get().GetAssetsByPath(FName(TEXT("/Game/EditorPrompt/Restore")), Assets, true);
		TArray<UObject*> Objs;
		for (const FAssetData& Data : Assets)
		{
			if (UObject* Obj = Data.GetAsset())
			{
				Objs.Add(Obj);
			}
		}
		if (Objs.Num() > 0)
		{
			ObjectTools::ForceDeleteObjects(Objs, false);
		}
	}

	void PurgeRestoreTempsImpl(bool bSavedTemps)
	{
		if (bSavedTemps)
		{
			DeletePathIfExists(RestoreTempRoot());
			DeletePathIfExists(RestoreStackPath());
			return;
		}
		DeleteGameRestoreAssets();
	}

	void RefreshAssetUsers(UObject* Asset)
	{
		UNiagaraSystem* Sys = Cast<UNiagaraSystem>(Asset);
		if (!Sys || !GEditor)
		{
			return;
		}
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			return;
		}
		const FString Pkg = ObjectPackageName(Sys);
		for (TActorIterator<ANiagaraActor> It(World); It; ++It)
		{
			UNiagaraComponent* Comp = It->GetNiagaraComponent();
			if (!Comp)
			{
				continue;
			}
			UNiagaraSystem* Cur = Comp->GetAsset();
			if (Cur && ObjectPackageName(Cur) == Pkg)
			{
				Comp->SetAsset(Sys);
				Comp->Activate(true);
				Comp->ResetSystem();
			}
		}
	}

	FString SnapshotAsset(UObject* Asset)
	{
		if (!Asset)
		{
			return FString();
		}
		const FString SourcePkg = ObjectPackageName(Asset);
		if (!SourcePkg.StartsWith(TEXT("/Game/")) || SourcePkg.Contains(TEXT("/EditorPrompt/Restore/")))
		{
			return FString();
		}
		const FString Tag = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
		const FString DestName = Asset->GetName() + TEXT("_tmp_") + Tag;
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UObject* Dup = AssetTools.DuplicateAsset(DestName, TEXT("/Game/EditorPrompt/Restore"), Asset);
		if (!Dup)
		{
			return FString();
		}
		TArray<UPackage*> Pkgs;
		Pkgs.Add(Dup->GetOutermost());
		UEditorLoadingAndSavingUtils::SavePackages(Pkgs, true);
		const FString TempDir = RestoreTempRoot() / Tag;
		FString CopyError;
		const bool bCopied = CopyPackageToFolder(
			ObjectPackageName(Dup),
			TempDir,
			FPackageName::GetShortName(SourcePkg),
			CopyError);
		TArray<UObject*> ToDelete;
		ToDelete.Add(Dup);
		ObjectTools::ForceDeleteObjects(ToDelete, false);
		if (!bCopied)
		{
			DeletePathIfExists(TempDir);
			return FString();
		}
		TArray<TPair<FString, FString>> Stack = LoadRestoreStack();
		Stack.Emplace(SourcePkg, TempDir);
		while (Stack.Num() > 12)
		{
			DeletePathIfExists(Stack[0].Value);
			Stack.RemoveAt(0);
		}
		SaveRestoreStack(Stack);
		return TempDir;
	}

	FString RestoreAsset(const FString& RequestedPath)
	{
		TArray<TPair<FString, FString>> Stack = LoadRestoreStack();
		if (Stack.Num() == 0)
		{
			return TEXT("No temp copy. Nothing to restore.");
		}
		FString Want = RequestedPath;
		Want.ReplaceInline(TEXT("\\"), TEXT("/"));
		int32 Index = Stack.Num() - 1;
		if (!Want.IsEmpty())
		{
			Index = INDEX_NONE;
			for (int32 I = Stack.Num() - 1; I >= 0; --I)
			{
				if (Want.Contains(Stack[I].Key) || Stack[I].Key.Contains(Want) || Want.Contains(FPackageName::GetShortName(Stack[I].Key)))
				{
					Index = I;
					break;
				}
			}
			if (Index == INDEX_NONE)
			{
				return TEXT("No temp copy for ") + Want;
			}
		}
		const FString SourcePkg = Stack[Index].Key;
		const FString TempDir = Stack[Index].Value;
		UObject* Live = LoadObject<UObject>(nullptr, *SourcePkg);
		TArray<TWeakObjectPtr<UNiagaraComponent>> Detached;
		if (GEditor)
		{
			if (UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				if (Live)
				{
					Editors->CloseAllEditorsForAsset(Live);
				}
			}
		}
		if (UNiagaraSystem* LiveSys = Cast<UNiagaraSystem>(Live))
		{
			DetachNiagaraUsers(LiveSys, &Detached);
		}
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		TArray<UPackage*> ToUnload;
		if (UPackage* Pkg = FindPackage(nullptr, *SourcePkg))
		{
			ToUnload.Add(Pkg);
		}
		if (ToUnload.Num() > 0)
		{
			FText UnloadError;
			UPackageTools::UnloadPackages(ToUnload, UnloadError);
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
		FString CopyError;
		if (!CopyFolderToPackage(TempDir, SourcePkg, CopyError))
		{
			UObject* Fallback = LoadObject<UObject>(nullptr, *SourcePkg);
			if (UNiagaraSystem* FallbackSys = Cast<UNiagaraSystem>(Fallback))
			{
				ReattachNiagaraUsers(FallbackSys, Detached);
				RefreshAssetUsers(FallbackSys);
			}
			return CopyError;
		}
		UPackage* ReloadedPkg = LoadPackage(nullptr, *SourcePkg, LOAD_None);
		UObject* Reloaded = LoadObject<UObject>(nullptr, *SourcePkg);
		if (!Reloaded && ReloadedPkg)
		{
			Reloaded = ReloadedPkg->FindAssetInPackage();
		}
		const FString WantName = FPackageName::GetShortName(SourcePkg);
		FString RenameError;
		if (Reloaded && Reloaded->GetName() != WantName && !RenameObjectAvoidingCollision(Reloaded, WantName, RenameError))
		{
			return TEXT("restored file but could not rename ") + Reloaded->GetName() + TEXT(" to ") + WantName + TEXT(": ") + RenameError;
		}
		if (Reloaded)
		{
			Reloaded->MarkPackageDirty();
			TArray<UPackage*> Pkgs;
			Pkgs.Add(Reloaded->GetOutermost());
			UEditorLoadingAndSavingUtils::SavePackages(Pkgs, true);
		}
		if (UNiagaraSystem* Sys = Cast<UNiagaraSystem>(Reloaded))
		{
			CompileNiagaraSystem(Sys);
			ReattachNiagaraUsers(Sys, Detached);
			RefreshAssetUsers(Sys);
		}
		DeletePathIfExists(TempDir);
		Stack.RemoveAt(Index);
		SaveRestoreStack(Stack);
		return Reloaded
			? (TEXT("restored ") + SourcePkg + TEXT(" from temp copy"))
			: (TEXT("copied files but reload failed: ") + SourcePkg);
	}

	void MaybeSnapshotForTool(const FString& Name, const TSharedPtr<FJsonObject>& Args)
	{
		static const TSet<FString> Mutate({
			TEXT("add_niagara_simulation_stage"),
			TEXT("add_niagara_module"),
			TEXT("remove_niagara_module"),
			TEXT("unbind_niagara_link"),
			TEXT("set_niagara_module_input"),
			TEXT("configure_niagara_emitter"),
			TEXT("set_niagara_sprite_texture"),
			TEXT("set_niagara_sprite_subuv"),
			TEXT("add_niagara_renderer"),
			TEXT("set_niagara_sprite_renderer"),
			TEXT("set_niagara_mesh_renderer"),
			TEXT("set_niagara_ribbon_renderer"),
			TEXT("set_niagara_attribute"),
			TEXT("set_niagara_curve"),
			TEXT("set_niagara_over_life"),
			TEXT("set_niagara_link"),
			TEXT("set_material_color"),
			TEXT("set_material_scalar"),
			TEXT("set_material_texture"),
			TEXT("create_flipbook_mic"),
			TEXT("create_noise_mic"),
			TEXT("bind_niagara_collection"),
			TEXT("set_niagara_user_object"),
			TEXT("add_niagara_di"),
			TEXT("set_niagara_sim_stage"),
			TEXT("add_niagara_emitter"),
			TEXT("set_niagara_grid_target"),
			TEXT("heal_niagara"),
		});
		if (!Mutate.Contains(Name))
		{
			return;
		}
		const FString Path = ArgStr(Args, TEXT("path"));
		if (Path.IsEmpty())
		{
			return;
		}
		if (UObject* Asset = LoadObject<UObject>(nullptr, *Path))
		{
			SnapshotAsset(Asset);
		}
	}

	UNiagaraSystem* EnsureRecipeSystem(const FString& Path, const FString& Look, FString& OutLog)
	{
		if (UNiagaraSystem* Existing = LoadObject<UNiagaraSystem>(nullptr, *Path))
		{
			SnapshotAsset(Existing);
			OutLog += TEXT("sys=") + Existing->GetPathName() + TEXT(" (existing)\n");
			return Existing;
		}
		FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
		const FString Template = PickNiagaraTemplatePath(Look);
		OutLog += CreateNiagaraSystemAt(Path, true, Template) + TEXT("\n");
		return LoadObject<UNiagaraSystem>(nullptr, *Path);
	}

	void SpawnRecipeWarmLight(const FString& Label)
	{
		TSharedRef<FJsonObject> LightArgs = MakeShared<FJsonObject>();
		const FVector Loc = CameraFrontLocation(280.f) + FVector(0, 0, 40);
		LightArgs->SetNumberField(TEXT("x"), Loc.X);
		LightArgs->SetNumberField(TEXT("y"), Loc.Y);
		LightArgs->SetNumberField(TEXT("z"), Loc.Z);
		LightArgs->SetNumberField(TEXT("r"), 1);
		LightArgs->SetNumberField(TEXT("g"), 0.45);
		LightArgs->SetNumberField(TEXT("b"), 0.08);
		LightArgs->SetNumberField(TEXT("intensity"), 8000);
		LightArgs->SetStringField(TEXT("label"), Label.IsEmpty() ? TEXT("VfxLight") : Label + TEXT("_Light"));
		SpawnTypedLight<APointLight>(LightArgs, TEXT("VfxLight"));
	}

	FString ApplyVfxLook(UWorld* World, const TSharedPtr<FJsonObject>& Args, bool& bOk)
	{
		bOk = false;
		if (!World)
		{
			return TEXT("No editor world");
		}
		const FString Look = InferLookToken(ArgStr(Args, TEXT("look")), ArgStr(Args, TEXT("description")));
		const FString Label = ArgStr(Args, TEXT("label"));
		FString Error;
		FString Log = TEXT("look=") + Look + TEXT("\n");
		const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "VfxLook", "Editor Prompt apply_vfx_look"));
		World->Modify();

		if (Look == TEXT("card_flipbook") || Look == TEXT("card_noise"))
		{
			UTexture2D* Picture = ImportTextureFile(
				ArgStr(Args, TEXT("texture")),
				Look == TEXT("card_flipbook") ? TEXT("atlas") : TEXT("albedo"),
				FString(),
				Error);
			if (!Picture)
			{
				return Error;
			}
			UMaterialInstanceConstant* MIC = nullptr;
			if (Look == TEXT("card_flipbook"))
			{
				MIC = CreateFlipbookMic(
					FString::Printf(TEXT("/Game/FX/Materials/MI_%s_Flipbook"), *Picture->GetName()),
					Picture,
					static_cast<float>(ArgNum(Args, TEXT("columns"), 4)),
					static_cast<float>(ArgNum(Args, TEXT("rows"), 4)),
					static_cast<float>(ArgNum(Args, TEXT("fps"), 10)),
					2.4f,
					1.f,
					Error);
			}
			else
			{
				UTexture2D* Noise = ImportTextureFile(ArgStr(Args, TEXT("noise")), TEXT("noise"), FString(), Error);
				if (!Noise)
				{
					return Error.IsEmpty() ? TEXT("noise texture required") : Error;
				}
				MIC = CreateNoiseMic(
					FString::Printf(TEXT("/Game/FX/Materials/MI_%s_Noise"), *Picture->GetName()),
					Picture,
					Noise,
					static_cast<float>(ArgNum(Args, TEXT("strength"), 0.07)),
					static_cast<float>(ArgNum(Args, TEXT("tiling"), 1)),
					static_cast<float>(ArgNum(Args, TEXT("speed_u"), 0.05)),
					static_cast<float>(ArgNum(Args, TEXT("speed_v"), 0.035)),
					2.4f,
					1.f,
					Error);
			}
			if (!MIC)
			{
				return Error.IsEmpty() ? TEXT("MIC failed") : Error;
			}
			AStaticMeshActor* Card = SpawnRecipePlane(World, MIC, Label.IsEmpty() ? TEXT("VfxCard") : Label);
			if (!Card)
			{
				return TEXT("Spawn plane failed");
			}
			SpawnRecipeWarmLight(Card->GetActorLabel());
			EnableFxViewport();
			FocusRecipeActor(Card);
			bOk = true;
			return Log + TEXT("mic=") + MIC->GetPathName() + TEXT("\nactor=") + Card->GetActorLabel();
		}

		FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
		FString SysPath = ArgStr(Args, TEXT("path")).TrimStartAndEnd();
		if (SysPath.IsEmpty())
		{
			SysPath = UniqueRecipeSystemPath(Look);
		}
		UNiagaraSystem* Sys = EnsureRecipeSystem(SysPath, Look, Log);
		if (!Sys)
		{
			return TEXT("Niagara system missing at ") + SysPath;
		}
		FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, FString());
		if (!Handle)
		{
			return TEXT("Emitter not found");
		}
		UNiagaraGraph* Graph = EmitterGraph(Handle->GetEmitterData());
		if (!Graph)
		{
			return TEXT("Emitter graph missing");
		}
		TSharedRef<FJsonObject> Spawn = MakeShared<FJsonObject>();
		Spawn->SetNumberField(TEXT("value"), ArgNum(Args, TEXT("spawn_rate"), 28));
		SetFirstNamedInput(*Handle, Graph, TEXT("SpawnRate"), Spawn, Log);
		TSharedRef<FJsonObject> Life = MakeShared<FJsonObject>();
		Life->SetNumberField(TEXT("value"), ArgNum(Args, TEXT("lifetime"), 1.4));
		SetFirstNamedInput(*Handle, Graph, TEXT("Lifetime"), Life, Log);
		TSharedRef<FJsonObject> Size = MakeShared<FJsonObject>();
		Size->SetNumberField(TEXT("size"), ArgNum(Args, TEXT("size"), 6));
		SetFirstNamedInput(*Handle, Graph, TEXT("SpriteSize"), Size, Log);
		TSharedRef<FJsonObject> Color = MakeShared<FJsonObject>();
		Color->SetNumberField(TEXT("r"), 1);
		Color->SetNumberField(TEXT("g"), 0.4);
		Color->SetNumberField(TEXT("b"), 0.05);
		SetFirstNamedInput(*Handle, Graph, TEXT("Color"), Color, Log);

		if (Look == TEXT("ribbon"))
		{
			UNiagaraEmitter* Emitter = Handle->GetInstance().Emitter;
			UNiagaraRibbonRendererProperties* Ribbon = FindRibbonRenderer(Sys, FString());
			if (!Ribbon && Emitter)
			{
				Ribbon = NewObject<UNiagaraRibbonRendererProperties>(Emitter, TEXT("RibbonRenderer"));
				Emitter->AddRenderer(Ribbon, Handle->GetInstance().Version);
			}
			if (Ribbon)
			{
				Ribbon->Modify();
				Ribbon->FacingMode = ENiagaraRibbonFacingMode::Screen;
			}
			DisableSpriteRenderers(Sys);
			EnsureRibbonBindings(*Handle, Log);
		}
		else if (Look == TEXT("mesh"))
		{
			UNiagaraEmitter* Emitter = Handle->GetInstance().Emitter;
			UNiagaraMeshRendererProperties* MeshR = FindMeshRenderer(Sys, FString());
			if (!MeshR && Emitter)
			{
				MeshR = NewObject<UNiagaraMeshRendererProperties>(Emitter, TEXT("MeshRenderer"));
				Emitter->AddRenderer(MeshR, Handle->GetInstance().Version);
			}
			if (MeshR)
			{
				MeshR->Modify();
				if (MeshR->Meshes.Num() == 0)
				{
					MeshR->Meshes.AddDefaulted();
				}
				const FString MeshPath = ArgStr(Args, TEXT("mesh"), TEXT("/Engine/BasicShapes/Cube.Cube"));
				if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath))
				{
					MeshR->Meshes[0].Mesh = Mesh;
					MeshR->Meshes[0].Scale = FVector(0.15);
				}
			}
			DisableSpriteRenderers(Sys);
			SetOrAddAttribute(*Handle, SYS_PARAM_PARTICLES_SCALE, TEXT("0.15,0.15,0.15"), TEXT("particle_spawn"), Log);
		}
		else
		{
			FString SpriteErr;
			EnsureSpriteRenderer(Sys, FString(), SpriteErr);
			UNiagaraNodeOutput* Update = FindOutputNode(Graph, ENiagaraScriptUsage::ParticleUpdateScript);
			UNiagaraNodeFunctionCall* ScaleCall = FindCallOnStacks(Graph, TEXT("ScaleSpriteSize"));
			if (!ScaleCall)
			{
				ScaleCall = TryAddLibraryModule(Update, TEXT("ScaleSpriteSize"));
			}
			TArray<TPair<float, float>> Keys;
			ParseFloatKeys(TEXT("0,1;1,0"), Keys);
			if (ScaleCall)
			{
				ApplyCurveToModule(*ScaleCall, FString(), Keys, Log);
			}
		}

		ApplyParticleSpread(
			Sys,
			*Handle,
			Graph,
			static_cast<float>(ArgNum(Args, TEXT("spread"), 28)),
			static_cast<float>(ArgNum(Args, TEXT("speed"), 42)),
			static_cast<float>(ArgNum(Args, TEXT("gravity"), -32)),
			Log);

		const FString TexFile = ArgStr(Args, TEXT("texture"));
		if (!TexFile.IsEmpty() && Look != TEXT("ribbon") && Look != TEXT("mesh"))
		{
			UTexture2D* SpriteTex = ImportTextureFile(TexFile, TEXT("albedo"), FString(), Error);
			if (SpriteTex)
			{
				UNiagaraSpriteRendererProperties* Sprite = FindSpriteRenderer(Sys, FString());
				if (Sprite)
				{
					ApplySpriteImageTexture(Sprite, Sys, SpriteTex, Error);
				}
			}
		}
		Sys->MarkPackageDirty();
		Log += CompileNiagaraSystem(Sys);
		ANiagaraActor* Niagara = World->SpawnActor<ANiagaraActor>(CameraFrontLocation(280.f), FRotator::ZeroRotator);
		if (!Niagara)
		{
			return TEXT("Spawn Niagara failed");
		}
		TouchActor(Niagara);
		Niagara->SetActorLabel(Label.IsEmpty() ? TEXT("VfxParticles") : Label);
		Niagara->SetActorScale3D(FVector(2.f));
		if (UNiagaraComponent* C = Niagara->GetNiagaraComponent())
		{
			C->SetAsset(Sys);
			C->SetAutoActivate(true);
			C->Activate(true);
		}
		SpawnRecipeWarmLight(Niagara->GetActorLabel());
		EnableFxViewport();
		FocusRecipeActor(Niagara);
		bOk = NiagaraCompiledOk(Log);
		return Log + TEXT("\nactor=") + Niagara->GetActorLabel() + TEXT(" sys=") + Sys->GetPathName();
	}

	bool AddNiagaraModuleByRef(
		UNiagaraSystem* Sys,
		ENiagaraScriptUsage Usage,
		const FString& EmitterName,
		const FString& ModuleRef,
		const FString& StageName,
		FString& Out)
	{
		if (!Sys)
		{
			Out = TEXT("system missing");
			return false;
		}
		FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
		UNiagaraNodeOutput* Output = nullptr;
		if (Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript)
		{
			Output = SystemStackOutput(Sys, Usage);
		}
		else
		{
			FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, EmitterName);
			if (!Handle)
			{
				Out = TEXT("Emitter not found");
				return false;
			}
			UNiagaraGraph* Graph = EmitterGraph(Handle->GetEmitterData());
			if (Usage == ENiagaraScriptUsage::ParticleSimulationStageScript)
			{
				UNiagaraSimulationStageBase* Stage = FindSimStage(Handle->GetEmitterData(), StageName);
				if (!Stage || !Stage->Script)
				{
					Out = TEXT("Simulation stage not found. create_niagara_grid2d first, or pass stage=");
					return false;
				}
				Output = FindOutputNode(Graph, Usage, Stage->Script->GetUsageId());
			}
			else
			{
				Output = FindOutputNode(Graph, Usage);
			}
		}
		if (!Output)
		{
			Out = TEXT("Stack output node not found");
			return false;
		}
		UNiagaraScript* Module = LoadObject<UNiagaraScript>(nullptr, *ModuleRef);
		if (!Module)
		{
			FNiagaraEditorUtilities::FGetFilteredScriptAssetsOptions Filter;
			Filter.ScriptUsageToInclude = ENiagaraScriptUsage::Module;
			Filter.TargetUsageToMatch = Usage;
			TArray<FAssetData> Assets;
			FNiagaraEditorUtilities::GetFilteredScriptAssets(Filter, Assets);
			UNiagaraScript* Fuzzy = nullptr;
			for (const FAssetData& Asset : Assets)
			{
				if (Asset.AssetName.ToString().Equals(ModuleRef, ESearchCase::IgnoreCase))
				{
					Module = Cast<UNiagaraScript>(Asset.GetAsset());
					if (Module)
					{
						break;
					}
				}
				if (!Fuzzy && Asset.GetObjectPathString().Contains(ModuleRef))
				{
					Fuzzy = Cast<UNiagaraScript>(Asset.GetAsset());
				}
			}
			if (!Module)
			{
				Module = Fuzzy;
			}
		}
		if (!Module)
		{
			Out = TEXT("Module script not found");
			return false;
		}
		UNiagaraNodeFunctionCall* Added = FNiagaraStackGraphUtilities::AddScriptModuleToStack(Module, *Output);
		if (!Added)
		{
			Out = TEXT("Add module failed");
			return false;
		}
		Sys->MarkPackageDirty();
		Out = TEXT("Added ") + Added->GetFunctionName() + TEXT(" on ") + UsageLabel(Usage) + TEXT("\n") + CompileNiagaraSystem(Sys);
		return NiagaraCompiledOk(Out);
	}

	FString ReadViewportSummary(bool& bOk)
	{
		bOk = false;
		FEditorViewportClient* Client = ActiveViewport();
		FViewport* Viewport = Client && Client->Viewport ? Client->Viewport : (GEditor ? GEditor->GetActiveViewport() : nullptr);
		if (!Viewport)
		{
			return TEXT("no viewport");
		}
		const FIntPoint Size = Viewport->GetSizeXY();
		if (Size.X < 2 || Size.Y < 2)
		{
			return TEXT("viewport has no size");
		}
		if (Client)
		{
			Client->Invalidate();
		}
		Viewport->Draw();
		FlushRenderingCommands();
		TArray<FColor> Pixels;
		if (!Viewport->ReadPixels(Pixels) || Pixels.Num() != Size.X * Size.Y)
		{
			return TEXT("viewport read failed");
		}
		for (FColor& Pixel : Pixels)
		{
			Pixel.A = 255;
		}
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("EditorPrompt") / TEXT("screenshots");
		IFileManager::Get().MakeDirectory(*Dir, true);
		const FString File = Dir / FString::Printf(TEXT("vp_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		TArray64<uint8> Compressed;
		FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Compressed);
		if (!FFileHelper::SaveArrayToFile(Compressed, *File))
		{
			return TEXT("screenshot write failed");
		}
		const int32 Cols = 8;
		const int32 Rows = 6;
		FString Grid;
		double SumR = 0.0;
		double SumG = 0.0;
		double SumB = 0.0;
		int32 MinL = 255;
		int32 MaxL = 0;
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			for (int32 Col = 0; Col < Cols; ++Col)
			{
				int32 R = 0;
				int32 G = 0;
				int32 B = 0;
				int32 N = 0;
				const int32 X0 = Col * Size.X / Cols;
				const int32 X1 = (Col + 1) * Size.X / Cols;
				const int32 Y0 = Row * Size.Y / Rows;
				const int32 Y1 = (Row + 1) * Size.Y / Rows;
				const int32 StepX = FMath::Max(1, (X1 - X0) / 8);
				const int32 StepY = FMath::Max(1, (Y1 - Y0) / 8);
				for (int32 Y = Y0; Y < Y1; Y += StepY)
				{
					for (int32 X = X0; X < X1; X += StepX)
					{
						const FColor& Color = Pixels[Y * Size.X + X];
						R += Color.R;
						G += Color.G;
						B += Color.B;
						++N;
					}
				}
				const int32 L = N > 0 ? (R + G + B) / (3 * N) : 0;
				MinL = FMath::Min(MinL, L);
				MaxL = FMath::Max(MaxL, L);
				SumR += N > 0 ? static_cast<double>(R) / N : 0.0;
				SumG += N > 0 ? static_cast<double>(G) / N : 0.0;
				SumB += N > 0 ? static_cast<double>(B) / N : 0.0;
				Grid += FString::FromInt(FMath::Clamp(L * 10 / 256, 0, 9));
			}
			Grid += TEXT("\n");
		}
		bOk = true;
		return FString::Printf(
			TEXT("viewport %dx%d file=%s mean=%.0f,%.0f,%.0f span=%d\n%s"),
			Size.X,
			Size.Y,
			*File,
			SumR / (Cols * Rows),
			SumG / (Cols * Rows),
			SumB / (Cols * Rows),
			MaxL - MinL,
			*Grid) + TEXT("grid is 8x6 brightness 0-9, top to bottom. span is the brightest cell minus the darkest.");
	}
}

TArray<TSharedPtr<FJsonValue>> FEditorPromptMcpTools::ListTools()
{
	TArray<TSharedPtr<FJsonValue>> Tools;

	auto Xyz = []()
	{
		TSharedRef<FJsonObject> P = XyzProps();
		return P;
	};

	{
		TSharedRef<FJsonObject> P = Xyz();
		Tools.Add(MakeTool(TEXT("spawn_cube"), TEXT("Spawn Engine BasicShapes Cube. Undoable. Default 0,0,0."), P));
	}
	Tools.Add(MakeTool(TEXT("get_level_info"), TEXT("Current editor map name, path, dirty flag."), EmptyProps()));
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("/Game path or empty for current")));
		Tools.Add(MakeTool(TEXT("save_level"), TEXT("Save current editor level. Project paths only."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("/Game/Maps/Name without extension")));
		Tools.Add(MakeTool(TEXT("save_level_as"), TEXT("Save current level to a new /Game path."), P));
	}
	Tools.Add(MakeTool(TEXT("new_level"), TEXT("Create a blank editor map (unsaved)."), EmptyProps()));
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("/Game map path or .umap")));
		Tools.Add(MakeTool(TEXT("open_level"), TEXT("Open a map inside the project."), P));
	}
	Tools.Add(MakeTool(TEXT("list_levels"), TEXT("List /Game world/map assets."), EmptyProps()));
	Tools.Add(MakeTool(TEXT("save_dirty_packages"), TEXT("Save dirty packages under the project."), EmptyProps()));

	Tools.Add(MakeTool(TEXT("list_actors"), TEXT("List editor-world actor labels (truncated)."), EmptyProps()));
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("query"), Prop(TEXT("string"), TEXT("Label substring")));
		P->SetObjectField(TEXT("class"), Prop(TEXT("string"), TEXT("Class name substring")));
		Tools.Add(MakeTool(TEXT("find_actors"), TEXT("Filter actors by label or class name."), P));
	}
	{
		TSharedRef<FJsonObject> P = Xyz();
		P->SetObjectField(TEXT("pitch"), Prop(TEXT("number"), TEXT("Pitch")));
		P->SetObjectField(TEXT("yaw"), Prop(TEXT("number"), TEXT("Yaw")));
		P->SetObjectField(TEXT("roll"), Prop(TEXT("number"), TEXT("Roll")));
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("New actor label")));
		P->SetObjectField(TEXT("mesh"), Prop(TEXT("string"), TEXT("Static mesh path")));
		P->SetObjectField(TEXT("class"), Prop(TEXT("string"), TEXT("Actor class path")));
		Tools.Add(MakeTool(TEXT("spawn_actor"), TEXT("Spawn a static mesh or actor class. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = Xyz();
		P->SetObjectField(TEXT("pitch"), Prop(TEXT("number"), TEXT("Pitch if face_camera is false")));
		P->SetObjectField(TEXT("yaw"), Prop(TEXT("number"), TEXT("Yaw if face_camera is false")));
		P->SetObjectField(TEXT("roll"), Prop(TEXT("number"), TEXT("Roll if face_camera is false")));
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Label, default FlipbookCard")));
		P->SetObjectField(TEXT("material"), Prop(TEXT("string"), TEXT("Material or MIC path")));
		P->SetObjectField(TEXT("width"), Prop(TEXT("number"), TEXT("Width in uu, default 180")));
		P->SetObjectField(TEXT("height"), Prop(TEXT("number"), TEXT("Height in uu, default 260")));
		P->SetObjectField(TEXT("sx"), Prop(TEXT("number"), TEXT("Scale X")));
		P->SetObjectField(TEXT("sy"), Prop(TEXT("number"), TEXT("Scale Y")));
		P->SetObjectField(TEXT("sz"), Prop(TEXT("number"), TEXT("Scale Z")));
		P->SetObjectField(TEXT("face_camera"), Prop(TEXT("boolean"), TEXT("Billboard toward the viewport, default true")));
		Tools.Add(MakeTool(TEXT("spawn_plane"), TEXT("Spawn an Engine Plane in front of the camera. Default faces the viewport. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("X")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Y")));
		P->SetObjectField(TEXT("z"), Prop(TEXT("number"), TEXT("Z")));
		Tools.Add(MakeTool(TEXT("move_actor"), TEXT("Set actor location. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("pitch"), Prop(TEXT("number"), TEXT("Pitch")));
		P->SetObjectField(TEXT("yaw"), Prop(TEXT("number"), TEXT("Yaw")));
		P->SetObjectField(TEXT("roll"), Prop(TEXT("number"), TEXT("Roll")));
		Tools.Add(MakeTool(TEXT("rotate_actor"), TEXT("Set actor rotation. Undoable."), P));
	}
	Tools.Add(MakeTool(TEXT("face_viewport_camera"), TEXT("Rotate an actor so its +Z faces the viewport camera. Undoable."), LabelProps()));
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("scale"), Prop(TEXT("number"), TEXT("Uniform scale")));
		P->SetObjectField(TEXT("sx"), Prop(TEXT("number"), TEXT("Scale X")));
		P->SetObjectField(TEXT("sy"), Prop(TEXT("number"), TEXT("Scale Y")));
		P->SetObjectField(TEXT("sz"), Prop(TEXT("number"), TEXT("Scale Z")));
		Tools.Add(MakeTool(TEXT("scale_actor"), TEXT("Set actor scale. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("X")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Y")));
		P->SetObjectField(TEXT("z"), Prop(TEXT("number"), TEXT("Z")));
		P->SetObjectField(TEXT("pitch"), Prop(TEXT("number"), TEXT("Pitch")));
		P->SetObjectField(TEXT("yaw"), Prop(TEXT("number"), TEXT("Yaw")));
		P->SetObjectField(TEXT("roll"), Prop(TEXT("number"), TEXT("Roll")));
		P->SetObjectField(TEXT("scale"), Prop(TEXT("number"), TEXT("Uniform scale")));
		Tools.Add(MakeTool(TEXT("set_actor_transform"), TEXT("Set location, rotation, scale. Undoable."), P));
	}
	Tools.Add(MakeTool(TEXT("get_actor_transform"), TEXT("Get location, rotation, scale."), LabelProps()));
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("new_label"), Prop(TEXT("string"), TEXT("Duplicate label")));
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("Offset X")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Offset Y")));
		P->SetObjectField(TEXT("z"), Prop(TEXT("number"), TEXT("Offset Z")));
		Tools.Add(MakeTool(TEXT("duplicate_actor"), TEXT("Duplicate an actor. Undoable."), P));
	}
	Tools.Add(MakeTool(TEXT("destroy_actor"), TEXT("Destroy actor by label. Undoable."), LabelProps()));
	Tools.Add(MakeTool(TEXT("select_actor"), TEXT("Select actor in the viewport."), LabelProps()));
	Tools.Add(MakeTool(TEXT("get_selected_actors"), TEXT("Labels of the current selection."), EmptyProps()));
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("new_label"), Prop(TEXT("string"), TEXT("New outliner name")));
		Tools.Add(MakeTool(TEXT("rename_actor"), TEXT("Rename actor. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("visible"), Prop(TEXT("boolean"), TEXT("Visible in editor")));
		Tools.Add(MakeTool(TEXT("set_actor_visible"), TEXT("Show or hide actor. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("mobility"), Prop(TEXT("string"), TEXT("Static or Movable")));
		Tools.Add(MakeTool(TEXT("set_actor_mobility"), TEXT("Set root mobility. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Child label")));
		P->SetObjectField(TEXT("parent"), Prop(TEXT("string"), TEXT("Parent label")));
		Tools.Add(MakeTool(TEXT("attach_actor"), TEXT("Attach child to parent. Undoable."), P));
	}
	Tools.Add(MakeTool(TEXT("detach_actor"), TEXT("Detach actor to world. Undoable."), LabelProps()));
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("tag"), Prop(TEXT("string"), TEXT("Tag")));
		P->SetObjectField(TEXT("remove"), Prop(TEXT("boolean"), TEXT("If true, remove tag")));
		Tools.Add(MakeTool(TEXT("set_actor_tag"), TEXT("Add or remove an actor tag. Undoable."), P));
	}

	auto LightSpawnProps = []()
	{
		TSharedRef<FJsonObject> P = XyzProps();
		P->SetObjectField(TEXT("pitch"), Prop(TEXT("number"), TEXT("Pitch")));
		P->SetObjectField(TEXT("yaw"), Prop(TEXT("number"), TEXT("Yaw")));
		P->SetObjectField(TEXT("roll"), Prop(TEXT("number"), TEXT("Roll")));
		P->SetObjectField(TEXT("r"), Prop(TEXT("number"), TEXT("Red 0-1 or 0-255")));
		P->SetObjectField(TEXT("g"), Prop(TEXT("number"), TEXT("Green")));
		P->SetObjectField(TEXT("b"), Prop(TEXT("number"), TEXT("Blue")));
		P->SetObjectField(TEXT("intensity"), Prop(TEXT("number"), TEXT("Lumens / intensity")));
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Label")));
		return P;
	};
	Tools.Add(MakeTool(TEXT("spawn_point_light"), TEXT("Spawn a point light. Undoable."), LightSpawnProps()));
	{
		TSharedRef<FJsonObject> P = LightSpawnProps();
		P->SetObjectField(TEXT("inner_cone"), Prop(TEXT("number"), TEXT("Inner cone degrees")));
		P->SetObjectField(TEXT("outer_cone"), Prop(TEXT("number"), TEXT("Outer cone degrees")));
		Tools.Add(MakeTool(TEXT("spawn_spot_light"), TEXT("Spawn a spot light. Undoable."), P));
	}
	Tools.Add(MakeTool(TEXT("spawn_directional_light"), TEXT("Spawn a directional light. Undoable."), LightSpawnProps()));
	{
		TSharedRef<FJsonObject> P = LightSpawnProps();
		P->SetObjectField(TEXT("width"), Prop(TEXT("number"), TEXT("Source width")));
		P->SetObjectField(TEXT("height"), Prop(TEXT("number"), TEXT("Source height")));
		Tools.Add(MakeTool(TEXT("spawn_rect_light"), TEXT("Spawn a rect light. Undoable."), P));
	}
	Tools.Add(MakeTool(TEXT("spawn_sky_light"), TEXT("Spawn a sky light. Undoable."), LightSpawnProps()));
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("r"), Prop(TEXT("number"), TEXT("R")));
		P->SetObjectField(TEXT("g"), Prop(TEXT("number"), TEXT("G")));
		P->SetObjectField(TEXT("b"), Prop(TEXT("number"), TEXT("B")));
		Tools.Add(MakeTool(TEXT("set_light_color"), TEXT("Set light color. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("intensity"), Prop(TEXT("number"), TEXT("Intensity")));
		Tools.Add(MakeTool(TEXT("set_light_intensity"), TEXT("Set light intensity. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("radius"), Prop(TEXT("number"), TEXT("Attenuation radius cm")));
		Tools.Add(MakeTool(TEXT("set_light_attenuation"), TEXT("Set point/spot attenuation. Undoable."), P));
	}
	Tools.Add(MakeTool(TEXT("recapture_sky_light"), TEXT("Recapture a sky light."), LabelProps()));

	Tools.Add(MakeTool(TEXT("list_niagara_assets"), TEXT("List Niagara System assets under /Game."), EmptyProps()));
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("look"), Prop(TEXT("string"), TEXT("particles, ribbon, mesh, or all")));
		P->SetObjectField(TEXT("max"), Prop(TEXT("number"), TEXT("Max rows, default 20")));
		Tools.Add(MakeTool(TEXT("list_niagara_templates"), TEXT("List engine/project Niagara system and emitter templates (the ones in the create-system wizard)."), P));
	}
	{
		TSharedRef<FJsonObject> P = Xyz();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path")));
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Label")));
		P->SetObjectField(TEXT("scale"), Prop(TEXT("number"), TEXT("Actor scale, default 4 if omitted")));
		Tools.Add(MakeTool(TEXT("spawn_niagara"), TEXT("Spawn a Niagara system. Omit x/y/z to place in front of the camera. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path to play")));
		P->SetObjectField(TEXT("actor"), Prop(TEXT("string"), TEXT("Outliner label, or player for the possessed pawn")));
		P->SetObjectField(TEXT("overlap"), Prop(TEXT("string"), TEXT("Optional actor label. Plays only while XYZ bounds overlap. Empty plays the whole time.")));
		P->SetObjectField(TEXT("pad"), Prop(TEXT("number"), TEXT("Extra cm added to both overlap boxes. Default 0.")));
		P->SetObjectField(TEXT("surface_z"), Prop(TEXT("number"), TEXT("World Z for the effect. XY follows the actor. Omit to attach to the actor origin.")));
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("Offset X")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Offset Y")));
		P->SetObjectField(TEXT("z"), Prop(TEXT("number"), TEXT("Offset added to surface_z, or to the actor origin when surface_z is omitted")));
		P->SetObjectField(TEXT("id"), Prop(TEXT("string"), TEXT("Optional binding id")));
		P->SetObjectField(TEXT("component"), Prop(TEXT("string"), TEXT("Component name on the actor. Position and overlap bounds use it instead of the whole actor.")));
		P->SetObjectField(TEXT("socket"), Prop(TEXT("string"), TEXT("Socket name on the actor. Position comes from that socket.")));
		P->SetObjectField(TEXT("overlap_component"), Prop(TEXT("string"), TEXT("Component name on the overlap actor. Bounds use that component.")));
		P->SetObjectField(TEXT("kill"), Prop(TEXT("boolean"), TEXT("Clear already-spawned particles when playback stops. Default true.")));
		P->SetObjectField(TEXT("user_float"), Prop(TEXT("string"), TEXT("User parameter name. Written each tick as surface_z, or the point Z.")));
		P->SetObjectField(TEXT("user_position"), Prop(TEXT("string"), TEXT("User vector parameter. Written each tick as the follow position.")));
		Tools.Add(MakeTool(TEXT("follow_niagara"), TEXT("Play an existing Niagara system while the editor is playing. actor=player uses the possessed pawn. overlap= uses full XYZ bounds of the actor or a named component. socket= reads a socket. surface_z locks the effect to a world height while XY follows the actor. user_float and user_position write Niagara user parameters. Not Blueprint and not effect-specific."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("actor"), Prop(TEXT("string"), TEXT("Actor label or binding id. Empty clears every follow.")));
		Tools.Add(MakeTool(TEXT("clear_niagara_follow"), TEXT("Remove Niagara follow bindings by actor label or id. Empty clears all."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("material"), Prop(TEXT("string"), TEXT("/Game material or material instance. Instances edit their parent material.")));
		P->SetObjectField(TEXT("actor"), Prop(TEXT("string"), TEXT("Outliner label, or player for the possessed pawn. Default player.")));
		P->SetObjectField(TEXT("radius"), Prop(TEXT("number"), TEXT("Centimetres around the actor that tilt the normal. Default 180.")));
		P->SetObjectField(TEXT("strength"), Prop(TEXT("number"), TEXT("Tangent normal tilt. Default 0.45. 0 clears the warp.")));
		P->SetObjectField(TEXT("surface_z"), Prop(TEXT("number"), TEXT("World Z written as the sample height. XY still follows the actor. Omit to use the actor origin Z.")));
		P->SetObjectField(TEXT("overlap"), Prop(TEXT("string"), TEXT("Optional actor label. Strength stays 0 unless XYZ bounds overlap.")));
		P->SetObjectField(TEXT("overlap_component"), Prop(TEXT("string"), TEXT("Component name on the overlap actor.")));
		P->SetObjectField(TEXT("component"), Prop(TEXT("string"), TEXT("Component name on the driver actor. Overlap bounds use it.")));
		P->SetObjectField(TEXT("pad"), Prop(TEXT("number"), TEXT("Extra cm added to both overlap boxes. Default 0.")));
		Tools.Add(MakeTool(TEXT("distort_normals"), TEXT("Tilt a surface material's tangent normal around a moving actor and enable pixel-normal-offset refraction so the surface warps nearby. One shared driver. Not a particle trail."), P));
	}
	Tools.Add(MakeTool(TEXT("clear_normal_distort"), TEXT("Stop the normal-distort driver. Graph nodes stay in the material and strength goes to 0."), EmptyProps()));
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Parameter name. User. is added when omitted.")));
		P->SetObjectField(TEXT("type"), Prop(TEXT("string"), TEXT("float, vector, color, or bool. Default float.")));
		Tools.Add(MakeTool(TEXT("add_niagara_user"), TEXT("Add a user parameter on a Niagara system so a component can write it at runtime. Not effect-specific."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name, default first")));
		Tools.Add(MakeTool(TEXT("add_niagara_collision"), TEXT("Add the Collision module to particle_update so particles can collide with the world."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("actor"), Prop(TEXT("string"), TEXT("Actor that stamps. player uses the possessed pawn.")));
		P->SetObjectField(TEXT("id"), Prop(TEXT("string"), TEXT("Optional id")));
		P->SetObjectField(TEXT("socket"), Prop(TEXT("string"), TEXT("Socket used for the stamp position.")));
		P->SetObjectField(TEXT("component"), Prop(TEXT("string"), TEXT("Component used for the stamp position and overlap bounds.")));
		P->SetObjectField(TEXT("overlap"), Prop(TEXT("string"), TEXT("Optional actor. Stamps only while XYZ bounds overlap.")));
		P->SetObjectField(TEXT("overlap_component"), Prop(TEXT("string"), TEXT("Component on the overlap actor.")));
		P->SetObjectField(TEXT("pad"), Prop(TEXT("number"), TEXT("Overlap pad cm. Default 0.")));
		P->SetObjectField(TEXT("bounds_actor"), Prop(TEXT("string"), TEXT("Actor whose XY bounds define the texture coverage.")));
		P->SetObjectField(TEXT("center_x"), Prop(TEXT("number"), TEXT("World X at the texture center.")));
		P->SetObjectField(TEXT("center_y"), Prop(TEXT("number"), TEXT("World Y at the texture center.")));
		P->SetObjectField(TEXT("size"), Prop(TEXT("number"), TEXT("World centimetres across the texture. Default 2000.")));
		P->SetObjectField(TEXT("radius"), Prop(TEXT("number"), TEXT("Stamp radius in centimetres. Default 80.")));
		P->SetObjectField(TEXT("fade"), Prop(TEXT("number"), TEXT("How fast the stamp fades, per second. Default 0.75.")));
		P->SetObjectField(TEXT("resolution"), Prop(TEXT("number"), TEXT("Texture resolution. Default 512.")));
		P->SetObjectField(TEXT("material"), Prop(TEXT("string"), TEXT("Optional /Game material. A texture parameter is added when parameter= is set.")));
		P->SetObjectField(TEXT("parameter"), Prop(TEXT("string"), TEXT("Texture parameter name. Default Trail.")));
		Tools.Add(MakeTool(TEXT("stamp_trail"), TEXT("Paint a fading stamp into a render target in world XY as an actor moves. The texture is generic; connect it in a material yourself."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("id"), Prop(TEXT("string"), TEXT("Trail id. Empty clears every trail.")));
		Tools.Add(MakeTool(TEXT("clear_stamp_trail"), TEXT("Stop a stamp trail. The render target asset stays."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("material"), Prop(TEXT("string"), TEXT("/Game material or instance.")));
		Tools.Add(MakeTool(TEXT("list_material_expressions"), TEXT("List material expression desc and class names."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("material"), Prop(TEXT("string"), TEXT("/Game material or instance.")));
		P->SetObjectField(TEXT("class"), Prop(TEXT("string"), TEXT("Expression class: Add, Multiply, Constant, ScalarParameter, TextureSampleParameter2D, ComponentMask, Lerp, WorldPosition, and other MaterialExpression names.")));
		P->SetObjectField(TEXT("desc"), Prop(TEXT("string"), TEXT("Id used by connect_material.")));
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("Node X")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Node Y")));
		P->SetObjectField(TEXT("value"), Prop(TEXT("number"), TEXT("Constant or scalar default.")));
		P->SetObjectField(TEXT("r"), Prop(TEXT("number"), TEXT("Color R")));
		P->SetObjectField(TEXT("g"), Prop(TEXT("number"), TEXT("Color G")));
		P->SetObjectField(TEXT("b"), Prop(TEXT("number"), TEXT("Color B")));
		P->SetObjectField(TEXT("parameter"), Prop(TEXT("string"), TEXT("Parameter name.")));
		P->SetObjectField(TEXT("texture"), Prop(TEXT("string"), TEXT("Texture asset path.")));
		P->SetObjectField(TEXT("collection"), Prop(TEXT("string"), TEXT("Material parameter collection path.")));
		Tools.Add(MakeTool(TEXT("add_material_expression"), TEXT("Add one material expression. Refer to it later by desc. Not effect-specific."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("material"), Prop(TEXT("string"), TEXT("/Game material or instance.")));
		P->SetObjectField(TEXT("from"), Prop(TEXT("string"), TEXT("Source expression desc.")));
		P->SetObjectField(TEXT("from_pin"), Prop(TEXT("string"), TEXT("Source output pin. Empty uses the first output.")));
		P->SetObjectField(TEXT("to"), Prop(TEXT("string"), TEXT("Target expression desc.")));
		P->SetObjectField(TEXT("to_pin"), Prop(TEXT("string"), TEXT("Target input pin.")));
		P->SetObjectField(TEXT("property"), Prop(TEXT("string"), TEXT("Material pin instead of to=: BaseColor, Emissive, Opacity, Normal, Roughness, Metallic, Refraction, WorldPositionOffset.")));
		Tools.Add(MakeTool(TEXT("connect_material"), TEXT("Connect a material expression to another expression or to a material property."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("playing"), Prop(TEXT("boolean"), TEXT("Play if true")));
		Tools.Add(MakeTool(TEXT("set_niagara_playing"), TEXT("Play or stop Niagara. Undoable."), P));
	}
	Tools.Add(MakeTool(TEXT("reset_niagara"), TEXT("Reset a Niagara component."), LabelProps()));
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("User parameter")));
		P->SetObjectField(TEXT("value"), Prop(TEXT("number"), TEXT("Float")));
		Tools.Add(MakeTool(TEXT("set_niagara_float"), TEXT("Set Niagara user float."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("User parameter")));
		P->SetObjectField(TEXT("r"), Prop(TEXT("number"), TEXT("R")));
		P->SetObjectField(TEXT("g"), Prop(TEXT("number"), TEXT("G")));
		P->SetObjectField(TEXT("b"), Prop(TEXT("number"), TEXT("B")));
		Tools.Add(MakeTool(TEXT("set_niagara_color"), TEXT("Set Niagara user color."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("/Game path for the new system")));
		P->SetObjectField(TEXT("defaults"), Prop(TEXT("boolean"), TEXT("Add default spawn/color/sprite modules when not using a template. Default true")));
		P->SetObjectField(TEXT("template"), Prop(TEXT("string"), TEXT("Existing Niagara system or emitter path to duplicate. Empty: pick from look, else empty sprite emitter")));
		P->SetObjectField(TEXT("look"), Prop(TEXT("string"), TEXT("particles, ribbon, or mesh — picks a matching template when template is omitted")));
		Tools.Add(MakeTool(TEXT("create_niagara_system"), TEXT("Create a Niagara System. Prefer duplicating a template from list_niagara_templates."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("/Game path for the new Grid2D system")));
		P->SetObjectField(TEXT("cells"), Prop(TEXT("number"), TEXT("Grid resolution on each axis, default 64")));
		P->SetObjectField(TEXT("world_size"), Prop(TEXT("number"), TEXT("World box size in cm, default 200")));
		P->SetObjectField(TEXT("attributes"), Prop(TEXT("number"), TEXT("Unnamed float attributes on the Grid2D Collection, default 1")));
		Tools.Add(MakeTool(TEXT("create_niagara_grid2d"), TEXT("Create a GPU Niagara System from scratch: Grid2D Collection DI + DataInterface simulation stage. Not a sprite emitter."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("System path")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name, default first")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Stage name, default GridIterate")));
		Tools.Add(MakeTool(TEXT("add_niagara_simulation_stage"), TEXT("Add a Grid2D DataInterface simulation stage to an existing GPU emitter."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("stack"), Prop(TEXT("string"), TEXT("emitter_update, particle_spawn, particle_update, emitter_spawn, simulation_stage")));
		P->SetObjectField(TEXT("query"), Prop(TEXT("string"), TEXT("Name substring")));
		Tools.Add(MakeTool(TEXT("list_niagara_modules"), TEXT("List Niagara library modules that can go on a stack."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("System path")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name, default first")));
		Tools.Add(MakeTool(TEXT("list_niagara_stack"), TEXT("Dump emitters, stacks, modules, and Module input names (set name=SpawnRate, not InputMap)."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("System path")));
		P->SetObjectField(TEXT("stack"), Prop(TEXT("string"), TEXT("emitter_update / particle_spawn / particle_update / simulation_stage / system_spawn / system_update")));
		P->SetObjectField(TEXT("module"), Prop(TEXT("string"), TEXT("Module asset path or name")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name")));
		P->SetObjectField(TEXT("stage"), Prop(TEXT("string"), TEXT("Simulation stage name when stack=simulation_stage")));
		Tools.Add(MakeTool(TEXT("add_niagara_module"), TEXT("Add a Niagara module script to an emitter stack, a simulation stage, or system_spawn / system_update."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("System path")));
		P->SetObjectField(TEXT("module"), Prop(TEXT("string"), TEXT("Module function name e.g. KillParticles")));
		P->SetObjectField(TEXT("stack"), Prop(TEXT("string"), TEXT("Optional stack if the name is on more than one")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name")));
		Tools.Add(MakeTool(TEXT("remove_niagara_module"), TEXT("Remove a module from an emitter stack. Use this to drop KillParticles or any module that will not compile."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("System path")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name")));
		P->SetObjectField(TEXT("module"), Prop(TEXT("string"), TEXT("Module name")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Module input to unbind")));
		P->SetObjectField(TEXT("stack"), Prop(TEXT("string"), TEXT("Optional stack if the name is on more than one")));
		Tools.Add(MakeTool(TEXT("unbind_niagara_link"), TEXT("Clear a set_niagara_link bind and delete its Map Get. Fixes type-mismatch compile errors."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("System path")));
		P->SetObjectField(TEXT("module"), Prop(TEXT("string"), TEXT("Module function name e.g. SpawnRate")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Input name e.g. SpawnRate or Color")));
		P->SetObjectField(TEXT("stack"), Prop(TEXT("string"), TEXT("Stack of that module")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name")));
		P->SetObjectField(TEXT("value"), Prop(TEXT("number"), TEXT("Float value")));
		P->SetObjectField(TEXT("r"), Prop(TEXT("number"), TEXT("Color R")));
		P->SetObjectField(TEXT("g"), Prop(TEXT("number"), TEXT("Color G")));
		P->SetObjectField(TEXT("b"), Prop(TEXT("number"), TEXT("Color B")));
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("Vec3 X")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Vec3 Y")));
		P->SetObjectField(TEXT("z"), Prop(TEXT("number"), TEXT("Vec3 Z")));
		P->SetObjectField(TEXT("size"), Prop(TEXT("number"), TEXT("Vec2 uniform")));
		P->SetObjectField(TEXT("sx"), Prop(TEXT("number"), TEXT("Vec2 X")));
		P->SetObjectField(TEXT("sy"), Prop(TEXT("number"), TEXT("Vec2 Y")));
		Tools.Add(MakeTool(TEXT("set_niagara_module_input"), TEXT("Set a listed module input (float, color, vec2, vec3, int). Unknown names fail."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("System path")));
		Tools.Add(MakeTool(TEXT("compile_niagara"), TEXT("Compile a Niagara System, wait, and return error/warning text. Also applies stack Fix Issue: module order and missing SolveForcesAndVelocity / ApplyInitialForces."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("System path")));
		Tools.Add(MakeTool(TEXT("heal_niagara"), TEXT("Scan Niagara stack issues (unmet module dependencies, red Fix Issue icons) and apply the same fixes: reorder GravityForce before SolveForcesAndVelocity, add missing providers. SystemState is added on the system spawn and system update scripts, not the emitter. Then compile."), P));
	}

	Tools.Add(MakeTool(TEXT("list_materials"), TEXT("List materials and MICs under /Game."), EmptyProps()));
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Material path")));
		P->SetObjectField(TEXT("slot"), Prop(TEXT("number"), TEXT("Slot index")));
		Tools.Add(MakeTool(TEXT("assign_material"), TEXT("Assign material to actor. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Material path")));
		P->SetObjectField(TEXT("slot"), Prop(TEXT("number"), TEXT("Slot index")));
		Tools.Add(MakeTool(TEXT("assign_material_slot"), TEXT("Assign material to a mesh slot. Undoable."), P));
	}
	Tools.Add(MakeTool(TEXT("get_actor_materials"), TEXT("List materials on an actor."), LabelProps()));
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("/Game path for the MIC")));
		P->SetObjectField(TEXT("r"), Prop(TEXT("number"), TEXT("R")));
		P->SetObjectField(TEXT("g"), Prop(TEXT("number"), TEXT("G")));
		P->SetObjectField(TEXT("b"), Prop(TEXT("number"), TEXT("B")));
		Tools.Add(MakeTool(TEXT("create_color_material"), TEXT("Create a solid-color material instance under /Game."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("parent"), Prop(TEXT("string"), TEXT("Parent material path")));
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("/Game save path")));
		Tools.Add(MakeTool(TEXT("create_material_instance"), TEXT("Create a MIC from a parent material."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("MIC path")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Vector param")));
		P->SetObjectField(TEXT("r"), Prop(TEXT("number"), TEXT("R")));
		P->SetObjectField(TEXT("g"), Prop(TEXT("number"), TEXT("G")));
		P->SetObjectField(TEXT("b"), Prop(TEXT("number"), TEXT("B")));
		Tools.Add(MakeTool(TEXT("set_material_color"), TEXT("Set a MIC vector/color parameter."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("MIC path")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Scalar param")));
		P->SetObjectField(TEXT("value"), Prop(TEXT("number"), TEXT("Float")));
		Tools.Add(MakeTool(TEXT("set_material_scalar"), TEXT("Set a MIC scalar parameter."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("MIC path")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Texture param, default Texture")));
		P->SetObjectField(TEXT("texture"), Prop(TEXT("string"), TEXT("UTexture2D path")));
		Tools.Add(MakeTool(TEXT("set_material_texture"), TEXT("Set a MIC texture parameter."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("/Game MIC path, default /Game/FX/Materials/MI_<tex>_Flipbook")));
		P->SetObjectField(TEXT("texture"), Prop(TEXT("string"), TEXT("Atlas Texture2D path")));
		P->SetObjectField(TEXT("columns"), Prop(TEXT("number"), TEXT("Atlas columns, default 4")));
		P->SetObjectField(TEXT("rows"), Prop(TEXT("number"), TEXT("Atlas rows, default 4")));
		P->SetObjectField(TEXT("fps"), Prop(TEXT("number"), TEXT("Frames per second, default 10")));
		P->SetObjectField(TEXT("emissive"), Prop(TEXT("number"), TEXT("EmissiveStrength, default 2.4")));
		P->SetObjectField(TEXT("opacity"), Prop(TEXT("number"), TEXT("OpacityStrength, default 1")));
		Tools.Add(MakeTool(TEXT("create_flipbook_mic"), TEXT("MIC of /Game/FX/M_EditorPromptFlipbook. Time-driven atlas UV. Not a material graph hand."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("/Game MIC path, default /Game/FX/Materials/MI_<tex>_Noise")));
		P->SetObjectField(TEXT("texture"), Prop(TEXT("string"), TEXT("Sprite Texture2D path")));
		P->SetObjectField(TEXT("noise"), Prop(TEXT("string"), TEXT("Baked RG noise Texture2D path")));
		P->SetObjectField(TEXT("strength"), Prop(TEXT("number"), TEXT("UV warp amount, default 0.06")));
		P->SetObjectField(TEXT("tiling"), Prop(TEXT("number"), TEXT("Noise UV tiling, default 1")));
		P->SetObjectField(TEXT("speed_u"), Prop(TEXT("number"), TEXT("Noise pan U, default 0.05")));
		P->SetObjectField(TEXT("speed_v"), Prop(TEXT("number"), TEXT("Noise pan V, default 0.035")));
		P->SetObjectField(TEXT("emissive"), Prop(TEXT("number"), TEXT("EmissiveStrength, default 2.4")));
		P->SetObjectField(TEXT("opacity"), Prop(TEXT("number"), TEXT("OpacityStrength, default 1")));
		Tools.Add(MakeTool(TEXT("create_noise_mic"), TEXT("MIC of /Game/FX/M_EditorPromptNoiseSprite. Baked RG curl offsets sprite UVs. Not a graph hand."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("look"), Prop(TEXT("string"), TEXT("card_flipbook, card_noise, particles, ribbon, mesh")));
		P->SetObjectField(TEXT("description"), Prop(TEXT("string"), TEXT("Look text if look omitted")));
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path. Empty creates a unique /Game/FX/NS_<look>_<id>, never NS_Live")));
		P->SetObjectField(TEXT("texture"), Prop(TEXT("string"), TEXT("PNG path or Texture2D path")));
		P->SetObjectField(TEXT("noise"), Prop(TEXT("string"), TEXT("Baked RG noise PNG when look=card_noise")));
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Actor label")));
		P->SetObjectField(TEXT("columns"), Prop(TEXT("number"), TEXT("Atlas columns")));
		P->SetObjectField(TEXT("rows"), Prop(TEXT("number"), TEXT("Atlas rows")));
		P->SetObjectField(TEXT("fps"), Prop(TEXT("number"), TEXT("Atlas fps")));
		P->SetObjectField(TEXT("strength"), Prop(TEXT("number"), TEXT("Noise UV warp")));
		P->SetObjectField(TEXT("tiling"), Prop(TEXT("number"), TEXT("Noise tiling")));
		P->SetObjectField(TEXT("speed_u"), Prop(TEXT("number"), TEXT("Noise pan U")));
		P->SetObjectField(TEXT("speed_v"), Prop(TEXT("number"), TEXT("Noise pan V")));
		P->SetObjectField(TEXT("mesh"), Prop(TEXT("string"), TEXT("Static mesh path when look=mesh")));
		P->SetObjectField(TEXT("spread"), Prop(TEXT("number"), TEXT("Particle spawn sphere radius, default 28")));
		P->SetObjectField(TEXT("speed"), Prop(TEXT("number"), TEXT("Particle upward speed, default 42")));
		P->SetObjectField(TEXT("gravity"), Prop(TEXT("number"), TEXT("Particle gravity Z, default -32")));
		Tools.Add(MakeTool(TEXT("apply_vfx_look"), TEXT("One look: import baked PNG(s), MIC or a unique Niagara system, spawn in front of camera. Does not reuse leftover NS_Live unless path names it. Particles spawn in a sphere, not a laser line. Not a Niagara graph editor."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("file"), Prop(TEXT("string"), TEXT("PNG path under the project")));
		P->SetObjectField(TEXT("dest"), Prop(TEXT("string"), TEXT("/Game dest folder, default /Game/FX/Textures")));
		P->SetObjectField(TEXT("kind"), Prop(TEXT("string"), TEXT("mask, albedo, sprite, atlas, or noise (wrap, linear RG)")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Optional asset name")));
		Tools.Add(MakeTool(TEXT("import_texture"), TEXT("Import a project PNG as a Texture2D."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path")));
		P->SetObjectField(TEXT("texture"), Prop(TEXT("string"), TEXT("Texture2D path")));
		P->SetObjectField(TEXT("mode"), Prop(TEXT("string"), TEXT("sprite (default), cutout, or material")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name")));
		Tools.Add(MakeTool(TEXT("set_niagara_sprite_texture"), TEXT("Put a PNG on the sprite renderer. Default is a per-system MIC. mode=cutout only crops particle bounds. mode=material assigns a MIC path."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path")));
		P->SetObjectField(TEXT("columns"), Prop(TEXT("number"), TEXT("SubImageSize X")));
		P->SetObjectField(TEXT("rows"), Prop(TEXT("number"), TEXT("SubImageSize Y")));
		P->SetObjectField(TEXT("blend"), Prop(TEXT("boolean"), TEXT("bSubImageBlend, default true")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name")));
		Tools.Add(MakeTool(TEXT("set_niagara_sprite_subuv"), TEXT("Set sprite SubImageSize and blend. Use with a SubUV or flipbook material."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name")));
		Tools.Add(MakeTool(TEXT("list_niagara_renderers"), TEXT("Dump sprite renderer facing, alignment, pivot, SubUV."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name")));
		P->SetObjectField(TEXT("kind"), Prop(TEXT("string"), TEXT("sprite, mesh, or ribbon")));
		P->SetObjectField(TEXT("mesh"), Prop(TEXT("string"), TEXT("Static mesh path when kind=mesh")));
		P->SetObjectField(TEXT("material"), Prop(TEXT("string"), TEXT("Material when kind=ribbon or mesh override")));
		P->SetObjectField(TEXT("width"), Prop(TEXT("number"), TEXT("Ribbon width when kind=ribbon")));
		Tools.Add(MakeTool(TEXT("add_niagara_renderer"), TEXT("Add a sprite, mesh, or ribbon renderer to an emitter."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name")));
		P->SetObjectField(TEXT("alignment"), Prop(TEXT("string"), TEXT("automatic, unaligned, velocity, custom")));
		P->SetObjectField(TEXT("facing"), Prop(TEXT("string"), TEXT("automatic, camera, camera_plane, camera_position")));
		P->SetObjectField(TEXT("pivot_u"), Prop(TEXT("number"), TEXT("Pivot U, 0.5 is center")));
		P->SetObjectField(TEXT("pivot_v"), Prop(TEXT("number"), TEXT("Pivot V, 0.5 is center")));
		P->SetObjectField(TEXT("sort"), Prop(TEXT("string"), TEXT("none, depth, distance")));
		P->SetObjectField(TEXT("shadows"), Prop(TEXT("boolean"), TEXT("Cast shadows")));
		Tools.Add(MakeTool(TEXT("set_niagara_sprite_renderer"), TEXT("Set sprite renderer facing, alignment, pivot, sort."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name")));
		P->SetObjectField(TEXT("spawn_rate"), Prop(TEXT("number"), TEXT("SpawnRate")));
		P->SetObjectField(TEXT("lifetime"), Prop(TEXT("number"), TEXT("Lifetime seconds")));
		P->SetObjectField(TEXT("size"), Prop(TEXT("number"), TEXT("Sprite size")));
		P->SetObjectField(TEXT("sx"), Prop(TEXT("number"), TEXT("Sprite size X")));
		P->SetObjectField(TEXT("sy"), Prop(TEXT("number"), TEXT("Sprite size Y")));
		P->SetObjectField(TEXT("r"), Prop(TEXT("number"), TEXT("Color R")));
		P->SetObjectField(TEXT("g"), Prop(TEXT("number"), TEXT("Color G")));
		P->SetObjectField(TEXT("b"), Prop(TEXT("number"), TEXT("Color B")));
		P->SetObjectField(TEXT("a"), Prop(TEXT("number"), TEXT("Color A")));
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("Velocity X")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Velocity Y")));
		P->SetObjectField(TEXT("z"), Prop(TEXT("number"), TEXT("Velocity Z")));
		P->SetObjectField(TEXT("gravity"), Prop(TEXT("number"), TEXT("Gravity Z, Unreal down is negative")));
		P->SetObjectField(TEXT("spread"), Prop(TEXT("number"), TEXT("Spawn sphere radius, default 28. Stops a single-line burst.")));
		P->SetObjectField(TEXT("speed"), Prop(TEXT("number"), TEXT("Upward speed if z omitted")));
		Tools.Add(MakeTool(TEXT("configure_niagara_emitter"), TEXT("Author a sprite emitter: spawn rate, lifetime, size, color, velocity, gravity. Always adds a spawn sphere so sparks are not a laser line. Adds GravityForce if needed."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name")));
		P->SetObjectField(TEXT("mesh"), Prop(TEXT("string"), TEXT("Static mesh path")));
		P->SetObjectField(TEXT("material"), Prop(TEXT("string"), TEXT("Optional override material")));
		P->SetObjectField(TEXT("facing"), Prop(TEXT("string"), TEXT("default, velocity, camera, plane")));
		P->SetObjectField(TEXT("sx"), Prop(TEXT("number"), TEXT("Mesh scale X")));
		P->SetObjectField(TEXT("sy"), Prop(TEXT("number"), TEXT("Mesh scale Y")));
		P->SetObjectField(TEXT("sz"), Prop(TEXT("number"), TEXT("Mesh scale Z")));
		P->SetObjectField(TEXT("scale"), Prop(TEXT("number"), TEXT("Uniform mesh scale")));
		Tools.Add(MakeTool(TEXT("set_niagara_mesh_renderer"), TEXT("Set mesh renderer mesh, facing, scale, material."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name")));
		P->SetObjectField(TEXT("material"), Prop(TEXT("string"), TEXT("Ribbon material")));
		P->SetObjectField(TEXT("facing"), Prop(TEXT("string"), TEXT("screen or custom")));
		P->SetObjectField(TEXT("width"), Prop(TEXT("number"), TEXT("Particles.RibbonWidth")));
		Tools.Add(MakeTool(TEXT("set_niagara_ribbon_renderer"), TEXT("Set ribbon renderer material, facing, width."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Attribute, e.g. Heat or Particles.SpriteSize")));
		P->SetObjectField(TEXT("type"), Prop(TEXT("string"), TEXT("float, vec2, vec3, color")));
		P->SetObjectField(TEXT("stack"), Prop(TEXT("string"), TEXT("particle_spawn or particle_update")));
		P->SetObjectField(TEXT("value"), Prop(TEXT("number"), TEXT("Float default")));
		P->SetObjectField(TEXT("r"), Prop(TEXT("number"), TEXT("Color R")));
		P->SetObjectField(TEXT("g"), Prop(TEXT("number"), TEXT("Color G")));
		P->SetObjectField(TEXT("b"), Prop(TEXT("number"), TEXT("Color B")));
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("Vec3 X")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Vec3 Y")));
		P->SetObjectField(TEXT("z"), Prop(TEXT("number"), TEXT("Vec3 Z")));
		P->SetObjectField(TEXT("size"), Prop(TEXT("number"), TEXT("Vec2 uniform")));
		P->SetObjectField(TEXT("sx"), Prop(TEXT("number"), TEXT("Vec2 X")));
		P->SetObjectField(TEXT("sy"), Prop(TEXT("number"), TEXT("Vec2 Y")));
		Tools.Add(MakeTool(TEXT("set_niagara_attribute"), TEXT("Add or set a particle attribute on spawn/update (assignment module)."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name")));
		P->SetObjectField(TEXT("module"), Prop(TEXT("string"), TEXT("Module name that has a curve DI")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Curve input name, optional")));
		P->SetObjectField(TEXT("keys"), Prop(TEXT("string"), TEXT("time,value;time,value e.g. 0,1;1,0")));
		Tools.Add(MakeTool(TEXT("set_niagara_curve"), TEXT("Write a lifetime curve on a module curve input. keys=0,1;0.5,0.8;1,0"), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name")));
		P->SetObjectField(TEXT("target"), Prop(TEXT("string"), TEXT("size or color")));
		P->SetObjectField(TEXT("keys"), Prop(TEXT("string"), TEXT("time,value;... default 0,1;1,0")));
		Tools.Add(MakeTool(TEXT("set_niagara_over_life"), TEXT("Size or color over NormalizedAge. Adds ScaleSpriteSize or ScaleColor and writes the curve."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name")));
		P->SetObjectField(TEXT("module"), Prop(TEXT("string"), TEXT("Module name")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Module input to bind")));
		P->SetObjectField(TEXT("source"), Prop(TEXT("string"), TEXT("Particles.NormalizedAge or another attribute. unbind/none/false clears the link")));
		P->SetObjectField(TEXT("stack"), Prop(TEXT("string"), TEXT("Optional stack if the name is on more than one")));
		Tools.Add(MakeTool(TEXT("set_niagara_link"), TEXT("Bind a module input to a particle attribute (Map Get). source=unbind clears it and deletes the Map Get."), P));
	}

	Tools.Add(MakeTool(TEXT("get_viewport_camera"), TEXT("Active level-editor camera location and rotation."), EmptyProps()));
	{
		TSharedRef<FJsonObject> P = Xyz();
		P->SetObjectField(TEXT("pitch"), Prop(TEXT("number"), TEXT("Pitch")));
		P->SetObjectField(TEXT("yaw"), Prop(TEXT("number"), TEXT("Yaw")));
		P->SetObjectField(TEXT("roll"), Prop(TEXT("number"), TEXT("Roll")));
		Tools.Add(MakeTool(TEXT("set_viewport_camera"), TEXT("Move the active editor viewport camera."), P));
	}
	Tools.Add(MakeTool(TEXT("focus_actor"), TEXT("Frame the viewport on an actor."), LabelProps()));
	{
		TSharedRef<FJsonObject> P = Xyz();
		P->SetObjectField(TEXT("pitch"), Prop(TEXT("number"), TEXT("Pitch")));
		P->SetObjectField(TEXT("yaw"), Prop(TEXT("number"), TEXT("Yaw")));
		P->SetObjectField(TEXT("roll"), Prop(TEXT("number"), TEXT("Roll")));
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Label")));
		Tools.Add(MakeTool(TEXT("spawn_camera"), TEXT("Spawn a camera actor. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = Xyz();
		P->SetObjectField(TEXT("pitch"), Prop(TEXT("number"), TEXT("Pitch")));
		P->SetObjectField(TEXT("yaw"), Prop(TEXT("number"), TEXT("Yaw")));
		P->SetObjectField(TEXT("roll"), Prop(TEXT("number"), TEXT("Roll")));
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Label")));
		Tools.Add(MakeTool(TEXT("spawn_cine_camera"), TEXT("Spawn a cinematic camera. Undoable."), P));
	}
	Tools.Add(MakeTool(TEXT("capture_viewport"), TEXT("Read the active viewport, write a PNG, and return an 8x6 brightness grid. Fails when the pixels cannot be read."), EmptyProps()));
	Tools.Add(MakeTool(TEXT("read_viewport"), TEXT("Read the active viewport, write a PNG, and return an 8x6 brightness grid plus mean color. Same pixels as capture_viewport."), EmptyProps()));

	Tools.Add(MakeTool(TEXT("spawn_sky_atmosphere"), TEXT("Spawn default Sky Atmosphere. Undoable."), EmptyProps()));
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("mie"), Prop(TEXT("number"), TEXT("Mie scattering scale")));
		P->SetObjectField(TEXT("disk"), Prop(TEXT("number"), TEXT("Sun disk size")));
		Tools.Add(MakeTool(TEXT("set_sky_atmosphere"), TEXT("Tweak sky atmosphere mie or sun disk."), P));
	}
	{
		TSharedRef<FJsonObject> P = Xyz();
		P->SetObjectField(TEXT("density"), Prop(TEXT("number"), TEXT("Fog density")));
		P->SetObjectField(TEXT("r"), Prop(TEXT("number"), TEXT("R")));
		P->SetObjectField(TEXT("g"), Prop(TEXT("number"), TEXT("G")));
		P->SetObjectField(TEXT("b"), Prop(TEXT("number"), TEXT("B")));
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Label")));
		Tools.Add(MakeTool(TEXT("spawn_exponential_fog"), TEXT("Spawn exponential height fog. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("density"), Prop(TEXT("number"), TEXT("Fog density")));
		P->SetObjectField(TEXT("r"), Prop(TEXT("number"), TEXT("R")));
		P->SetObjectField(TEXT("g"), Prop(TEXT("number"), TEXT("G")));
		P->SetObjectField(TEXT("b"), Prop(TEXT("number"), TEXT("B")));
		Tools.Add(MakeTool(TEXT("set_exponential_fog"), TEXT("Edit exponential fog. Undoable."), P));
	}
	Tools.Add(MakeTool(TEXT("spawn_post_process_volume"), TEXT("Spawn unbound post-process volume. Undoable."), EmptyProps()));
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("exposure"), Prop(TEXT("number"), TEXT("Auto exposure bias")));
		Tools.Add(MakeTool(TEXT("set_post_exposure"), TEXT("Set post-process exposure bias."), P));
	}

	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("prefix"), Prop(TEXT("string"), TEXT("/Game prefix")));
		P->SetObjectField(TEXT("max"), Prop(TEXT("number"), TEXT("Max rows")));
		Tools.Add(MakeTool(TEXT("list_assets"), TEXT("List assets under a /Game prefix."), P));
	}
	Tools.Add(MakeTool(TEXT("list_static_meshes"), TEXT("List static meshes under /Game."), EmptyProps()));
	Tools.Add(MakeTool(TEXT("list_blueprints"), TEXT("List blueprints under /Game."), EmptyProps()));
	Tools.Add(MakeTool(TEXT("list_textures"), TEXT("List textures under /Game."), EmptyProps()));
	Tools.Add(MakeTool(TEXT("list_sounds"), TEXT("List sounds under /Game."), EmptyProps()));
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Asset path")));
		Tools.Add(MakeTool(TEXT("asset_exists"), TEXT("Whether an asset path exists."), P));
	}

	Tools.Add(MakeTool(TEXT("list_components"), TEXT("List components on an actor."), LabelProps()));
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("mesh"), Prop(TEXT("string"), TEXT("Static mesh path")));
		Tools.Add(MakeTool(TEXT("set_static_mesh"), TEXT("Replace the actor static mesh. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("component"), Prop(TEXT("string"), TEXT("Component name")));
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("X")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Y")));
		P->SetObjectField(TEXT("z"), Prop(TEXT("number"), TEXT("Z")));
		Tools.Add(MakeTool(TEXT("set_component_relative_transform"), TEXT("Set component relative location. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("component"), Prop(TEXT("string"), TEXT("Component name")));
		P->SetObjectField(TEXT("visible"), Prop(TEXT("boolean"), TEXT("Visible")));
		Tools.Add(MakeTool(TEXT("set_component_visible"), TEXT("Show or hide a component. Undoable."), P));
	}

	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("simulate"), Prop(TEXT("boolean"), TEXT("Simulate physics")));
		Tools.Add(MakeTool(TEXT("set_simulate_physics"), TEXT("Toggle physics simulation. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("enabled"), Prop(TEXT("boolean"), TEXT("Collision enabled")));
		Tools.Add(MakeTool(TEXT("set_collision_enabled"), TEXT("Enable or disable collision. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("profile"), Prop(TEXT("string"), TEXT("Collision profile name")));
		Tools.Add(MakeTool(TEXT("set_collision_profile"), TEXT("Set collision profile. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("gravity"), Prop(TEXT("boolean"), TEXT("Enable gravity")));
		Tools.Add(MakeTool(TEXT("set_actor_enable_gravity"), TEXT("Toggle gravity. Undoable."), P));
	}

	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("folder"), Prop(TEXT("string"), TEXT("Outliner folder path")));
		Tools.Add(MakeTool(TEXT("set_actor_folder"), TEXT("Move actor to an outliner folder. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("folder"), Prop(TEXT("string"), TEXT("Folder path")));
		Tools.Add(MakeTool(TEXT("create_actor_folder"), TEXT("Create an outliner folder."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("hidden"), Prop(TEXT("boolean"), TEXT("Hidden in game")));
		Tools.Add(MakeTool(TEXT("set_actor_hidden_in_game"), TEXT("Set Hidden in Game. Undoable."), P));
	}
	Tools.Add(MakeTool(TEXT("isolate_actor"), TEXT("Select and frame one actor."), LabelProps()));

	{
		TSharedRef<FJsonObject> P = Xyz();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Sound asset path")));
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Label")));
		Tools.Add(MakeTool(TEXT("spawn_ambient_sound"), TEXT("Spawn an ambient sound actor. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("volume"), Prop(TEXT("number"), TEXT("Volume multiplier")));
		Tools.Add(MakeTool(TEXT("set_sound_volume"), TEXT("Set ambient sound volume. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("playing"), Prop(TEXT("boolean"), TEXT("Play")));
		Tools.Add(MakeTool(TEXT("set_sound_playing"), TEXT("Play or stop ambient sound."), P));
	}
	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("radius"), Prop(TEXT("number"), TEXT("Falloff distance")));
		Tools.Add(MakeTool(TEXT("set_sound_attenuation"), TEXT("Set sound falloff. Undoable."), P));
	}

	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("/Game path")));
		Tools.Add(MakeTool(TEXT("create_level_sequence"), TEXT("Create an empty level sequence asset."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Sequence path")));
		Tools.Add(MakeTool(TEXT("open_level_sequence"), TEXT("Open a level sequence in the editor."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Sequence path")));
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Actor label")));
		Tools.Add(MakeTool(TEXT("sequence_bind_actor"), TEXT("Bind an actor to a level sequence."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Sequence path")));
		P->SetObjectField(TEXT("start"), Prop(TEXT("number"), TEXT("Start frame")));
		P->SetObjectField(TEXT("end"), Prop(TEXT("number"), TEXT("End frame")));
		Tools.Add(MakeTool(TEXT("sequence_set_range"), TEXT("Set sequence playback range."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Sequence path")));
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Bound actor name from sequence_bind_actor")));
		P->SetObjectField(TEXT("frame"), Prop(TEXT("number"), TEXT("Frame number")));
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("Translation X")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Translation Y")));
		P->SetObjectField(TEXT("z"), Prop(TEXT("number"), TEXT("Translation Z")));
		P->SetObjectField(TEXT("pitch"), Prop(TEXT("number"), TEXT("Pitch")));
		P->SetObjectField(TEXT("yaw"), Prop(TEXT("number"), TEXT("Yaw")));
		P->SetObjectField(TEXT("roll"), Prop(TEXT("number"), TEXT("Roll")));
		Tools.Add(MakeTool(TEXT("sequence_add_key"), TEXT("Key a bound actor's transform on a level sequence. Only fields you pass are keyed."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Sequence path")));
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Bound camera actor name")));
		P->SetObjectField(TEXT("start"), Prop(TEXT("number"), TEXT("Start frame")));
		P->SetObjectField(TEXT("end"), Prop(TEXT("number"), TEXT("End frame")));
		Tools.Add(MakeTool(TEXT("sequence_camera_cut"), TEXT("Add a camera cut on a level sequence."), P));
	}

	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("/Game path")));
		P->SetObjectField(TEXT("parent"), Prop(TEXT("string"), TEXT("Parent class path, default Actor")));
		Tools.Add(MakeTool(TEXT("create_blueprint"), TEXT("Create an empty Blueprint asset (no graph)."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Blueprint path")));
		Tools.Add(MakeTool(TEXT("compile_blueprint"), TEXT("Compile a Blueprint."), P));
	}
	{
		TSharedRef<FJsonObject> P = Xyz();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Blueprint path")));
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Label")));
		Tools.Add(MakeTool(TEXT("spawn_blueprint"), TEXT("Spawn a Blueprint actor. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Blueprint path")));
		P->SetObjectField(TEXT("graph"), Prop(TEXT("string"), TEXT("Graph name. Empty is the event graph. Use a function name after add_blueprint_function.")));
		Tools.Add(MakeTool(TEXT("list_blueprint_nodes"), TEXT("List nodes, ids, and pin names on one Blueprint graph."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Blueprint path")));
		Tools.Add(MakeTool(TEXT("list_blueprint_variables"), TEXT("List Blueprint variables, components, and functions."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Blueprint path")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Variable name")));
		P->SetObjectField(TEXT("type"), Prop(TEXT("string"), TEXT("bool, int, float, string, name, vector, rotator, transform, object, or class. Default float.")));
		P->SetObjectField(TEXT("class"), Prop(TEXT("string"), TEXT("Object or class type path when type is object or class.")));
		P->SetObjectField(TEXT("value"), Prop(TEXT("string"), TEXT("Optional default value")));
		Tools.Add(MakeTool(TEXT("add_blueprint_variable"), TEXT("Add a member variable to a Blueprint."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Blueprint path")));
		P->SetObjectField(TEXT("class"), Prop(TEXT("string"), TEXT("static_mesh, box, sphere, capsule, scene, camera, arrow, audio, or a component class path")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Component variable name")));
		P->SetObjectField(TEXT("profile"), Prop(TEXT("string"), TEXT("Optional collision profile, such as OverlapAllDynamic")));
		P->SetObjectField(TEXT("radius"), Prop(TEXT("number"), TEXT("Sphere radius when class is sphere")));
		Tools.Add(MakeTool(TEXT("add_blueprint_component"), TEXT("Add a component to a Blueprint."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Blueprint path")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Function name")));
		Tools.Add(MakeTool(TEXT("add_blueprint_function"), TEXT("Add a function graph. Then add nodes with graph set to that name."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Blueprint path")));
		P->SetObjectField(TEXT("graph"), Prop(TEXT("string"), TEXT("Graph name. Empty is the event graph.")));
		P->SetObjectField(TEXT("kind"), Prop(TEXT("string"), TEXT("event, call, branch, sequence, cast, get, set, custom_event, spawn, delay, self, timeline, input, print, or make_vector. Default call.")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Event, function, variable, custom event, or timeline name.")));
		P->SetObjectField(TEXT("id"), Prop(TEXT("string"), TEXT("Id used by connect_blueprint. Defaults to name, or kind when name is empty.")));
		P->SetObjectField(TEXT("class"), Prop(TEXT("string"), TEXT("Class path for call, cast, or spawn.")));
		P->SetObjectField(TEXT("action"), Prop(TEXT("string"), TEXT("Input Action asset path when kind is input.")));
		P->SetObjectField(TEXT("value"), Prop(TEXT("string"), TEXT("Duration for delay, or text for print.")));
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("Node X")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Node Y")));
		Tools.Add(MakeTool(TEXT("add_blueprint_node"), TEXT("Add a gameplay node to a Blueprint graph. Read the returned pin names before connecting."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Blueprint path")));
		P->SetObjectField(TEXT("graph"), Prop(TEXT("string"), TEXT("Graph name. Empty is the event graph.")));
		P->SetObjectField(TEXT("from"), Prop(TEXT("string"), TEXT("Source node id")));
		P->SetObjectField(TEXT("from_pin"), Prop(TEXT("string"), TEXT("Source pin. Empty uses the first output.")));
		P->SetObjectField(TEXT("to"), Prop(TEXT("string"), TEXT("Target node id")));
		P->SetObjectField(TEXT("to_pin"), Prop(TEXT("string"), TEXT("Target pin. Empty uses the first input.")));
		Tools.Add(MakeTool(TEXT("connect_blueprint"), TEXT("Connect two Blueprint nodes by id."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Blueprint path")));
		P->SetObjectField(TEXT("graph"), Prop(TEXT("string"), TEXT("Graph name. Empty is the event graph.")));
		P->SetObjectField(TEXT("id"), Prop(TEXT("string"), TEXT("Node id")));
		P->SetObjectField(TEXT("pin"), Prop(TEXT("string"), TEXT("Input pin name")));
		P->SetObjectField(TEXT("value"), Prop(TEXT("string"), TEXT("Default value")));
		Tools.Add(MakeTool(TEXT("set_blueprint_pin"), TEXT("Set a Blueprint pin default."), P));
	}
	Tools.Add(MakeTool(TEXT("list_landscapes"), TEXT("List landscapes with world bounds, scale, material, and paint layers."), EmptyProps()));
	{
		TSharedRef<FJsonObject> P = Xyz();
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Outliner label. Default EP_Landscape.")));
		P->SetObjectField(TEXT("size"), Prop(TEXT("number"), TEXT("Approximate width in metres. Default 252.")));
		P->SetObjectField(TEXT("scale"), Prop(TEXT("number"), TEXT("cm per quad. Default 100.")));
		P->SetObjectField(TEXT("quads"), Prop(TEXT("number"), TEXT("Quads per section: 7, 15, 31, 63, 127, or 255. Default 63.")));
		P->SetObjectField(TEXT("sections"), Prop(TEXT("number"), TEXT("Sections per component, 1 or 2. Default 1.")));
		P->SetObjectField(TEXT("layers"), Prop(TEXT("string"), TEXT("Paint layers. Name=texture|normal or Name=#hex, separated by ';'. The first layer fills the ground.")));
		P->SetObjectField(TEXT("tiling"), Prop(TEXT("number"), TEXT("Metres per texture repeat. Default 4.")));
		P->SetObjectField(TEXT("material"), Prop(TEXT("string"), TEXT("Existing landscape material. Empty builds one from layers.")));
		P->SetObjectField(TEXT("height_blend"), Prop(TEXT("boolean"), TEXT("Blend layers by texture height for natural edges. Default false.")));
		Tools.Add(MakeTool(TEXT("create_landscape"), TEXT("Create a flat landscape centred at x,y,z with paint layers and a layer-blend material."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("landscape"), Prop(TEXT("string"), TEXT("Landscape label. Empty is the first landscape.")));
		P->SetObjectField(TEXT("layers"), Prop(TEXT("string"), TEXT("Name=texture|normal or Name=#hex, separated by ';'.")));
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("/Game material path. Default /Game/EditorPrompt/Landscape/M_<label>.")));
		P->SetObjectField(TEXT("tiling"), Prop(TEXT("number"), TEXT("Metres per texture repeat. Default 4.")));
		P->SetObjectField(TEXT("roughness"), Prop(TEXT("number"), TEXT("0-1. Default 0.85.")));
		P->SetObjectField(TEXT("variation"), Prop(TEXT("number"), TEXT("Large-scale colour variation 0-1 that hides tiling. Default 0.3.")));
		P->SetObjectField(TEXT("assign"), Prop(TEXT("boolean"), TEXT("Assign to the landscape and register layers. Default true.")));
		P->SetObjectField(TEXT("height_blend"), Prop(TEXT("boolean"), TEXT("Blend layers by texture height for natural edges. Default false.")));
		Tools.Add(MakeTool(TEXT("create_landscape_material"), TEXT("Build or rebuild a landscape layer-blend material and register its paint layers. Landscape grass already set is kept."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("landscape"), Prop(TEXT("string"), TEXT("Landscape label. Empty is the first landscape.")));
		P->SetObjectField(TEXT("layer"), Prop(TEXT("string"), TEXT("Paint layer that grows this grass")));
		P->SetObjectField(TEXT("mesh"), Prop(TEXT("string"), TEXT("One or more static mesh paths separated by ';'")));
		P->SetObjectField(TEXT("density"), Prop(TEXT("number"), TEXT("Instances per 10 square metres, shared across meshes. Default 20.")));
		P->SetObjectField(TEXT("scale_min"), Prop(TEXT("number"), TEXT("Default 0.8")));
		P->SetObjectField(TEXT("scale_max"), Prop(TEXT("number"), TEXT("Default 1.2")));
		P->SetObjectField(TEXT("cull_end"), Prop(TEXT("number"), TEXT("Hide beyond this distance in cm. Default 6000.")));
		P->SetObjectField(TEXT("align"), Prop(TEXT("boolean"), TEXT("Tilt to the ground. Default true.")));
		P->SetObjectField(TEXT("shadow"), Prop(TEXT("boolean"), TEXT("Cast dynamic shadows. Default false.")));
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Grass type path. Default /Game/EditorPrompt/Landscape/LG_<layer>.")));
		P->SetObjectField(TEXT("clear"), Prop(TEXT("boolean"), TEXT("Remove grass from this layer.")));
		Tools.Add(MakeTool(TEXT("set_landscape_grass"), TEXT("Grow grass automatically wherever a landscape layer is painted. Painting or erasing the layer updates it."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("landscape"), Prop(TEXT("string"), TEXT("Landscape label. Empty is the first landscape.")));
		P->SetObjectField(TEXT("mode"), Prop(TEXT("string"), TEXT("raise, lower, flatten, smooth, noise, or terrace. Default raise.")));
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("Brush centre world X. Omit x and y for the whole landscape.")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Brush centre world Y")));
		P->SetObjectField(TEXT("radius"), Prop(TEXT("number"), TEXT("Brush radius cm. Default 1000.")));
		P->SetObjectField(TEXT("falloff"), Prop(TEXT("number"), TEXT("Soft edge fraction 0-1. Default 0.5.")));
		P->SetObjectField(TEXT("strength"), Prop(TEXT("number"), TEXT("cm for raise, lower, noise, and terrace step. 0-1 for flatten and smooth.")));
		P->SetObjectField(TEXT("height"), Prop(TEXT("number"), TEXT("World Z for flatten. Default is the height at the centre.")));
		P->SetObjectField(TEXT("noise_scale"), Prop(TEXT("number"), TEXT("Noise feature size in metres. Default 30.")));
		P->SetObjectField(TEXT("seed"), Prop(TEXT("number"), TEXT("Noise seed")));
		Tools.Add(MakeTool(TEXT("sculpt_landscape"), TEXT("Sculpt landscape height with a round brush or across the whole landscape."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("landscape"), Prop(TEXT("string"), TEXT("Landscape label. Empty is the first landscape.")));
		P->SetObjectField(TEXT("layer"), Prop(TEXT("string"), TEXT("Paint layer name from the landscape material")));
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("Brush centre world X. Omit x and y for the whole landscape.")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Brush centre world Y")));
		P->SetObjectField(TEXT("radius"), Prop(TEXT("number"), TEXT("Brush radius cm. Default 1000.")));
		P->SetObjectField(TEXT("falloff"), Prop(TEXT("number"), TEXT("Soft edge fraction 0-1. Default 0.5.")));
		P->SetObjectField(TEXT("strength"), Prop(TEXT("number"), TEXT("0-1. Default 1.")));
		P->SetObjectField(TEXT("erase"), Prop(TEXT("boolean"), TEXT("Remove the layer instead of adding it.")));
		P->SetObjectField(TEXT("min_slope"), Prop(TEXT("number"), TEXT("Only paint at or above this slope in degrees.")));
		P->SetObjectField(TEXT("max_slope"), Prop(TEXT("number"), TEXT("Only paint at or below this slope in degrees.")));
		P->SetObjectField(TEXT("min_z"), Prop(TEXT("number"), TEXT("Only paint at or above this world Z.")));
		P->SetObjectField(TEXT("max_z"), Prop(TEXT("number"), TEXT("Only paint at or below this world Z.")));
		P->SetObjectField(TEXT("noise"), Prop(TEXT("number"), TEXT("Break the paint into patches, 0-1.")));
		P->SetObjectField(TEXT("noise_scale"), Prop(TEXT("number"), TEXT("Patch size in metres. Default 20.")));
		P->SetObjectField(TEXT("seed"), Prop(TEXT("number"), TEXT("Patch seed")));
		Tools.Add(MakeTool(TEXT("paint_landscape_layer"), TEXT("Paint a landscape layer with a brush, or auto-paint by slope and height."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("texture"), Prop(TEXT("string"), TEXT("Imported texture with alpha, such as a grass or flower cutout")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Asset name. Default from the texture.")));
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Mesh path. Default /Game/EditorPrompt/Foliage/SM_<name>.")));
		P->SetObjectField(TEXT("width"), Prop(TEXT("number"), TEXT("Card width cm. Default 40.")));
		P->SetObjectField(TEXT("height"), Prop(TEXT("number"), TEXT("Card height cm. Default 40.")));
		P->SetObjectField(TEXT("cards"), Prop(TEXT("number"), TEXT("Crossed cards per clump, 1-8. Default 3.")));
		P->SetObjectField(TEXT("clumps"), Prop(TEXT("number"), TEXT("Clumps in one mesh, 1-12. Default 1.")));
		P->SetObjectField(TEXT("spread"), Prop(TEXT("number"), TEXT("Clump spread cm.")));
		P->SetObjectField(TEXT("tilt"), Prop(TEXT("number"), TEXT("Lean in degrees. Default 8.")));
		P->SetObjectField(TEXT("wind"), Prop(TEXT("number"), TEXT("Top sway in cm. 0 disables. Default 4.")));
		P->SetObjectField(TEXT("seed"), Prop(TEXT("number"), TEXT("Layout seed")));
		Tools.Add(MakeTool(TEXT("create_card_mesh"), TEXT("Build a crossed-card static mesh and a masked two-sided wind material for grass, flowers, or small plants."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("mesh"), Prop(TEXT("string"), TEXT("Static mesh path")));
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Foliage type path. Default /Game/EditorPrompt/Foliage/FT_<mesh>.")));
		P->SetObjectField(TEXT("scale_min"), Prop(TEXT("number"), TEXT("Default 0.8")));
		P->SetObjectField(TEXT("scale_max"), Prop(TEXT("number"), TEXT("Default 1.2")));
		P->SetObjectField(TEXT("align"), Prop(TEXT("boolean"), TEXT("Align to the ground normal. Default false.")));
		P->SetObjectField(TEXT("random_yaw"), Prop(TEXT("boolean"), TEXT("Default true")));
		P->SetObjectField(TEXT("shadow"), Prop(TEXT("boolean"), TEXT("Cast shadows. Default true. Small grass is cheaper with false.")));
		P->SetObjectField(TEXT("cull_end"), Prop(TEXT("number"), TEXT("Hide beyond this distance in cm. 0 never.")));
		P->SetObjectField(TEXT("z_offset"), Prop(TEXT("number"), TEXT("Sink or lift in cm")));
		Tools.Add(MakeTool(TEXT("create_foliage_type"), TEXT("Create or update a foliage type for a static mesh."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("type"), Prop(TEXT("string"), TEXT("Foliage type path")));
		P->SetObjectField(TEXT("mesh"), Prop(TEXT("string"), TEXT("Static mesh path. A foliage type is made if missing.")));
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("Brush centre world X. Omit x and y to cover the landscape.")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Brush centre world Y")));
		P->SetObjectField(TEXT("radius"), Prop(TEXT("number"), TEXT("Brush radius cm. Default 1000.")));
		P->SetObjectField(TEXT("falloff"), Prop(TEXT("number"), TEXT("Thinner toward the edge, 0-1. Default 0.3.")));
		P->SetObjectField(TEXT("count"), Prop(TEXT("number"), TEXT("Instances to place. Overrides density.")));
		P->SetObjectField(TEXT("density"), Prop(TEXT("number"), TEXT("Instances per 100 square metres. Default 20.")));
		P->SetObjectField(TEXT("spacing"), Prop(TEXT("number"), TEXT("Minimum distance between instances in cm")));
		P->SetObjectField(TEXT("landscape"), Prop(TEXT("string"), TEXT("Landscape for layer filter or full cover")));
		P->SetObjectField(TEXT("layer"), Prop(TEXT("string"), TEXT("Only where this landscape layer is painted")));
		P->SetObjectField(TEXT("min_weight"), Prop(TEXT("number"), TEXT("Layer weight needed, 0-1. Default 0.5.")));
		P->SetObjectField(TEXT("max_slope"), Prop(TEXT("number"), TEXT("Skip steeper ground, degrees")));
		P->SetObjectField(TEXT("surface"), Prop(TEXT("string"), TEXT("Only place on this actor label")));
		P->SetObjectField(TEXT("scale_min"), Prop(TEXT("number"), TEXT("Default from the foliage type")));
		P->SetObjectField(TEXT("scale_max"), Prop(TEXT("number"), TEXT("Default from the foliage type")));
		P->SetObjectField(TEXT("align"), Prop(TEXT("boolean"), TEXT("Align to ground normal")));
		P->SetObjectField(TEXT("random_yaw"), Prop(TEXT("boolean"), TEXT("Default true")));
		P->SetObjectField(TEXT("z_offset"), Prop(TEXT("number"), TEXT("cm")));
		P->SetObjectField(TEXT("seed"), Prop(TEXT("number"), TEXT("Scatter seed")));
		Tools.Add(MakeTool(TEXT("paint_foliage"), TEXT("Scatter foliage instances onto the ground in a brush or across the landscape. Undoable."), P));
	}
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("type"), Prop(TEXT("string"), TEXT("Only this foliage type or mesh path part")));
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("Brush centre world X. Omit x and y for everywhere.")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Brush centre world Y")));
		P->SetObjectField(TEXT("radius"), Prop(TEXT("number"), TEXT("Brush radius cm. Default 1000.")));
		P->SetObjectField(TEXT("keep"), Prop(TEXT("number"), TEXT("Fraction to keep, 0-1, for thinning. Default 0.")));
		Tools.Add(MakeTool(TEXT("erase_foliage"), TEXT("Remove or thin foliage instances. Undoable."), P));
	}
	Tools.Add(MakeTool(TEXT("list_foliage"), TEXT("List foliage types in the level with instance counts."), EmptyProps()));
	Tools.Add(MakeTool(TEXT("list_library_assets"), TEXT("List Megascans and Fab assets already imported under /Game. Does not download."), EmptyProps()));
	Tools.Add(MakeTool(TEXT("open_asset_library"), TEXT("Open the Fab or Quixel Bridge window. The user must be signed in there to download new assets."), EmptyProps()));

	Tools.Add(MakeTool(TEXT("editor_undo"), TEXT("Undo one editor transaction. Actor spawn/move only; Niagara graphs often will not revert."), EmptyProps()));
	Tools.Add(MakeTool(TEXT("editor_redo"), TEXT("Redo one editor transaction."), EmptyProps()));
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Optional /Game Niagara or MIC path. Empty restores the last snapshotted asset.")));
		Tools.Add(MakeTool(TEXT("restore_asset"), TEXT("Restore a Niagara/MIC from a temp .uasset copy under Saved/EditorPrompt/restore. Not a screenshot. Temps are deleted when the editor exits. Use when the user says put it back / 원래대로. Do not re-author the graph."), P));
	}
	Tools.Add(MakeTool(TEXT("redraw_viewports"), TEXT("Redraw level viewports."), EmptyProps()));
	Tools.Add(MakeTool(TEXT("get_editor_world_name"), TEXT("Editor world name."), EmptyProps()));

	EditorPromptMcp::AppendRegisteredTools(Tools);
	return Tools;
}

FString FEditorPromptMcpTools::Call(const FString& Name, const TSharedPtr<FJsonObject>& Args, bool& bOk)
{
	FString Message;
	bool LocalOk = false;
	RunGame([&]()
	{
		LocalOk = false;
		UWorld* World = EditorWorld();
		if (!GEditor)
		{
			Message = TEXT("GEditor is null");
			return;
		}

		auto NeedWorld = [&]() -> bool
		{
			if (World)
			{
				return true;
			}
			Message = TEXT("No editor world");
			return false;
		};

		auto NeedActor = [&](const TCHAR* Key = TEXT("label")) -> AActor*
		{
			if (!NeedWorld())
			{
				return nullptr;
			}
			AActor* Actor = FindLabel(World, ArgStr(Args, Key));
			if (!Actor)
			{
				Message = TEXT("Actor not found");
			}
			return Actor;
		};

		MaybeSnapshotForTool(Name, Args);

		if (Name == TEXT("restore_asset"))
		{
			Message = RestoreAsset(ArgStr(Args, TEXT("path")));
			LocalOk = Message.StartsWith(TEXT("restored "));
			Redraw();
			return;
		}

		if (EditorPromptMcp::TryCallRegistered(Name, Args, World, Message, LocalOk))
		{
			return;
		}

		if (Name == TEXT("spawn_cube"))
		{
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "SpawnCube", "Editor Prompt spawn_cube"));
			Message = SpawnMeshAt(ArgLoc(Args), FRotator::ZeroRotator, TEXT("/Engine/BasicShapes/Cube.Cube"), TEXT("PromptCube"));
			LocalOk = !Message.StartsWith(TEXT("Failed")) && !Message.StartsWith(TEXT("No ")) && !Message.StartsWith(TEXT("SpawnActor"));
			return;
		}

		if (Name == TEXT("get_level_info"))
		{
			if (!NeedWorld())
			{
				return;
			}
			const FString Map = World->GetMapName();
			const FString Path = World->GetOutermost() ? World->GetOutermost()->GetName() : TEXT("");
			const bool bDirty = World->GetOutermost() && World->GetOutermost()->IsDirty();
			Message = FString::Printf(TEXT("map=%s path=%s dirty=%s"), *Map, *Path, bDirty ? TEXT("true") : TEXT("false"));
			LocalOk = true;
			return;
		}
		if (Name == TEXT("save_level"))
		{
			FEditorFileUtils::SaveCurrentLevel();
			Message = TEXT("Saved current level");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("save_level_as"))
		{
			if (!NeedWorld())
			{
				return;
			}
			FString Path = ArgStr(Args, TEXT("path"));
			if (!IsGamePath(Path))
			{
				Message = TEXT("path must start with /Game");
				return;
			}
			if (Path.EndsWith(TEXT(".umap")))
			{
				Path.LeftChopInline(5);
			}
			if (!UEditorLoadingAndSavingUtils::SaveMap(World, Path))
			{
				Message = TEXT("SaveMap failed");
				return;
			}
			Message = TEXT("Saved as ") + Path;
			LocalOk = true;
			return;
		}
		if (Name == TEXT("new_level"))
		{
			UEditorLoadingAndSavingUtils::NewBlankMap(false);
			Message = TEXT("New blank map");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("open_level"))
		{
			FString Path = ArgStr(Args, TEXT("path"));
			if (Path.IsEmpty())
			{
				Message = TEXT("path required");
				return;
			}
			if (!Path.StartsWith(TEXT("/")))
			{
				Path = TEXT("/Game/") + Path;
			}
			if (!IsGamePath(Path) && !Path.Contains(FPaths::ProjectDir()))
			{
				Message = TEXT("path must be in the project");
				return;
			}
			FString Filename = Path;
			if (Path.StartsWith(TEXT("/")))
			{
				Filename = FPackageName::LongPackageNameToFilename(Path.Replace(TEXT(".umap"), TEXT("")), FPackageName::GetMapPackageExtension());
			}
			FEditorFileUtils::LoadMap(Filename);
			Message = TEXT("Opened ") + Path;
			LocalOk = true;
			return;
		}
		if (Name == TEXT("list_levels"))
		{
			Message = ListAssetsOf(UWorld::StaticClass(), TEXT("/Game"), 40);
			LocalOk = true;
			return;
		}
		if (Name == TEXT("save_dirty_packages"))
		{
			UEditorLoadingAndSavingUtils::SaveDirtyPackages(true, true);
			Message = TEXT("Saved dirty packages");
			LocalOk = true;
			return;
		}

		if (Name == TEXT("list_actors") || Name == TEXT("find_actors"))
		{
			if (!NeedWorld())
			{
				return;
			}
			const FString Query = ArgStr(Args, TEXT("query"));
			const FString Cls = ArgStr(Args, TEXT("class"));
			FString Out;
			int32 N = 0;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->bIsEditorPreviewActor)
				{
					continue;
				}
				const FString Line = ListActorLine(*It);
				if (!Query.IsEmpty() && !It->GetActorLabel().Contains(Query))
				{
					continue;
				}
				if (!Cls.IsEmpty() && !It->GetClass()->GetName().Contains(Cls))
				{
					continue;
				}
				Out += Line + TEXT("\n");
				if (++N >= 80)
				{
					Out += TEXT("...\n");
					break;
				}
			}
			Message = Out.IsEmpty() ? TEXT("(none)") : Out;
			LocalOk = true;
			return;
		}
		if (Name == TEXT("spawn_actor"))
		{
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "SpawnActor", "Editor Prompt spawn_actor"));
			const FString ClassPath = ArgStr(Args, TEXT("class"));
			const FString MeshPath = ArgStr(Args, TEXT("mesh"));
			const FString Label = ArgStr(Args, TEXT("label"));
			if (!ClassPath.IsEmpty())
			{
				if (!NeedWorld())
				{
					return;
				}
				UClass* Cls = LoadClass<AActor>(nullptr, *ClassPath);
				if (!Cls)
				{
					Cls = FindObject<UClass>(nullptr, *ClassPath);
				}
				if (!Cls)
				{
					Message = TEXT("class not found");
					return;
				}
				World->Modify();
				AActor* Actor = World->SpawnActor<AActor>(Cls, ArgLoc(Args), ArgRot(Args));
				if (!Actor)
				{
					Message = TEXT("SpawnActor failed");
					return;
				}
				TouchActor(Actor);
				if (!Label.IsEmpty())
				{
					Actor->SetActorLabel(Label);
				}
				Redraw();
				Message = FString::Printf(TEXT("Spawned '%s'"), *Actor->GetActorLabel());
				LocalOk = true;
				return;
			}
			Message = SpawnMeshAt(ArgLoc(Args), ArgRot(Args), MeshPath, Label);
			LocalOk = Message.StartsWith(TEXT("Spawned"));
			return;
		}
		if (Name == TEXT("spawn_plane"))
		{
			if (!NeedWorld())
			{
				return;
			}
			const FVector Loc = ArgsHaveLoc(Args) ? ArgLoc(Args) : CameraFrontLocation(280.f);
			const bool bFace = ArgBool(Args, TEXT("face_camera"), true);
			const FRotator Rot = bFace ? BillboardToViewport(Loc) : ArgRot(Args);
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "SpawnPlane", "Editor Prompt spawn_plane"));
			World->Modify();
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
			if (!Mesh)
			{
				Message = TEXT("Engine Plane mesh not found");
				return;
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Loc, Rot, Params);
			if (!Actor)
			{
				Message = TEXT("Spawn plane failed");
				return;
			}
			TouchActor(Actor);
			FString Label = ArgStr(Args, TEXT("label"));
			if (Label.IsEmpty())
			{
				Label = TEXT("FlipbookCard");
			}
			Actor->SetActorLabel(Label);
			Actor->SetMobility(EComponentMobility::Movable);
			FVector Scale(1.8f, -2.6f, 1.f);
			if (ArgsHaveScale(Args))
			{
				Scale = ArgScale(Args);
			}
			else
			{
				const float Width = static_cast<float>(ArgNum(Args, TEXT("width"), 180));
				const float Height = static_cast<float>(ArgNum(Args, TEXT("height"), 260));
				Scale = FVector(Width / 100.f, -Height / 100.f, 1.f);
			}
			Actor->SetActorScale3D(Scale);
			if (UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent())
			{
				Comp->SetFlags(RF_Transactional);
				Comp->Modify();
				Comp->SetStaticMesh(Mesh);
				Comp->SetMobility(EComponentMobility::Movable);
				Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Comp->SetCastShadow(false);
				const FString MatPath = ArgStr(Args, TEXT("material"));
				if (!MatPath.IsEmpty())
				{
					if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *MatPath))
					{
						Comp->SetMaterial(0, Mat);
					}
				}
			}
			EnableFxViewport();
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(Actor, true, true, true);
			Redraw();
			Message = FString::Printf(TEXT("Spawned '%s'"), *Actor->GetActorLabel());
			LocalOk = true;
			return;
		}
		if (Name == TEXT("move_actor"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "MoveActor", "Editor Prompt move_actor"));
			TouchActor(Actor);
			Actor->SetActorLocation(ArgLoc(Args));
			Redraw();
			Message = TEXT("Moved ") + Actor->GetActorLabel();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("rotate_actor"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "RotateActor", "Editor Prompt rotate_actor"));
			TouchActor(Actor);
			Actor->SetActorRotation(ArgRot(Args));
			Redraw();
			Message = TEXT("Rotated ") + Actor->GetActorLabel();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("face_viewport_camera"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "FaceCam", "Editor Prompt face_viewport_camera"));
			TouchActor(Actor);
			Actor->SetActorRotation(BillboardToViewport(Actor->GetActorLocation()));
			Redraw();
			Message = TEXT("Faced camera ") + Actor->GetActorLabel();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("scale_actor"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "ScaleActor", "Editor Prompt scale_actor"));
			TouchActor(Actor);
			Actor->SetActorScale3D(ArgScale(Args));
			Redraw();
			Message = TEXT("Scaled ") + Actor->GetActorLabel();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_actor_transform"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "SetXform", "Editor Prompt set_actor_transform"));
			TouchActor(Actor);
			Actor->SetActorLocation(ArgLoc(Args));
			Actor->SetActorRotation(ArgRot(Args));
			Actor->SetActorScale3D(ArgScale(Args));
			Redraw();
			Message = TEXT("Transform ") + Actor->GetActorLabel();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("get_actor_transform"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FVector L = Actor->GetActorLocation();
			const FRotator R = Actor->GetActorRotation();
			const FVector S = Actor->GetActorScale3D();
			Message = FString::Printf(TEXT("loc=%.1f,%.1f,%.1f rot=%.1f,%.1f,%.1f scale=%.2f,%.2f,%.2f"),
				L.X, L.Y, L.Z, R.Pitch, R.Yaw, R.Roll, S.X, S.Y, S.Z);
			LocalOk = true;
			return;
		}
		if (Name == TEXT("duplicate_actor"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "DupActor", "Editor Prompt duplicate_actor"));
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(Actor, true, true, true);
			GEditor->edactDuplicateSelected(World->GetCurrentLevel(), true);
			AActor* Dup = nullptr;
			USelection* Sel = GEditor->GetSelectedActors();
			if (Sel && Sel->Num() > 0)
			{
				Dup = Cast<AActor>(Sel->GetSelectedObject(0));
			}
			if (Dup)
			{
				const FVector Off(ArgNum(Args, TEXT("x")), ArgNum(Args, TEXT("y")), ArgNum(Args, TEXT("z")));
				if (!Off.IsNearlyZero())
				{
					Dup->SetActorLocation(Actor->GetActorLocation() + Off);
				}
				const FString NewLabel = ArgStr(Args, TEXT("new_label"));
				if (!NewLabel.IsEmpty())
				{
					Dup->SetActorLabel(NewLabel);
				}
				Message = TEXT("Duplicated ") + Dup->GetActorLabel();
				LocalOk = true;
			}
			else
			{
				Message = TEXT("Duplicate failed");
			}
			Redraw();
			return;
		}
		if (Name == TEXT("destroy_actor"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "DestroyActor", "Editor Prompt destroy_actor"));
			const FString Lab = Actor->GetActorLabel();
			Actor->Modify();
			World->DestroyActor(Actor);
			Redraw();
			Message = TEXT("Destroyed ") + Lab;
			LocalOk = true;
			return;
		}
		if (Name == TEXT("select_actor"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(Actor, true, true, true);
			GEditor->NoteSelectionChange();
			Redraw();
			Message = TEXT("Selected ") + Actor->GetActorLabel();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("get_selected_actors"))
		{
			FString Out;
			USelection* Sel = GEditor->GetSelectedActors();
			if (Sel)
			{
				for (FSelectionIterator It(*Sel); It; ++It)
				{
					if (AActor* Actor = Cast<AActor>(*It))
					{
						Out += Actor->GetActorLabel() + TEXT("\n");
					}
				}
			}
			Message = Out.IsEmpty() ? TEXT("(none)") : Out;
			LocalOk = true;
			return;
		}
		if (Name == TEXT("rename_actor"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FString NewLabel = ArgStr(Args, TEXT("new_label"));
			if (NewLabel.IsEmpty())
			{
				Message = TEXT("new_label required");
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "RenameActor", "Editor Prompt rename_actor"));
			TouchActor(Actor);
			Actor->SetActorLabel(NewLabel);
			Message = TEXT("Renamed to ") + NewLabel;
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_actor_visible"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "VisActor", "Editor Prompt set_actor_visible"));
			TouchActor(Actor);
			Actor->SetIsTemporarilyHiddenInEditor(!ArgBool(Args, TEXT("visible"), true));
			Redraw();
			Message = TEXT("Visibility updated");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_actor_mobility"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "Mobility", "Editor Prompt set_actor_mobility"));
			TouchActor(Actor);
			const FString Mob = ArgStr(Args, TEXT("mobility"), TEXT("Movable"));
			const EComponentMobility::Type Type = Mob.Equals(TEXT("Static"), ESearchCase::IgnoreCase)
				? EComponentMobility::Static
				: EComponentMobility::Movable;
			if (USceneComponent* Root = Actor->GetRootComponent())
			{
				Root->Modify();
				Root->SetMobility(Type);
			}
			Message = TEXT("Mobility set");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("attach_actor"))
		{
			AActor* Child = NeedActor(TEXT("label"));
			AActor* Parent = NeedWorld() ? FindLabel(World, ArgStr(Args, TEXT("parent"))) : nullptr;
			if (!Child || !Parent)
			{
				Message = TEXT("child or parent not found");
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "Attach", "Editor Prompt attach_actor"));
			TouchActor(Child);
			Child->AttachToActor(Parent, FAttachmentTransformRules::KeepWorldTransform);
			Message = TEXT("Attached");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("detach_actor"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "Detach", "Editor Prompt detach_actor"));
			TouchActor(Actor);
			Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			Message = TEXT("Detached");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_actor_tag"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FName Tag(*ArgStr(Args, TEXT("tag")));
			if (Tag.IsNone())
			{
				Message = TEXT("tag required");
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "Tag", "Editor Prompt set_actor_tag"));
			TouchActor(Actor);
			if (ArgBool(Args, TEXT("remove"), false))
			{
				Actor->Tags.Remove(Tag);
			}
			else
			{
				Actor->Tags.AddUnique(Tag);
			}
			Message = TEXT("Tag updated");
			LocalOk = true;
			return;
		}

		if (Name == TEXT("spawn_point_light"))
		{
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "PtLight", "Editor Prompt spawn_point_light"));
			Message = SpawnTypedLight<APointLight>(Args, TEXT("PromptPointLight"));
			LocalOk = Message.StartsWith(TEXT("Spawned"));
			if (LocalOk)
			{
				if (AActor* Actor = FindLabel(World, ArgStr(Args, TEXT("label"), TEXT("PromptPointLight"))))
				{
					if (USpotLightComponent* Ignore = nullptr)
					{
						(void)Ignore;
					}
					if (UPointLightComponent* C = Cast<UPointLightComponent>(LightComp(Actor)))
					{
						const double Radius = ArgNum(Args, TEXT("radius"), -1);
						if (Radius > 0)
						{
							C->SetAttenuationRadius(static_cast<float>(Radius));
						}
					}
				}
			}
			return;
		}
		if (Name == TEXT("spawn_spot_light"))
		{
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "Spot", "Editor Prompt spawn_spot_light"));
			Message = SpawnTypedLight<ASpotLight>(Args, TEXT("PromptSpotLight"));
			LocalOk = Message.StartsWith(TEXT("Spawned"));
			if (ASpotLight* Spot = Cast<ASpotLight>(FindLabel(World, ArgStr(Args, TEXT("label"), TEXT("PromptSpotLight")))))
			{
				if (USpotLightComponent* C = Cast<USpotLightComponent>(Spot->GetLightComponent()))
				{
					const double Inner = ArgNum(Args, TEXT("inner_cone"), -1);
					const double Outer = ArgNum(Args, TEXT("outer_cone"), -1);
					if (Inner >= 0)
					{
						C->SetInnerConeAngle(static_cast<float>(Inner));
					}
					if (Outer >= 0)
					{
						C->SetOuterConeAngle(static_cast<float>(Outer));
					}
				}
			}
			return;
		}
		if (Name == TEXT("spawn_directional_light"))
		{
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "DirLight", "Editor Prompt spawn_directional_light"));
			Message = SpawnTypedLight<ADirectionalLight>(Args, TEXT("PromptSun"));
			LocalOk = Message.StartsWith(TEXT("Spawned"));
			return;
		}
		if (Name == TEXT("spawn_rect_light"))
		{
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "RectLight", "Editor Prompt spawn_rect_light"));
			Message = SpawnTypedLight<ARectLight>(Args, TEXT("PromptRectLight"));
			LocalOk = Message.StartsWith(TEXT("Spawned"));
			if (ARectLight* Rect = Cast<ARectLight>(FindLabel(World, ArgStr(Args, TEXT("label"), TEXT("PromptRectLight")))))
			{
				if (URectLightComponent* C = Cast<URectLightComponent>(Rect->GetLightComponent()))
				{
					const double W = ArgNum(Args, TEXT("width"), -1);
					const double H = ArgNum(Args, TEXT("height"), -1);
					if (W > 0)
					{
						C->SetSourceWidth(static_cast<float>(W));
					}
					if (H > 0)
					{
						C->SetSourceHeight(static_cast<float>(H));
					}
				}
			}
			return;
		}
		if (Name == TEXT("spawn_sky_light"))
		{
			if (!NeedWorld())
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "SkyLight", "Editor Prompt spawn_sky_light"));
			World->Modify();
			ASkyLight* Light = World->SpawnActor<ASkyLight>(ArgLoc(Args), ArgRot(Args));
			if (!Light)
			{
				Message = TEXT("Spawn light failed");
				return;
			}
			TouchActor(Light);
			const FString Label = ArgStr(Args, TEXT("label"), TEXT("PromptSkyLight"));
			if (!Label.IsEmpty())
			{
				Light->SetActorLabel(Label);
			}
			if (USkyLightComponent* C = Light->GetLightComponent())
			{
				C->Modify();
				C->SetLightColor(ArgColor(Args));
				const double Intensity = ArgNum(Args, TEXT("intensity"), -1);
				if (Intensity >= 0)
				{
					C->SetIntensity(static_cast<float>(Intensity));
				}
			}
			Redraw();
			Message = FString::Printf(TEXT("Spawned light '%s'"), *Light->GetActorLabel());
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_light_color"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "LightColor", "Editor Prompt set_light_color"));
			if (ULightComponent* C = LightComp(Actor))
			{
				C->Modify();
				C->SetLightColor(ArgColor(Args));
			}
			else if (USkyLightComponent* Sky = Actor->FindComponentByClass<USkyLightComponent>())
			{
				Sky->Modify();
				Sky->SetLightColor(ArgColor(Args));
			}
			else
			{
				Message = TEXT("Not a light");
				return;
			}
			Redraw();
			Message = TEXT("Light color set");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_light_intensity"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "LightInt", "Editor Prompt set_light_intensity"));
			const float Intensity = static_cast<float>(ArgNum(Args, TEXT("intensity"), 8));
			if (ULightComponent* C = LightComp(Actor))
			{
				C->Modify();
				C->SetIntensity(Intensity);
			}
			else if (USkyLightComponent* Sky = Actor->FindComponentByClass<USkyLightComponent>())
			{
				Sky->Modify();
				Sky->SetIntensity(Intensity);
			}
			else
			{
				Message = TEXT("Not a light");
				return;
			}
			Redraw();
			Message = TEXT("Intensity set");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_light_attenuation"))
		{
			AActor* Actor = NeedActor();
			UPointLightComponent* C = Actor ? Actor->FindComponentByClass<UPointLightComponent>() : nullptr;
			if (!C)
			{
				Message = TEXT("Not a point/spot light");
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "LightAtt", "Editor Prompt set_light_attenuation"));
			C->Modify();
			C->SetAttenuationRadius(static_cast<float>(ArgNum(Args, TEXT("radius"), 1000)));
			Redraw();
			Message = TEXT("Attenuation set");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("recapture_sky_light"))
		{
			AActor* Actor = NeedActor();
			USkyLightComponent* C = Actor ? Actor->FindComponentByClass<USkyLightComponent>() : nullptr;
			if (!C)
			{
				Message = TEXT("Not a sky light");
				return;
			}
			C->SetCaptureIsDirty();
			C->RecaptureSky();
			Redraw();
			Message = TEXT("Sky light recaptured");
			LocalOk = true;
			return;
		}

		auto NiagaraComp = [&](AActor* Actor) -> UNiagaraComponent*
		{
			return Actor ? Actor->FindComponentByClass<UNiagaraComponent>() : nullptr;
		};

		if (Name == TEXT("list_niagara_assets"))
		{
			Message = ListAssetsOf(UNiagaraSystem::StaticClass(), TEXT("/Game"), 40);
			LocalOk = true;
			return;
		}
		if (Name == TEXT("list_niagara_templates"))
		{
			FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
			const FString Look = ArgStr(Args, TEXT("look"), TEXT("all")).ToLower();
			Message = ListNiagaraTemplates(Look.IsEmpty() ? TEXT("all") : Look, ArgInt(Args, TEXT("max"), 20));
			LocalOk = true;
			return;
		}
		if (Name == TEXT("follow_niagara"))
		{
			FEditorPromptFollowBinding Binding;
			Binding.Path = ArgStr(Args, TEXT("path"));
			Binding.Actor = ArgStr(Args, TEXT("actor"));
			Binding.Overlap = ArgStr(Args, TEXT("overlap"));
			Binding.Id = ArgStr(Args, TEXT("id"));
			Binding.Offset = FVector(ArgNum(Args, TEXT("x")), ArgNum(Args, TEXT("y")), ArgNum(Args, TEXT("z")));
			Binding.OverlapPad = static_cast<float>(ArgNum(Args, TEXT("pad"), 0));
			Binding.Component = ArgStr(Args, TEXT("component"));
			Binding.Socket = ArgStr(Args, TEXT("socket"));
			Binding.OverlapComponent = ArgStr(Args, TEXT("overlap_component"));
			Binding.UserFloat = ArgStr(Args, TEXT("user_float"));
			Binding.UserPosition = ArgStr(Args, TEXT("user_position"));
			Binding.bKillOnStop = ArgBool(Args, TEXT("kill"), true);
			if (Args.IsValid() && Args->HasField(TEXT("surface_z")))
			{
				Binding.bLockSurfaceZ = true;
				Binding.SurfaceZ = static_cast<float>(ArgNum(Args, TEXT("surface_z")));
			}
			Message = FEditorPromptFollow::Add(Binding);
			LocalOk = Message.StartsWith(TEXT("follow id="));
			return;
		}
		if (Name == TEXT("clear_niagara_follow"))
		{
			Message = FEditorPromptFollow::Remove(ArgStr(Args, TEXT("actor")));
			LocalOk = !Message.Contains(TEXT("no follow"));
			return;
		}
		if (Name == TEXT("distort_normals"))
		{
			FEditorPromptDistortBinding Binding;
			Binding.MaterialPath = ArgStr(Args, TEXT("material"));
			Binding.Actor = ArgStr(Args, TEXT("actor"), TEXT("player"));
			Binding.Overlap = ArgStr(Args, TEXT("overlap"));
			Binding.Component = ArgStr(Args, TEXT("component"));
			Binding.OverlapComponent = ArgStr(Args, TEXT("overlap_component"));
			Binding.Radius = static_cast<float>(ArgNum(Args, TEXT("radius"), 180));
			Binding.Strength = static_cast<float>(ArgNum(Args, TEXT("strength"), 0.45));
			Binding.OverlapPad = static_cast<float>(ArgNum(Args, TEXT("pad"), 0));
			if (Args.IsValid() && Args->HasField(TEXT("surface_z")))
			{
				Binding.bLockSurfaceZ = true;
				Binding.SurfaceZ = static_cast<float>(ArgNum(Args, TEXT("surface_z")));
			}
			Message = FEditorPromptDistort::Apply(Binding);
			LocalOk = Message.StartsWith(TEXT("distort material="));
			return;
		}
		if (Name == TEXT("clear_normal_distort"))
		{
			Message = FEditorPromptDistort::Clear();
			LocalOk = Message.StartsWith(TEXT("normal distort"));
			return;
		}
		if (Name == TEXT("add_niagara_user"))
		{
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			FString Param = ArgStr(Args, TEXT("name"));
			if (Param.IsEmpty())
			{
				Message = TEXT("name is empty");
				return;
			}
			if (!Param.Contains(TEXT(".")))
			{
				Param = TEXT("User.") + Param;
			}
			const FString TypeName = ArgStr(Args, TEXT("type"), TEXT("float")).ToLower();
			FNiagaraTypeDefinition Type = FNiagaraTypeDefinition::GetFloatDef();
			if (TypeName.Contains(TEXT("vec")) || TypeName.Contains(TEXT("position")))
			{
				Type = FNiagaraTypeDefinition::GetVec3Def();
			}
			else if (TypeName.Contains(TEXT("color")))
			{
				Type = FNiagaraTypeDefinition::GetColorDef();
			}
			else if (TypeName.Contains(TEXT("bool")))
			{
				Type = FNiagaraTypeDefinition::GetBoolDef();
			}
			FNiagaraVariable Variable(Type, *Param);
			Sys->Modify();
			const bool bAdded = Sys->GetExposedParameters().AddParameter(Variable, true, true);
			Sys->MarkPackageDirty();
			Message = (bAdded ? TEXT("added ") : TEXT("present ")) + Param;
			LocalOk = true;
			return;
		}
		if (Name == TEXT("add_niagara_collision"))
		{
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			LocalOk = AddNiagaraModuleByRef(
				Sys,
				ENiagaraScriptUsage::ParticleUpdateScript,
				ArgStr(Args, TEXT("emitter")),
				TEXT("Collision"),
				FString(),
				Message);
			return;
		}
		if (Name == TEXT("stamp_trail"))
		{
			FEditorPromptTrailBinding Binding;
			Binding.Actor = ArgStr(Args, TEXT("actor"), TEXT("player"));
			Binding.Id = ArgStr(Args, TEXT("id"));
			Binding.Socket = ArgStr(Args, TEXT("socket"));
			Binding.Component = ArgStr(Args, TEXT("component"));
			Binding.Overlap = ArgStr(Args, TEXT("overlap"));
			Binding.OverlapComponent = ArgStr(Args, TEXT("overlap_component"));
			Binding.BoundsActor = ArgStr(Args, TEXT("bounds_actor"));
			Binding.MaterialPath = ArgStr(Args, TEXT("material"));
			Binding.Parameter = ArgStr(Args, TEXT("parameter"), TEXT("Trail"));
			Binding.Size = static_cast<float>(ArgNum(Args, TEXT("size"), 2000));
			Binding.Radius = static_cast<float>(ArgNum(Args, TEXT("radius"), 80));
			Binding.Fade = static_cast<float>(ArgNum(Args, TEXT("fade"), 0.75));
			Binding.OverlapPad = static_cast<float>(ArgNum(Args, TEXT("pad"), 0));
			Binding.Resolution = ArgInt(Args, TEXT("resolution"), 512);
			if (Args.IsValid() && (Args->HasField(TEXT("center_x")) || Args->HasField(TEXT("center_y"))))
			{
				Binding.bHasCenter = true;
				Binding.CenterX = static_cast<float>(ArgNum(Args, TEXT("center_x")));
				Binding.CenterY = static_cast<float>(ArgNum(Args, TEXT("center_y")));
			}
			Message = FEditorPromptTrail::Apply(Binding);
			LocalOk = Message.StartsWith(TEXT("trail id="));
			return;
		}
		if (Name == TEXT("clear_stamp_trail"))
		{
			Message = FEditorPromptTrail::Clear(ArgStr(Args, TEXT("id")));
			LocalOk = !Message.Contains(TEXT("no trail"));
			return;
		}
		if (Name == TEXT("list_material_expressions"))
		{
			Message = FEditorPromptGraphs::ListMaterialExpressions(ArgStr(Args, TEXT("material")));
			LocalOk = !Message.Contains(TEXT("not found")) && !Message.StartsWith(TEXT("material path")) && !Message.StartsWith(TEXT("engine material"));
			return;
		}
		if (Name == TEXT("add_material_expression"))
		{
			Message = FEditorPromptGraphs::AddMaterialExpression(ArgStr(Args, TEXT("material")), Args);
			LocalOk = Message.StartsWith(TEXT("added ")) || Message.StartsWith(TEXT("expression exists"));
			return;
		}
		if (Name == TEXT("connect_material"))
		{
			Message = FEditorPromptGraphs::ConnectMaterial(ArgStr(Args, TEXT("material")), Args);
			LocalOk = Message.StartsWith(TEXT("connected"));
			return;
		}
		if (Name == TEXT("list_blueprint_nodes"))
		{
			Message = FEditorPromptGraphs::ListBlueprintNodes(ArgStr(Args, TEXT("path")), ArgStr(Args, TEXT("graph")));
			LocalOk = !Message.Contains(TEXT("not found"));
			return;
		}
		if (Name == TEXT("list_blueprint_variables"))
		{
			Message = FEditorPromptGraphs::ListBlueprintVariables(ArgStr(Args, TEXT("path")));
			LocalOk = !Message.Contains(TEXT("not found"));
			return;
		}
		if (Name == TEXT("add_blueprint_variable"))
		{
			Message = FEditorPromptGraphs::AddBlueprintVariable(ArgStr(Args, TEXT("path")), Args);
			LocalOk = Message.StartsWith(TEXT("variable "));
			return;
		}
		if (Name == TEXT("add_blueprint_component"))
		{
			Message = FEditorPromptGraphs::AddBlueprintComponent(ArgStr(Args, TEXT("path")), Args);
			LocalOk = Message.StartsWith(TEXT("component "));
			return;
		}
		if (Name == TEXT("add_blueprint_function"))
		{
			Message = FEditorPromptGraphs::AddBlueprintFunction(ArgStr(Args, TEXT("path")), Args);
			LocalOk = Message.StartsWith(TEXT("function "));
			return;
		}
		if (Name == TEXT("add_blueprint_node"))
		{
			Message = FEditorPromptGraphs::AddBlueprintNode(ArgStr(Args, TEXT("path")), Args);
			LocalOk = Message.StartsWith(TEXT("added ")) || Message.StartsWith(TEXT("node exists"));
			return;
		}
		if (Name == TEXT("connect_blueprint"))
		{
			Message = FEditorPromptGraphs::ConnectBlueprint(ArgStr(Args, TEXT("path")), Args);
			LocalOk = Message.StartsWith(TEXT("connected"));
			return;
		}
		if (Name == TEXT("set_blueprint_pin"))
		{
			Message = FEditorPromptGraphs::SetBlueprintPin(ArgStr(Args, TEXT("path")), Args);
			LocalOk = Message.StartsWith(TEXT("pin set"));
			return;
		}
		if (FEditorPromptWorld::Handle(Name, Args, Message, LocalOk))
		{
			return;
		}
		if (Name == TEXT("list_library_assets"))
		{
			Message = FEditorPromptGraphs::ListLibraryAssets();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("open_asset_library"))
		{
			Message = FEditorPromptGraphs::OpenAssetLibrary();
			LocalOk = Message.Contains(TEXT("opened"));
			return;
		}
		if (Name == TEXT("sequence_add_key"))
		{
			Message = FEditorPromptGraphs::AddSequenceKey(ArgStr(Args, TEXT("path")), Args);
			LocalOk = Message.StartsWith(TEXT("keyed "));
			return;
		}
		if (Name == TEXT("sequence_camera_cut"))
		{
			Message = FEditorPromptGraphs::AddCameraCut(ArgStr(Args, TEXT("path")), Args);
			LocalOk = Message.StartsWith(TEXT("camera cut"));
			return;
		}
		if (Name == TEXT("spawn_niagara"))
		{
			if (!NeedWorld())
			{
				return;
			}
			const FString Path = ArgStr(Args, TEXT("path"));
			UNiagaraSystem* Sys = LoadObject<UNiagaraSystem>(nullptr, *Path);
			if (!Sys)
			{
				Message = TEXT("Niagara system not found");
				return;
			}
			const FVector Loc = ArgsHaveLoc(Args) ? ArgLoc(Args) : CameraFrontLocation(280.f);
			const FVector Scale = ArgsHaveScale(Args) ? ArgScale(Args) : FVector(4.f);
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "Niagara", "Editor Prompt spawn_niagara"));
			World->Modify();
			ANiagaraActor* Actor = World->SpawnActor<ANiagaraActor>(Loc, ArgRot(Args));
			if (!Actor)
			{
				Message = TEXT("Spawn Niagara failed");
				return;
			}
			TouchActor(Actor);
			const FString Label = ArgStr(Args, TEXT("label"));
			if (!Label.IsEmpty())
			{
				Actor->SetActorLabel(Label);
			}
			Actor->SetActorScale3D(Scale);
			if (UNiagaraComponent* C = Actor->GetNiagaraComponent())
			{
				C->SetAsset(Sys);
				C->SetAutoActivate(true);
				C->Activate(true);
			}
			EnableFxViewport();
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(Actor, true, true, true);
			GEditor->MoveViewportCamerasToActor(*Actor, false);
			Redraw();
			Message = TEXT("Spawned Niagara ") + Actor->GetActorLabel();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_niagara_playing"))
		{
			UNiagaraComponent* C = NiagaraComp(NeedActor());
			if (!C)
			{
				Message = TEXT("No Niagara component");
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "NiagaraPlay", "Editor Prompt set_niagara_playing"));
			C->Modify();
			if (ArgBool(Args, TEXT("playing"), true))
			{
				C->Activate(true);
			}
			else
			{
				C->Deactivate();
			}
			Message = TEXT("Niagara playing updated");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("reset_niagara"))
		{
			UNiagaraComponent* C = NiagaraComp(NeedActor());
			if (!C)
			{
				Message = TEXT("No Niagara component");
				return;
			}
			C->ResetSystem();
			Message = TEXT("Niagara reset");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_niagara_float"))
		{
			UNiagaraComponent* C = NiagaraComp(NeedActor());
			if (!C)
			{
				Message = TEXT("No Niagara component");
				return;
			}
			C->SetVariableFloat(*ArgStr(Args, TEXT("name")), static_cast<float>(ArgNum(Args, TEXT("value"))));
			Message = TEXT("Niagara float set");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_niagara_color"))
		{
			UNiagaraComponent* C = NiagaraComp(NeedActor());
			if (!C)
			{
				Message = TEXT("No Niagara component");
				return;
			}
			C->SetVariableLinearColor(*ArgStr(Args, TEXT("name")), ArgColor(Args));
			Message = TEXT("Niagara color set");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("create_niagara_system"))
		{
			FString Path = ArgStr(Args, TEXT("path"));
			if (Path.IsEmpty())
			{
				Message = TEXT("path required");
				return;
			}
			FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
			const bool bDefaults = Args.IsValid() && Args->HasField(TEXT("defaults"))
				? ArgBool(Args, TEXT("defaults"), true)
				: true;
			FString Template = ArgStr(Args, TEXT("template"));
			const FString Look = ArgStr(Args, TEXT("look")).ToLower();
			if (Template.IsEmpty() && !Look.IsEmpty())
			{
				Template = PickNiagaraTemplatePath(Look);
			}
			Message = CreateNiagaraSystemAt(Path, bDefaults, Template);
			LocalOk = NiagaraCompiledOk(Message);
			return;
		}
		if (Name == TEXT("create_niagara_grid2d"))
		{
			FString Path = ArgStr(Args, TEXT("path"));
			if (Path.IsEmpty())
			{
				Message = TEXT("path required");
				return;
			}
			FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
			const int32 Cells = FMath::RoundToInt(ArgNum(Args, TEXT("cells"), 64));
			const float WorldSize = static_cast<float>(ArgNum(Args, TEXT("world_size"), 200));
			const int32 Attributes = FMath::RoundToInt(ArgNum(Args, TEXT("attributes"), 1));
			Message = CreateNiagaraGrid2DAt(Path, Cells, WorldSize, Attributes);
			LocalOk = NiagaraCompiledOk(Message);
			return;
		}
		if (Name == TEXT("add_niagara_simulation_stage"))
		{
			FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, ArgStr(Args, TEXT("emitter")));
			if (!Handle)
			{
				Message = TEXT("Emitter not found");
				return;
			}
			UNiagaraEmitter* Emitter = Handle->GetInstance().Emitter;
			FVersionedNiagaraEmitterData* Data = Handle->GetEmitterData();
			UNiagaraGraph* Graph = EmitterGraph(Data);
			UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
			if (!Emitter || !Data || !Graph || !Source)
			{
				Message = TEXT("Emitter graph missing");
				return;
			}
			Data->SimTarget = ENiagaraSimTarget::GPUComputeSim;
			const FNiagaraVariable GridVar = MakeGrid2DVariable();
			FindOrAddGrid2DInput(Graph, GridVar);
			FString StageName = ArgStr(Args, TEXT("name"), TEXT("GridIterate"));
			if (FindSimStage(Data, StageName))
			{
				Message = TEXT("Simulation stage already exists");
				return;
			}
			UNiagaraSimulationStageGeneric* Stage = AddGridSimStage(
				Emitter, Handle->GetInstance().Version, Graph, Source, GridVar, *StageName);
			if (!Stage)
			{
				Message = TEXT("Add simulation stage failed");
				return;
			}
			Graph->NotifyGraphChanged();
			Sys->MarkPackageDirty();
			if (UNiagaraNodeOutput* StageOut = FindOutputNode(
				Graph, ENiagaraScriptUsage::ParticleSimulationStageScript, Stage->Script->GetUsageId()))
			{
				EditorPromptEnsureDiOnSimStage(Graph, StageOut, GridVar);
			}
			Message = TEXT("Added sim_stage=") + Stage->SimulationStageName.ToString() + TEXT("\n") + CompileNiagaraSystem(Sys);
			LocalOk = NiagaraCompiledOk(Message);
			return;
		}
		if (Name == TEXT("list_niagara_modules"))
		{
			FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
			FNiagaraEditorUtilities::FGetFilteredScriptAssetsOptions Filter;
			Filter.ScriptUsageToInclude = ENiagaraScriptUsage::Module;
			Filter.TargetUsageToMatch = UsageFromStack(ArgStr(Args, TEXT("stack"), TEXT("particle_update")));
			Filter.bIncludeDeprecatedScripts = false;
			Filter.bIncludeNonLibraryScripts = false;
			TArray<FAssetData> Assets;
			FNiagaraEditorUtilities::GetFilteredScriptAssets(Filter, Assets);
			const FString Query = ArgStr(Args, TEXT("query"));
			FString Out;
			int32 N = 0;
			for (const FAssetData& A : Assets)
			{
				const FString Line = A.AssetName.ToString() + TEXT(" | ") + A.GetObjectPathString();
				if (!Query.IsEmpty() && !Line.Contains(Query))
				{
					continue;
				}
				Out += Line + TEXT("\n");
				if (++N >= 50)
				{
					Out += TEXT("...\n");
					break;
				}
			}
			Message = Out.IsEmpty() ? TEXT("(none)") : Out;
			LocalOk = true;
			return;
		}
		if (Name == TEXT("list_niagara_stack"))
		{
			FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			FString Out;
			for (ENiagaraScriptUsage Usage : { ENiagaraScriptUsage::SystemSpawnScript, ENiagaraScriptUsage::SystemUpdateScript })
			{
				UNiagaraNodeOutput* Output = SystemStackOutput(Sys, Usage);
				if (!Output)
				{
					continue;
				}
				TArray<UNiagaraNodeFunctionCall*> Calls;
				CollectFunctionCalls(Output, Calls);
				Out += FString::Printf(TEXT("stack=%s\n"), UsageLabel(Usage));
				for (UNiagaraNodeFunctionCall* Call : Calls)
				{
					Out += TEXT("  module=") + Call->GetFunctionName() + TEXT("\n");
				}
			}
			static const ENiagaraScriptUsage Stacks[] = {
				ENiagaraScriptUsage::EmitterSpawnScript,
				ENiagaraScriptUsage::EmitterUpdateScript,
				ENiagaraScriptUsage::ParticleSpawnScript,
				ENiagaraScriptUsage::ParticleUpdateScript,
			};
			for (const FNiagaraEmitterHandle& Handle : Sys->GetEmitterHandles())
			{
				FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
				UNiagaraGraph* Graph = EmitterGraph(Data);
				Out += TEXT("emitter=") + Handle.GetName().ToString()
					+ TEXT(" unique=") + Handle.GetUniqueInstanceName();
				if (Data)
				{
					Out += TEXT(" sim=") + FString(Data->SimTarget == ENiagaraSimTarget::GPUComputeSim ? TEXT("GPU") : TEXT("CPU"));
				}
				Out += TEXT("\n");
				if (!Graph)
				{
					continue;
				}
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					UNiagaraNodeInput* Input = Cast<UNiagaraNodeInput>(Node);
					if (!Input || Input->Usage != ENiagaraInputNodeUsage::Parameter)
					{
						continue;
					}
					if (UNiagaraDataInterfaceGrid2DCollection* Grid = Cast<UNiagaraDataInterfaceGrid2DCollection>(GetInputNodeDataInterface(Input)))
					{
						Out += FString::Printf(
							TEXT("  di=%s type=Grid2DCollection cells=%d,%d world=%.0f,%.0f attributes=%d preview=%d\n"),
							*Input->Input.GetName().ToString(),
							Grid->NumCellsX, Grid->NumCellsY,
							Grid->WorldBBoxSize.X, Grid->WorldBBoxSize.Y,
							Grid->NumAttributes,
							Grid->bPreviewGrid ? 1 : 0);
					}
				}
				if (Data)
				{
					for (UNiagaraSimulationStageBase* Stage : Data->GetSimulationStages())
					{
						if (!Stage)
						{
							continue;
						}
						Out += TEXT("  sim_stage=") + Stage->SimulationStageName.ToString();
						if (UNiagaraSimulationStageGeneric* Generic = Cast<UNiagaraSimulationStageGeneric>(Stage))
						{
							Out += TEXT(" iterate=") + FString(
								Generic->IterationSource == ENiagaraIterationSource::DataInterface
									? TEXT("DataInterface")
									: Generic->IterationSource == ENiagaraIterationSource::DirectSet
										? TEXT("DirectSet")
										: TEXT("Particles"));
							if (Generic->DataInterface.BoundVariable.IsValid())
							{
								Out += TEXT(" bound=") + Generic->DataInterface.BoundVariable.GetName().ToString();
							}
						}
						Out += TEXT("\n");
						UNiagaraNodeOutput* StageOut = Stage->Script
							? FindOutputNode(Graph, ENiagaraScriptUsage::ParticleSimulationStageScript, Stage->Script->GetUsageId())
							: nullptr;
						TArray<UNiagaraNodeFunctionCall*> StageCalls;
						CollectFunctionCalls(StageOut, StageCalls);
						if (StageCalls.Num() == 0)
						{
							Out += TEXT("    (no modules)\n");
						}
						else
						{
							for (UNiagaraNodeFunctionCall* Call : StageCalls)
							{
								Out += TEXT("    module=") + Call->GetFunctionName();
								if (Call->FunctionScript)
								{
									Out += TEXT(" | ") + Call->FunctionScript->GetPathName();
								}
								Out += TEXT("\n");
								Out += ModuleInputLines(Call);
							}
							TArray<FString> StageLines;
							CollectUnmetStackDependencies(Sys, StageOut, StageLines);
							for (const FString& Line : StageLines)
							{
								Out += TEXT("    issue=") + Line + TEXT("\n");
							}
						}
					}
				}
				for (ENiagaraScriptUsage Usage : Stacks)
				{
					UNiagaraNodeOutput* Output = FindOutputNode(Graph, Usage);
					if (!Output)
					{
						continue;
					}
					TArray<UNiagaraNodeFunctionCall*> Calls;
					CollectFunctionCalls(Output, Calls);
					Out += FString::Printf(TEXT("  stack=%s\n"), UsageLabel(Usage));
					if (Calls.Num() == 0)
					{
						Out += TEXT("    (no modules)\n");
						continue;
					}
					for (UNiagaraNodeFunctionCall* Call : Calls)
					{
						Out += TEXT("    module=") + Call->GetFunctionName();
						if (Call->FunctionScript)
						{
							Out += TEXT(" | ") + Call->FunctionScript->GetPathName();
						}
						Out += TEXT("\n");
						Out += ModuleInputLines(Call);
					}
					TArray<FString> StackLines;
					CollectUnmetStackDependencies(Sys, Output, StackLines);
					for (const FString& Line : StackLines)
					{
						Out += TEXT("    issue=") + Line + TEXT("\n");
					}
				}
			}
			Message = Out.IsEmpty() ? TEXT("(empty system)") : Out;
			LocalOk = true;
			return;
		}
		if (Name == TEXT("add_niagara_module"))
		{
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			const ENiagaraScriptUsage Usage = UsageFromStack(ArgStr(Args, TEXT("stack"), TEXT("particle_update")));
			LocalOk = AddNiagaraModuleByRef(Sys, Usage, ArgStr(Args, TEXT("emitter")), ArgStr(Args, TEXT("module")), ArgStr(Args, TEXT("stage")), Message);
			return;
		}
		if (Name == TEXT("remove_niagara_module"))
		{
			FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, ArgStr(Args, TEXT("emitter")));
			if (!Handle)
			{
				Message = TEXT("Emitter not found");
				return;
			}
			UNiagaraGraph* Graph = EmitterGraph(Handle->GetEmitterData());
			UNiagaraNodeFunctionCall* Call = nullptr;
			ENiagaraScriptUsage Usage = ENiagaraScriptUsage::ParticleUpdateScript;
			if (!ResolveModuleCall(Graph, ArgStr(Args, TEXT("module")), ArgStr(Args, TEXT("stack")), Call, Usage, Message))
			{
				return;
			}
			FString Log;
			if (!RemoveStackModule(*Sys, *Handle, *Call, Usage, Log))
			{
				Message = Log;
				return;
			}
			Sys->MarkPackageDirty();
			Message = Log + TEXT("\n") + CompileNiagaraSystem(Sys);
			LocalOk = NiagaraCompiledOk(Message);
			return;
		}
		if (Name == TEXT("unbind_niagara_link"))
		{
			FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, ArgStr(Args, TEXT("emitter")));
			if (!Handle)
			{
				Message = TEXT("Emitter not found");
				return;
			}
			UNiagaraGraph* Graph = EmitterGraph(Handle->GetEmitterData());
			UNiagaraNodeFunctionCall* Call = nullptr;
			ENiagaraScriptUsage Usage = ENiagaraScriptUsage::ParticleUpdateScript;
			if (!ResolveModuleCall(Graph, ArgStr(Args, TEXT("module")), ArgStr(Args, TEXT("stack")), Call, Usage, Message))
			{
				return;
			}
			const FString InputName = ArgStr(Args, TEXT("name"));
			if (InputName.IsEmpty())
			{
				Message = TEXT("name required");
				return;
			}
			FString Log;
			if (!UnbindModuleInput(*Call, InputName, Log))
			{
				Message = Log;
				return;
			}
			Sys->MarkPackageDirty();
			Message = Log + TEXT("\n") + CompileNiagaraSystem(Sys);
			LocalOk = NiagaraCompiledOk(Message);
			return;
		}
		if (Name == TEXT("set_niagara_module_input"))
		{
			FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, ArgStr(Args, TEXT("emitter")));
			if (!Handle)
			{
				Message = TEXT("Emitter not found");
				return;
			}
			UNiagaraGraph* Graph = EmitterGraph(Handle->GetEmitterData());
			const FString ModuleQuery = ArgStr(Args, TEXT("module"));
			UNiagaraNodeFunctionCall* Call = nullptr;
			ENiagaraScriptUsage Usage = ENiagaraScriptUsage::ParticleUpdateScript;
			if (!ResolveModuleCall(Graph, ModuleQuery, ArgStr(Args, TEXT("stack")), Call, Usage, Message))
			{
				return;
			}
			const FString QueryName = ArgStr(Args, TEXT("name"));
			if (QueryName.IsEmpty())
			{
				Message = TEXT("name required");
				return;
			}
			TArray<FNiagaraVariable> Inputs;
			CollectModuleInputs(Call, Inputs);
			const FNiagaraVariable* Matched = FindListedModuleInput(Inputs, QueryName);
			if (!Matched)
			{
				FString Known;
				for (const FNiagaraVariable& Var : Inputs)
				{
					if (!Known.IsEmpty())
					{
						Known += TEXT(", ");
					}
					Known += ShortModuleInputName(Var);
				}
				Message = Known.IsEmpty()
					? FString::Printf(TEXT("Unknown input '%s' (module has no settable inputs)"), *QueryName)
					: FString::Printf(TEXT("Unknown input '%s'. Known: %s"), *QueryName, *Known);
				return;
			}
			FString WriteError;
			if (!WriteListedInput(*Handle, *Call, Usage, *Matched, Args, WriteError))
			{
				Message = WriteError.IsEmpty() ? TEXT("Failed to write input (check stack matches the module)") : WriteError;
				return;
			}
			Sys->MarkPackageDirty();
			Message = TEXT("Input set\n") + CompileNiagaraSystem(Sys);
			LocalOk = NiagaraCompiledOk(Message);
			return;
		}
		if (Name == TEXT("compile_niagara") || Name == TEXT("heal_niagara"))
		{
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
			Message = CompileNiagaraSystem(Sys);
			LocalOk = NiagaraCompiledOk(Message);
			return;
		}

		if (Name == TEXT("list_materials"))
		{
			Message = ListAssetsOf(UMaterialInterface::StaticClass(), TEXT("/Game"), 40);
			LocalOk = true;
			return;
		}
		if (Name == TEXT("assign_material") || Name == TEXT("assign_material_slot"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *ArgStr(Args, TEXT("path")));
			if (!Mat)
			{
				Message = TEXT("Material not found");
				return;
			}
			const int32 Slot = ArgInt(Args, TEXT("slot"), 0);
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "AssignMat", "Editor Prompt assign_material"));
			bool bAny = false;
			TArray<UStaticMeshComponent*> Comps;
			Actor->GetComponents(Comps);
			for (UStaticMeshComponent* C : Comps)
			{
				C->Modify();
				C->SetMaterial(Slot, Mat);
				bAny = true;
			}
			Redraw();
			Message = bAny ? TEXT("Material assigned") : TEXT("No mesh component");
			LocalOk = bAny;
			return;
		}
		if (Name == TEXT("get_actor_materials"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			FString Out;
			TArray<UMeshComponent*> Comps;
			Actor->GetComponents(Comps);
			for (UMeshComponent* C : Comps)
			{
				const int32 Num = C->GetNumMaterials();
				for (int32 I = 0; I < Num; ++I)
				{
					UMaterialInterface* M = C->GetMaterial(I);
					Out += FString::Printf(TEXT("%s[%d]=%s\n"), *C->GetName(), I, M ? *M->GetPathName() : TEXT("none"));
				}
			}
			Message = Out.IsEmpty() ? TEXT("(none)") : Out;
			LocalOk = true;
			return;
		}
		if (Name == TEXT("create_color_material") || Name == TEXT("create_material_instance"))
		{
			FString Path = ArgStr(Args, TEXT("path"));
			if (!IsGamePath(Path))
			{
				Message = TEXT("path must start with /Game");
				return;
			}
			FString ParentPath = ArgStr(Args, TEXT("parent"), TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
			UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *ParentPath);
			if (!Parent)
			{
				Message = TEXT("parent material not found");
				return;
			}
			FString AssetName;
			FString PackagePath;
			Path.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			if (AssetName.IsEmpty())
			{
				Message = TEXT("invalid path");
				return;
			}
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
			UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
			Factory->InitialParent = Parent;
			UObject* Created = AssetTools.CreateAsset(AssetName, PackagePath, UMaterialInstanceConstant::StaticClass(), Factory);
			UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Created);
			if (!MIC)
			{
				Message = TEXT("Create MIC failed");
				return;
			}
			if (Name == TEXT("create_color_material"))
			{
				MIC->SetVectorParameterValueEditorOnly(TEXT("Color"), ArgColor(Args));
				MIC->SetVectorParameterValueEditorOnly(TEXT("BaseColor"), ArgColor(Args));
			}
			MIC->MarkPackageDirty();
			Message = MIC->GetPathName();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("create_flipbook_mic"))
		{
			UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *ArgStr(Args, TEXT("texture")));
			if (!Tex)
			{
				Message = TEXT("Texture not found");
				return;
			}
			FString MicPath = ArgStr(Args, TEXT("path"));
			if (MicPath.IsEmpty())
			{
				MicPath = FString::Printf(TEXT("/Game/FX/Materials/MI_%s_Flipbook"), *Tex->GetName());
			}
			if (!IsGamePath(MicPath))
			{
				Message = TEXT("path must start with /Game");
				return;
			}
			FString ApplyError;
			const float Columns = FMath::Max(1.f, static_cast<float>(ArgNum(Args, TEXT("columns"), 4)));
			const float Rows = FMath::Max(1.f, static_cast<float>(ArgNum(Args, TEXT("rows"), 4)));
			const float Fps = FMath::Max(0.1f, static_cast<float>(ArgNum(Args, TEXT("fps"), 10)));
			const float Emissive = static_cast<float>(ArgNum(Args, TEXT("emissive"), 2.4));
			const float Opacity = static_cast<float>(ArgNum(Args, TEXT("opacity"), 1));
			UMaterialInstanceConstant* MIC = CreateFlipbookMic(MicPath, Tex, Columns, Rows, Fps, Emissive, Opacity, ApplyError);
			if (!MIC)
			{
				Message = ApplyError.IsEmpty() ? TEXT("Create flipbook MIC failed") : ApplyError;
				return;
			}
			Message = MIC->GetPathName();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("create_noise_mic"))
		{
			UTexture2D* SpriteTex = LoadObject<UTexture2D>(nullptr, *ArgStr(Args, TEXT("texture")));
			UTexture2D* NoiseTex = LoadObject<UTexture2D>(nullptr, *ArgStr(Args, TEXT("noise")));
			if (!SpriteTex)
			{
				Message = TEXT("Sprite texture not found");
				return;
			}
			if (!NoiseTex)
			{
				Message = TEXT("Noise texture not found");
				return;
			}
			FString MicPath = ArgStr(Args, TEXT("path"));
			if (MicPath.IsEmpty())
			{
				MicPath = FString::Printf(TEXT("/Game/FX/Materials/MI_%s_Noise"), *SpriteTex->GetName());
			}
			if (!IsGamePath(MicPath))
			{
				Message = TEXT("path must start with /Game");
				return;
			}
			FString ApplyError;
			UMaterialInstanceConstant* MIC = CreateNoiseMic(
				MicPath,
				SpriteTex,
				NoiseTex,
				static_cast<float>(ArgNum(Args, TEXT("strength"), 0.06)),
				static_cast<float>(ArgNum(Args, TEXT("tiling"), 1)),
				static_cast<float>(ArgNum(Args, TEXT("speed_u"), 0.05)),
				static_cast<float>(ArgNum(Args, TEXT("speed_v"), 0.035)),
				static_cast<float>(ArgNum(Args, TEXT("emissive"), 2.4)),
				static_cast<float>(ArgNum(Args, TEXT("opacity"), 1)),
				ApplyError);
			if (!MIC)
			{
				Message = ApplyError.IsEmpty() ? TEXT("Create noise MIC failed") : ApplyError;
				return;
			}
			Message = MIC->GetPathName();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("apply_vfx_look"))
		{
			if (!NeedWorld())
			{
				return;
			}
			Message = ApplyVfxLook(World, Args, LocalOk);
			return;
		}
		if (Name == TEXT("set_material_color"))
		{
			UMaterialInstanceConstant* MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *ArgStr(Args, TEXT("path")));
			if (!MIC)
			{
				Message = TEXT("MIC not found");
				return;
			}
			MIC->SetVectorParameterValueEditorOnly(*ArgStr(Args, TEXT("name"), TEXT("Color")), ArgColor(Args));
			MIC->MarkPackageDirty();
			Message = TEXT("MIC color set");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_material_scalar"))
		{
			UMaterialInstanceConstant* MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *ArgStr(Args, TEXT("path")));
			if (!MIC)
			{
				Message = TEXT("MIC not found");
				return;
			}
			MIC->SetScalarParameterValueEditorOnly(*ArgStr(Args, TEXT("name")), static_cast<float>(ArgNum(Args, TEXT("value"))));
			MIC->MarkPackageDirty();
			Message = TEXT("MIC scalar set");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_material_texture"))
		{
			UMaterialInstanceConstant* MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *ArgStr(Args, TEXT("path")));
			if (!MIC)
			{
				Message = TEXT("MIC not found");
				return;
			}
			UTexture* Tex = LoadObject<UTexture>(nullptr, *ArgStr(Args, TEXT("texture")));
			if (!Tex)
			{
				Message = TEXT("Texture not found");
				return;
			}
			const FName Param(*ArgStr(Args, TEXT("name"), TEXT("Texture")));
			MIC->SetTextureParameterValueEditorOnly(Param, Tex);
			MIC->MarkPackageDirty();
			Message = TEXT("MIC texture set");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("import_texture"))
		{
			FString File = ArgStr(Args, TEXT("file"));
			if (File.IsEmpty() || !FPaths::FileExists(File) || !IsUnderProject(File))
			{
				Message = TEXT("file must exist under the project");
				return;
			}
			FString Dest = ArgStr(Args, TEXT("dest"), TEXT("/Game/FX/Textures"));
			if (!Dest.StartsWith(TEXT("/Game")))
			{
				Message = TEXT("dest must start with /Game");
				return;
			}
			const FString FullFile = FPaths::ConvertRelativePathToFull(File);
			const FString Kind = ArgStr(Args, TEXT("kind"), TEXT("albedo")).ToLower();
			const FString NewName = ArgStr(Args, TEXT("name"));
			if (!NewName.IsEmpty())
			{
				if (UTexture2D* Existing = FindTextureNamedUnder(Dest, NewName))
				{
					Existing->Modify();
					if (Existing->AssetImportData)
					{
						Existing->AssetImportData->Update(FullFile);
					}
					const bool bReimported = FReimportManager::Instance()->Reimport(
						Existing, false, false, FullFile, nullptr, INDEX_NONE, false, true);
					if (!bReimported)
					{
						Message = TEXT("Reimport failed");
						return;
					}
					ApplyImportedTextureKind(Existing, Kind);
					Message = Existing->GetPathName();
					LocalOk = true;
					return;
				}
			}
			UAutomatedAssetImportData* ImportData = NewObject<UAutomatedAssetImportData>();
			ImportData->Filenames.Add(FullFile);
			ImportData->DestinationPath = Dest;
			ImportData->bReplaceExisting = true;
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
			TArray<UObject*> Imported = AssetTools.ImportAssetsAutomated(ImportData);
			if (Imported.Num() == 0)
			{
				Message = TEXT("Import failed");
				return;
			}
			FString Out;
			for (UObject* Obj : Imported)
			{
				if (UTexture2D* Tex = Cast<UTexture2D>(Obj))
				{
					if (!NewName.IsEmpty() && Tex->GetName() != NewName)
					{
						FString RenameError;
						if (!RenameObjectAvoidingCollision(Tex, NewName, RenameError))
						{
							Message = RenameError;
							return;
						}
					}
					ApplyImportedTextureKind(Tex, Kind);
					Out += Tex->GetPathName() + TEXT("\n");
				}
				else if (Obj)
				{
					Out += Obj->GetPathName() + TEXT("\n");
				}
			}
			Message = Out.IsEmpty() ? TEXT("Import produced no texture") : Out.TrimEnd();
			LocalOk = !Out.IsEmpty();
			return;
		}
		if (Name == TEXT("set_niagara_sprite_texture"))
		{
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *ArgStr(Args, TEXT("texture")));
			UNiagaraSpriteRendererProperties* Sprite = FindSpriteRenderer(Sys, ArgStr(Args, TEXT("emitter")));
			if (!Sprite)
			{
				Message = TEXT("Sprite renderer not found");
				return;
			}
			Sprite->Modify();
			const FString Mode = ArgStr(Args, TEXT("mode"), TEXT("sprite")).ToLower();
			if (Mode.Contains(TEXT("material")))
			{
				UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *ArgStr(Args, TEXT("texture")));
				if (!Mat)
				{
					Message = TEXT("Material not found");
					return;
				}
				Sprite->Material = Mat;
				Sprite->bUseMaterialCutoutTexture = false;
				Sprite->CutoutTexture = nullptr;
				Message = TEXT("Sprite material set");
			}
			else if (Mode.Contains(TEXT("cutout")))
			{
				if (!Tex)
				{
					Message = TEXT("Texture not found");
					return;
				}
				if (UMaterialInterface* BaseMat = NiagaraSpriteMaterialForCutout())
				{
					Sprite->Material = BaseMat;
				}
				Sprite->bUseMaterialCutoutTexture = true;
				Sprite->CutoutTexture = Tex;
				Sprite->UpdateCutoutTexture();
				Message = TEXT("Sprite cutout set");
			}
			else
			{
				if (!Tex)
				{
					Message = TEXT("Texture not found");
					return;
				}
				FString ApplyError;
				if (!ApplySpriteImageTexture(Sprite, Sys, Tex, ApplyError))
				{
					Message = ApplyError.IsEmpty() ? TEXT("Sprite texture failed") : ApplyError;
					return;
				}
				Message = FString::Printf(TEXT("Sprite texture set mic=%s"), *SpriteMicPathFor(Sys));
			}
			Sys->MarkPackageDirty();
			EnableFxViewport();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_niagara_sprite_subuv"))
		{
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			UNiagaraSpriteRendererProperties* Sprite = FindSpriteRenderer(Sys, ArgStr(Args, TEXT("emitter")));
			if (!Sprite)
			{
				Message = TEXT("Sprite renderer not found");
				return;
			}
			const float Columns = FMath::Max(1.f, static_cast<float>(ArgNum(Args, TEXT("columns"), 4)));
			const float Rows = FMath::Max(1.f, static_cast<float>(ArgNum(Args, TEXT("rows"), 4)));
			Sprite->Modify();
			Sprite->SubImageSize = FVector2D(Columns, Rows);
			Sprite->bSubImageBlend = ArgBool(Args, TEXT("blend"), true);
			Sys->MarkPackageDirty();
			EnableFxViewport();
			Message = FString::Printf(TEXT("Sprite SubUV %.0fx%.0f blend=%s"),
				Columns, Rows, Sprite->bSubImageBlend ? TEXT("true") : TEXT("false"));
			LocalOk = true;
			return;
		}
		if (Name == TEXT("list_niagara_renderers"))
		{
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			FString Out;
			for (const FNiagaraEmitterHandle& Handle : Sys->GetEmitterHandles())
			{
				FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
				Out += TEXT("emitter=") + Handle.GetName().ToString() + TEXT("\n");
				if (!Data)
				{
					continue;
				}
				for (UNiagaraRendererProperties* R : Data->GetRenderers())
				{
					if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(R))
					{
						Out += FString::Printf(
							TEXT("  sprite facing=%s alignment=%s pivot=%.2f,%.2f subuv=%.0fx%.0f shadows=%d mat=%s\n"),
							FacingLabel(Sprite->FacingMode),
							AlignmentLabel(Sprite->Alignment),
							Sprite->PivotInUVSpace.X, Sprite->PivotInUVSpace.Y,
							Sprite->SubImageSize.X, Sprite->SubImageSize.Y,
							Sprite->bCastShadows ? 1 : 0,
							Sprite->Material ? *Sprite->Material->GetPathName() : TEXT("none"));
					}
					else if (UNiagaraMeshRendererProperties* MeshR = Cast<UNiagaraMeshRendererProperties>(R))
					{
						const UStaticMesh* Mesh = (MeshR->Meshes.Num() > 0) ? MeshR->Meshes[0].Mesh.Get() : nullptr;
						Out += FString::Printf(TEXT("  mesh mesh=%s facing=%d slots=%d\n"),
							Mesh ? *Mesh->GetPathName() : TEXT("none"),
							static_cast<int32>(MeshR->FacingMode),
							MeshR->Meshes.Num());
					}
					else if (UNiagaraRibbonRendererProperties* Ribbon = Cast<UNiagaraRibbonRendererProperties>(R))
					{
						Out += FString::Printf(TEXT("  ribbon facing=%d mat=%s\n"),
							static_cast<int32>(Ribbon->FacingMode),
							Ribbon->Material ? *Ribbon->Material->GetPathName() : TEXT("none"));
					}
					else if (R)
					{
						Out += TEXT("  renderer=") + R->GetClass()->GetName() + TEXT("\n");
					}
				}
			}
			Message = Out.IsEmpty() ? TEXT("(none)") : Out;
			LocalOk = true;
			return;
		}
		if (Name == TEXT("add_niagara_renderer"))
		{
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, ArgStr(Args, TEXT("emitter")));
			UNiagaraEmitter* Emitter = Handle ? Handle->GetInstance().Emitter : nullptr;
			if (!Emitter || !Handle)
			{
				Message = TEXT("Emitter not found");
				return;
			}
			const FString Kind = ArgStr(Args, TEXT("kind"), TEXT("sprite")).ToLower();
			if (Kind.Contains(TEXT("mesh")))
			{
				UNiagaraMeshRendererProperties* MeshR = FindMeshRenderer(Sys, ArgStr(Args, TEXT("emitter")));
				if (!MeshR)
				{
					MeshR = NewObject<UNiagaraMeshRendererProperties>(Emitter, TEXT("MeshRenderer"));
					Emitter->AddRenderer(MeshR, Handle->GetInstance().Version);
				}
				MeshR->Modify();
				if (MeshR->Meshes.Num() == 0)
				{
					MeshR->Meshes.AddDefaulted();
				}
				const FString MeshPath = ArgStr(Args, TEXT("mesh"));
				if (!MeshPath.IsEmpty())
				{
					if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath))
					{
						MeshR->Meshes[0].Mesh = Mesh;
					}
				}
				DisableSpriteRenderers(Sys);
				FString AttrLog;
				SetOrAddAttribute(*Handle, SYS_PARAM_PARTICLES_SCALE, TEXT("1.0,1.0,1.0"), TEXT("particle_spawn"), AttrLog);
				Message = TEXT("Mesh renderer ready\n") + AttrLog;
			}
			else if (Kind.Contains(TEXT("ribbon")))
			{
				UNiagaraRibbonRendererProperties* Ribbon = FindRibbonRenderer(Sys, ArgStr(Args, TEXT("emitter")));
				if (!Ribbon)
				{
					Ribbon = NewObject<UNiagaraRibbonRendererProperties>(Emitter, TEXT("RibbonRenderer"));
					Emitter->AddRenderer(Ribbon, Handle->GetInstance().Version);
				}
				Ribbon->Modify();
				const FString MatPath = ArgStr(Args, TEXT("material"));
				if (!MatPath.IsEmpty())
				{
					if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *MatPath))
					{
						Ribbon->Material = Mat;
					}
				}
				DisableSpriteRenderers(Sys);
				FString BindLog;
				EnsureRibbonBindings(*Handle, BindLog);
				const float Width = static_cast<float>(ArgNum(Args, TEXT("width"), 20));
				FString AttrLog;
				SetOrAddAttribute(*Handle, SYS_PARAM_PARTICLES_RIBBONWIDTH, FString::Printf(TEXT("%g"), Width), TEXT("particle_spawn"), AttrLog);
				Message = TEXT("Ribbon renderer ready\n") + BindLog + AttrLog;
			}
			else
			{
				FString Error;
				if (!EnsureSpriteRenderer(Sys, ArgStr(Args, TEXT("emitter")), Error))
				{
					Message = Error;
					return;
				}
				Message = TEXT("Sprite renderer ready");
			}
			Sys->MarkPackageDirty();
			Message += TEXT("\n") + CompileNiagaraSystem(Sys);
			LocalOk = NiagaraCompiledOk(Message);
			return;
		}
		if (Name == TEXT("set_niagara_sprite_renderer"))
		{
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			FString Error;
			UNiagaraSpriteRendererProperties* Sprite = EnsureSpriteRenderer(Sys, ArgStr(Args, TEXT("emitter")), Error);
			if (!Sprite)
			{
				Message = Error;
				return;
			}
			Sprite->Modify();
			if (Args.IsValid() && Args->HasField(TEXT("alignment")))
			{
				Sprite->Alignment = ParseSpriteAlignment(ArgStr(Args, TEXT("alignment")));
			}
			if (Args.IsValid() && Args->HasField(TEXT("facing")))
			{
				Sprite->FacingMode = ParseSpriteFacing(ArgStr(Args, TEXT("facing")));
			}
			if (Args.IsValid() && (Args->HasField(TEXT("pivot_u")) || Args->HasField(TEXT("pivot_v"))))
			{
				Sprite->PivotInUVSpace = FVector2D(
					static_cast<float>(ArgNum(Args, TEXT("pivot_u"), 0.5)),
					static_cast<float>(ArgNum(Args, TEXT("pivot_v"), 0.5)));
			}
			if (Args.IsValid() && Args->HasField(TEXT("sort")))
			{
				Sprite->SortMode = ParseSpriteSort(ArgStr(Args, TEXT("sort")));
			}
			if (Args.IsValid() && Args->HasField(TEXT("shadows")))
			{
				Sprite->bCastShadows = ArgBool(Args, TEXT("shadows"), true);
			}
			Sys->MarkPackageDirty();
			EnableFxViewport();
			Message = FString::Printf(TEXT("Sprite renderer facing=%s alignment=%s pivot=%.2f,%.2f"),
				FacingLabel(Sprite->FacingMode), AlignmentLabel(Sprite->Alignment),
				Sprite->PivotInUVSpace.X, Sprite->PivotInUVSpace.Y);
			LocalOk = true;
			return;
		}
		if (Name == TEXT("configure_niagara_emitter"))
		{
			FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, ArgStr(Args, TEXT("emitter")));
			if (!Handle)
			{
				Message = TEXT("Emitter not found");
				return;
			}
			UNiagaraGraph* Graph = EmitterGraph(Handle->GetEmitterData());
			FString Log;
			auto BindValue = [&](const TCHAR* Field, const TCHAR* InputName, TSharedPtr<FJsonObject> One)
			{
				if (!Args.IsValid() || !Args->HasField(Field))
				{
					return;
				}
				if (!SetFirstNamedInput(*Handle, Graph, InputName, One, Log))
				{
					Log += FString::Printf(TEXT("missing input %s\n"), InputName);
				}
			};
			if (Args.IsValid() && Args->HasField(TEXT("spawn_rate")))
			{
				TSharedRef<FJsonObject> One = MakeShared<FJsonObject>();
				One->SetNumberField(TEXT("value"), ArgNum(Args, TEXT("spawn_rate")));
				BindValue(TEXT("spawn_rate"), TEXT("SpawnRate"), One);
			}
			if (Args.IsValid() && Args->HasField(TEXT("lifetime")))
			{
				TSharedRef<FJsonObject> One = MakeShared<FJsonObject>();
				One->SetNumberField(TEXT("value"), ArgNum(Args, TEXT("lifetime")));
				if (!SetFirstNamedInput(*Handle, Graph, TEXT("Lifetime"), One, Log))
				{
					Log += TEXT("missing input Lifetime\n");
				}
			}
			if (Args.IsValid() && (Args->HasField(TEXT("size")) || Args->HasField(TEXT("sx")) || Args->HasField(TEXT("sy"))))
			{
				if (!SetFirstNamedInput(*Handle, Graph, TEXT("SpriteSize"), Args, Log)
					&& !SetFirstNamedInput(*Handle, Graph, TEXT("Size"), Args, Log))
				{
					Log += TEXT("missing input SpriteSize\n");
				}
			}
			if (Args.IsValid() && (Args->HasField(TEXT("r")) || Args->HasField(TEXT("g")) || Args->HasField(TEXT("b"))))
			{
				if (!SetFirstNamedInput(*Handle, Graph, TEXT("Color"), Args, Log))
				{
					Log += TEXT("missing input Color\n");
				}
			}
			if (Args.IsValid() && (Args->HasField(TEXT("x")) || Args->HasField(TEXT("y")) || Args->HasField(TEXT("z"))))
			{
				if (!SetFirstNamedInput(*Handle, Graph, TEXT("Velocity"), Args, Log))
				{
					Log += TEXT("missing input Velocity\n");
				}
			}
			if (Args.IsValid() && Args->HasField(TEXT("gravity")))
			{
				TSharedRef<FJsonObject> One = MakeShared<FJsonObject>();
				One->SetNumberField(TEXT("x"), 0);
				One->SetNumberField(TEXT("y"), 0);
				One->SetNumberField(TEXT("z"), ArgNum(Args, TEXT("gravity")));
				One->SetNumberField(TEXT("value"), ArgNum(Args, TEXT("gravity")));
				if (!SetFirstNamedInput(*Handle, Graph, TEXT("Gravity"), One, Log))
				{
					UNiagaraNodeOutput* Update = FindOutputNode(Graph, ENiagaraScriptUsage::ParticleUpdateScript);
					if (TryAddLibraryModule(Update, TEXT("GravityForce")))
					{
						SetFirstNamedInput(*Handle, Graph, TEXT("Gravity"), One, Log);
					}
					else
					{
						Log += TEXT("could not add GravityForce\n");
					}
				}
			}
			const float Spread = static_cast<float>(ArgNum(Args, TEXT("spread"), 28));
			const float UpSpeed = static_cast<float>(
				Args.IsValid() && Args->HasField(TEXT("z")) ? ArgNum(Args, TEXT("z")) : ArgNum(Args, TEXT("speed"), 42));
			const float GravityZ = static_cast<float>(ArgNum(Args, TEXT("gravity"), -32));
			ApplyParticleSpread(Sys, *Handle, Graph, Spread, FMath::Clamp(UpSpeed, 12.f, 80.f), GravityZ, Log);
			Sys->MarkPackageDirty();
			Message = Log.IsEmpty() ? TEXT("No emitter fields passed") : (Log + CompileNiagaraSystem(Sys));
			LocalOk = NiagaraCompiledOk(Message) && !Log.Contains(TEXT("could not add"));
			return;
		}
		if (Name == TEXT("set_niagara_mesh_renderer"))
		{
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			UNiagaraMeshRendererProperties* MeshR = FindMeshRenderer(Sys, ArgStr(Args, TEXT("emitter")));
			if (!MeshR)
			{
				Message = TEXT("Mesh renderer not found; add_niagara_renderer kind=mesh first");
				return;
			}
			MeshR->Modify();
			if (MeshR->Meshes.Num() == 0)
			{
				MeshR->Meshes.AddDefaulted();
			}
			const FString MeshPath = ArgStr(Args, TEXT("mesh"));
			if (!MeshPath.IsEmpty())
			{
				UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
				if (!Mesh)
				{
					Message = TEXT("Static mesh not found");
					return;
				}
				MeshR->Meshes[0].Mesh = Mesh;
			}
			if (Args.IsValid() && Args->HasField(TEXT("facing")))
			{
				const FString F = ArgStr(Args, TEXT("facing")).ToLower();
				if (F.Contains(TEXT("velocity")))
				{
					MeshR->FacingMode = ENiagaraMeshFacingMode::Velocity;
				}
				else if (F.Contains(TEXT("plane")))
				{
					MeshR->FacingMode = ENiagaraMeshFacingMode::CameraPlane;
				}
				else if (F.Contains(TEXT("camera")))
				{
					MeshR->FacingMode = ENiagaraMeshFacingMode::CameraPosition;
				}
				else
				{
					MeshR->FacingMode = ENiagaraMeshFacingMode::Default;
				}
			}
			if (Args.IsValid() && (Args->HasField(TEXT("scale")) || Args->HasField(TEXT("sx"))))
			{
				const float Uniform = static_cast<float>(ArgNum(Args, TEXT("scale"), 1));
				MeshR->Meshes[0].Scale = FVector(
					ArgNum(Args, TEXT("sx"), Uniform),
					ArgNum(Args, TEXT("sy"), Uniform),
					ArgNum(Args, TEXT("sz"), Uniform));
			}
			const FString MatPath = ArgStr(Args, TEXT("material"));
			if (!MatPath.IsEmpty())
			{
				if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *MatPath))
				{
					MeshR->bOverrideMaterials = true;
					if (MeshR->OverrideMaterials.Num() == 0)
					{
						MeshR->OverrideMaterials.AddDefaulted();
					}
					MeshR->OverrideMaterials[0].ExplicitMat = Mat;
				}
			}
			Sys->MarkPackageDirty();
			EnableFxViewport();
			Message = TEXT("Mesh renderer set\n") + CompileNiagaraSystem(Sys);
			LocalOk = NiagaraCompiledOk(Message);
			return;
		}
		if (Name == TEXT("set_niagara_ribbon_renderer"))
		{
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			UNiagaraRibbonRendererProperties* Ribbon = FindRibbonRenderer(Sys, ArgStr(Args, TEXT("emitter")));
			if (!Ribbon)
			{
				Message = TEXT("Ribbon renderer not found; add_niagara_renderer kind=ribbon first");
				return;
			}
			Ribbon->Modify();
			const FString MatPath = ArgStr(Args, TEXT("material"));
			if (!MatPath.IsEmpty())
			{
				UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *MatPath);
				if (!Mat)
				{
					Message = TEXT("Material not found");
					return;
				}
				Ribbon->Material = Mat;
			}
			if (Args.IsValid() && Args->HasField(TEXT("facing")))
			{
				Ribbon->FacingMode = ArgStr(Args, TEXT("facing")).ToLower().Contains(TEXT("custom"))
					? ENiagaraRibbonFacingMode::Custom
					: ENiagaraRibbonFacingMode::Screen;
			}
			if (Args.IsValid() && Args->HasField(TEXT("width")))
			{
				FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, ArgStr(Args, TEXT("emitter")));
				if (Handle)
				{
					FString AttrLog;
					SetOrAddAttribute(*Handle, SYS_PARAM_PARTICLES_RIBBONWIDTH,
						FString::Printf(TEXT("%g"), ArgNum(Args, TEXT("width"))), TEXT("particle_spawn"), AttrLog);
					Message = TEXT("Ribbon renderer set\n") + AttrLog;
				}
			}
			else
			{
				Message = TEXT("Ribbon renderer set");
			}
			if (FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, ArgStr(Args, TEXT("emitter"))))
			{
				FString BindLog;
				EnsureRibbonBindings(*Handle, BindLog);
				Message += TEXT("\n") + BindLog;
			}
			Sys->MarkPackageDirty();
			EnableFxViewport();
			Message += TEXT("\n") + CompileNiagaraSystem(Sys);
			LocalOk = NiagaraCompiledOk(Message);
			return;
		}
		if (Name == TEXT("set_niagara_attribute"))
		{
			FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, ArgStr(Args, TEXT("emitter")));
			if (!Handle)
			{
				Message = TEXT("Emitter not found");
				return;
			}
			const FString AttrName = ArgStr(Args, TEXT("name"));
			if (AttrName.IsEmpty())
			{
				Message = TEXT("name required");
				return;
			}
			const FNiagaraVariable Var = ResolveParticleVariable(AttrName, ArgStr(Args, TEXT("type")));
			const FString Default = DefaultStringFor(Var, Args);
			FString Log;
			if (!SetOrAddAttribute(*Handle, Var, Default, ArgStr(Args, TEXT("stack"), TEXT("particle_spawn")), Log))
			{
				Message = Log;
				return;
			}
			Sys->MarkPackageDirty();
			Message = Log + TEXT("\n") + CompileNiagaraSystem(Sys);
			LocalOk = NiagaraCompiledOk(Message);
			return;
		}
		if (Name == TEXT("set_niagara_curve"))
		{
			FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, ArgStr(Args, TEXT("emitter")));
			if (!Handle)
			{
				Message = TEXT("Emitter not found");
				return;
			}
			TArray<TPair<float, float>> Keys;
			if (!ParseFloatKeys(ArgStr(Args, TEXT("keys")), Keys))
			{
				Message = TEXT("keys required, e.g. 0,1;0.5,0.6;1,0");
				return;
			}
			UNiagaraNodeFunctionCall* Call = FindCallOnStacks(EmitterGraph(Handle->GetEmitterData()), ArgStr(Args, TEXT("module")));
			if (!Call)
			{
				Message = TEXT("Module not found");
				return;
			}
			FString Log;
			if (!ApplyCurveToModule(*Call, ArgStr(Args, TEXT("name")), Keys, Log))
			{
				Message = Log;
				return;
			}
			Sys->MarkPackageDirty();
			Message = Log + TEXT("\n") + CompileNiagaraSystem(Sys);
			LocalOk = NiagaraCompiledOk(Message);
			return;
		}
		if (Name == TEXT("set_niagara_over_life"))
		{
			FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, ArgStr(Args, TEXT("emitter")));
			if (!Handle)
			{
				Message = TEXT("Emitter not found");
				return;
			}
			UNiagaraGraph* Graph = EmitterGraph(Handle->GetEmitterData());
			UNiagaraNodeOutput* Update = FindOutputNode(Graph, ENiagaraScriptUsage::ParticleUpdateScript);
			const FString Target = ArgStr(Args, TEXT("target"), TEXT("size")).ToLower();
			const TCHAR* ModuleName = Target.Contains(TEXT("color")) ? TEXT("ScaleColor") : TEXT("ScaleSpriteSize");
			UNiagaraNodeFunctionCall* Call = FindCallOnStacks(Graph, ModuleName);
			if (!Call)
			{
				Call = TryAddLibraryModule(Update, ModuleName);
			}
			if (!Call)
			{
				Message = FString::Printf(TEXT("Could not add %s"), ModuleName);
				return;
			}
			TArray<TPair<float, float>> Keys;
			if (!ParseFloatKeys(ArgStr(Args, TEXT("keys"), TEXT("0,1;1,0")), Keys))
			{
				Message = TEXT("keys required, e.g. 0,1;1,0");
				return;
			}
			FString Log;
			if (!ApplyCurveToModule(*Call, FString(), Keys, Log))
			{
				Message = Log;
				return;
			}
			Sys->MarkPackageDirty();
			Message = Log + TEXT("\n") + CompileNiagaraSystem(Sys);
			LocalOk = NiagaraCompiledOk(Message);
			return;
		}
		if (Name == TEXT("set_niagara_link"))
		{
			FModuleManager::LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
			UNiagaraSystem* Sys = LoadOrFailSystem(ArgStr(Args, TEXT("path")), Message);
			if (!Sys)
			{
				return;
			}
			FNiagaraEmitterHandle* Handle = FindEmitterHandle(Sys, ArgStr(Args, TEXT("emitter")));
			if (!Handle)
			{
				Message = TEXT("Emitter not found");
				return;
			}
			UNiagaraGraph* Graph = EmitterGraph(Handle->GetEmitterData());
			UNiagaraNodeFunctionCall* Call = nullptr;
			ENiagaraScriptUsage Usage = ENiagaraScriptUsage::ParticleUpdateScript;
			if (!ResolveModuleCall(Graph, ArgStr(Args, TEXT("module")), ArgStr(Args, TEXT("stack")), Call, Usage, Message))
			{
				return;
			}
			const FString InputName = ArgStr(Args, TEXT("name"));
			if (InputName.IsEmpty())
			{
				Message = TEXT("name required");
				return;
			}
			TArray<FNiagaraVariable> Inputs;
			CollectModuleInputs(Call, Inputs);
			const FNiagaraVariable* Matched = FindListedModuleInput(Inputs, InputName);
			if (!Matched)
			{
				Message = TEXT("Unknown input on module");
				return;
			}
			FString Source = ArgStr(Args, TEXT("source"), TEXT("Particles.NormalizedAge"));
			if (!Source.Contains(TEXT(".")))
			{
				Source = TEXT("Particles.") + Source;
			}
			FString UnbindLog;
			if (!UnbindModuleInput(*Call, InputName, UnbindLog))
			{
				Message = UnbindLog;
				return;
			}
			if (Source.Equals(TEXT("false"), ESearchCase::IgnoreCase)
				|| Source.Equals(TEXT("none"), ESearchCase::IgnoreCase)
				|| Source.Equals(TEXT("unbind"), ESearchCase::IgnoreCase)
				|| Source.Equals(TEXT("Particles.false"), ESearchCase::IgnoreCase))
			{
				Sys->MarkPackageDirty();
				Message = UnbindLog + TEXT("\n") + CompileNiagaraSystem(Sys);
				LocalOk = NiagaraCompiledOk(Message);
				return;
			}
			const FNiagaraParameterHandle ModuleHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(*ShortModuleInputName(*Matched));
			const FNiagaraParameterHandle Aliased = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(ModuleHandle, Call);
			UEdGraphPin& Pin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
				*Call, Aliased, Matched->GetType(), FGuid(), FGuid());
			TSet<FNiagaraVariable> Known;
			FNiagaraStackGraphUtilities::SetLinkedValueHandleForFunctionInput(
				Pin, FNiagaraParameterHandle(FName(*Source)), Known);
			Sys->MarkPackageDirty();
			Message = TEXT("Linked ") + InputName + TEXT(" -> ") + Source + TEXT("\n") + CompileNiagaraSystem(Sys);
			LocalOk = NiagaraCompiledOk(Message);
			return;
		}

		if (Name == TEXT("get_viewport_camera"))
		{
			FEditorViewportClient* Client = ActiveViewport();
			if (!Client)
			{
				Message = TEXT("No viewport");
				return;
			}
			const FVector L = Client->GetViewLocation();
			const FRotator R = Client->GetViewRotation();
			Message = FString::Printf(TEXT("loc=%.1f,%.1f,%.1f rot=%.1f,%.1f,%.1f fov=%.1f"),
				L.X, L.Y, L.Z, R.Pitch, R.Yaw, R.Roll, Client->ViewFOV);
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_viewport_camera"))
		{
			FEditorViewportClient* Client = ActiveViewport();
			if (!Client)
			{
				Message = TEXT("No viewport");
				return;
			}
			Client->SetViewLocation(ArgLoc(Args));
			Client->SetViewRotation(ArgRot(Args));
			Redraw();
			Message = TEXT("Viewport camera set");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("focus_actor"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(Actor, true, true, true);
			GEditor->MoveViewportCamerasToActor(*Actor, false);
			Redraw();
			Message = TEXT("Focused ") + Actor->GetActorLabel();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("spawn_camera"))
		{
			if (!NeedWorld())
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "Cam", "Editor Prompt spawn_camera"));
			ACameraActor* Cam = World->SpawnActor<ACameraActor>(ArgLoc(Args), ArgRot(Args));
			if (!Cam)
			{
				Message = TEXT("Spawn camera failed");
				return;
			}
			TouchActor(Cam);
			const FString Label = ArgStr(Args, TEXT("label"));
			if (!Label.IsEmpty())
			{
				Cam->SetActorLabel(Label);
			}
			Message = TEXT("Spawned camera ") + Cam->GetActorLabel();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("spawn_cine_camera"))
		{
			if (!NeedWorld())
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "Cine", "Editor Prompt spawn_cine_camera"));
			ACineCameraActor* Cam = World->SpawnActor<ACineCameraActor>(ArgLoc(Args), ArgRot(Args));
			if (!Cam)
			{
				Message = TEXT("Spawn cine camera failed");
				return;
			}
			TouchActor(Cam);
			const FString Label = ArgStr(Args, TEXT("label"));
			if (!Label.IsEmpty())
			{
				Cam->SetActorLabel(Label);
			}
			Message = TEXT("Spawned cine camera ") + Cam->GetActorLabel();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("capture_viewport") || Name == TEXT("read_viewport"))
		{
			EnableFxViewport();
			Redraw();
			Message = ReadViewportSummary(LocalOk);
			return;
		}

		if (Name == TEXT("spawn_sky_atmosphere"))
		{
			if (!NeedWorld())
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "SkyAtmo", "Editor Prompt spawn_sky_atmosphere"));
			ASkyAtmosphere* A = World->SpawnActor<ASkyAtmosphere>();
			if (!A)
			{
				Message = TEXT("Spawn sky atmosphere failed");
				return;
			}
			TouchActor(A);
			A->SetActorLabel(TEXT("PromptSkyAtmosphere"));
			Message = TEXT("Spawned Sky Atmosphere");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_sky_atmosphere"))
		{
			ASkyAtmosphere* A = nullptr;
			if (!ArgStr(Args, TEXT("label")).IsEmpty())
			{
				A = Cast<ASkyAtmosphere>(NeedActor());
			}
			if (!A && NeedWorld())
			{
				for (TActorIterator<ASkyAtmosphere> It(World); It; ++It)
				{
					A = *It;
					break;
				}
			}
			if (!A)
			{
				Message = TEXT("No sky atmosphere");
				return;
			}
			if (USkyAtmosphereComponent* C = A->GetComponent())
			{
				C->Modify();
				const double Mie = ArgNum(Args, TEXT("mie"), -1);
				const double Disk = ArgNum(Args, TEXT("disk"), -1);
				if (Mie >= 0)
				{
					C->SetMieScatteringScale(static_cast<float>(Mie));
				}
				if (Disk >= 0)
				{
					C->SetAerialPespectiveViewDistanceScale(static_cast<float>(Disk));
				}
			}
			Redraw();
			Message = TEXT("Sky atmosphere updated");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("spawn_exponential_fog"))
		{
			if (!NeedWorld())
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "Fog", "Editor Prompt spawn_exponential_fog"));
			AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(ArgLoc(Args), FRotator::ZeroRotator);
			if (!Fog)
			{
				Message = TEXT("Spawn fog failed");
				return;
			}
			TouchActor(Fog);
			const FString Label = ArgStr(Args, TEXT("label"), TEXT("PromptFog"));
			Fog->SetActorLabel(Label);
			if (UExponentialHeightFogComponent* C = Fog->GetComponent())
			{
				const double Density = ArgNum(Args, TEXT("density"), -1);
				if (Density >= 0)
				{
					C->SetFogDensity(static_cast<float>(Density));
				}
				C->SetFogInscatteringColor(ArgColor(Args));
			}
			Redraw();
			Message = TEXT("Spawned fog ") + Fog->GetActorLabel();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_exponential_fog"))
		{
			AExponentialHeightFog* Fog = Cast<AExponentialHeightFog>(NeedActor());
			if (!Fog)
			{
				Message = TEXT("Not fog");
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "SetFog", "Editor Prompt set_exponential_fog"));
			if (UExponentialHeightFogComponent* C = Fog->GetComponent())
			{
				C->Modify();
				const double Density = ArgNum(Args, TEXT("density"), -1);
				if (Density >= 0)
				{
					C->SetFogDensity(static_cast<float>(Density));
				}
				C->SetFogInscatteringColor(ArgColor(Args));
			}
			Redraw();
			Message = TEXT("Fog updated");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("spawn_post_process_volume"))
		{
			if (!NeedWorld())
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "PPV", "Editor Prompt spawn_post_process_volume"));
			APostProcessVolume* V = World->SpawnActor<APostProcessVolume>();
			if (!V)
			{
				Message = TEXT("Spawn PPV failed");
				return;
			}
			TouchActor(V);
			V->SetActorLabel(TEXT("PromptPostProcess"));
			V->bUnbound = true;
			Redraw();
			Message = TEXT("Spawned post-process volume");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_post_exposure"))
		{
			APostProcessVolume* V = Cast<APostProcessVolume>(NeedActor());
			if (!V)
			{
				Message = TEXT("Not a post-process volume");
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "Exposure", "Editor Prompt set_post_exposure"));
			V->Modify();
			V->Settings.bOverride_AutoExposureBias = true;
			V->Settings.AutoExposureBias = static_cast<float>(ArgNum(Args, TEXT("exposure"), 0));
			Redraw();
			Message = TEXT("Exposure set");
			LocalOk = true;
			return;
		}

		if (Name == TEXT("list_assets"))
		{
			Message = ListAssetsOf(nullptr, ArgStr(Args, TEXT("prefix"), TEXT("/Game")), ArgInt(Args, TEXT("max"), 40));
			LocalOk = true;
			return;
		}
		if (Name == TEXT("list_static_meshes"))
		{
			Message = ListAssetsOf(UStaticMesh::StaticClass(), TEXT("/Game"), 40);
			LocalOk = true;
			return;
		}
		if (Name == TEXT("list_blueprints"))
		{
			Message = ListAssetsOf(UBlueprint::StaticClass(), TEXT("/Game"), 40);
			LocalOk = true;
			return;
		}
		if (Name == TEXT("list_textures"))
		{
			Message = ListAssetsOf(UTexture::StaticClass(), TEXT("/Game"), 40);
			LocalOk = true;
			return;
		}
		if (Name == TEXT("list_sounds"))
		{
			Message = ListAssetsOf(USoundBase::StaticClass(), TEXT("/Game"), 40);
			LocalOk = true;
			return;
		}
		if (Name == TEXT("asset_exists"))
		{
			const FString Path = ArgStr(Args, TEXT("path"));
			const bool bExists = LoadObject<UObject>(nullptr, *Path) != nullptr;
			Message = bExists ? TEXT("true") : TEXT("false");
			LocalOk = true;
			return;
		}

		if (Name == TEXT("list_components"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			FString Out;
			for (UActorComponent* C : Actor->GetComponents())
			{
				if (C)
				{
					Out += C->GetName() + TEXT(" | ") + C->GetClass()->GetName() + TEXT("\n");
				}
			}
			Message = Out.IsEmpty() ? TEXT("(none)") : Out;
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_static_mesh"))
		{
			AActor* Actor = NeedActor();
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *ArgStr(Args, TEXT("mesh")));
			if (!Actor || !Mesh)
			{
				Message = TEXT("Actor or mesh missing");
				return;
			}
			UStaticMeshComponent* C = Actor->FindComponentByClass<UStaticMeshComponent>();
			if (!C)
			{
				Message = TEXT("No static mesh component");
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "SetMesh", "Editor Prompt set_static_mesh"));
			C->Modify();
			C->SetStaticMesh(Mesh);
			Redraw();
			Message = TEXT("Mesh set");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_component_relative_transform"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FString CompName = ArgStr(Args, TEXT("component"));
			USceneComponent* Comp = nullptr;
			for (UActorComponent* C : Actor->GetComponents())
			{
				if (USceneComponent* S = Cast<USceneComponent>(C))
				{
					if (CompName.IsEmpty() || S->GetName().Equals(CompName, ESearchCase::IgnoreCase))
					{
						Comp = S;
						break;
					}
				}
			}
			if (!Comp)
			{
				Message = TEXT("Component not found");
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "CompXform", "Editor Prompt set_component_relative_transform"));
			Comp->Modify();
			Comp->SetRelativeLocation(ArgLoc(Args));
			Redraw();
			Message = TEXT("Component transform set");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_component_visible"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FString CompName = ArgStr(Args, TEXT("component"));
			USceneComponent* Comp = Actor->FindComponentByClass<USceneComponent>();
			for (UActorComponent* C : Actor->GetComponents())
			{
				if (C && C->GetName().Equals(CompName, ESearchCase::IgnoreCase))
				{
					if (USceneComponent* S = Cast<USceneComponent>(C))
					{
						Comp = S;
					}
				}
			}
			if (!Comp)
			{
				Message = TEXT("Component not found");
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "CompVis", "Editor Prompt set_component_visible"));
			Comp->Modify();
			Comp->SetVisibility(ArgBool(Args, TEXT("visible"), true), true);
			Redraw();
			Message = TEXT("Component visibility set");
			LocalOk = true;
			return;
		}

		if (Name == TEXT("set_simulate_physics"))
		{
			AActor* Actor = NeedActor();
			UPrimitiveComponent* C = Actor ? Actor->FindComponentByClass<UPrimitiveComponent>() : nullptr;
			if (!C)
			{
				Message = TEXT("No primitive");
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "Phys", "Editor Prompt set_simulate_physics"));
			C->Modify();
			C->SetSimulatePhysics(ArgBool(Args, TEXT("simulate"), true));
			Message = TEXT("Physics updated");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_collision_enabled"))
		{
			AActor* Actor = NeedActor();
			UPrimitiveComponent* C = Actor ? Actor->FindComponentByClass<UPrimitiveComponent>() : nullptr;
			if (!C)
			{
				Message = TEXT("No primitive");
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "Col", "Editor Prompt set_collision_enabled"));
			C->Modify();
			C->SetCollisionEnabled(ArgBool(Args, TEXT("enabled"), true) ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
			Message = TEXT("Collision updated");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_collision_profile"))
		{
			AActor* Actor = NeedActor();
			UPrimitiveComponent* C = Actor ? Actor->FindComponentByClass<UPrimitiveComponent>() : nullptr;
			if (!C)
			{
				Message = TEXT("No primitive");
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "ColProf", "Editor Prompt set_collision_profile"));
			C->Modify();
			C->SetCollisionProfileName(*ArgStr(Args, TEXT("profile"), TEXT("BlockAll")));
			Message = TEXT("Collision profile set");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_actor_enable_gravity"))
		{
			AActor* Actor = NeedActor();
			UPrimitiveComponent* C = Actor ? Actor->FindComponentByClass<UPrimitiveComponent>() : nullptr;
			if (!C)
			{
				Message = TEXT("No primitive");
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "Grav", "Editor Prompt set_actor_enable_gravity"));
			C->Modify();
			C->SetEnableGravity(ArgBool(Args, TEXT("gravity"), true));
			Message = TEXT("Gravity updated");
			LocalOk = true;
			return;
		}

		if (Name == TEXT("set_actor_folder"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "Folder", "Editor Prompt set_actor_folder"));
			TouchActor(Actor);
			Actor->SetFolderPath(FName(*ArgStr(Args, TEXT("folder"))));
			Message = TEXT("Folder set");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("create_actor_folder"))
		{
			if (!NeedWorld())
			{
				return;
			}
			const FString Folder = ArgStr(Args, TEXT("folder"));
			if (Folder.IsEmpty())
			{
				Message = TEXT("folder required");
				return;
			}
			FActorFolders::Get().CreateFolder(*World, FFolder(FFolder::GetWorldRootFolder(World).GetRootObject(), FName(*Folder)));
			Message = TEXT("Folder created ") + Folder;
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_actor_hidden_in_game"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "HiddenGame", "Editor Prompt set_actor_hidden_in_game"));
			TouchActor(Actor);
			Actor->SetActorHiddenInGame(ArgBool(Args, TEXT("hidden"), true));
			Message = TEXT("Hidden-in-game updated");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("isolate_actor"))
		{
			AActor* Actor = NeedActor();
			if (!Actor)
			{
				return;
			}
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(Actor, true, true, true);
			GEditor->MoveViewportCamerasToActor(*Actor, false);
			Redraw();
			Message = TEXT("Isolated ") + Actor->GetActorLabel();
			LocalOk = true;
			return;
		}

		if (Name == TEXT("spawn_ambient_sound"))
		{
			if (!NeedWorld())
			{
				return;
			}
			USoundBase* Sound = LoadObject<USoundBase>(nullptr, *ArgStr(Args, TEXT("path")));
			if (!Sound)
			{
				Message = TEXT("Sound not found");
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "Snd", "Editor Prompt spawn_ambient_sound"));
			AAmbientSound* S = World->SpawnActor<AAmbientSound>(ArgLoc(Args), FRotator::ZeroRotator);
			if (!S)
			{
				Message = TEXT("Spawn sound failed");
				return;
			}
			TouchActor(S);
			const FString Label = ArgStr(Args, TEXT("label"));
			if (!Label.IsEmpty())
			{
				S->SetActorLabel(Label);
			}
			if (UAudioComponent* C = S->GetAudioComponent())
			{
				C->SetSound(Sound);
			}
			Message = TEXT("Spawned sound ") + S->GetActorLabel();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("set_sound_volume") || Name == TEXT("set_sound_playing") || Name == TEXT("set_sound_attenuation"))
		{
			AAmbientSound* S = Cast<AAmbientSound>(NeedActor());
			if (!S || !S->GetAudioComponent())
			{
				Message = TEXT("Not an ambient sound");
				return;
			}
			UAudioComponent* C = S->GetAudioComponent();
			C->Modify();
			if (Name == TEXT("set_sound_volume"))
			{
				C->SetVolumeMultiplier(static_cast<float>(ArgNum(Args, TEXT("volume"), 1)));
			}
			else if (Name == TEXT("set_sound_playing"))
			{
				if (ArgBool(Args, TEXT("playing"), true))
				{
					C->Play();
				}
				else
				{
					C->Stop();
				}
			}
			else
			{
				C->AttenuationOverrides.FalloffDistance = static_cast<float>(ArgNum(Args, TEXT("radius"), 3600));
				C->bOverrideAttenuation = true;
			}
			Message = TEXT("Sound updated");
			LocalOk = true;
			return;
		}

		if (Name == TEXT("create_level_sequence"))
		{
			FString Path = ArgStr(Args, TEXT("path"));
			if (!IsGamePath(Path))
			{
				Message = TEXT("path must start with /Game");
				return;
			}
			FString AssetName;
			FString PackagePath;
			Path.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			UPackage* Pkg = CreatePackage(*(PackagePath / AssetName));
			ULevelSequence* Seq = NewObject<ULevelSequence>(Pkg, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
			if (!Seq)
			{
				Message = TEXT("Create sequence failed");
				return;
			}
			Seq->Initialize();
			FAssetRegistryModule::AssetCreated(Seq);
			Pkg->MarkPackageDirty();
			Message = Seq->GetPathName();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("open_level_sequence"))
		{
			ULevelSequence* Seq = LoadObject<ULevelSequence>(nullptr, *ArgStr(Args, TEXT("path")));
			if (!Seq)
			{
				Message = TEXT("Sequence not found");
				return;
			}
			if (UAssetEditorSubsystem* Sys = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				Sys->OpenEditorForAsset(Seq);
			}
			Message = TEXT("Opened sequence");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("sequence_bind_actor"))
		{
			ULevelSequence* Seq = LoadObject<ULevelSequence>(nullptr, *ArgStr(Args, TEXT("path")));
			AActor* Actor = NeedActor();
			if (!Seq || !Actor || !Seq->GetMovieScene())
			{
				Message = TEXT("Sequence or actor missing");
				return;
			}
			UMovieScene* Movie = Seq->GetMovieScene();
			Movie->Modify();
			const FGuid Guid = Movie->AddPossessable(Actor->GetActorLabel(), Actor->GetClass());
			Seq->BindPossessableObject(Guid, *Actor, World);
			Seq->MarkPackageDirty();
			Message = TEXT("Bound actor to sequence");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("sequence_set_range"))
		{
			ULevelSequence* Seq = LoadObject<ULevelSequence>(nullptr, *ArgStr(Args, TEXT("path")));
			if (!Seq || !Seq->GetMovieScene())
			{
				Message = TEXT("Sequence not found");
				return;
			}
			UMovieScene* Movie = Seq->GetMovieScene();
			Movie->Modify();
			const int32 Start = ArgInt(Args, TEXT("start"), 0);
			const int32 End = ArgInt(Args, TEXT("end"), 100);
			Movie->SetPlaybackRange(FFrameNumber(Start), FMath::Max(1, End - Start));
			Seq->MarkPackageDirty();
			Message = TEXT("Sequence range set");
			LocalOk = true;
			return;
		}

		if (Name == TEXT("create_blueprint"))
		{
			FString Path = ArgStr(Args, TEXT("path"));
			if (!IsGamePath(Path))
			{
				Message = TEXT("path must start with /Game");
				return;
			}
			FString ParentPath = ArgStr(Args, TEXT("parent"), TEXT("/Script/Engine.Actor"));
			UClass* Parent = LoadClass<UObject>(nullptr, *ParentPath);
			if (!Parent)
			{
				Parent = AActor::StaticClass();
			}
			FString AssetName;
			FString PackagePath;
			Path.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			UPackage* Pkg = CreatePackage(*(PackagePath / AssetName));
			UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
				Parent, Pkg, *AssetName, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
			if (!BP)
			{
				Message = TEXT("Create blueprint failed");
				return;
			}
			FAssetRegistryModule::AssetCreated(BP);
			BP->MarkPackageDirty();
			Message = BP->GetPathName();
			LocalOk = true;
			return;
		}
		if (Name == TEXT("compile_blueprint"))
		{
			UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *ArgStr(Args, TEXT("path")));
			if (!BP)
			{
				Message = TEXT("Blueprint not found");
				return;
			}
			FKismetEditorUtilities::CompileBlueprint(BP);
			Message = TEXT("Compiled");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("spawn_blueprint"))
		{
			if (!NeedWorld())
			{
				return;
			}
			UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *ArgStr(Args, TEXT("path")));
			UClass* Cls = BP ? BP->GeneratedClass.Get() : LoadClass<AActor>(nullptr, *ArgStr(Args, TEXT("path")));
			if (!Cls)
			{
				Message = TEXT("Blueprint class not found");
				return;
			}
			const FScopedTransaction Txn(NSLOCTEXT("EditorPrompt", "SpawnBP", "Editor Prompt spawn_blueprint"));
			AActor* Actor = World->SpawnActor<AActor>(Cls, ArgLoc(Args), ArgRot(Args));
			if (!Actor)
			{
				Message = TEXT("Spawn blueprint failed");
				return;
			}
			TouchActor(Actor);
			const FString Label = ArgStr(Args, TEXT("label"));
			if (!Label.IsEmpty())
			{
				Actor->SetActorLabel(Label);
			}
			Message = TEXT("Spawned ") + Actor->GetActorLabel();
			LocalOk = true;
			return;
		}

		if (Name == TEXT("editor_undo"))
		{
			GEditor->UndoTransaction();
			Redraw();
			Message = TEXT("Undo");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("editor_redo"))
		{
			GEditor->RedoTransaction();
			Redraw();
			Message = TEXT("Redo");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("redraw_viewports"))
		{
			Redraw();
			Message = TEXT("Redrawn");
			LocalOk = true;
			return;
		}
		if (Name == TEXT("get_editor_world_name"))
		{
			Message = World ? World->GetMapName() : TEXT("none");
			LocalOk = World != nullptr;
			return;
		}

		Message = TEXT("Unknown tool: ") + Name;
	});
	bOk = LocalOk;
	return Message;
}

void FEditorPromptMcpTools::PurgeRestoreTemps(bool bSavedTemps)
{
	PurgeRestoreTempsImpl(bSavedTemps);
}
