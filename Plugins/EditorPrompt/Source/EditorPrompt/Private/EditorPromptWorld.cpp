#include "EditorPromptWorld.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSourceData.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "FoliageType.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "InstancedFoliage.h"
#include "InstancedFoliageActor.h"
#include "Landscape.h"
#include "LandscapeEdit.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Engine/Texture2D.h"
#include "LandscapeGrassType.h"
#include "Materials/MaterialExpressionLandscapeGrassOutput.h"
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "Materials/MaterialExpressionLandscapeLayerCoords.h"
#include "Materials/MaterialExpressionLandscapeLayerSample.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Math/RandomStream.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

namespace
{
	constexpr double HeightUnitToLocal = 1.0 / 128.0;
	constexpr int32 MaxFoliagePerCall = 20000;

	FString Str(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, const FString& Fallback = FString())
	{
		FString Value;
		if (Args.IsValid() && Args->TryGetStringField(Key, Value) && !Value.IsEmpty())
		{
			return Value;
		}
		return Fallback;
	}

	double Num(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, double Fallback = 0.0)
	{
		double Value = Fallback;
		if (Args.IsValid())
		{
			Args->TryGetNumberField(Key, Value);
		}
		return Value;
	}

	bool Has(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key)
	{
		return Args.IsValid() && Args->HasField(Key);
	}

	bool Bool(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, bool Fallback)
	{
		bool Value = Fallback;
		if (Args.IsValid())
		{
			Args->TryGetBoolField(Key, Value);
		}
		return Value;
	}

	UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	void SaveAsset(UObject* Object)
	{
		if (!Object)
		{
			return;
		}
		Object->MarkPackageDirty();
		TArray<UPackage*> Packages;
		Packages.Add(Object->GetOutermost());
		UEditorLoadingAndSavingUtils::SavePackages(Packages, true);
	}

	bool SplitGamePath(const FString& InPath, FString& OutPackage, FString& OutAsset)
	{
		FString Path = InPath;
		int32 Dot = INDEX_NONE;
		if (Path.FindChar(TEXT('.'), Dot))
		{
			Path = Path.Left(Dot);
		}
		if (!Path.StartsWith(TEXT("/Game/")))
		{
			return false;
		}
		OutPackage = Path;
		OutAsset = FPackageName::GetShortName(Path);
		return !OutAsset.IsEmpty();
	}

	template <typename T>
	T* LoadOrCreateAsset(const FString& Path, bool& bOutCreated)
	{
		bOutCreated = false;
		FString PackageName;
		FString AssetName;
		if (!SplitGamePath(Path, PackageName, AssetName))
		{
			return nullptr;
		}
		if (T* Existing = LoadObject<T>(nullptr, *(PackageName + TEXT(".") + AssetName)))
		{
			return Existing;
		}
		UPackage* Package = CreatePackage(*PackageName);
		T* Created = NewObject<T>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		if (Created)
		{
			FAssetRegistryModule::AssetCreated(Created);
			bOutCreated = true;
		}
		return Created;
	}

	FString SafeName(const FString& In)
	{
		FString Out;
		for (TCHAR C : In)
		{
			Out.AppendChar(FChar::IsAlnum(C) ? C : TEXT('_'));
		}
		return Out.IsEmpty() ? TEXT("Unnamed") : Out;
	}

	double Fbm(double X, double Y, int32 Seed)
	{
		const double Offset = Seed * 137.17;
		double Sum = 0.0;
		double Amp = 0.5;
		double Freq = 1.0;
		for (int32 Octave = 0; Octave < 4; ++Octave)
		{
			Sum += Amp * FMath::PerlinNoise2D(FVector2D(X * Freq + Offset, Y * Freq - Offset));
			Amp *= 0.5;
			Freq *= 2.0;
		}
		return FMath::Clamp(Sum * 1.6, -1.0, 1.0);
	}

	double BrushWeight(double Dist, double Radius, double Falloff)
	{
		if (Dist >= Radius)
		{
			return 0.0;
		}
		const double Inner = Radius * (1.0 - FMath::Clamp(Falloff, 0.0, 1.0));
		if (Dist <= Inner)
		{
			return 1.0;
		}
		const double T = (Dist - Inner) / FMath::Max(1e-3, Radius - Inner);
		return 0.5 * (1.0 + FMath::Cos(PI * T));
	}

	double SoftRange(double Value, double Min, double Max, double Soft)
	{
		const double Low = Min <= -1e8 ? 1.0 : FMath::SmoothStep(Min - Soft, Min + Soft, Value);
		const double High = Max >= 1e8 ? 1.0 : 1.0 - FMath::SmoothStep(Max - Soft, Max + Soft, Value);
		return Low * High;
	}

	// ---------------------------------------------------------------- landscape

	ALandscape* FindLandscape(const FString& Label)
	{
		UWorld* World = EditorWorld();
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<ALandscape> It(World); It; ++It)
		{
			if (Label.IsEmpty() || It->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase)
				|| It->GetName().Equals(Label, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
		return nullptr;
	}

	struct FRegion
	{
		int32 X1 = 0;
		int32 Y1 = 0;
		int32 X2 = -1;
		int32 Y2 = -1;
		int32 Width() const { return X2 - X1 + 1; }
		int32 Height() const { return Y2 - Y1 + 1; }
		int32 Num() const { return Width() * Height(); }
		int32 Index(int32 X, int32 Y) const { return (Y - Y1) * Width() + (X - X1); }
	};

	struct FBrush
	{
		FRegion Region;
		FVector LocalCenter = FVector::ZeroVector;
		double LocalRadius = 0.0;
		double Falloff = 0.5;
		bool bAll = false;
		FTransform ToWorld;

		double Weight(int32 X, int32 Y) const
		{
			if (bAll)
			{
				return 1.0;
			}
			return BrushWeight(FVector2D(X - LocalCenter.X, Y - LocalCenter.Y).Size(), LocalRadius, Falloff);
		}
	};

	bool MakeBrush(ALandscape* Landscape, const TSharedPtr<FJsonObject>& Args, FBrush& Out, FString& OutError)
	{
		ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
		int32 MinX = 0;
		int32 MinY = 0;
		int32 MaxX = 0;
		int32 MaxY = 0;
		if (!Info || !Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
		{
			OutError = TEXT("landscape has no components");
			return false;
		}
		Out.ToWorld = Landscape->GetActorTransform();
		Out.bAll = Bool(Args, TEXT("all"), false) || !Has(Args, TEXT("x")) || !Has(Args, TEXT("y"));
		Out.Falloff = Num(Args, TEXT("falloff"), 0.5);
		if (Out.bAll)
		{
			Out.Region = { MinX, MinY, MaxX, MaxY };
			return true;
		}
		const FVector World(Num(Args, TEXT("x")), Num(Args, TEXT("y")), 0.0);
		Out.LocalCenter = Out.ToWorld.InverseTransformPosition(World);
		const double Scale = FMath::Max(1e-3, Out.ToWorld.GetScale3D().X);
		Out.LocalRadius = FMath::Max(1.0, Num(Args, TEXT("radius"), 1000.0) / Scale);
		Out.Region.X1 = FMath::Max(MinX, FMath::FloorToInt(Out.LocalCenter.X - Out.LocalRadius));
		Out.Region.Y1 = FMath::Max(MinY, FMath::FloorToInt(Out.LocalCenter.Y - Out.LocalRadius));
		Out.Region.X2 = FMath::Min(MaxX, FMath::CeilToInt(Out.LocalCenter.X + Out.LocalRadius));
		Out.Region.Y2 = FMath::Min(MaxY, FMath::CeilToInt(Out.LocalCenter.Y + Out.LocalRadius));
		if (Out.Region.X1 > Out.Region.X2 || Out.Region.Y1 > Out.Region.Y2)
		{
			OutError = TEXT("brush is outside the landscape");
			return false;
		}
		return true;
	}

	template <typename TBody>
	void EditLandscape(ALandscape* Landscape, TBody&& Body)
	{
		ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
		if (Landscape->HasLayersContent() && Landscape->GetLayerCount() > 0)
		{
			const FGuid LayerGuid = Landscape->GetLayer(0)->Guid;
			TWeakObjectPtr<ALandscape> Weak(Landscape);
			FScopedSetLandscapeEditingLayer Scope(Landscape, LayerGuid, [Weak]()
			{
				if (Weak.IsValid())
				{
					Weak->RequestLayersContentUpdateForceAll();
				}
			});
			FLandscapeEditDataInterface Edit(Info);
			Body(Edit);
			Edit.Flush();
		}
		else
		{
			FLandscapeEditDataInterface Edit(Info);
			Body(Edit);
			Edit.Flush();
		}
	}

	double HeightToWorldZ(const FTransform& ToWorld, uint16 Height)
	{
		return ToWorld.GetLocation().Z + (static_cast<double>(Height) - 32768.0) * HeightUnitToLocal * ToWorld.GetScale3D().Z;
	}

	uint16 WorldZToHeight(const FTransform& ToWorld, double Z)
	{
		const double Units = (Z - ToWorld.GetLocation().Z) / FMath::Max(1e-3, HeightUnitToLocal * ToWorld.GetScale3D().Z);
		return static_cast<uint16>(FMath::Clamp(FMath::RoundToDouble(32768.0 + Units), 0.0, 65535.0));
	}

	double SlopeDegrees(const TArray<uint16>& Heights, const FRegion& R, int32 X, int32 Y, const FTransform& ToWorld)
	{
		const int32 XL = FMath::Max(R.X1, X - 1);
		const int32 XR = FMath::Min(R.X2, X + 1);
		const int32 YD = FMath::Max(R.Y1, Y - 1);
		const int32 YU = FMath::Min(R.Y2, Y + 1);
		const FVector Scale = ToWorld.GetScale3D();
		const double ZPer = HeightUnitToLocal * Scale.Z;
		const double DX = XR > XL ? (Heights[R.Index(XR, Y)] - Heights[R.Index(XL, Y)]) * ZPer / ((XR - XL) * Scale.X) : 0.0;
		const double DY = YU > YD ? (Heights[R.Index(X, YU)] - Heights[R.Index(X, YD)]) * ZPer / ((YU - YD) * Scale.Y) : 0.0;
		return FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(DX * DX + DY * DY)));
	}

