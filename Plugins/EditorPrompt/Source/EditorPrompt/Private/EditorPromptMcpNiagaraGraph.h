#pragma once

#include "CoreMinimal.h"

class UNiagaraGraph;
class UNiagaraNodeOutput;
class UNiagaraSystem;
struct FNiagaraVariable;

void EditorPromptEnsureDiOnSimStage(UNiagaraGraph* Graph, UNiagaraNodeOutput* StageOut, const FNiagaraVariable& DiVar);
void EditorPromptCompileAndWait(UNiagaraSystem* Sys);
