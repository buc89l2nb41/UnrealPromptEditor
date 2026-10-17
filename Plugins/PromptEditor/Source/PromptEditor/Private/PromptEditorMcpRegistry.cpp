#include "PromptEditorMcpRegistry.h"
#include "PromptEditorMcpMesh.h"
#include "PromptEditorMcpNiagara.h"

namespace PromptEditorMcp
{
	struct FRegisteredTool
	{
		FString Name;
		FString Description;
		TSharedPtr<FJsonObject> Properties;
		FMcpToolFn Fn;
	};

	TMap<FName, FRegisteredTool>& ToolMap()
	{
		static TMap<FName, FRegisteredTool> Map;
		return Map;
	}

	void RegisterTool(const TCHAR* Name, const TCHAR* Description, TSharedRef<FJsonObject> Properties, FMcpToolFn Fn)
	{
		FRegisteredTool Entry;
		Entry.Name = Name;
		Entry.Description = Description;
		Entry.Properties = Properties;
		Entry.Fn = MoveTemp(Fn);
		ToolMap().Add(FName(Name), MoveTemp(Entry));
	}

	void AppendRegisteredTools(TArray<TSharedPtr<FJsonValue>>& OutTools)
	{
		EnsureRegistered();
		TArray<FName> Names;
		ToolMap().GetKeys(Names);
		Names.Sort(FNameLexicalLess());
		for (const FName& Name : Names)
		{
			const FRegisteredTool& Tool = ToolMap().FindChecked(Name);
			TSharedRef<FJsonObject> Props = Tool.Properties.IsValid()
				? Tool.Properties.ToSharedRef()
				: EmptyProps();
			OutTools.Add(MakeTool(*Tool.Name, *Tool.Description, Props));
		}
	}

	bool TryCallRegistered(const FString& Name, const TSharedPtr<FJsonObject>& Args, UWorld* World, FString& Message, bool& bOk)
	{
		EnsureRegistered();
		if (const FRegisteredTool* Tool = ToolMap().Find(FName(*Name)))
		{
			bOk = false;
			Message = Tool->Fn(Args, World, bOk);
			return true;
		}
		return false;
	}

	void EnsureRegistered()
	{
		static bool bOnce = false;
		if (!bOnce)
		{
			bOnce = true;
			RegisterPromptEditorNiagaraTools();
			RegisterPromptEditorMeshTools();
		}
	}
}
