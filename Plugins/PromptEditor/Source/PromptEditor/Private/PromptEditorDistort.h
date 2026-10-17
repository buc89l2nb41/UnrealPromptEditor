#pragma once

#include "CoreMinimal.h"

// Tilts a surface material's tangent normal around a moving actor and enables
// pixel-normal-offset refraction so that surface warps nearby. One shared driver.
struct FPromptEditorDistortBinding
{
	FString MaterialPath;
	FString Actor;
	FString Overlap;
	FString Component;
	FString OverlapComponent;
	float Radius = 180.f;
	float Strength = 0.45f;
	bool bLockSurfaceZ = false;
	float SurfaceZ = 0.f;
	float OverlapPad = 0.f;
};

class FPromptEditorDistort
{
public:
	static void Startup();
	static void Shutdown();
	static FString Apply(const FPromptEditorDistortBinding& Binding);
	static FString Clear();
};
