#include "PromptEditorMcpNiagara.h"
#include "PromptEditorMcpRegistry.h"
#include "PromptEditorMcpNiagaraGraph.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/ActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraActor.h"
#include "NiagaraCommon.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceCollisionQuery.h"
#include "NiagaraDataInterfaceGrid2DCollection.h"
#include "NiagaraDataInterfaceParticleRead.h"
#include "NiagaraDataInterfaceRenderTarget2D.h"
#include "NiagaraDataInterfaceSkeletalMesh.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNode.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraParameterCollection.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraShared.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraTypes.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"

using namespace PromptEditorMcp;

namespace
{
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

	UNiagaraGraph* EmitterGraph(FVersionedNiagaraEmitterData* Data)
	{
		if (!Data)
		{
			return nullptr;
		}
		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Data->GraphSource);
		return Source ? Source->NodeGraph.Get() : nullptr;
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

	UNiagaraSystem* LoadSystem(const FString& Path, FString& Error)
	{
		UNiagaraSystem* Sys = LoadObject<UNiagaraSystem>(nullptr, *Path);
		if (!Sys)
		{
			Error = TEXT("Niagara system not found");
		}
		return Sys;
	}

	bool EmitterContext(
		const TSharedPtr<FJsonObject>& Args,
		UNiagaraSystem*& OutSys,
		FNiagaraEmitterHandle*& OutHandle,
		FVersionedNiagaraEmitterData*& OutData,
		UNiagaraGraph*& OutGraph,
		UNiagaraScriptSource*& OutSource,
		FString& Error)
	{
		OutSys = LoadSystem(ArgStr(Args, TEXT("path")), Error);
		if (!OutSys)
		{
			return false;
		}
		OutHandle = FindEmitterHandle(OutSys, ArgStr(Args, TEXT("emitter")));
		OutData = OutHandle ? OutHandle->GetEmitterData() : nullptr;
		OutGraph = EmitterGraph(OutData);
		OutSource = OutData ? Cast<UNiagaraScriptSource>(OutData->GraphSource) : nullptr;
		if (!OutHandle || !OutData || !OutGraph || !OutSource)
		{
			Error = TEXT("Emitter graph missing");
			return false;
		}
		return true;
	}

	FNiagaraVariable DiVariable(const FString& Name, UClass* DiClass)
	{
		FString Full = Name;
		if (Full.IsEmpty())
		{
			Full = TEXT("Grid");
		}
		if (!Full.Contains(TEXT(".")))
		{
			Full = TEXT("Emitter.") + Full;
		}
		return FNiagaraVariable(FNiagaraTypeDefinition(DiClass), FName(*Full));
	}

	FNiagaraVariable UserVariable(const FString& Name, UClass* Class)
	{
		FString Full = Name;
		if (Full.IsEmpty())
		{
			Full = TEXT("Object");
		}
		if (!Full.StartsWith(TEXT("User.")))
		{
			Full = TEXT("User.") + Full;
		}
		return FNiagaraVariable(FNiagaraTypeDefinition(Class), FName(*Full));
	}

