#pragma once

#include "CoreMinimal.h"

// Fading world-XY stamp into a render target. Any material can sample that texture.
struct FPromptEditorTrailBinding
{
	FString Id;
	FString Actor;
	FString Socket;
	FString Component;
	FString Overlap;
	FString OverlapComponent;
	FString BoundsActor;
	FString MaterialPath;
	FString Parameter;
	float CenterX = 0.f;
	float CenterY = 0.f;
	float Size = 2000.f;
	float Radius = 80.f;
	float Fade = 0.75f;
	float OverlapPad = 0.f;
	int32 Resolution = 512;
	bool bHasCenter = false;
};

class FPromptEditorTrail
{
public:
	static void Startup();
	static void Shutdown();
	static FString Apply(const FPromptEditorTrailBinding& Binding);
	static FString Clear(const FString& Id);
};
