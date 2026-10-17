#pragma once

#include "CoreMinimal.h"

class UNiagaraGraph;
class UNiagaraNodeOutput;
class UNiagaraSystem;
struct FNiagaraVariable;

void PromptEditorEnsureDiOnSimStage(UNiagaraGraph* Graph, UNiagaraNodeOutput* StageOut, const FNiagaraVariable& DiVar);
void PromptEditorCompileAndWait(UNiagaraSystem* Sys);
