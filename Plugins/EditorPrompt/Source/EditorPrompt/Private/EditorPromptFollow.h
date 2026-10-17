#pragma once

#include "CoreMinimal.h"

// Plays an existing Niagara system on an actor while the editor is playing.
// actor "player" is the possessed pawn. overlap limits playback to bounds overlap.
struct FEditorPromptFollowBinding
{
	FString Id;
	FString Path;
	FString Actor;
	FString Overlap;
	FString Component;
	FString Socket;
	FString OverlapComponent;
	FString UserFloat;
	FString UserPosition;
	FVector Offset = FVector::ZeroVector;
	bool bLockSurfaceZ = false;
	float SurfaceZ = 0.f;
	float OverlapPad = 0.f;
	bool bKillOnStop = true;
};

class FEditorPromptFollow
{
public:
	static void Startup();
	static void Shutdown();
	static FString Add(const FEditorPromptFollowBinding& Binding);
	static FString Remove(const FString& ActorOrId);
	static FString List();
};