	UNiagaraNodeInput* FindOrAddDiInput(UNiagaraGraph* Graph, const FNiagaraVariable& Var, UClass* DiClass, FName ObjectName)
	{
		if (!Graph || !DiClass)
		{
			return nullptr;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UNiagaraNodeInput* Input = Cast<UNiagaraNodeInput>(Node);
			if (Input && Input->Usage == ENiagaraInputNodeUsage::Parameter && Input->Input == Var)
			{
				return Input;
			}
		}
		Graph->Modify();
		FGraphNodeCreator<UNiagaraNodeInput> Creator(*Graph);
		UNiagaraNodeInput* InputNode = Creator.CreateNode(false);
		InputNode->Input = Var;
		InputNode->Usage = ENiagaraInputNodeUsage::Parameter;
		InputNode->ExposureOptions.bRequired = false;
		InputNode->Input.AllocateData();
		Creator.Finalize();
		UNiagaraDataInterface* Di = NewObject<UNiagaraDataInterface>(InputNode, DiClass, ObjectName, RF_Transactional | RF_Public);
		SetInputNodeDataInterface(InputNode, Di);
		return InputNode;
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

	UNiagaraSimulationStageGeneric* AddSimStage(
		UNiagaraEmitter* Emitter,
		const FGuid& Version,
		UNiagaraGraph* Graph,
		UNiagaraScriptSource* Source,
		const FNiagaraVariable& DiVar,
		FName StageName,
		ENiagaraSimStageExecuteBehavior Execute,
		ENiagaraIterationSource Iterate)
	{
		if (!Emitter || !Graph || !Source)
		{
			return nullptr;
		}
		UNiagaraSimulationStageGeneric* Stage = NewObject<UNiagaraSimulationStageGeneric>(Emitter, StageName, RF_Transactional);
		Stage->SimulationStageName = StageName;
		Stage->ExecuteBehavior = Execute;
		Stage->IterationSource = Iterate;
		if (Iterate == ENiagaraIterationSource::DataInterface)
		{
			Stage->DataInterface = FNiagaraVariableDataInterfaceBinding(DiVar);
		}
		Stage->Script = NewObject<UNiagaraScript>(Stage, TEXT("SimulationStage"), RF_Transactional);
		Stage->Script->SetUsage(ENiagaraScriptUsage::ParticleSimulationStageScript);
		Stage->Script->SetUsageId(Stage->GetMergeId());
		Stage->Script->SetLatestSource(Source);
		Emitter->AddSimulationStage(Stage, Version);
		EnsureStackMap(Graph, ENiagaraScriptUsage::ParticleSimulationStageScript, Stage->Script->GetUsageId());
		return Stage;
	}

	FString CompileNiagaraSystem(UNiagaraSystem* Sys)
	{
		if (!Sys)
		{
			return TEXT("No system");
		}
		PromptEditorCompileAndWait(Sys);

		TArray<FString> Errors;
		auto Collect = [&Errors](UNiagaraScript* Script, const FString& Label)
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
				if (Event.Severity == FNiagaraCompileEventSeverity::Error)
				{
					Errors.Add(Label + TEXT(": ") + Event.Message);
				}
			}
		};
		Collect(Sys->GetSystemSpawnScript(), TEXT("system_spawn"));
		Collect(Sys->GetSystemUpdateScript(), TEXT("system_update"));
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
				if (Script)
				{
					Collect(Script, Handle.GetName().ToString());
				}
			}
		}
		if (Errors.Num() > 0)
		{
			FString Out = TEXT("Compiled FAILED\n");
			const int32 MaxN = FMath::Min(Errors.Num(), 20);
			for (int32 I = 0; I < MaxN; ++I)
			{
				Out += TEXT("error: ") + Errors[I] + TEXT("\n");
			}
			return Out.TrimEnd();
		}
		if (!Sys->IsReadyToRun())
		{
			FString Out = TEXT("Compiled not ready to run");
			if (Sys->HasOutstandingCompilationRequests(true))
			{
				Out += TEXT("\noutstanding compilation (incl GPU)");
			}
			for (const FNiagaraEmitterHandle& Handle : Sys->GetEmitterHandles())
			{
				FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
				UNiagaraScript* Gpu = Data ? Data->GetGPUComputeScript() : nullptr;
				if (Gpu && Gpu->IsScriptCompilationPending(true))
				{
					Out += TEXT("\n") + Handle.GetName().ToString() + TEXT(": GPU shader pending");
				}
				else if (Gpu && !Gpu->DidScriptCompilationSucceed(true))
				{
					Out += TEXT("\n") + Handle.GetName().ToString() + TEXT(": GPU shader failed");
				}
			}
			return Out;
		}
		return TEXT("Compiled ok");
	}

	void TouchSystem(UNiagaraSystem* Sys)
	{
		if (!Sys)
		{
			return;
		}
		Sys->Modify();
		Sys->MarkPackageDirty();
		if (UPackage* Pkg = Sys->GetOutermost())
		{
			Pkg->MarkPackageDirty();
		}
	}

	UNiagaraDataInterfaceGrid2DCollection* FindGridDi(UNiagaraGraph* Graph)
	{
		if (!Graph)
		{
			return nullptr;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UNiagaraNodeInput* Input = Cast<UNiagaraNodeInput>(Node);
			if (!Input)
			{
				continue;
			}
			if (UNiagaraDataInterfaceGrid2DCollection* Grid = Cast<UNiagaraDataInterfaceGrid2DCollection>(GetInputNodeDataInterface(Input)))
			{
				return Grid;
			}
		}
		return nullptr;
	}

	FString ConfigureGrid2DEmitter(UNiagaraEmitter* Emitter, const FGuid& Version, int32 Cells, float WorldSize, int32 Attributes)
	{
		FVersionedNiagaraEmitterData* Data = Emitter ? Emitter->GetEmitterData(Version) : nullptr;
		if (!Data)
		{
			Data = Emitter ? Emitter->GetLatestEmitterData() : nullptr;
		}
		UNiagaraGraph* Graph = EmitterGraph(Data);
		UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
		if (!Emitter || !Data || !Graph || !Source)
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
		const FNiagaraVariable GridVar = DiVariable(TEXT("Grid"), UNiagaraDataInterfaceGrid2DCollection::StaticClass());
		UNiagaraNodeInput* GridInput = FindOrAddDiInput(Graph, GridVar, UNiagaraDataInterfaceGrid2DCollection::StaticClass(), TEXT("Grid"));
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
			AddSimStage(
				Emitter, Version, Graph, Source, GridVar, FName(TEXT("GridIterate")),
				ENiagaraSimStageExecuteBehavior::Always, ENiagaraIterationSource::DataInterface);
		}
		if (UNiagaraSimulationStageBase* Stage = FindSimStage(Data, TEXT("GridIterate")))
		{
			UNiagaraNodeOutput* StageOut = Stage->Script
				? FindOutputNode(Graph, ENiagaraScriptUsage::ParticleSimulationStageScript, Stage->Script->GetUsageId())
				: nullptr;
			PromptEditorEnsureDiOnSimStage(Graph, StageOut, GridVar);
		}
		Graph->NotifyGraphChanged();
		return FString::Printf(TEXT("grid2d cells=%d stages=%d"), Cells, Data->GetSimulationStages().Num());
	}

	UPackage* CreateGamePackage(const FString& Path, FString& OutAssetName, FString& Error)
	{
		FString PackagePath;
		if (!SplitGameAssetPath(Path, PackagePath, OutAssetName, Error))
		{
			return nullptr;
		}
		return CreatePackage(*(PackagePath / OutAssetName));
	}

	ENiagaraSimStageExecuteBehavior ParseExecute(const FString& In)
	{
		const FString S = In.Replace(TEXT(" "), TEXT("_")).ToLower();
		if (S.Contains(TEXT("not")) && S.Contains(TEXT("reset")))
		{
			return ENiagaraSimStageExecuteBehavior::NotOnSimulationReset;
		}
		if (S.Contains(TEXT("reset")))
		{
			return ENiagaraSimStageExecuteBehavior::OnSimulationReset;
		}
		return ENiagaraSimStageExecuteBehavior::Always;
	}

	ENiagaraIterationSource ParseIterate(const FString& In)
	{
		const FString S = In.Replace(TEXT(" "), TEXT("_")).ToLower();
		if (S.Contains(TEXT("particle")))
		{
			return ENiagaraIterationSource::Particles;
		}
		return ENiagaraIterationSource::DataInterface;
	}

	const TCHAR* ExecuteLabel(ENiagaraSimStageExecuteBehavior Execute)
	{
		switch (Execute)
		{
		case ENiagaraSimStageExecuteBehavior::OnSimulationReset: return TEXT("reset");
		case ENiagaraSimStageExecuteBehavior::NotOnSimulationReset: return TEXT("not_reset");
		default: return TEXT("always");
		}
	}

	AActor* NeedActor(UWorld* World, const TSharedPtr<FJsonObject>& Args, FString& Error, const TCHAR* Key = TEXT("label"))
	{
		if (!World)
		{
			Error = TEXT("No editor world");
			return nullptr;
		}
		AActor* Actor = FindLabel(World, ArgStr(Args, Key));
		if (!Actor)
		{
			Error = TEXT("Actor not found");
		}
		return Actor;
	}

	UNiagaraComponent* NiagaraComp(AActor* Actor)
	{
		return Actor ? Actor->FindComponentByClass<UNiagaraComponent>() : nullptr;
	}

	void EnsureUserParam(UNiagaraSystem* Sys, const FNiagaraVariable& Var)
	{
		if (!Sys)
		{
			return;
		}
		FNiagaraUserRedirectionParameterStore& Store = Sys->GetExposedParameters();
		if (Store.IndexOf(Var) == INDEX_NONE)
		{
			Store.AddParameter(Var);
		}
	}

	TSharedRef<FJsonObject> PathEmitterProps()
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system /Game path")));
		P->SetObjectField(TEXT("emitter"), Prop(TEXT("string"), TEXT("Emitter name, default first")));
		return P;
	}

	UMaterialInterface* FindTextureParent(FName& OutParam)
	{
		static const TCHAR* Candidates[] = {
			TEXT("/Engine/EngineMaterials/Widget3DPassThrough.Widget3DPassThrough"),
			TEXT("/Niagara/DefaultMaterial/M_NiagaraDefaultSpriteMaterial.M_NiagaraDefaultSpriteMaterial"),
			TEXT("/Engine/EngineMaterials/DefaultTexturedMaterial.DefaultTexturedMaterial"),
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"),
		};
		for (const TCHAR* Path : Candidates)
		{
			UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, Path);
			if (!Mat)
			{
				continue;
			}
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			Mat->GetAllTextureParameterInfo(Infos, Ids);
			if (Infos.Num() > 0)
			{
				OutParam = Infos[0].Name;
				return Mat;
			}
		}
		return nullptr;
	}

	UTextureRenderTarget2D* MakeOrLoadRT(const FString& Path, int32 Size, FString& Error)
	{
		if (UTextureRenderTarget2D* Existing = LoadObject<UTextureRenderTarget2D>(nullptr, *Path))
		{
			return Existing;
		}
		FString AssetName;
		UPackage* Pkg = CreateGamePackage(Path, AssetName, Error);
		if (!Pkg)
		{
			return nullptr;
		}
		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(
			Pkg, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (!RT)
		{
			Error = TEXT("Create render target failed");
			return nullptr;
		}
		Size = FMath::Clamp(Size, 8, 4096);
		RT->RenderTargetFormat = RTF_RGBA16f;
		RT->ClearColor = FLinearColor(1.f, 0.f, 1.f, 1.f);
		RT->bAutoGenerateMips = false;
		RT->InitAutoFormat(Size, Size);
		RT->UpdateResourceImmediate(true);
		FAssetRegistryModule::AssetCreated(RT);
		Pkg->MarkPackageDirty();
		return RT;
	}

	UMaterialInstanceConstant* MakeRtMic(UTexture* Texture, const FString& Path, FString& Error)
	{
		FName TexParam;
		UMaterialInterface* Parent = FindTextureParent(TexParam);
		if (!Parent || !Texture)
		{
			Error = TEXT("No parent material with a texture parameter");
			return nullptr;
		}
		if (UMaterialInstanceConstant* Existing = LoadObject<UMaterialInstanceConstant>(nullptr, *Path))
		{
			Existing->Modify();
			Existing->SetTextureParameterValueEditorOnly(TexParam, Texture);
			Existing->MarkPackageDirty();
			Existing->PostEditChange();
			return Existing;
		}
		FString AssetName;
		FString PackagePath;
		if (!SplitGameAssetPath(Path, PackagePath, AssetName, Error))
		{
			return nullptr;
		}
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
		Factory->InitialParent = Parent;
		UObject* Created = AssetTools.CreateAsset(AssetName, PackagePath, UMaterialInstanceConstant::StaticClass(), Factory);
		UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Created);
		if (!MIC)
		{
			Error = TEXT("Create MIC failed");
			return nullptr;
		}
		MIC->SetTextureParameterValueEditorOnly(TexParam, Texture);
		MIC->MarkPackageDirty();
		MIC->PostEditChange();
		return MIC;
	}
}

