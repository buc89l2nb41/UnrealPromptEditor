#include "PromptEditorMcpNiagaraGraph.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_Niagara.h"
#include "Modules/ModuleManager.h"
#include "NiagaraCommon.h"
#include "NiagaraConstants.h"
#include "NiagaraEmitter.h"
#include "NiagaraGraph.h"
#include "NiagaraModule.h"
#include "NiagaraNode.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScript.h"
#include "NiagaraShared.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "ShaderCompiler.h"

namespace
{
	const FName AddPinSubCategory(TEXT("DynamicAddPin"));
	const FName ParameterPinSubCategory(TEXT("ParameterPin"));

	UClass* FindNiagaraNodeClass(const TCHAR* Name)
	{
		if (UClass* Class = FindFirstObject<UClass>(Name, EFindFirstObjectOptions::EnsureIfAmbiguous))
		{
			return Class;
		}
		const FString Path = FString::Printf(TEXT("/Script/NiagaraEditor.%s"), Name);
		return StaticLoadClass(UEdGraphNode::StaticClass(), nullptr, *Path);
	}

	UEdGraphPin* MapPin(UNiagaraNode* Node, EEdGraphPinDirection Dir)
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
			if (Pin && UEdGraphSchema_Niagara::PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef())
			{
				return Pin;
			}
		}
		return nullptr;
	}

	bool IsAddPin(const UEdGraphPin* Pin)
	{
		return Pin
			&& Pin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryMisc
			&& Pin->PinType.PinSubCategory == AddPinSubCategory;
	}

	UEdGraphPin* FindAddPin(UEdGraphNode* Node, EEdGraphPinDirection Dir)
	{
		if (!Node)
		{
			return nullptr;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Dir && IsAddPin(Pin))
			{
				return Pin;
			}
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Dir && Pin->GetName() == TEXT("Add"))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	UEdGraphNode* CreateMapNode(UNiagaraGraph* Graph, UClass* Class)
	{
		if (!Graph || !Class)
		{
			return nullptr;
		}
		UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, Class, NAME_None, RF_Transactional);
		if (!Node)
		{
			return nullptr;
		}
		Graph->AddNode(Node, false, false);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		return Node;
	}

	UEdGraphPin* PromoteAddPin(UEdGraphNode* Node, EEdGraphPinDirection Dir, const FNiagaraVariable& Var)
	{
		UEdGraphPin* AddPin = FindAddPin(Node, Dir);
		if (!AddPin)
		{
			return nullptr;
		}
		AddPin->Modify();
		AddPin->PinType = UEdGraphSchema_Niagara::TypeDefinitionToPinType(Var.GetType());
		AddPin->PinName = Var.GetName();
		AddPin->PinFriendlyName = FText::FromName(Var.GetName());
		AddPin->PinType.PinSubCategory = ParameterPinSubCategory;
		if (!AddPin->PersistentGuid.IsValid())
		{
			AddPin->PersistentGuid = FGuid::NewGuid();
		}
		Node->CreatePin(
			Dir,
			FEdGraphPinType(
				UEdGraphSchema_Niagara::PinCategoryMisc,
				AddPinSubCategory,
				nullptr,
				EPinContainerType::None,
				false,
				FEdGraphTerminalType()),
			TEXT("Add"));
		return AddPin;
	}

	void CollectUpstream(UNiagaraNodeOutput* StageOut, TArray<UEdGraphNode*>& OutNodes)
	{
		OutNodes.Reset();
		if (!StageOut)
		{
			return;
		}
		TArray<UEdGraphPin*> InPins;
		StageOut->GetInputPins(InPins);
		TSet<const UEdGraphNode*> Seen;
		TArray<UEdGraphNode*> Stack;
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
		while (Stack.Num() > 0)
		{
			UEdGraphNode* Node = Stack.Pop(EAllowShrinking::No);
			if (!Node || Seen.Contains(Node))
			{
				continue;
			}
			Seen.Add(Node);
			OutNodes.Add(Node);
			if (UNiagaraNode* NiagaraNode = Cast<UNiagaraNode>(Node))
			{
				TArray<UEdGraphPin*> Pins;
				NiagaraNode->GetInputPins(Pins);
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
			}
		}
	}

	bool ChainAlreadyReads(UNiagaraNodeOutput* StageOut, const FNiagaraVariable& DiVar, UClass* GetClass)
	{
		TArray<UEdGraphNode*> Nodes;
		CollectUpstream(StageOut, Nodes);
		for (UEdGraphNode* Node : Nodes)
		{
			if (!Node || !GetClass || !Node->IsA(GetClass))
			{
				continue;
			}
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Output && Pin->PinName == DiVar.GetName() && !IsAddPin(Pin))
				{
					return true;
				}
			}
		}
		return false;
	}

	void BypassMapNode(UNiagaraGraph* Graph, UEdGraphNode* Node)
	{
		UNiagaraNode* NiagaraNode = Cast<UNiagaraNode>(Node);
		if (!Graph || !NiagaraNode)
		{
			return;
		}
		UEdGraphPin* In = MapPin(NiagaraNode, EGPD_Input);
		UEdGraphPin* Out = MapPin(NiagaraNode, EGPD_Output);
		UEdGraphPin* Prev = (In && In->LinkedTo.Num() > 0) ? In->LinkedTo[0] : nullptr;
		TArray<UEdGraphPin*> Nexts;
		if (Out)
		{
			Nexts = Out->LinkedTo;
			Out->BreakAllPinLinks();
		}
		if (In)
		{
			In->BreakAllPinLinks();
		}
		for (UEdGraphPin* Next : Nexts)
		{
			if (Prev && Next)
			{
				Prev->MakeLinkTo(Next);
			}
		}
		Graph->RemoveNode(Node);
	}

	void StripMapSetsWriting(UNiagaraGraph* Graph, const FNiagaraVariable& DiVar, UClass* SetClass)
	{
		if (!Graph || !SetClass)
		{
			return;
		}
		TArray<UEdGraphNode*> ToRemove;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || !Node->IsA(SetClass))
			{
				continue;
			}
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Input && Pin->PinName == DiVar.GetName() && !IsAddPin(Pin))
				{
					ToRemove.Add(Node);
					break;
				}
			}
		}
		for (UEdGraphNode* Node : ToRemove)
		{
			BypassMapNode(Graph, Node);
		}
	}

	void CollectSimStageOutputs(UNiagaraGraph* Graph, UNiagaraNodeOutput* Extra, TArray<UNiagaraNodeOutput*>& Outs)
	{
		Outs.Reset();
		if (Extra)
		{
			Outs.Add(Extra);
		}
		if (!Graph)
		{
			return;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UNiagaraNodeOutput* Out = Cast<UNiagaraNodeOutput>(Node))
			{
				if (Out->GetUsage() == ENiagaraScriptUsage::ParticleSimulationStageScript)
				{
					Outs.AddUnique(Out);
				}
			}
		}
	}

	FNiagaraVariable MakeTransientDi(const FNiagaraVariable& DiVar)
	{
		FString Leaf = DiVar.GetName().ToString();
		int32 Dot = INDEX_NONE;
		if (Leaf.FindLastChar(TEXT('.'), Dot))
		{
			Leaf = Leaf.Mid(Dot + 1);
		}
		if (Leaf.IsEmpty())
		{
			Leaf = TEXT("Grid");
		}
		const FString Full = FNiagaraConstants::TransientNamespace.ToString() + TEXT(".") + Leaf + TEXT("Ref");
		return FNiagaraVariable(DiVar.GetType(), FName(*Full));
	}

	void SpliceRead(UNiagaraGraph* Graph, UNiagaraNodeOutput* StageOut, const FNiagaraVariable& DiVar, UClass* SetClass, UClass* GetClass)
	{
		if (!Graph || !StageOut || !SetClass || !GetClass)
		{
			return;
		}
		if (ChainAlreadyReads(StageOut, DiVar, GetClass))
		{
			return;
		}
		UEdGraphPin* OutputIn = MapPin(StageOut, EGPD_Input);
		if (!OutputIn)
		{
			return;
		}
		UEdGraphNode* SetRaw = CreateMapNode(Graph, SetClass);
		UEdGraphNode* GetRaw = CreateMapNode(Graph, GetClass);
		if (!SetRaw || !GetRaw)
		{
			return;
		}
		const FNiagaraVariable TransientVar = MakeTransientDi(DiVar);
		UEdGraphPin* SetDiIn = PromoteAddPin(SetRaw, EGPD_Input, TransientVar);
		UEdGraphPin* GetDiOut = PromoteAddPin(GetRaw, EGPD_Output, DiVar);
		UNiagaraNode* SetNode = Cast<UNiagaraNode>(SetRaw);
		UNiagaraNode* GetNode = Cast<UNiagaraNode>(GetRaw);
		UEdGraphPin* SetIn = MapPin(SetNode, EGPD_Input);
		UEdGraphPin* SetOut = MapPin(SetNode, EGPD_Output);
		UEdGraphPin* GetIn = MapPin(GetNode, EGPD_Input);
		if (!SetIn || !SetOut || !GetIn || !SetDiIn || !GetDiOut)
		{
			return;
		}
		UEdGraphPin* PrevOut = OutputIn->LinkedTo.Num() > 0 ? OutputIn->LinkedTo[0] : nullptr;
		OutputIn->BreakAllPinLinks();
		if (PrevOut)
		{
			SetIn->MakeLinkTo(PrevOut);
			GetIn->MakeLinkTo(PrevOut);
		}
		SetOut->MakeLinkTo(OutputIn);
		SetDiIn->BreakAllPinLinks();
		SetDiIn->MakeLinkTo(GetDiOut);
	}
}