	ULandscapeLayerInfoObject* LoadOrCreateLayerInfo(const FString& LayerName)
	{
		bool bCreated = false;
		const FString Path = FString::Printf(TEXT("/Game/EditorPrompt/Landscape/LI_%s"), *SafeName(LayerName));
		ULandscapeLayerInfoObject* Info = LoadOrCreateAsset<ULandscapeLayerInfoObject>(Path, bCreated);
		if (Info && bCreated)
		{
			Info->LayerName = FName(*LayerName);
			Info->LayerUsageDebugColor = Info->GenerateLayerUsageDebugColor();
			SaveAsset(Info);
		}
		return Info;
	}

	ULandscapeLayerInfoObject* EnsureLayer(ALandscape* Landscape, const FString& LayerName)
	{
		ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
		if (!Info)
		{
			return nullptr;
		}
		const FName Name(*LayerName);
		if (ULandscapeLayerInfoObject* Existing = Info->GetLayerInfoByName(Name))
		{
			return Existing;
		}
		ULandscapeLayerInfoObject* LayerInfo = LoadOrCreateLayerInfo(LayerName);
		if (!LayerInfo)
		{
			return nullptr;
		}
		const int32 Index = Info->GetLayerInfoIndex(Name);
		if (Index == INDEX_NONE)
		{
			Info->Layers.Add(FLandscapeInfoLayerSettings(LayerInfo, Landscape));
		}
		else
		{
			Info->Layers[Index].LayerInfoObj = LayerInfo;
		}
		if (!Landscape->EditorLayerSettings.Contains(LayerInfo))
		{
			Landscape->Modify();
			Landscape->EditorLayerSettings.Add(FLandscapeEditorLayerSettings(LayerInfo));
		}
		return LayerInfo;
	}

	void AssignLandscapeMaterial(ALandscape* Landscape, UMaterialInterface* Material)
	{
		Landscape->Modify();
		Landscape->LandscapeMaterial = Material;
		if (FProperty* Property = FindFProperty<FProperty>(ALandscapeProxy::StaticClass(), TEXT("LandscapeMaterial")))
		{
			FPropertyChangedEvent Event(Property);
			Landscape->PostEditChangeProperty(Event);
		}
		if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
		{
			Info->UpdateLayerInfoMap(Landscape);
		}
	}

	struct FLayerSpec
	{
		FString Name;
		FString Color;
		FString Normal;
	};

	TArray<FLayerSpec> ParseLayers(const FString& Spec)
	{
		TArray<FLayerSpec> Out;
		TArray<FString> Parts;
		Spec.Replace(TEXT(","), TEXT(";")).ParseIntoArray(Parts, TEXT(";"), true);
		for (FString Part : Parts)
		{
			Part.TrimStartAndEndInline();
			if (Part.IsEmpty())
			{
				continue;
			}
			FLayerSpec Layer;
			FString Rest;
			if (!Part.Split(TEXT("="), &Layer.Name, &Rest))
			{
				Layer.Name = Part;
			}
			Layer.Name.TrimStartAndEndInline();
			Rest.TrimStartAndEndInline();
			if (!Rest.Split(TEXT("|"), &Layer.Color, &Layer.Normal))
			{
				Layer.Color = Rest;
			}
			Layer.Color.TrimStartAndEndInline();
			Layer.Normal.TrimStartAndEndInline();
			if (!Layer.Name.IsEmpty())
			{
				Out.Add(Layer);
			}
		}
		return Out;
	}

	FLinearColor DefaultLayerColor(const FString& Name)
	{
		const FString Key = Name.ToLower();
		if (Key.Contains(TEXT("grass")) || Key.Contains(TEXT("lawn"))) return FLinearColor(0.10f, 0.20f, 0.04f);
		if (Key.Contains(TEXT("moss"))) return FLinearColor(0.12f, 0.17f, 0.05f);
		if (Key.Contains(TEXT("dirt")) || Key.Contains(TEXT("soil")) || Key.Contains(TEXT("mud")) || Key.Contains(TEXT("ground"))) return FLinearColor(0.14f, 0.09f, 0.05f);
		if (Key.Contains(TEXT("rock")) || Key.Contains(TEXT("stone")) || Key.Contains(TEXT("cliff"))) return FLinearColor(0.20f, 0.19f, 0.18f);
		if (Key.Contains(TEXT("gravel")) || Key.Contains(TEXT("path")) || Key.Contains(TEXT("road"))) return FLinearColor(0.24f, 0.22f, 0.19f);
		if (Key.Contains(TEXT("sand")) || Key.Contains(TEXT("beach"))) return FLinearColor(0.55f, 0.45f, 0.28f);
		if (Key.Contains(TEXT("snow")) || Key.Contains(TEXT("ice"))) return FLinearColor(0.80f, 0.83f, 0.88f);
		const uint32 Hash = GetTypeHash(Key);
		return FLinearColor(0.12f + (Hash & 0xFF) / 1275.f, 0.12f + ((Hash >> 8) & 0xFF) / 1275.f, 0.10f + ((Hash >> 16) & 0xFF) / 1275.f);
	}

	UTexture* LoadTexture(const FString& Path)
	{
		return Path.StartsWith(TEXT("/")) ? LoadObject<UTexture>(nullptr, *Path) : nullptr;
	}