void RegisterPromptEditorNiagaraTools()
{
	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("/Game path for the collection")));
		RegisterTool(
			TEXT("create_niagara_parameter_collection"),
			TEXT("Create a Niagara Parameter Collection asset. Shared floats/vectors, not a character."),
			P,
			[](const TSharedPtr<FJsonObject>& Args, UWorld*, bool& bOk) -> FString
			{
				const FString Path = ArgStr(Args, TEXT("path"));
				FString AssetName;
				FString Error;
				UPackage* Pkg = CreateGamePackage(Path, AssetName, Error);
				if (!Pkg)
				{
					return Error;
				}
				if (LoadObject<UNiagaraParameterCollection>(nullptr, *Path)
					|| LoadObject<UNiagaraParameterCollection>(nullptr, *(Pkg->GetName() + TEXT(".") + AssetName)))
				{
					return TEXT("Asset already exists");
				}
				UNiagaraParameterCollection* Collection = NewObject<UNiagaraParameterCollection>(
					Pkg, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
				if (!Collection)
				{
					return TEXT("Create parameter collection failed");
				}
				Collection->RefreshCompileId();
				FAssetRegistryModule::AssetCreated(Collection);
				Pkg->MarkPackageDirty();
				bOk = true;
				return Collection->GetPathName();
			});
	}

	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Parameter collection path")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("Friendly parameter name")));
		P->SetObjectField(TEXT("value"), Prop(TEXT("number"), TEXT("Float value")));
		P->SetObjectField(TEXT("x"), Prop(TEXT("number"), TEXT("Vector X")));
		P->SetObjectField(TEXT("y"), Prop(TEXT("number"), TEXT("Vector Y")));
		P->SetObjectField(TEXT("z"), Prop(TEXT("number"), TEXT("Vector Z")));
		RegisterTool(
			TEXT("set_niagara_collection"),
			TEXT("Add or set a float or vector on a Niagara Parameter Collection default instance."),
			P,
			[](const TSharedPtr<FJsonObject>& Args, UWorld*, bool& bOk) -> FString
			{
				const FString Path = ArgStr(Args, TEXT("path"));
				const FString Name = ArgStr(Args, TEXT("name"));
				UNiagaraParameterCollection* Collection = LoadObject<UNiagaraParameterCollection>(nullptr, *Path);
				if (!Collection)
				{
					return TEXT("Parameter collection not found");
				}
				if (Name.IsEmpty())
				{
					return TEXT("name required");
				}
				UNiagaraParameterCollectionInstance* Inst = Collection->GetDefaultInstance();
				if (!Inst)
				{
					return TEXT("Collection has no default instance");
				}
				Collection->Modify();
				Inst->Modify();
				const bool bVec = ArgsHaveLoc(Args);
				if (bVec)
				{
					Collection->AddParameter(*Name, FNiagaraTypeDefinition::GetVec3Def());
					Inst->SetVectorParameter(Name, ArgLoc(Args));
				}
				else
				{
					Collection->AddParameter(*Name, FNiagaraTypeDefinition::GetFloatDef());
					Inst->SetFloatParameter(Name, static_cast<float>(ArgNum(Args, TEXT("value"))));
				}
				Collection->MarkPackageDirty();
				bOk = true;
				return FString::Printf(TEXT("Set %s on %s"), *Name, *Collection->GetName());
			});
	}

	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path")));
		P->SetObjectField(TEXT("collection"), Prop(TEXT("string"), TEXT("Parameter collection path")));
		RegisterTool(
			TEXT("bind_niagara_collection"),
			TEXT("Attach a parameter collection override instance to a Niagara system."),
			P,
			[](const TSharedPtr<FJsonObject>& Args, UWorld*, bool& bOk) -> FString
			{
				FString Error;
				UNiagaraSystem* Sys = LoadSystem(ArgStr(Args, TEXT("path")), Error);
				if (!Sys)
				{
					return Error;
				}
				UNiagaraParameterCollection* Collection = LoadObject<UNiagaraParameterCollection>(
					nullptr, *ArgStr(Args, TEXT("collection")));
				if (!Collection)
				{
					return TEXT("Parameter collection not found");
				}
				if (Sys->GetParameterCollectionOverride(Collection))
				{
					bOk = true;
					return TEXT("Collection already bound");
				}
				Sys->Modify();
				UNiagaraParameterCollectionInstance* Inst = NewObject<UNiagaraParameterCollectionInstance>(
					Sys, NAME_None, RF_Transactional);
				Inst->SetParent(Collection);
				if (FArrayProperty* Prop = FindFProperty<FArrayProperty>(
					UNiagaraSystem::StaticClass(), TEXT("ParameterCollectionOverrides")))
				{
					TArray<TObjectPtr<UNiagaraParameterCollectionInstance>>* Arr =
						Prop->ContainerPtrToValuePtr<TArray<TObjectPtr<UNiagaraParameterCollectionInstance>>>(Sys);
					if (Arr)
					{
						Arr->Add(Inst);
					}
				}
				TouchSystem(Sys);
				bOk = true;
				return TEXT("Bound collection ") + Collection->GetName();
			});
	}

	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("/Game path for the render target")));
		P->SetObjectField(TEXT("width"), Prop(TEXT("number"), TEXT("Width, default 256")));
		P->SetObjectField(TEXT("height"), Prop(TEXT("number"), TEXT("Height, default same as width")));
		P->SetObjectField(TEXT("size"), Prop(TEXT("number"), TEXT("Square size if width/height omitted")));
		P->SetObjectField(TEXT("format"), Prop(TEXT("string"), TEXT("rgba8, rgba16f, or r32f")));
		RegisterTool(
			TEXT("create_render_target"),
			TEXT("Create a TextureRenderTarget2D under /Game. Not a Niagara system."),
			P,
			[](const TSharedPtr<FJsonObject>& Args, UWorld*, bool& bOk) -> FString
			{
				const FString Path = ArgStr(Args, TEXT("path"));
				FString AssetName;
				FString Error;
				UPackage* Pkg = CreateGamePackage(Path, AssetName, Error);
				if (!Pkg)
				{
					return Error;
				}
				if (LoadObject<UTextureRenderTarget2D>(nullptr, *Path)
					|| LoadObject<UTextureRenderTarget2D>(nullptr, *(Pkg->GetName() + TEXT(".") + AssetName)))
				{
					return TEXT("Asset already exists");
				}
				const int32 Size = FMath::Clamp(ArgInt(Args, TEXT("size"), 256), 8, 4096);
				const int32 Width = FMath::Clamp(ArgInt(Args, TEXT("width"), Size), 8, 4096);
				const int32 Height = FMath::Clamp(ArgInt(Args, TEXT("height"), Width), 8, 4096);
				UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(
					Pkg, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
				if (!RT)
				{
					return TEXT("Create render target failed");
				}
				const FString Format = ArgStr(Args, TEXT("format"), TEXT("rgba16f")).ToLower();
				if (Format.Contains(TEXT("r32")))
				{
					RT->RenderTargetFormat = RTF_R32f;
				}
				else if (Format.Contains(TEXT("8")))
				{
					RT->RenderTargetFormat = RTF_RGBA8;
				}
				else
				{
					RT->RenderTargetFormat = RTF_RGBA16f;
				}
				RT->ClearColor = FLinearColor::Black;
				RT->bAutoGenerateMips = false;
				RT->InitAutoFormat(Width, Height);
				RT->UpdateResourceImmediate(true);
				FAssetRegistryModule::AssetCreated(RT);
				Pkg->MarkPackageDirty();
				bOk = true;
				return FString::Printf(TEXT("%s %dx%d"), *RT->GetPathName(), Width, Height);
			});
	}

	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path. Omit if using label.")));
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Spawned Niagara actor. Omit if using path.")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("User parameter name")));
		P->SetObjectField(TEXT("kind"), Prop(TEXT("string"), TEXT("mesh, texture, or rt")));
		P->SetObjectField(TEXT("object"), Prop(TEXT("string"), TEXT("Asset path of the object to bind")));
		RegisterTool(
			TEXT("set_niagara_user_object"),
			TEXT("Set a User object on a Niagara system asset or spawned component (mesh, texture, or render target)."),
			P,
			[](const TSharedPtr<FJsonObject>& Args, UWorld* World, bool& bOk) -> FString
			{
				const FString Kind = ArgStr(Args, TEXT("kind"), TEXT("rt")).ToLower();
				const FString Name = ArgStr(Args, TEXT("name"));
				const FString ObjectPath = ArgStr(Args, TEXT("object"));
				if (Name.IsEmpty() || ObjectPath.IsEmpty())
				{
					return TEXT("name and object required");
				}
				UClass* Class = UTextureRenderTarget2D::StaticClass();
				if (Kind.Contains(TEXT("mesh")))
				{
					Class = USkeletalMesh::StaticClass();
				}
				else if (Kind.Contains(TEXT("tex")))
				{
					Class = UTexture::StaticClass();
				}
				UObject* Object = StaticLoadObject(Class, nullptr, *ObjectPath);
				if (!Object && Kind.Contains(TEXT("rt")))
				{
					Object = LoadObject<UTextureRenderTarget2D>(nullptr, *ObjectPath);
				}
				if (!Object)
				{
					return TEXT("Object asset not found");
				}
				const FNiagaraVariable Var = UserVariable(Name, Object->GetClass());
				const FString SysPath = ArgStr(Args, TEXT("path"));
				if (!SysPath.IsEmpty())
				{
					FString Error;
					UNiagaraSystem* Sys = LoadSystem(SysPath, Error);
					if (!Sys)
					{
						return Error;
					}
					Sys->Modify();
					EnsureUserParam(Sys, Var);
					Sys->GetExposedParameters().SetUObject(Object, Var);
					TouchSystem(Sys);
					bOk = true;
					return TEXT("User object set on system ") + Var.GetName().ToString();
				}
				FString Error;
				UNiagaraComponent* Comp = NiagaraComp(NeedActor(World, Args, Error));
				if (!Comp)
				{
					return Error.IsEmpty() ? TEXT("No Niagara component") : Error;
				}
				const FScopedTransaction Txn(NSLOCTEXT("PromptEditor", "UserObject", "PromptEditor set_niagara_user_object"));
				TouchActor(Comp->GetOwner());
				Comp->Modify();
				const FName ParamName(*Name);
				if (UTextureRenderTarget* RT = Cast<UTextureRenderTarget>(Object))
				{
					Comp->SetVariableTextureRenderTarget(ParamName, RT);
				}
				else if (UTexture* Tex = Cast<UTexture>(Object))
				{
					Comp->SetVariableTexture(ParamName, Tex);
				}
				else
				{
					Comp->SetVariableObject(ParamName, Object);
				}
				Redraw();
				bOk = true;
				return TEXT("User object set on actor");
			});
	}

	{
		TSharedRef<FJsonObject> P = PathEmitterProps();
		P->SetObjectField(TEXT("kind"), Prop(TEXT("string"), TEXT("grid2d, rt, par, query, or mesh")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("DI parameter name, default from kind")));
		P->SetObjectField(TEXT("source_emitter"), Prop(TEXT("string"), TEXT("Emitter to read when kind=par")));
		P->SetObjectField(TEXT("size"), Prop(TEXT("number"), TEXT("RT DI size when kind=rt")));
		P->SetObjectField(TEXT("mesh"), Prop(TEXT("string"), TEXT("Skeletal mesh path when kind=mesh")));
		P->SetObjectField(TEXT("source_mode"), Prop(TEXT("string"), TEXT("default, attach, or mesh when kind=mesh")));
		P->SetObjectField(TEXT("tag"), Prop(TEXT("string"), TEXT("Component tag for mesh sampling")));
		RegisterTool(
			TEXT("add_niagara_di"),
			TEXT("Add an emitter data interface: Grid2D, Render Target, Particle Attribute Reader, Collision Query, or Skeletal Mesh."),
			P,
			[](const TSharedPtr<FJsonObject>& Args, UWorld*, bool& bOk) -> FString
			{
				UNiagaraSystem* Sys = nullptr;
				FNiagaraEmitterHandle* Handle = nullptr;
				FVersionedNiagaraEmitterData* Data = nullptr;
				UNiagaraGraph* Graph = nullptr;
				UNiagaraScriptSource* Source = nullptr;
				FString Error;
				if (!EmitterContext(Args, Sys, Handle, Data, Graph, Source, Error))
				{
					return Error;
				}
				(void)Source;
				const FString Kind = ArgStr(Args, TEXT("kind")).ToLower();
				UClass* DiClass = nullptr;
				FString DefaultName;
				if (Kind == TEXT("grid2d") || Kind == TEXT("grid"))
				{
					DiClass = UNiagaraDataInterfaceGrid2DCollection::StaticClass();
					DefaultName = TEXT("Grid");
				}
				else if (Kind == TEXT("rt") || Kind.Contains(TEXT("render")))
				{
					DiClass = UNiagaraDataInterfaceRenderTarget2D::StaticClass();
					DefaultName = TEXT("RenderTarget");
				}
				else if (Kind == TEXT("par") || Kind.Contains(TEXT("particle")))
				{
					DiClass = UNiagaraDataInterfaceParticleRead::StaticClass();
					DefaultName = TEXT("ParticleRead");
				}
				else if (Kind.Contains(TEXT("query")) || Kind.Contains(TEXT("collision")))
				{
					DiClass = UNiagaraDataInterfaceCollisionQuery::StaticClass();
					DefaultName = TEXT("CollisionQuery");
				}
				else if (Kind.Contains(TEXT("mesh")))
				{
					DiClass = UNiagaraDataInterfaceSkeletalMesh::StaticClass();
					DefaultName = TEXT("SkeletalMesh");
				}
				else
				{
					return TEXT("kind must be grid2d, rt, par, query, or mesh");
				}
				const FString Name = ArgStr(Args, TEXT("name"), DefaultName);
				Sys->Modify();
				Graph->Modify();
				const FNiagaraVariable Var = DiVariable(Name, DiClass);
				UNiagaraNodeInput* Input = FindOrAddDiInput(Graph, Var, DiClass, *Name);
				UNiagaraDataInterface* Di = GetInputNodeDataInterface(Input);
				if (!Di)
				{
					return TEXT("Data interface create failed");
				}
				if (UNiagaraDataInterfaceRenderTarget2D* RTDI = Cast<UNiagaraDataInterfaceRenderTarget2D>(Di))
				{
					const int32 Size = FMath::Clamp(ArgInt(Args, TEXT("size"), 256), 8, 4096);
					RTDI->Size = FIntPoint(Size, Size);
				}
				if (UNiagaraDataInterfaceParticleRead* Par = Cast<UNiagaraDataInterfaceParticleRead>(Di))
				{
					const FString SourceEmitter = ArgStr(Args, TEXT("source_emitter"));
					Par->EmitterBinding.BindingMode = SourceEmitter.IsEmpty()
						? ENiagaraDataInterfaceEmitterBindingMode::Self
						: ENiagaraDataInterfaceEmitterBindingMode::Other;
					if (!SourceEmitter.IsEmpty())
					{
						Par->EmitterBinding.EmitterName = *SourceEmitter;
					}
				}
				if (UNiagaraDataInterfaceSkeletalMesh* MeshDi = Cast<UNiagaraDataInterfaceSkeletalMesh>(Di))
				{
					const FString Mode = ArgStr(Args, TEXT("source_mode"), TEXT("attach")).ToLower();
					if (Mode.Contains(TEXT("mesh")))
					{
						MeshDi->SourceMode = ENDISkeletalMesh_SourceMode::DefaultMeshOnly;
					}
					else if (Mode.Contains(TEXT("attach")))
					{
						MeshDi->SourceMode = ENDISkeletalMesh_SourceMode::AttachParent;
					}
					else
					{
						MeshDi->SourceMode = ENDISkeletalMesh_SourceMode::Default;
					}
					const FString MeshPath = ArgStr(Args, TEXT("mesh"));
					if (!MeshPath.IsEmpty())
					{
						if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath))
						{
							MeshDi->DefaultMesh = Mesh;
#if WITH_EDITORONLY_DATA
							MeshDi->PreviewMesh = Mesh;
#endif
						}
					}
					const FString Tag = ArgStr(Args, TEXT("tag"));
					if (!Tag.IsEmpty())
					{
						if (FArrayProperty* TagsProp = FindFProperty<FArrayProperty>(
							UNiagaraDataInterfaceSkeletalMesh::StaticClass(), TEXT("ComponentTags")))
						{
							if (TArray<FName>* Tags = TagsProp->ContainerPtrToValuePtr<TArray<FName>>(MeshDi))
							{
								Tags->AddUnique(*Tag);
							}
						}
					}
				}
				if (UNiagaraDataInterfaceGrid2DCollection* Grid = Cast<UNiagaraDataInterfaceGrid2DCollection>(Di))
				{
					Grid->bPreviewGrid = true;
					Grid->PreviewAttribute = FName(TEXT("Attribute At Index 0"));
					for (UNiagaraSimulationStageBase* Stage : Data->GetSimulationStages())
					{
						if (Stage && Stage->Script)
						{
							PromptEditorEnsureDiOnSimStage(
								Graph,
								FindOutputNode(Graph, ENiagaraScriptUsage::ParticleSimulationStageScript, Stage->Script->GetUsageId()),
								Var);
						}
					}
				}
				Graph->NotifyGraphChanged();
				TouchSystem(Sys);
				const FString Compile = CompileNiagaraSystem(Sys);
				bOk = true;
				return FString::Printf(TEXT("Added %s %s\n%s"), *Kind, *Var.GetName().ToString(), *Compile);
			});
	}

	{
		TSharedRef<FJsonObject> P = PathEmitterProps();
		P->SetObjectField(TEXT("stage"), Prop(TEXT("string"), TEXT("Stage name. Created if missing.")));
		P->SetObjectField(TEXT("execute"), Prop(TEXT("string"), TEXT("always, reset, or not_reset")));
		P->SetObjectField(TEXT("iterate"), Prop(TEXT("string"), TEXT("data_interface or particles")));
		P->SetObjectField(TEXT("di"), Prop(TEXT("string"), TEXT("DI parameter to iterate, default Emitter.Grid")));
		RegisterTool(
			TEXT("set_niagara_sim_stage"),
			TEXT("Create or configure a simulation stage: execute always/reset, iterate particles or a data interface."),
			P,
			[](const TSharedPtr<FJsonObject>& Args, UWorld*, bool& bOk) -> FString
			{
				UNiagaraSystem* Sys = nullptr;
				FNiagaraEmitterHandle* Handle = nullptr;
				FVersionedNiagaraEmitterData* Data = nullptr;
				UNiagaraGraph* Graph = nullptr;
				UNiagaraScriptSource* Source = nullptr;
				FString Error;
				if (!EmitterContext(Args, Sys, Handle, Data, Graph, Source, Error))
				{
					return Error;
				}
				const FString StageName = ArgStr(Args, TEXT("stage"), TEXT("GridIterate"));
				const ENiagaraSimStageExecuteBehavior Execute = ParseExecute(ArgStr(Args, TEXT("execute"), TEXT("always")));
				const ENiagaraIterationSource Iterate = ParseIterate(ArgStr(Args, TEXT("iterate"), TEXT("data_interface")));
				const FString DiName = ArgStr(Args, TEXT("di"), TEXT("Emitter.Grid"));
				UClass* DiClass = UNiagaraDataInterfaceGrid2DCollection::StaticClass();
				if (DiName.Contains(TEXT("RenderTarget")) || DiName.Contains(TEXT("RT")))
				{
					DiClass = UNiagaraDataInterfaceRenderTarget2D::StaticClass();
				}
				const FNiagaraVariable DiVar = DiVariable(DiName, DiClass);
				Sys->Modify();
				UNiagaraSimulationStageGeneric* Stage = Cast<UNiagaraSimulationStageGeneric>(FindSimStage(Data, StageName));
				if (!Stage)
				{
					UNiagaraEmitter* Emitter = Handle->GetInstance().Emitter;
					Stage = AddSimStage(
						Emitter, Handle->GetInstance().Version, Graph, Source, DiVar, *StageName, Execute, Iterate);
				}
				if (!Stage)
				{
					return TEXT("Simulation stage failed");
				}
				Stage->Modify();
				Stage->ExecuteBehavior = Execute;
				Stage->IterationSource = Iterate;
				if (Iterate == ENiagaraIterationSource::DataInterface)
				{
					Stage->DataInterface = FNiagaraVariableDataInterfaceBinding(DiVar);
				}
				if (Stage->Script)
				{
					PromptEditorEnsureDiOnSimStage(
						Graph,
						FindOutputNode(Graph, ENiagaraScriptUsage::ParticleSimulationStageScript, Stage->Script->GetUsageId()),
						DiVar);
				}
				Graph->NotifyGraphChanged();
				TouchSystem(Sys);
				const FString Compile = CompileNiagaraSystem(Sys);
				bOk = true;
				return FString::Printf(
					TEXT("Stage %s execute=%s iterate=%s\n%s"),
					*Stage->SimulationStageName.ToString(),
					ExecuteLabel(Execute),
					Iterate == ENiagaraIterationSource::Particles ? TEXT("particles") : TEXT("data_interface"),
					*Compile);
			});
	}

	{
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetObjectField(TEXT("path"), Prop(TEXT("string"), TEXT("Niagara system path")));
		P->SetObjectField(TEXT("kind"), Prop(TEXT("string"), TEXT("sprite, grid2d, or empty")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("New emitter name")));
		P->SetObjectField(TEXT("cells"), Prop(TEXT("number"), TEXT("Grid cells when kind=grid2d")));
		P->SetObjectField(TEXT("world_size"), Prop(TEXT("number"), TEXT("Grid world size when kind=grid2d")));
		RegisterTool(
			TEXT("add_niagara_emitter"),
			TEXT("Add a sprite, Grid2D, or empty emitter to an existing Niagara system."),
			P,
			[](const TSharedPtr<FJsonObject>& Args, UWorld*, bool& bOk) -> FString
			{
				FString Error;
				UNiagaraSystem* Sys = LoadSystem(ArgStr(Args, TEXT("path")), Error);
				if (!Sys)
				{
					return Error;
				}
				const FString Kind = ArgStr(Args, TEXT("kind"), TEXT("sprite")).ToLower();
				const bool bDefaults = Kind.Contains(TEXT("sprite"));
				const bool bGrid = Kind.Contains(TEXT("grid"));
				Sys->Modify();
				UNiagaraEmitter* Emitter = NewObject<UNiagaraEmitter>(GetTransientPackage(), NAME_None, RF_Transactional);
				UNiagaraEmitterFactoryNew::InitializeEmitter(Emitter, bDefaults && !bGrid);
				FNiagaraEditorUtilities::AddEmitterToSystem(*Sys, *Emitter, Emitter->GetExposedVersion().VersionGuid, true);
				if (Sys->GetEmitterHandles().Num() == 0)
				{
					return TEXT("Add emitter failed");
				}
				FNiagaraEmitterHandle& Handle = Sys->GetEmitterHandles().Last();
				FString EmitterName = ArgStr(Args, TEXT("name"));
				if (EmitterName.IsEmpty())
				{
					EmitterName = bGrid ? TEXT("GridEmitter") : (bDefaults ? TEXT("SpriteEmitter") : TEXT("Emitter"));
				}
				Handle.SetName(*EmitterName, *Sys);
				FString Setup;
				if (bGrid)
				{
					UNiagaraEmitter* Live = Handle.GetInstance().Emitter;
					Setup = ConfigureGrid2DEmitter(
						Live, Handle.GetInstance().Version,
						ArgInt(Args, TEXT("cells"), 64),
						static_cast<float>(ArgNum(Args, TEXT("world_size"), 200)),
						1);
				}
				TouchSystem(Sys);
				const FString Compile = CompileNiagaraSystem(Sys);
				bOk = true;
				return FString::Printf(TEXT("Added emitter %s\n%s\n%s"), *Handle.GetName().ToString(), *Setup, *Compile);
			});
	}

	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("parent"), Prop(TEXT("string"), TEXT("Parent actor label")));
		P->SetObjectField(TEXT("socket"), Prop(TEXT("string"), TEXT("Optional skeletal mesh socket")));
		RegisterTool(
			TEXT("attach_niagara"),
			TEXT("Attach a Niagara actor to a parent. Optional skeletal mesh socket. Editor viewport, not PIE."),
			P,
			[](const TSharedPtr<FJsonObject>& Args, UWorld* World, bool& bOk) -> FString
			{
				FString Error;
				AActor* Child = NeedActor(World, Args, Error);
				AActor* Parent = World ? FindLabel(World, ArgStr(Args, TEXT("parent"))) : nullptr;
				if (!Child || !Parent)
				{
					return Error.IsEmpty() ? FString(TEXT("child or parent not found")) : Error;
				}
				const FScopedTransaction Txn(NSLOCTEXT("PromptEditor", "AttachNiagara", "PromptEditor attach_niagara"));
				TouchActor(Child);
				const FString Socket = ArgStr(Args, TEXT("socket"));
				if (!Socket.IsEmpty())
				{
					if (USkeletalMeshComponent* Skel = Parent->FindComponentByClass<USkeletalMeshComponent>())
					{
						Child->AttachToComponent(Skel, FAttachmentTransformRules::KeepWorldTransform, *Socket);
						Redraw();
						bOk = true;
						return TEXT("Attached to socket ") + Socket;
					}
					return TEXT("Parent has no skeletal mesh component");
				}
				Child->AttachToActor(Parent, FAttachmentTransformRules::KeepWorldTransform);
				Redraw();
				bOk = true;
				return TEXT("Attached");
			});
	}

	{
		TSharedRef<FJsonObject> P = LabelProps();
		P->SetObjectField(TEXT("tag"), Prop(TEXT("string"), TEXT("Component tag")));
		P->SetObjectField(TEXT("component"), Prop(TEXT("string"), TEXT("Component name. Empty = skeletal mesh components, else root.")));
		P->SetObjectField(TEXT("remove"), Prop(TEXT("boolean"), TEXT("Remove the tag if true")));
		RegisterTool(
			TEXT("set_component_tag"),
			TEXT("Add or remove a component tag. Used with skeletal mesh sampling on an attached Niagara system."),
			P,
			[](const TSharedPtr<FJsonObject>& Args, UWorld* World, bool& bOk) -> FString
			{
				FString Error;
				AActor* Actor = NeedActor(World, Args, Error);
				if (!Actor)
				{
					return Error;
				}
				const FName Tag(*ArgStr(Args, TEXT("tag")));
				if (Tag.IsNone())
				{
					return TEXT("tag required");
				}
				const FScopedTransaction Txn(NSLOCTEXT("PromptEditor", "CompTag", "PromptEditor set_component_tag"));
				TouchActor(Actor);
				const FString CompName = ArgStr(Args, TEXT("component"));
				const bool bRemove = ArgBool(Args, TEXT("remove"), false);
				TArray<UActorComponent*> Targets;
				if (!CompName.IsEmpty())
				{
					TArray<UActorComponent*> All;
					Actor->GetComponents(All);
					for (UActorComponent* Comp : All)
					{
						if (Comp && Comp->GetName().Equals(CompName, ESearchCase::IgnoreCase))
						{
							Targets.Add(Comp);
						}
					}
				}
				else
				{
					TArray<USkeletalMeshComponent*> Skels;
					Actor->GetComponents(Skels);
					for (USkeletalMeshComponent* Skel : Skels)
					{
						Targets.Add(Skel);
					}
					if (Targets.Num() == 0 && Actor->GetRootComponent())
					{
						Targets.Add(Actor->GetRootComponent());
					}
				}
				if (Targets.Num() == 0)
				{
					return TEXT("No matching component");
				}
				for (UActorComponent* Comp : Targets)
				{
					Comp->Modify();
					if (bRemove)
					{
						Comp->ComponentTags.Remove(Tag);
					}
					else
					{
						Comp->ComponentTags.AddUnique(Tag);
					}
				}
				bOk = true;
				return FString::Printf(TEXT("%s tag %s on %d component(s)"),
					bRemove ? TEXT("Removed") : TEXT("Added"), *Tag.ToString(), Targets.Num());
			});
	}

	{
		TSharedRef<FJsonObject> P = PathEmitterProps();
		P->SetObjectField(TEXT("render_target"), Prop(TEXT("string"), TEXT("TextureRenderTarget2D path")));
		P->SetObjectField(TEXT("name"), Prop(TEXT("string"), TEXT("User parameter name, default RenderTarget")));
		RegisterTool(
			TEXT("set_niagara_grid_target"),
			TEXT("Bind a Grid2D Collection to a user render target (export). Cell count follows the RT size when possible."),
			P,
			[](const TSharedPtr<FJsonObject>& Args, UWorld*, bool& bOk) -> FString
			{
				UNiagaraSystem* Sys = nullptr;
				FNiagaraEmitterHandle* Handle = nullptr;
				FVersionedNiagaraEmitterData* Data = nullptr;
				UNiagaraGraph* Graph = nullptr;
				UNiagaraScriptSource* Source = nullptr;
				FString Error;
				if (!EmitterContext(Args, Sys, Handle, Data, Graph, Source, Error))
				{
					return Error;
				}
				UNiagaraDataInterfaceGrid2DCollection* Grid = FindGridDi(Graph);
				if (!Grid)
				{
					const FNiagaraVariable GridVar = DiVariable(TEXT("Grid"), UNiagaraDataInterfaceGrid2DCollection::StaticClass());
					UNiagaraNodeInput* Input = FindOrAddDiInput(
						Graph, GridVar, UNiagaraDataInterfaceGrid2DCollection::StaticClass(), TEXT("Grid"));
					Grid = Cast<UNiagaraDataInterfaceGrid2DCollection>(GetInputNodeDataInterface(Input));
				}
				if (!Grid)
				{
					return TEXT("Grid2D Collection not found");
				}
				const FString RtPath = ArgStr(Args, TEXT("render_target"));
				UTextureRenderTarget2D* RT = RtPath.IsEmpty()
					? nullptr
					: LoadObject<UTextureRenderTarget2D>(nullptr, *RtPath);
				if (!RtPath.IsEmpty() && !RT)
				{
					return TEXT("Render target not found");
				}
				const FNiagaraVariable UserVar = UserVariable(
					ArgStr(Args, TEXT("name"), TEXT("RenderTarget")),
					UTextureRenderTarget2D::StaticClass());
				Sys->Modify();
				Grid->Modify();
				EnsureUserParam(Sys, UserVar);
				Grid->RenderTargetUserParameter.Parameter = UserVar;
				Grid->bPreviewGrid = true;
				Grid->PreviewAttribute = FName(TEXT("Attribute At Index 0"));
				if (RT)
				{
					Sys->GetExposedParameters().SetUObject(RT, UserVar);
					Grid->NumCellsX = RT->SizeX;
					Grid->NumCellsY = RT->SizeY;
					Grid->NumCellsMaxAxis = FMath::Max(RT->SizeX, RT->SizeY);
					Grid->SetGridFromMaxAxis = false;
				}
				const FNiagaraVariable GridVar = DiVariable(TEXT("Grid"), UNiagaraDataInterfaceGrid2DCollection::StaticClass());
				if (UNiagaraSimulationStageBase* Stage = FindSimStage(Data, FString()))
				{
					if (Stage->Script)
					{
						PromptEditorEnsureDiOnSimStage(
							Graph,
							FindOutputNode(Graph, ENiagaraScriptUsage::ParticleSimulationStageScript, Stage->Script->GetUsageId()),
							GridVar);
					}
				}
				Graph->NotifyGraphChanged();
				TouchSystem(Sys);
				const FString Compile = CompileNiagaraSystem(Sys);
				bOk = true;
				return FString::Printf(
					TEXT("Grid export %s cells=%d x %d\n%s"),
					*UserVar.GetName().ToString(), Grid->NumCellsX, Grid->NumCellsY, *Compile);
			});
	}

	{
		TSharedRef<FJsonObject> P = PathEmitterProps();
		P->SetObjectField(TEXT("render_target"), Prop(TEXT("string"), TEXT("RT path, default /Game/FX/RT_Grid2D")));
		P->SetObjectField(TEXT("label"), Prop(TEXT("string"), TEXT("Preview plane label, default GridPreview")));
		P->SetObjectField(TEXT("size"), Prop(TEXT("number"), TEXT("RT and plane size. Default 256 / world box.")));
		RegisterTool(
			TEXT("preview_niagara_grid"),
			TEXT("Compile a Grid2D system, export it to a render target, and spawn a textured plane in the level viewport so the field is visible. Not sprite fire."),
			P,
			[](const TSharedPtr<FJsonObject>& Args, UWorld* World, bool& bOk) -> FString
			{
				if (!World)
				{
					return TEXT("No editor world");
				}
				UNiagaraSystem* Sys = nullptr;
				FNiagaraEmitterHandle* Handle = nullptr;
				FVersionedNiagaraEmitterData* Data = nullptr;
				UNiagaraGraph* Graph = nullptr;
				UNiagaraScriptSource* Source = nullptr;
				FString Error;
				if (!EmitterContext(Args, Sys, Handle, Data, Graph, Source, Error))
				{
					return Error;
				}
				const FNiagaraVariable GridVar = DiVariable(TEXT("Grid"), UNiagaraDataInterfaceGrid2DCollection::StaticClass());
				UNiagaraDataInterfaceGrid2DCollection* Grid = FindGridDi(Graph);
				if (!Grid)
				{
					UNiagaraNodeInput* Input = FindOrAddDiInput(
						Graph, GridVar, UNiagaraDataInterfaceGrid2DCollection::StaticClass(), TEXT("Grid"));
					Grid = Cast<UNiagaraDataInterfaceGrid2DCollection>(GetInputNodeDataInterface(Input));
				}
				if (!Grid)
				{
					return TEXT("Grid2D Collection not found");
				}
				for (FNiagaraEmitterHandle& EachHandle : Sys->GetEmitterHandles())
				{
					FVersionedNiagaraEmitterData* EachData = EachHandle.GetEmitterData();
					UNiagaraGraph* EachGraph = EmitterGraph(EachData);
					if (!EachData || !EachGraph)
					{
						continue;
					}
					for (UNiagaraSimulationStageBase* Stage : EachData->GetSimulationStages())
					{
						if (Stage && Stage->Script)
						{
							PromptEditorEnsureDiOnSimStage(
								EachGraph,
								FindOutputNode(
									EachGraph,
									ENiagaraScriptUsage::ParticleSimulationStageScript,
									Stage->Script->GetUsageId()),
								GridVar);
						}
					}
				}
				const int32 Size = FMath::Clamp(ArgInt(Args, TEXT("size"), Grid->NumCellsX > 0 ? Grid->NumCellsX : 256), 8, 4096);
				const FString RtPath = ArgStr(Args, TEXT("render_target"), TEXT("/Game/FX/RT_Grid2D"));
				UTextureRenderTarget2D* RT = MakeOrLoadRT(RtPath, Size, Error);
				if (!RT)
				{
					return Error;
				}
				const FNiagaraVariable UserVar = UserVariable(TEXT("RenderTarget"), UTextureRenderTarget2D::StaticClass());
				Sys->Modify();
				Grid->Modify();
				EnsureUserParam(Sys, UserVar);
				Grid->RenderTargetUserParameter.Parameter = UserVar;
				Grid->bPreviewGrid = true;
				Grid->PreviewAttribute = FName(TEXT("Attribute At Index 0"));
				Grid->NumCellsX = Size;
				Grid->NumCellsY = Size;
				Grid->NumCellsMaxAxis = Size;
				Grid->SetGridFromMaxAxis = false;
				Sys->GetExposedParameters().SetUObject(RT, UserVar);
				Graph->NotifyGraphChanged();
				TouchSystem(Sys);
				const FString Compile = CompileNiagaraSystem(Sys);

				FString MicError;
				UMaterialInstanceConstant* MIC = MakeRtMic(RT, TEXT("/Game/FX/MI_GridPreview"), MicError);
				if (!MIC)
				{
					return Compile + TEXT("\n") + MicError;
				}
				UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
				if (!PlaneMesh)
				{
					return TEXT("Engine plane mesh missing");
				}
				const FScopedTransaction Txn(NSLOCTEXT("PromptEditor", "PreviewGrid", "PromptEditor preview_niagara_grid"));
				World->Modify();
				const FVector Loc = ArgsHaveLoc(Args) ? ArgLoc(Args) : CameraFrontLocation(280.f);
				FVector CamLoc = Loc + FVector(0, 0, 200);
				if (FEditorViewportClient* Client = ActiveViewport())
				{
					CamLoc = Client->GetViewLocation();
				}
				AStaticMeshActor* Plane = World->SpawnActor<AStaticMeshActor>(
					Loc, FRotationMatrix::MakeFromZ(CamLoc - Loc).Rotator());
				if (!Plane)
				{
					return TEXT("Spawn plane failed");
				}
				TouchActor(Plane);
				const FString Label = ArgStr(Args, TEXT("label"), TEXT("GridPreview"));
				Plane->SetActorLabel(Label);
				const float WorldCm = Grid->WorldBBoxSize.X > 1.f ? Grid->WorldBBoxSize.X : 200.f;
				Plane->SetActorScale3D(FVector(WorldCm / 100.f));
				if (UStaticMeshComponent* Comp = Plane->GetStaticMeshComponent())
				{
					Comp->Modify();
					Comp->SetStaticMesh(PlaneMesh);
					Comp->SetMaterial(0, MIC);
					Comp->SetMobility(EComponentMobility::Movable);
				}
				ANiagaraActor* Fx = World->SpawnActor<ANiagaraActor>(Loc, FRotator::ZeroRotator);
				if (Fx)
				{
					TouchActor(Fx);
					Fx->SetActorLabel(ArgStr(Args, TEXT("niagara_label"), TEXT("GridSim")));
					if (UNiagaraComponent* Niagara = Fx->GetNiagaraComponent())
					{
						Niagara->SetAsset(Sys);
						Niagara->SetVariableTextureRenderTarget(UserVar.GetName(), RT);
						Niagara->SetAutoActivate(true);
						Niagara->Activate(true);
					}
				}
				EnableFxViewport();
				GEditor->SelectNone(false, true, false);
				GEditor->SelectActor(Plane, true, true, true);
				GEditor->MoveViewportCamerasToActor(*Plane, false);
				Redraw();
				bOk = Compile.Contains(TEXT("Compiled ok"));
				return FString::Printf(
					TEXT("%s\nrt=%s plane=%s\n%s"),
					*Sys->GetPathName(), *RT->GetPathName(), *Plane->GetActorLabel(), *Compile);
			});
	}
}