void PromptEditorEnsureDiOnSimStage(UNiagaraGraph* Graph, UNiagaraNodeOutput* StageOut, const FNiagaraVariable& DiVar)
{
	if (!Graph || !DiVar.IsValid())
	{
		return;
	}
	UClass* SetClass = FindNiagaraNodeClass(TEXT("NiagaraNodeParameterMapSet"));
	UClass* GetClass = FindNiagaraNodeClass(TEXT("NiagaraNodeParameterMapGet"));
	if (!SetClass || !GetClass)
	{
		return;
	}
	Graph->Modify();
	StripMapSetsWriting(Graph, DiVar, SetClass);
	TArray<UNiagaraNodeOutput*> Outs;
	CollectSimStageOutputs(Graph, StageOut, Outs);
	for (UNiagaraNodeOutput* Out : Outs)
	{
		SpliceRead(Graph, Out, DiVar, SetClass, GetClass);
	}
	Graph->NotifyGraphChanged();
}

void PromptEditorCompileAndWait(UNiagaraSystem* Sys)
{
	if (!Sys)
	{
		return;
	}
	Sys->RequestCompile(true);
	INiagaraModule& NiagaraModule = FModuleManager::LoadModuleChecked<INiagaraModule>(TEXT("Niagara"));
	for (int32 Pass = 0; Pass < 8; ++Pass)
	{
		Sys->WaitForCompilationComplete(true, false);
		NiagaraModule.ProcessShaderCompilationQueue();
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->ProcessAsyncResults(false, true);
		}
		for (FNiagaraEmitterHandle& Handle : Sys->GetEmitterHandles())
		{
			FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			UNiagaraScript* Gpu = Data ? Data->GetGPUComputeScript() : nullptr;
			FNiagaraShaderScript* Shader = Gpu ? Gpu->GetRenderThreadScript() : nullptr;
			if (Shader && !Shader->IsCompilationFinished())
			{
				Shader->FinishCompilation();
			}
		}
		if (!Sys->HasOutstandingCompilationRequests(true) && Sys->IsReadyToRun())
		{
			break;
		}
	}
}