	TArray<TPair<FName, ULandscapeGrassType*>> CollectGrass(UMaterial* Material)
	{
		TArray<TPair<FName, ULandscapeGrassType*>> Out;
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			if (UMaterialExpressionLandscapeGrassOutput* Grass = Cast<UMaterialExpressionLandscapeGrassOutput>(Expression))
			{
				for (const FGrassInput& Input : Grass->GrassTypes)
				{
					if (Input.GrassType)
					{
						Out.Add(TPair<FName, ULandscapeGrassType*>(Input.Name, Input.GrassType));
					}
				}
			}
		}
		return Out;
	}

	void SetGrassNode(UMaterial* Material, const FName Layer, ULandscapeGrassType* Type)
	{
		UMaterialExpressionLandscapeGrassOutput* Output = nullptr;
		UMaterialExpressionLandscapeLayerSample* Sample = nullptr;
		for (UMaterialExpression* Expression : Material->GetExpressions())
		{
			if (!Output)
			{
				Output = Cast<UMaterialExpressionLandscapeGrassOutput>(Expression);
			}
			UMaterialExpressionLandscapeLayerSample* Candidate = Cast<UMaterialExpressionLandscapeLayerSample>(Expression);
			if (Candidate && Candidate->ParameterName == Layer)
			{
				Sample = Candidate;
			}
		}
		if (!Output)
		{
			if (!Type)
			{
				return;
			}
			Output = Cast<UMaterialExpressionLandscapeGrassOutput>(UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionLandscapeGrassOutput::StaticClass(), 300, 800));
			Output->GrassTypes.Reset();
		}
		Output->GrassTypes.RemoveAll([Layer, Type](const FGrassInput& Input)
		{
			return Input.Name == Layer || (!Input.GrassType && !Input.Input.Expression);
		});
		if (!Type)
		{
			return;
		}
		if (!Sample)
		{
			Sample = Cast<UMaterialExpressionLandscapeLayerSample>(UMaterialEditingLibrary::CreateMaterialExpression(
				Material, UMaterialExpressionLandscapeLayerSample::StaticClass(), 0, 800 + Output->GrassTypes.Num() * 120));
			Sample->ParameterName = Layer;
		}
		FGrassInput Input(Layer);
		Input.GrassType = Type;
		Input.Input.Connect(0, Sample);
		Output->GrassTypes.Add(Input);
	}

	UMaterial* BuildLandscapeMaterial(const FString& Path, const TArray<FLayerSpec>& Layers, double MappingScale,
		double Roughness, double Variation, bool bHeightBlend, FString& OutError)
	{
		bool bCreated = false;
		UMaterial* Material = LoadOrCreateAsset<UMaterial>(Path, bCreated);
		if (!Material)
		{
			OutError = TEXT("material path must be under /Game");
			return nullptr;
		}
		Material->Modify();
		const TArray<TPair<FName, ULandscapeGrassType*>> KeptGrass = CollectGrass(Material);
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
		Material->SetShadingModel(MSM_DefaultLit);
		Material->BlendMode = BLEND_Opaque;

		auto Make = [Material](UClass* Class, int32 X, int32 Y)
		{
			return UMaterialEditingLibrary::CreateMaterialExpression(Material, Class, X, Y);
		};

		UMaterialExpressionLandscapeLayerCoords* Coords = Cast<UMaterialExpressionLandscapeLayerCoords>(
			Make(UMaterialExpressionLandscapeLayerCoords::StaticClass(), -1200, 0));
		Coords->MappingScale = static_cast<float>(FMath::Max(0.01, MappingScale));

		UMaterialExpressionLandscapeLayerBlend* ColorBlend = Cast<UMaterialExpressionLandscapeLayerBlend>(
			Make(UMaterialExpressionLandscapeLayerBlend::StaticClass(), -400, 0));
		UMaterialExpressionLandscapeLayerBlend* NormalBlend = nullptr;
		bool bAnyNormal = false;
		for (const FLayerSpec& Layer : Layers)
		{
			bAnyNormal |= LoadTexture(Layer.Normal) != nullptr;
		}
		if (bAnyNormal)
		{
			NormalBlend = Cast<UMaterialExpressionLandscapeLayerBlend>(
				Make(UMaterialExpressionLandscapeLayerBlend::StaticClass(), -400, 500));
		}

		int32 Row = 0;
		for (const FLayerSpec& Layer : Layers)
		{
			FLayerBlendInput Input;
			Input.LayerName = FName(*Layer.Name);
			Input.BlendType = bHeightBlend ? LB_HeightBlend : LB_WeightBlend;
			Input.PreviewWeight = Row == 0 ? 1.f : 0.f;
			Input.ConstHeightInput = 0.5f;
			if (UTexture* Texture = LoadTexture(Layer.Color))
			{
				UMaterialExpressionTextureSampleParameter2D* Sample = Cast<UMaterialExpressionTextureSampleParameter2D>(
					Make(UMaterialExpressionTextureSampleParameter2D::StaticClass(), -800, Row * 260));
				Sample->ParameterName = FName(*(Layer.Name + TEXT("_Color")));
				Sample->Texture = Texture;
				Sample->AutoSetSampleType();
				Sample->Coordinates.Connect(0, Coords);
				Input.LayerInput.Connect(0, Sample);
				if (bHeightBlend)
				{
					const UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
					Input.HeightInput.Connect(Texture2D && Texture2D->HasAlphaChannel() ? 4 : 2, Sample);
				}
			}
			else
			{
				FLinearColor Color = DefaultLayerColor(Layer.Name);
				if (Layer.Color.StartsWith(TEXT("#")))
				{
					Color = FLinearColor(FColor::FromHex(Layer.Color));
				}
				Input.ConstLayerInput = FVector(Color.R, Color.G, Color.B);
			}
			ColorBlend->Layers.Add(Input);

			if (NormalBlend)
			{
				FLayerBlendInput NormalInput;
				NormalInput.LayerName = Input.LayerName;
				NormalInput.BlendType = Input.BlendType;
				NormalInput.PreviewWeight = Input.PreviewWeight;
				NormalInput.ConstHeightInput = Input.ConstHeightInput;
				NormalInput.HeightInput = Input.HeightInput;
				NormalInput.ConstLayerInput = FVector(0, 0, 1);
				if (UTexture* Texture = LoadTexture(Layer.Normal))
				{
					UMaterialExpressionTextureSampleParameter2D* Sample = Cast<UMaterialExpressionTextureSampleParameter2D>(
						Make(UMaterialExpressionTextureSampleParameter2D::StaticClass(), -800, 500 + Row * 260));
					Sample->ParameterName = FName(*(Layer.Name + TEXT("_Normal")));
					Sample->Texture = Texture;
					Sample->AutoSetSampleType();
					Sample->Coordinates.Connect(0, Coords);
					NormalInput.LayerInput.Connect(0, Sample);
				}
				NormalBlend->Layers.Add(NormalInput);
			}
			++Row;
		}

		UMaterialExpression* BaseColor = ColorBlend;
		if (Variation > 0.0)
		{
			UMaterialExpressionNoise* Noise = Cast<UMaterialExpressionNoise>(Make(UMaterialExpressionNoise::StaticClass(), -400, -300));
			Noise->Scale = 0.004f;
			Noise->Levels = 4;
			Noise->OutputMin = static_cast<float>(1.0 - Variation);
			Noise->OutputMax = static_cast<float>(1.0 + Variation * 0.5);
			UMaterialExpressionMultiply* Mul = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -150, 0));
			Mul->A.Connect(0, ColorBlend);
			Mul->B.Connect(0, Noise);
			BaseColor = Mul;
		}
		UMaterialEditingLibrary::ConnectMaterialProperty(BaseColor, TEXT(""), MP_BaseColor);
		if (NormalBlend)
		{
			UMaterialEditingLibrary::ConnectMaterialProperty(NormalBlend, TEXT(""), MP_Normal);
		}
		UMaterialExpressionConstant* Rough = Cast<UMaterialExpressionConstant>(Make(UMaterialExpressionConstant::StaticClass(), -150, 300));
		Rough->R = static_cast<float>(FMath::Clamp(Roughness, 0.0, 1.0));
		UMaterialEditingLibrary::ConnectMaterialProperty(Rough, TEXT(""), MP_Roughness);

		for (const TPair<FName, ULandscapeGrassType*>& Grass : KeptGrass)
		{
			SetGrassNode(Material, Grass.Key, Grass.Value);
		}
		UMaterialEditingLibrary::RecompileMaterial(Material);
		SaveAsset(Material);
		return Material;
	}

	FString LayerNames(ALandscape* Landscape)
	{
		FString Out;
		if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
		{
			for (const FLandscapeInfoLayerSettings& Layer : Info->Layers)
			{
				if (!Out.IsEmpty())
				{
					Out += TEXT(",");
				}
				Out += Layer.GetLayerName().ToString();
				if (!Layer.LayerInfoObj)
				{
					Out += TEXT("(no info)");
				}
			}
		}
		return Out.IsEmpty() ? TEXT("(none)") : Out;
	}

	FString ListLandscapes()
	{
		UWorld* World = EditorWorld();
		if (!World)
		{
			return TEXT("no editor world");
		}
		FString Out;
		for (TActorIterator<ALandscape> It(World); It; ++It)
		{
			ALandscape* Landscape = *It;
			ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
			int32 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
			if (Info)
			{
				Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY);
			}
			const FTransform T = Landscape->GetActorTransform();
			const FVector Min = T.TransformPosition(FVector(MinX, MinY, 0));
			const FVector Max = T.TransformPosition(FVector(MaxX, MaxY, 0));
			Out += FString::Printf(TEXT("landscape=%s min=(%.0f,%.0f) max=(%.0f,%.0f) z=%.0f cm_per_quad=%.0f edit_layers=%s material=%s layers=%s\n"),
				*Landscape->GetActorLabel(), Min.X, Min.Y, Max.X, Max.Y, T.GetLocation().Z, T.GetScale3D().X,
				Landscape->HasLayersContent() ? TEXT("yes") : TEXT("no"),
				Landscape->LandscapeMaterial ? *Landscape->LandscapeMaterial->GetPathName() : TEXT("(none)"),
				*LayerNames(Landscape));
		}
		return Out.IsEmpty() ? TEXT("no landscape in this level") : Out;
	}

	FString CreateLandscape(const TSharedPtr<FJsonObject>& Args, bool& bOk)
	{
		UWorld* World = EditorWorld();
		if (!World)
		{
			return TEXT("no editor world");
		}
		const FString Label = Str(Args, TEXT("label"), TEXT("EP_Landscape"));
		if (ALandscape* Existing = FindLandscape(Label))
		{
			bOk = true;
			return TEXT("landscape exists ") + Existing->GetActorLabel() + TEXT(". Use sculpt_landscape and paint_landscape_layer on it.");
		}
		const int32 QuadsPerSection = FMath::Clamp(static_cast<int32>(Num(Args, TEXT("quads"), 63)), 7, 255);
		const int32 Sections = FMath::Clamp(static_cast<int32>(Num(Args, TEXT("sections"), 1)), 1, 2);
		const int32 QuadsPerComponent = QuadsPerSection * Sections;
		const double Scale = FMath::Clamp(Num(Args, TEXT("scale"), 100.0), 10.0, 1000.0);
		const double SizeM = FMath::Clamp(Num(Args, TEXT("size"), 252.0), 10.0, 8000.0);
		const int32 Components = FMath::Clamp(FMath::CeilToInt(SizeM * 100.0 / Scale / QuadsPerComponent), 1, 32);
		const int32 SizeX = Components * QuadsPerComponent + 1;
		const int32 SizeY = SizeX;

		const TArray<FLayerSpec> Layers = ParseLayers(Str(Args, TEXT("layers")));
		UMaterialInterface* Material = nullptr;
		const FString MaterialPath = Str(Args, TEXT("material"));
		if (!MaterialPath.IsEmpty())
		{
			Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
			if (!Material)
			{
				return TEXT("material not found ") + MaterialPath;
			}
		}
		else if (Layers.Num() > 0)
		{
			FString Error;
			Material = BuildLandscapeMaterial(FString::Printf(TEXT("/Game/EditorPrompt/Landscape/M_%s"), *SafeName(Label)),
				Layers, Num(Args, TEXT("tiling"), 4.0) * 100.0 / Scale, 0.85, 0.3, Bool(Args, TEXT("height_blend"), false), Error);
			if (!Material)
			{
				return Error;
			}
		}

		TArray<uint16> Heights;
		Heights.Init(32768, SizeX * SizeY);
		TArray<FLandscapeImportLayerInfo> ImportLayers;
		TArray<ULandscapeLayerInfoObject*> LayerInfos;
		for (int32 Index = 0; Index < Layers.Num(); ++Index)
		{
			ULandscapeLayerInfoObject* LayerInfo = LoadOrCreateLayerInfo(Layers[Index].Name);
			if (!LayerInfo)
			{
				return TEXT("layer info create failed ") + Layers[Index].Name;
			}
			FLandscapeImportLayerInfo ImportLayer(FName(*Layers[Index].Name));
			ImportLayer.LayerInfo = LayerInfo;
			ImportLayer.LayerData.Init(Index == 0 ? 255 : 0, SizeX * SizeY);
			ImportLayers.Add(ImportLayer);
			LayerInfos.Add(LayerInfo);
		}
		TMap<FGuid, TArray<uint16>> HeightPerLayer;
		HeightPerLayer.Add(FGuid(), Heights);
		TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialPerLayer;
		MaterialPerLayer.Add(FGuid(), ImportLayers);

		const FVector Center(Num(Args, TEXT("x")), Num(Args, TEXT("y")), Num(Args, TEXT("z")));
		const double Half = Components * QuadsPerComponent * Scale * 0.5;
		const FVector Origin = Center - FVector(Half, Half, 0.0);

		FScopedTransaction Transaction(FText::FromString(TEXT("EditorPrompt create landscape")));
		ALandscape* Landscape = World->SpawnActor<ALandscape>(Origin, FRotator::ZeroRotator);
		if (!Landscape)
		{
			return TEXT("landscape spawn failed");
		}
		Landscape->bCanHaveLayersContent = false;
		Landscape->LandscapeMaterial = Material;
		Landscape->SetActorRelativeScale3D(FVector(Scale, Scale, 100.0));
		Landscape->StaticLightingLOD = FMath::DivideAndRoundUp(FMath::CeilLogTwo((SizeX * SizeY) / (2048 * 2048) + 1), (uint32)2);
		Landscape->Import(FGuid::NewGuid(), 0, 0, SizeX - 1, SizeY - 1, Sections, QuadsPerSection, HeightPerLayer,
			TEXT(""), MaterialPerLayer, ELandscapeImportAlphamapType::Additive);
		Landscape->SetActorLabel(Label);

		if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
		{
			Info->UpdateLayerInfoMap(Landscape);
			for (ULandscapeLayerInfoObject* LayerInfo : LayerInfos)
			{
				if (!Landscape->EditorLayerSettings.Contains(LayerInfo))
				{
					Landscape->EditorLayerSettings.Add(FLandscapeEditorLayerSettings(LayerInfo));
				}
				const int32 Index = Info->GetLayerInfoIndex(LayerInfo->LayerName);
				if (Index == INDEX_NONE)
				{
					Info->Layers.Add(FLandscapeInfoLayerSettings(LayerInfo, Landscape));
				}
				else
				{
					Info->Layers[Index].LayerInfoObj = LayerInfo;
				}
			}
		}
		Landscape->MarkPackageDirty();
		bOk = true;
		return FString::Printf(TEXT("landscape %s components=%dx%d size=%.0fm center=(%.0f,%.0f,%.0f) material=%s layers=%s"),
			*Label, Components, Components, Components * QuadsPerComponent * Scale / 100.0, Center.X, Center.Y, Center.Z,
			Material ? *Material->GetPathName() : TEXT("(none)"), *LayerNames(Landscape));
	}

	FString CreateLandscapeMaterial(const TSharedPtr<FJsonObject>& Args, bool& bOk)
	{
		const TArray<FLayerSpec> Layers = ParseLayers(Str(Args, TEXT("layers")));
		if (Layers.Num() == 0)
		{
			return TEXT("layers is empty. Example: Grass=/Game/T_Grass;Dirt=#5a3d22;Rock");
		}
		ALandscape* Landscape = FindLandscape(Str(Args, TEXT("landscape")));
		const double Scale = Landscape ? FMath::Max(1.0, Landscape->GetActorScale3D().X) : 100.0;
		const FString Label = Landscape ? Landscape->GetActorLabel() : TEXT("Landscape");
		const FString Path = Str(Args, TEXT("path"), FString::Printf(TEXT("/Game/EditorPrompt/Landscape/M_%s"), *SafeName(Label)));
		FString Error;
		UMaterial* Material = BuildLandscapeMaterial(Path, Layers, Num(Args, TEXT("tiling"), 4.0) * 100.0 / Scale,
			Num(Args, TEXT("roughness"), 0.85), Num(Args, TEXT("variation"), 0.3), Bool(Args, TEXT("height_blend"), false), Error);
		if (!Material)
		{
			return Error;
		}
		FString Assigned;
		if (Landscape && Bool(Args, TEXT("assign"), true))
		{
			AssignLandscapeMaterial(Landscape, Material);
			for (const FLayerSpec& Layer : Layers)
			{
				EnsureLayer(Landscape, Layer.Name);
			}
			Assigned = TEXT(" assigned to ") + Landscape->GetActorLabel() + TEXT(" layers=") + LayerNames(Landscape);
		}
		bOk = true;
		return TEXT("landscape material ") + Material->GetPathName() + Assigned;
	}

	FString SetLandscapeGrass(const TSharedPtr<FJsonObject>& Args, bool& bOk)
	{
		ALandscape* Landscape = FindLandscape(Str(Args, TEXT("landscape")));
		if (!Landscape || !Landscape->LandscapeMaterial)
		{
			return TEXT("landscape with a material not found. Call create_landscape or create_landscape_material.");
		}
		UMaterial* Material = Landscape->LandscapeMaterial->GetMaterial();
		if (!Material || !Material->GetPathName().StartsWith(TEXT("/Game/")))
		{
			return TEXT("the landscape material is not an editable /Game material");
		}
		const FString LayerName = Str(Args, TEXT("layer"));
		if (LayerName.IsEmpty())
		{
			return TEXT("layer is empty");
		}
		const FName Layer(*LayerName);
		if (Bool(Args, TEXT("clear"), false))
		{
			Material->Modify();
			SetGrassNode(Material, Layer, nullptr);
			UMaterialEditingLibrary::RecompileMaterial(Material);
			SaveAsset(Material);
			Landscape->FlushGrassComponents();
			bOk = true;
			return TEXT("landscape grass cleared layer=") + LayerName;
		}

		TArray<FString> MeshPaths;
		Str(Args, TEXT("mesh")).ParseIntoArray(MeshPaths, TEXT(";"), true);
		TArray<UStaticMesh*> Meshes;
		for (FString& MeshPath : MeshPaths)
		{
			MeshPath.TrimStartAndEndInline();
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
			if (!Mesh)
			{
				return TEXT("mesh not found ") + MeshPath;
			}
			Meshes.Add(Mesh);
		}
		if (Meshes.Num() == 0)
		{
			return TEXT("mesh is empty. Pass one or more static mesh paths separated by ';'.");
		}

		bool bCreated = false;
		const FString TypePath = Str(Args, TEXT("path"), FString::Printf(TEXT("/Game/EditorPrompt/Landscape/LG_%s"), *SafeName(LayerName)));
		ULandscapeGrassType* Type = LoadOrCreateAsset<ULandscapeGrassType>(TypePath, bCreated);
		if (!Type)
		{
			return TEXT("grass type path must be under /Game");
		}
		const float Density = static_cast<float>(FMath::Clamp(Num(Args, TEXT("density"), 20.0), 0.0, 1000.0));
		const int32 CullEnd = static_cast<int32>(FMath::Max(0.0, Num(Args, TEXT("cull_end"), 6000.0)));
		const FFloatInterval Scale(static_cast<float>(Num(Args, TEXT("scale_min"), 0.8)), static_cast<float>(Num(Args, TEXT("scale_max"), 1.2)));
		Type->Modify();
		Type->GrassVarieties.Reset();
		for (UStaticMesh* Mesh : Meshes)
		{
			FGrassVariety Variety;
			Variety.GrassMesh = Mesh;
			Variety.GrassDensity = FPerPlatformFloat(Density / Meshes.Num());
			Variety.GrassDensityQuality.Default = Density / Meshes.Num();
			Variety.StartCullDistance = FPerPlatformInt(CullEnd * 7 / 10);
			Variety.StartCullDistanceQuality.Default = CullEnd * 7 / 10;
			Variety.EndCullDistance = FPerPlatformInt(CullEnd);
			Variety.EndCullDistanceQuality.Default = CullEnd;
			Variety.Scaling = EGrassScaling::Uniform;
			Variety.ScaleX = Scale;
			Variety.ScaleY = Scale;
			Variety.ScaleZ = Scale;
			Variety.RandomRotation = true;
			Variety.AlignToSurface = Bool(Args, TEXT("align"), true);
			Variety.bCastDynamicShadow = Bool(Args, TEXT("shadow"), false);
			Variety.bUseGrid = false;
			Type->GrassVarieties.Add(Variety);
		}
		Type->PostEditChange();
		SaveAsset(Type);

		Material->Modify();
		SetGrassNode(Material, Layer, Type);
		UMaterialEditingLibrary::RecompileMaterial(Material);
		SaveAsset(Material);
		Landscape->FlushGrassComponents();
		bOk = true;
		return FString::Printf(TEXT("landscape grass layer=%s type=%s meshes=%d density=%.1f per 10m2 cull_end=%dcm"),
			*LayerName, *Type->GetPathName(), Meshes.Num(), Density, CullEnd);
	}

	FString SculptLandscape(const TSharedPtr<FJsonObject>& Args, bool& bOk)
	{
		ALandscape* Landscape = FindLandscape(Str(Args, TEXT("landscape")));
		if (!Landscape)
		{
			return TEXT("landscape not found. Call list_landscapes or create_landscape.");
		}
		FBrush Brush;
		FString Error;
		if (!MakeBrush(Landscape, Args, Brush, Error))
		{
			return Error;
		}
		const FString Mode = Str(Args, TEXT("mode"), TEXT("raise")).ToLower();
		const bool bCm = Mode == TEXT("raise") || Mode == TEXT("lower") || Mode == TEXT("noise") || Mode == TEXT("terrace");
		const double Strength = Num(Args, TEXT("strength"), bCm ? 200.0 : 1.0);
		const double NoiseScale = FMath::Max(1.0, Num(Args, TEXT("noise_scale"), 30.0)) * 100.0;
		const int32 Seed = static_cast<int32>(Num(Args, TEXT("seed"), 1));
		const FRegion& R = Brush.Region;

		TArray<uint16> Data;
		Data.SetNumZeroed(R.Num());
		int32 Changed = 0;
		FScopedTransaction Transaction(FText::FromString(TEXT("EditorPrompt sculpt landscape")));
		EditLandscape(Landscape, [&](FLandscapeEditDataInterface& Edit)
		{
			Edit.GetHeightDataFast(R.X1, R.Y1, R.X2, R.Y2, Data.GetData(), 0);
			const TArray<uint16> Source = Data;
			double TargetZ = Num(Args, TEXT("height"), 0.0);
			if (!Has(Args, TEXT("height")))
			{
				const int32 CX = FMath::Clamp(FMath::RoundToInt(Brush.bAll ? (R.X1 + R.X2) * 0.5 : Brush.LocalCenter.X), R.X1, R.X2);
				const int32 CY = FMath::Clamp(FMath::RoundToInt(Brush.bAll ? (R.Y1 + R.Y2) * 0.5 : Brush.LocalCenter.Y), R.Y1, R.Y2);
				TargetZ = HeightToWorldZ(Brush.ToWorld, Source[R.Index(CX, CY)]);
			}
			for (int32 Y = R.Y1; Y <= R.Y2; ++Y)
			{
				for (int32 X = R.X1; X <= R.X2; ++X)
				{
					const double W = Brush.Weight(X, Y);
					if (W <= 0.0)
					{
						continue;
					}
					const int32 I = R.Index(X, Y);
					const double Z = HeightToWorldZ(Brush.ToWorld, Source[I]);
					double NewZ = Z;
					if (Mode == TEXT("raise"))
					{
						NewZ = Z + Strength * W;
					}
					else if (Mode == TEXT("lower"))
					{
						NewZ = Z - Strength * W;
					}
					else if (Mode == TEXT("flatten") || Mode == TEXT("set"))
					{
						NewZ = FMath::Lerp(Z, TargetZ, FMath::Clamp(Strength, 0.0, 1.0) * W);
					}
					else if (Mode == TEXT("smooth"))
					{
						double Sum = 0.0;
						int32 Count = 0;
						for (int32 DY = -2; DY <= 2; ++DY)
						{
							for (int32 DX = -2; DX <= 2; ++DX)
							{
								const int32 SX = FMath::Clamp(X + DX, R.X1, R.X2);
								const int32 SY = FMath::Clamp(Y + DY, R.Y1, R.Y2);
								Sum += HeightToWorldZ(Brush.ToWorld, Source[R.Index(SX, SY)]);
								++Count;
							}
						}
						NewZ = FMath::Lerp(Z, Sum / Count, FMath::Clamp(Strength, 0.0, 1.0) * W);
					}
					else if (Mode == TEXT("noise"))
					{
						const FVector P = Brush.ToWorld.TransformPosition(FVector(X, Y, 0));
						NewZ = Z + Fbm(P.X / NoiseScale, P.Y / NoiseScale, Seed) * Strength * W;
					}
					else if (Mode == TEXT("terrace"))
					{
						const double Step = FMath::Max(10.0, Strength);
						NewZ = FMath::Lerp(Z, FMath::RoundToDouble(Z / Step) * Step, W);
					}
					const uint16 NewHeight = WorldZToHeight(Brush.ToWorld, NewZ);
					if (NewHeight != Source[I])
					{
						Data[I] = NewHeight;
						++Changed;
					}
				}
			}
			Edit.SetHeightData(R.X1, R.Y1, R.X2, R.Y2, Data.GetData(), 0, true);
		});
		bOk = true;
		return FString::Printf(TEXT("sculpted %s mode=%s vertices=%d"), *Landscape->GetActorLabel(), *Mode, Changed);
	}

	FString PaintLandscapeLayer(const TSharedPtr<FJsonObject>& Args, bool& bOk)
	{
		ALandscape* Landscape = FindLandscape(Str(Args, TEXT("landscape")));
		if (!Landscape)
		{
			return TEXT("landscape not found. Call list_landscapes or create_landscape.");
		}
		const FString LayerName = Str(Args, TEXT("layer"));
		if (LayerName.IsEmpty())
		{
			return TEXT("layer is empty");
		}
		ULandscapeLayerInfoObject* LayerInfo = EnsureLayer(Landscape, LayerName);
		if (!LayerInfo)
		{
			return TEXT("layer info could not be created");
		}
		FBrush Brush;
		FString Error;
		if (!MakeBrush(Landscape, Args, Brush, Error))
		{
			return Error;
		}
		const double Strength = FMath::Clamp(Num(Args, TEXT("strength"), 1.0), 0.0, 1.0);
		const bool bErase = Bool(Args, TEXT("erase"), false);
		const double MinSlope = Num(Args, TEXT("min_slope"), -1e9);
		const double MaxSlope = Num(Args, TEXT("max_slope"), 1e9);
		const double MinZ = Num(Args, TEXT("min_z"), -1e9);
		const double MaxZ = Num(Args, TEXT("max_z"), 1e9);
		const double NoiseAmount = FMath::Clamp(Num(Args, TEXT("noise"), 0.0), 0.0, 1.0);
		const double NoiseScale = FMath::Max(1.0, Num(Args, TEXT("noise_scale"), 20.0)) * 100.0;
		const int32 Seed = static_cast<int32>(Num(Args, TEXT("seed"), 1));
		const bool bNeedsHeight = Has(Args, TEXT("min_slope")) || Has(Args, TEXT("max_slope")) || Has(Args, TEXT("min_z")) || Has(Args, TEXT("max_z"));
		const FRegion& R = Brush.Region;

		TArray<uint8> Weights;
		Weights.SetNumZeroed(R.Num());
		TArray<uint16> Heights;
		int32 Changed = 0;
		FScopedTransaction Transaction(FText::FromString(TEXT("EditorPrompt paint landscape layer")));
		EditLandscape(Landscape, [&](FLandscapeEditDataInterface& Edit)
		{
			if (bNeedsHeight)
			{
				Heights.SetNumZeroed(R.Num());
				Edit.GetHeightDataFast(R.X1, R.Y1, R.X2, R.Y2, Heights.GetData(), 0);
			}
			Edit.GetWeightDataFast(LayerInfo, R.X1, R.Y1, R.X2, R.Y2, Weights.GetData(), 0);
			for (int32 Y = R.Y1; Y <= R.Y2; ++Y)
			{
				for (int32 X = R.X1; X <= R.X2; ++X)
				{
					double W = Brush.Weight(X, Y) * Strength;
					if (W <= 0.0)
					{
						continue;
					}
					const int32 I = R.Index(X, Y);
					if (bNeedsHeight)
					{
						W *= SoftRange(SlopeDegrees(Heights, R, X, Y, Brush.ToWorld), MinSlope, MaxSlope, 3.0);
						W *= SoftRange(HeightToWorldZ(Brush.ToWorld, Heights[I]), MinZ, MaxZ, 50.0);
					}
					if (NoiseAmount > 0.0)
					{
						const FVector P = Brush.ToWorld.TransformPosition(FVector(X, Y, 0));
						const double N = FMath::Clamp(0.5 + 0.8 * Fbm(P.X / NoiseScale, P.Y / NoiseScale, Seed), 0.0, 1.0);
						W *= FMath::Lerp(1.0, N, NoiseAmount);
					}
					if (W <= 0.0)
					{
						continue;
					}
					const double Old = Weights[I];
					const double New = bErase ? Old * (1.0 - W) : Old + (255.0 - Old) * W;
					const uint8 Value = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(New), 0, 255));
					if (Value != Weights[I])
					{
						Weights[I] = Value;
						++Changed;
					}
				}
			}
			Edit.SetAlphaData(LayerInfo, R.X1, R.Y1, R.X2, R.Y2, Weights.GetData(), 0,
				ELandscapeLayerPaintingRestriction::None, true, false);
		});
		bOk = true;
		FString Note;
		if (Landscape->LandscapeMaterial)
		{
			bool bInMaterial = false;
			if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
			{
				const int32 Index = Info->GetLayerInfoIndex(FName(*LayerName));
				bInMaterial = Index != INDEX_NONE;
			}
			if (!bInMaterial)
			{
				Note = TEXT(" warning: the landscape material has no layer with this name, so it will not show.");
			}
		}
		else
		{
			Note = TEXT(" warning: the landscape has no material. Call create_landscape_material.");
		}
		return FString::Printf(TEXT("painted %s layer=%s vertices=%d%s"), *Landscape->GetActorLabel(), *LayerName, Changed, *Note);
	}

	// ---------------------------------------------------------------- foliage

	UMaterial* BuildCardMaterial(const FString& Path, UTexture* Texture, double Wind, FString& OutError)
	{
		bool bCreated = false;
		UMaterial* Material = LoadOrCreateAsset<UMaterial>(Path, bCreated);
		if (!Material)
		{
			OutError = TEXT("material path must be under /Game");
			return nullptr;
		}
		Material->Modify();
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Material);
		Material->SetShadingModel(MSM_DefaultLit);
		Material->BlendMode = BLEND_Masked;
		Material->TwoSided = true;
		bool bNeedsRecompile = false;
		UMaterialEditingLibrary::SetMaterialUsage(Material, MATUSAGE_InstancedStaticMeshes, bNeedsRecompile);
		UMaterialEditingLibrary::SetMaterialUsage(Material, MATUSAGE_StaticLighting, bNeedsRecompile);

		auto Make = [Material](UClass* Class, int32 X, int32 Y)
		{
			return UMaterialEditingLibrary::CreateMaterialExpression(Material, Class, X, Y);
		};
		UMaterialExpressionTextureSampleParameter2D* Sample = Cast<UMaterialExpressionTextureSampleParameter2D>(
			Make(UMaterialExpressionTextureSampleParameter2D::StaticClass(), -600, 0));
		Sample->ParameterName = TEXT("Card");
		Sample->Texture = Texture;
		Sample->AutoSetSampleType();
		UMaterialEditingLibrary::ConnectMaterialProperty(Sample, TEXT("RGB"), MP_BaseColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(Sample, TEXT("A"), MP_OpacityMask);
		UMaterialExpressionConstant* Rough = Cast<UMaterialExpressionConstant>(Make(UMaterialExpressionConstant::StaticClass(), -300, 200));
		Rough->R = 0.8f;
		UMaterialEditingLibrary::ConnectMaterialProperty(Rough, TEXT(""), MP_Roughness);

		if (Wind > 0.0)
		{
			UMaterialExpressionTime* Time = Cast<UMaterialExpressionTime>(Make(UMaterialExpressionTime::StaticClass(), -1200, 400));
			UMaterialExpressionMultiply* TimeSpeed = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -1000, 400));
			TimeSpeed->A.Connect(0, Time);
			TimeSpeed->ConstB = 0.6f;
			UMaterialExpressionWorldPosition* WorldPos = Cast<UMaterialExpressionWorldPosition>(Make(UMaterialExpressionWorldPosition::StaticClass(), -1200, 520));
			UMaterialExpressionComponentMask* WorldX = Cast<UMaterialExpressionComponentMask>(Make(UMaterialExpressionComponentMask::StaticClass(), -1000, 520));
			WorldX->Input.Connect(0, WorldPos);
			WorldX->R = 1;
			WorldX->G = 0;
			WorldX->B = 0;
			WorldX->A = 0;
			UMaterialExpressionMultiply* Phase = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -850, 520));
			Phase->A.Connect(0, WorldX);
			Phase->ConstB = 0.004f;
			UMaterialExpressionAdd* Add = Cast<UMaterialExpressionAdd>(Make(UMaterialExpressionAdd::StaticClass(), -700, 450));
			Add->A.Connect(0, TimeSpeed);
			Add->B.Connect(0, Phase);
			UMaterialExpressionSine* Sine = Cast<UMaterialExpressionSine>(Make(UMaterialExpressionSine::StaticClass(), -560, 450));
			Sine->Input.Connect(0, Add);
			UMaterialExpressionTextureCoordinate* UV = Cast<UMaterialExpressionTextureCoordinate>(Make(UMaterialExpressionTextureCoordinate::StaticClass(), -1000, 680));
			UMaterialExpressionComponentMask* V = Cast<UMaterialExpressionComponentMask>(Make(UMaterialExpressionComponentMask::StaticClass(), -850, 680));
			V->Input.Connect(0, UV);
			V->R = 0;
			V->G = 1;
			V->B = 0;
			V->A = 0;
			UMaterialExpressionOneMinus* Top = Cast<UMaterialExpressionOneMinus>(Make(UMaterialExpressionOneMinus::StaticClass(), -700, 680));
			Top->Input.Connect(0, V);
			UMaterialExpressionMultiply* Weighted = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -420, 500));
			Weighted->A.Connect(0, Sine);
			Weighted->B.Connect(0, Top);
			UMaterialExpressionConstant3Vector* Dir = Cast<UMaterialExpressionConstant3Vector>(Make(UMaterialExpressionConstant3Vector::StaticClass(), -420, 650));
			Dir->Constant = FLinearColor(static_cast<float>(Wind), static_cast<float>(Wind * 0.6), 0.f);
			UMaterialExpressionMultiply* Offset = Cast<UMaterialExpressionMultiply>(Make(UMaterialExpressionMultiply::StaticClass(), -260, 550));
			Offset->A.Connect(0, Weighted);
			Offset->B.Connect(0, Dir);
			UMaterialEditingLibrary::ConnectMaterialProperty(Offset, TEXT(""), MP_WorldPositionOffset);
		}

		UMaterialEditingLibrary::RecompileMaterial(Material);
		SaveAsset(Material);
		return Material;
	}

	FString CreateCardMesh(const TSharedPtr<FJsonObject>& Args, bool& bOk)
	{
		const FString TexturePath = Str(Args, TEXT("texture"));
		UTexture* Texture = LoadTexture(TexturePath);
		if (!Texture)
		{
			return TEXT("texture not found. Make one with request_texture and import_texture first.");
		}
		const FString Name = SafeName(Str(Args, TEXT("name"), FPackageName::GetShortName(Texture->GetOutermost()->GetName())));
		const FString MeshPath = Str(Args, TEXT("path"), FString::Printf(TEXT("/Game/EditorPrompt/Foliage/SM_%s"), *Name));
		FString MeshPackage;
		FString MeshAsset;
		if (!SplitGamePath(MeshPath, MeshPackage, MeshAsset))
		{
			return TEXT("path must be under /Game");
		}
		FString Error;
		UMaterial* Material = BuildCardMaterial(FPackageName::GetLongPackagePath(MeshPackage) / (TEXT("M_") + Name),
			Texture, FMath::Max(0.0, Num(Args, TEXT("wind"), 4.0)), Error);
		if (!Material)
		{
			return Error;
		}

		bool bCreated = false;
		UStaticMesh* Mesh = LoadOrCreateAsset<UStaticMesh>(MeshPath, bCreated);
		if (!Mesh)
		{
			return TEXT("mesh create failed");
		}
		Mesh->Modify();
		if (Mesh->GetNumSourceModels() == 0)
		{
			FStaticMeshSourceModel& Source = Mesh->AddSourceModel();
			Source.BuildSettings.bRecomputeNormals = false;
			Source.BuildSettings.bRecomputeTangents = true;
			Source.BuildSettings.bGenerateLightmapUVs = false;
		}
		FMeshDescription* Description = Mesh->CreateMeshDescription(0);
		if (!Description)
		{
			return TEXT("mesh description failed");
		}
		Description->Empty();
		FStaticMeshAttributes Attributes(*Description);
		Attributes.Register();
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		TPolygonGroupAttributesRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
		UVs.SetNumChannels(1);
		const FPolygonGroupID Group = Description->CreatePolygonGroup();
		SlotNames[Group] = TEXT("Card");

		const int32 Cards = FMath::Clamp(static_cast<int32>(Num(Args, TEXT("cards"), 3)), 1, 8);
		const int32 Clumps = FMath::Clamp(static_cast<int32>(Num(Args, TEXT("clumps"), 1)), 1, 12);
		const float Width = static_cast<float>(FMath::Max(1.0, Num(Args, TEXT("width"), 40.0)));
		const float Height = static_cast<float>(FMath::Max(1.0, Num(Args, TEXT("height"), 40.0)));
		const float Spread = static_cast<float>(Num(Args, TEXT("spread"), Width * 0.6));
		const float Tilt = FMath::DegreesToRadians(static_cast<float>(Num(Args, TEXT("tilt"), 8.0)));
		FRandomStream Random(static_cast<int32>(Num(Args, TEXT("seed"), 7)));
		const FVector2f Corners[4] = { FVector2f(0.f, 1.f), FVector2f(1.f, 1.f), FVector2f(1.f, 0.f), FVector2f(0.f, 0.f) };
		for (int32 Clump = 0; Clump < Clumps; ++Clump)
		{
			const FVector3f Base = Clump == 0 ? FVector3f::ZeroVector
				: FVector3f(static_cast<float>(Random.FRandRange(-Spread, Spread)), static_cast<float>(Random.FRandRange(-Spread, Spread)), 0.f);
			const float Yaw0 = static_cast<float>(Random.FRandRange(0.0, PI));
			const float Size = Clump == 0 ? 1.f : static_cast<float>(Random.FRandRange(0.7, 1.0));
			for (int32 Card = 0; Card < Cards; ++Card)
			{
				const float Yaw = Yaw0 + PI * Card / Cards;
				const FVector3f Along(FMath::Cos(Yaw), FMath::Sin(Yaw), 0.f);
				const FVector3f Side(-Along.Y, Along.X, 0.f);
				const float HalfW = Width * 0.5f * Size;
				const float H = Height * Size;
				const FVector3f Lean = Side * (FMath::Sin(Tilt) * H * (Card % 2 == 0 ? 1.f : -1.f));
				const FVector3f Points[4] = {
					Base - Along * HalfW,
					Base + Along * HalfW,
					Base + Along * HalfW + FVector3f(0.f, 0.f, H) + Lean,
					Base - Along * HalfW + FVector3f(0.f, 0.f, H) + Lean };
				FVertexInstanceID Instances[4];
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					const FVertexID Vertex = Description->CreateVertex();
					Positions[Vertex] = Points[Corner];
					Instances[Corner] = Description->CreateVertexInstance(Vertex);
					Normals[Instances[Corner]] = FVector3f(0.f, 0.f, 1.f);
					UVs.Set(Instances[Corner], 0, Corners[Corner]);
				}
				Description->CreateTriangle(Group, { Instances[0], Instances[1], Instances[2] });
				Description->CreateTriangle(Group, { Instances[0], Instances[2], Instances[3] });
			}
		}
		Mesh->CommitMeshDescription(0);
		TArray<FStaticMaterial> Materials;
		Materials.Add(FStaticMaterial(Material, TEXT("Card"), TEXT("Card")));
		Mesh->SetStaticMaterials(Materials);
		Mesh->Build(false);
		Mesh->PostEditChange();
		SaveAsset(Mesh);
		bOk = true;
		return FString::Printf(TEXT("card mesh %s material=%s cards=%d clumps=%d size=%.0fx%.0fcm"),
			*Mesh->GetPathName(), *Material->GetPathName(), Cards, Clumps, Width, Height);
	}

	UFoliageType_InstancedStaticMesh* EnsureFoliageType(UStaticMesh* Mesh, const FString& PathOverride, const TSharedPtr<FJsonObject>& Args, bool bApplyArgs)
	{
		const FString Path = PathOverride.IsEmpty()
			? FString::Printf(TEXT("/Game/EditorPrompt/Foliage/FT_%s"), *SafeName(Mesh->GetName()))
			: PathOverride;
		bool bCreated = false;
		UFoliageType_InstancedStaticMesh* Type = LoadOrCreateAsset<UFoliageType_InstancedStaticMesh>(Path, bCreated);
		if (!Type)
		{
			return nullptr;
		}
		if (!bCreated && !bApplyArgs)
		{
			return Type;
		}
		Type->Modify();
		Type->SetStaticMesh(Mesh);
		Type->Scaling = EFoliageScaling::Uniform;
		Type->ScaleX = FFloatInterval(static_cast<float>(Num(Args, TEXT("scale_min"), 0.8)), static_cast<float>(Num(Args, TEXT("scale_max"), 1.2)));
		Type->ScaleY = Type->ScaleX;
		Type->ScaleZ = Type->ScaleX;
		Type->AlignToNormal = Bool(Args, TEXT("align"), false) ? 1 : 0;
		Type->RandomYaw = Bool(Args, TEXT("random_yaw"), true) ? 1 : 0;
		Type->CastShadow = Bool(Args, TEXT("shadow"), true) ? 1 : 0;
		const int32 CullEnd = static_cast<int32>(Num(Args, TEXT("cull_end"), 0));
		Type->CullDistance = FInt32Interval(CullEnd > 0 ? CullEnd / 2 : 0, CullEnd);
		const float ZOffset = static_cast<float>(Num(Args, TEXT("z_offset"), 0));
		Type->ZOffset = FFloatInterval(ZOffset, ZOffset);
		Type->PostEditChange();
		SaveAsset(Type);
		return Type;
	}

	FString CreateFoliageType(const TSharedPtr<FJsonObject>& Args, bool& bOk)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Str(Args, TEXT("mesh")));
		if (!Mesh)
		{
			return TEXT("mesh not found");
		}
		UFoliageType_InstancedStaticMesh* Type = EnsureFoliageType(Mesh, Str(Args, TEXT("path")), Args, true);
		if (!Type)
		{
			return TEXT("foliage type path must be under /Game");
		}
		bOk = true;
		return TEXT("foliage type ") + Type->GetPathName() + TEXT(" mesh=") + Mesh->GetPathName();
	}

	UFoliageType* ResolveFoliageType(const TSharedPtr<FJsonObject>& Args, FString& OutError)
	{
		const FString TypePath = Str(Args, TEXT("type"));
		if (!TypePath.IsEmpty())
		{
			if (UFoliageType* Type = LoadObject<UFoliageType>(nullptr, *TypePath))
			{
				return Type;
			}
			OutError = TEXT("foliage type not found ") + TypePath;
			return nullptr;
		}
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Str(Args, TEXT("mesh")));
		if (!Mesh)
		{
			OutError = TEXT("pass type= or mesh=");
			return nullptr;
		}
		UFoliageType* Type = EnsureFoliageType(Mesh, FString(), Args, false);
		if (!Type)
		{
			OutError = TEXT("foliage type create failed");
		}
		return Type;
	}

	struct FWeightSampler
	{
		ALandscape* Landscape = nullptr;
		FRegion Region;
		TArray<uint8> Weights;
		FTransform ToWorld;

		bool Init(ALandscape* InLandscape, const FString& Layer, const FVector& Center, double Radius)
		{
			Landscape = InLandscape;
			ULandscapeInfo* Info = Landscape ? Landscape->GetLandscapeInfo() : nullptr;
			ULandscapeLayerInfoObject* LayerInfo = Info ? Info->GetLayerInfoByName(FName(*Layer)) : nullptr;
			int32 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
			if (!LayerInfo || !Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
			{
				return false;
			}
			ToWorld = Landscape->GetActorTransform();
			const FVector Local = ToWorld.InverseTransformPosition(Center);
			const double LocalRadius = Radius / FMath::Max(1e-3, ToWorld.GetScale3D().X) + 1.0;
			Region.X1 = FMath::Max(MinX, FMath::FloorToInt(Local.X - LocalRadius));
			Region.Y1 = FMath::Max(MinY, FMath::FloorToInt(Local.Y - LocalRadius));
			Region.X2 = FMath::Min(MaxX, FMath::CeilToInt(Local.X + LocalRadius));
			Region.Y2 = FMath::Min(MaxY, FMath::CeilToInt(Local.Y + LocalRadius));
			if (Region.X1 > Region.X2 || Region.Y1 > Region.Y2)
			{
				return false;
			}
			Weights.SetNumZeroed(Region.Num());
			FLandscapeEditDataInterface Edit(Info);
			Edit.GetWeightDataFast(LayerInfo, Region.X1, Region.Y1, Region.X2, Region.Y2, Weights.GetData(), 0);
			return true;
		}

		double Sample(const FVector& World) const
		{
			const FVector Local = ToWorld.InverseTransformPosition(World);
			const int32 X = FMath::RoundToInt(Local.X);
			const int32 Y = FMath::RoundToInt(Local.Y);
			if (X < Region.X1 || X > Region.X2 || Y < Region.Y1 || Y > Region.Y2)
			{
				return 0.0;
			}
			return Weights[Region.Index(X, Y)] / 255.0;
		}
	};

	FString PaintFoliage(const TSharedPtr<FJsonObject>& Args, bool& bOk)
	{
		UWorld* World = EditorWorld();
		if (!World)
		{
			return TEXT("no editor world");
		}
		FString Error;
		UFoliageType* Type = ResolveFoliageType(Args, Error);
		if (!Type)
		{
			return Error;
		}
		ALandscape* Landscape = FindLandscape(Str(Args, TEXT("landscape")));
		const bool bWholeLandscape = !Has(Args, TEXT("x")) || !Has(Args, TEXT("y"));
		FVector Center(Num(Args, TEXT("x")), Num(Args, TEXT("y")), 0.0);
		double Radius = FMath::Max(10.0, Num(Args, TEXT("radius"), 1000.0));
		if (bWholeLandscape)
		{
			if (!Landscape)
			{
				return TEXT("pass x, y, and radius, or have a landscape to cover");
			}
			const FBox Box = Landscape->GetComponentsBoundingBox(true);
			Center = Box.GetCenter();
			Radius = FVector2D(Box.GetExtent().X, Box.GetExtent().Y).Size();
		}
		const double AreaM2 = bWholeLandscape && Landscape
			? (Landscape->GetComponentsBoundingBox(true).GetSize().X * Landscape->GetComponentsBoundingBox(true).GetSize().Y) / 10000.0
			: PI * Radius * Radius / 10000.0;
		int32 Count = static_cast<int32>(Num(Args, TEXT("count"), 0));
		if (Count <= 0)
		{
			Count = FMath::RoundToInt(Num(Args, TEXT("density"), 20.0) * AreaM2 / 100.0);
		}
		Count = FMath::Clamp(Count, 1, MaxFoliagePerCall);

		const FString Layer = Str(Args, TEXT("layer"));
		const double MinWeight = FMath::Clamp(Num(Args, TEXT("min_weight"), 0.5), 0.0, 1.0);
		FWeightSampler Sampler;
		if (!Layer.IsEmpty() && !Sampler.Init(Landscape, Layer, Center, Radius))
		{
			return TEXT("layer filter needs a landscape that has layer ") + Layer;
		}

		const UFoliageType_InstancedStaticMesh* MeshType = Cast<UFoliageType_InstancedStaticMesh>(Type);
		const double ScaleMin = Num(Args, TEXT("scale_min"), MeshType ? MeshType->ScaleX.Min : 0.8);
		const double ScaleMax = Num(Args, TEXT("scale_max"), MeshType ? MeshType->ScaleX.Max : 1.2);
		const bool bAlign = Bool(Args, TEXT("align"), Type->AlignToNormal != 0);
		const bool bRandomYaw = Bool(Args, TEXT("random_yaw"), Type->RandomYaw != 0);
		const double MaxSlope = Num(Args, TEXT("max_slope"), 90.0);
		const double Spacing = FMath::Max(0.0, Num(Args, TEXT("spacing"), 0.0));
		const double ZOffset = Num(Args, TEXT("z_offset"), 0.0);
		const double Falloff = FMath::Clamp(Num(Args, TEXT("falloff"), 0.3), 0.0, 1.0);
		AActor* Surface = nullptr;
		const FString SurfaceLabel = Str(Args, TEXT("surface"));
		if (!SurfaceLabel.IsEmpty())
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (It->GetActorLabel().Equals(SurfaceLabel, ESearchCase::IgnoreCase))
				{
					Surface = *It;
					break;
				}
			}
			if (!Surface)
			{
				return TEXT("surface actor not found ") + SurfaceLabel;
			}
		}

		FCollisionQueryParams Params(SCENE_QUERY_STAT(EditorPromptFoliage), true);
		for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}

		FRandomStream Random(static_cast<int32>(Num(Args, TEXT("seed"), 1)));
		TMap<FIntPoint, TArray<FVector2D>> Grid;
		auto TooClose = [&](const FVector2D& P)
		{
			if (Spacing <= 0.0)
			{
				return false;
			}
			const FIntPoint Cell(FMath::FloorToInt(P.X / Spacing), FMath::FloorToInt(P.Y / Spacing));
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					if (const TArray<FVector2D>* Points = Grid.Find(Cell + FIntPoint(DX, DY)))
					{
						for (const FVector2D& Q : *Points)
						{
							if (FVector2D::DistSquared(P, Q) < Spacing * Spacing)
							{
								return true;
							}
						}
					}
				}
			}
			return false;
		};

		TArray<FFoliageInstance> Instances;
		Instances.Reserve(Count);
		const int32 Attempts = Count * 4;
		for (int32 Attempt = 0; Attempt < Attempts && Instances.Num() < Count; ++Attempt)
		{
			FVector2D P;
			if (bWholeLandscape)
			{
				const FBox Box = Landscape->GetComponentsBoundingBox(true);
				P = FVector2D(Random.FRandRange(Box.Min.X, Box.Max.X), Random.FRandRange(Box.Min.Y, Box.Max.Y));
			}
			else
			{
				const double Angle = Random.FRandRange(0.f, 2.f * PI);
				const double Dist = Radius * FMath::Sqrt(Random.FRand());
				if (Random.FRand() > BrushWeight(Dist, Radius, Falloff))
				{
					continue;
				}
				P = FVector2D(Center.X + FMath::Cos(Angle) * Dist, Center.Y + FMath::Sin(Angle) * Dist);
			}
			if (!Layer.IsEmpty() && Sampler.Sample(FVector(P, 0.0)) < MinWeight)
			{
				continue;
			}
			if (TooClose(P))
			{
				continue;
			}
			const FVector Start(P.X, P.Y, 1000000.0);
			const FVector End(P.X, P.Y, -1000000.0);
			FHitResult Hit;
			const bool bHit = Surface
				? Surface->ActorLineTraceSingle(Hit, Start, End, ECC_Visibility, Params)
				: World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
			if (!bHit)
			{
				continue;
			}
			if (FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Hit.ImpactNormal.Z, -1.0, 1.0))) > MaxSlope)
			{
				continue;
			}
			FFoliageInstance Instance;
			Instance.Location = Hit.ImpactPoint + FVector(0, 0, ZOffset);
			Instance.Rotation = FRotator(0.0, bRandomYaw ? Random.FRandRange(0.0, 360.0) : 0.0, 0.0);
			const float S = static_cast<float>(Random.FRandRange(ScaleMin, ScaleMax));
			Instance.DrawScale3D = FVector3f(S, S, S);
			if (bAlign)
			{
				Instance.AlignToNormal(Hit.ImpactNormal, Type->AlignMaxAngle);
			}
			Instance.BaseComponent = Hit.GetComponent();
			Instances.Add(Instance);
			if (Spacing > 0.0)
			{
				Grid.FindOrAdd(FIntPoint(FMath::FloorToInt(P.X / Spacing), FMath::FloorToInt(P.Y / Spacing))).Add(P);
			}
		}
		if (Instances.Num() == 0)
		{
			return TEXT("no instance placed. Check that the area has a surface with collision, and the layer or slope filter.");
		}

		FScopedTransaction Transaction(FText::FromString(TEXT("EditorPrompt paint foliage")));
		TMap<AInstancedFoliageActor*, TArray<const FFoliageInstance*>> PerActor;
		for (const FFoliageInstance& Instance : Instances)
		{
			if (AInstancedFoliageActor* Actor = AInstancedFoliageActor::Get(World, true, World->PersistentLevel, Instance.Location))
			{
				PerActor.FindOrAdd(Actor).Add(&Instance);
			}
		}
		for (const TPair<AInstancedFoliageActor*, TArray<const FFoliageInstance*>>& Pair : PerActor)
		{
			Pair.Key->Modify();
			FFoliageInfo* Info = nullptr;
			if (UFoliageType* Added = Pair.Key->AddFoliageType(Type, &Info))
			{
				if (Info)
				{
					Info->AddInstances(Added, Pair.Value);
				}
			}
		}
		bOk = true;
		return FString::Printf(TEXT("foliage placed=%d requested=%d type=%s"), Instances.Num(), Count, *Type->GetPathName());
	}

	bool TypeMatches(const UFoliageType* Type, const FString& Filter)
	{
		if (Filter.IsEmpty())
		{
			return true;
		}
		if (Type->GetPathName().Contains(Filter) || Type->GetName().Contains(Filter))
		{
			return true;
		}
		if (const UFoliageType_InstancedStaticMesh* MeshType = Cast<UFoliageType_InstancedStaticMesh>(Type))
		{
			return MeshType->GetStaticMesh() && MeshType->GetStaticMesh()->GetPathName().Contains(Filter);
		}
		return false;
	}

	FString EraseFoliage(const TSharedPtr<FJsonObject>& Args, bool& bOk)
	{
		UWorld* World = EditorWorld();
		if (!World)
		{
			return TEXT("no editor world");
		}
		const bool bAll = !Has(Args, TEXT("x")) || !Has(Args, TEXT("y"));
		const FVector2D Center(Num(Args, TEXT("x")), Num(Args, TEXT("y")));
		const double Radius = FMath::Max(1.0, Num(Args, TEXT("radius"), 1000.0));
		const FString Filter = Str(Args, TEXT("type"), Str(Args, TEXT("mesh")));
		const double Keep = FMath::Clamp(Num(Args, TEXT("keep"), 0.0), 0.0, 1.0);
		FRandomStream Random(static_cast<int32>(Num(Args, TEXT("seed"), 3)));
		int32 Removed = 0;
		FScopedTransaction Transaction(FText::FromString(TEXT("EditorPrompt erase foliage")));
		for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
		{
			AInstancedFoliageActor* Actor = *It;
			Actor->ForEachFoliageInfo([&](UFoliageType* Type, FFoliageInfo& Info)
			{
				if (!TypeMatches(Type, Filter))
				{
					return true;
				}
				TArray<int32> Remove;
				for (int32 Index = 0; Index < Info.Instances.Num(); ++Index)
				{
					const FVector& L = Info.Instances[Index].Location;
					if ((bAll || FVector2D::Distance(FVector2D(L.X, L.Y), Center) <= Radius) && Random.FRand() >= Keep)
					{
						Remove.Add(Index);
					}
				}
				if (Remove.Num() > 0)
				{
					Actor->Modify();
					Info.RemoveInstances(Remove, true);
					Removed += Remove.Num();
				}
				return true;
			});
		}
		bOk = true;
		return FString::Printf(TEXT("foliage removed=%d"), Removed);
	}

	FString ListFoliage()
	{
		UWorld* World = EditorWorld();
		if (!World)
		{
			return TEXT("no editor world");
		}
		TMap<FString, int32> Counts;
		for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
		{
			It->ForEachFoliageInfo([&](UFoliageType* Type, FFoliageInfo& Info)
			{
				FString Key = Type->GetPathName();
				if (const UFoliageType_InstancedStaticMesh* MeshType = Cast<UFoliageType_InstancedStaticMesh>(Type))
				{
					if (MeshType->GetStaticMesh())
					{
						Key += TEXT(" mesh=") + MeshType->GetStaticMesh()->GetPathName();
					}
				}
				Counts.FindOrAdd(Key) += Info.Instances.Num();
				return true;
			});
		}
		FString Out;
		for (const TPair<FString, int32>& Pair : Counts)
		{
			Out += FString::Printf(TEXT("type=%s instances=%d\n"), *Pair.Key, Pair.Value);
		}
		return Out.IsEmpty() ? TEXT("no foliage in this level") : Out;
	}
}

bool FEditorPromptWorld::Handle(const FString& Name, const TSharedPtr<FJsonObject>& Args, FString& OutMessage, bool& bOutOk)
{
	bOutOk = false;
	if (Name == TEXT("list_landscapes"))
	{
		OutMessage = ListLandscapes();
		bOutOk = !OutMessage.StartsWith(TEXT("no editor world"));
		return true;
	}
	if (Name == TEXT("create_landscape"))
	{
		OutMessage = CreateLandscape(Args, bOutOk);
		return true;
	}
	if (Name == TEXT("create_landscape_material"))
	{
		OutMessage = CreateLandscapeMaterial(Args, bOutOk);
		return true;
	}
	if (Name == TEXT("set_landscape_grass"))
	{
		OutMessage = SetLandscapeGrass(Args, bOutOk);
		return true;
	}
	if (Name == TEXT("sculpt_landscape"))
	{
		OutMessage = SculptLandscape(Args, bOutOk);
		return true;
	}
	if (Name == TEXT("paint_landscape_layer"))
	{
		OutMessage = PaintLandscapeLayer(Args, bOutOk);
		return true;
	}
	if (Name == TEXT("create_card_mesh"))
	{
		OutMessage = CreateCardMesh(Args, bOutOk);
		return true;
	}
	if (Name == TEXT("create_foliage_type"))
	{
		OutMessage = CreateFoliageType(Args, bOutOk);
		return true;
	}
	if (Name == TEXT("paint_foliage"))
	{
		OutMessage = PaintFoliage(Args, bOutOk);
		return true;
	}
	if (Name == TEXT("erase_foliage"))
	{
		OutMessage = EraseFoliage(Args, bOutOk);
		return true;
	}
	if (Name == TEXT("list_foliage"))
	{
		OutMessage = ListFoliage();
		bOutOk = !OutMessage.StartsWith(TEXT("no editor world"));
		return true;
	}
	return false;
}
