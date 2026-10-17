#include "EditorPromptGraphs.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "Framework/Docking/TabManager.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Self.h"
#include "K2Node_SpawnActor.h"
#include "K2Node_Timeline.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/Texture.h"
#include "FileHelpers.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LevelSequence.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "MovieScene.h"
#include "MovieSceneObjectBindingID.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"

namespace
{
	FString ArgStr(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, const FString& Fallback = FString())
	{
		FString Value;
		if (Args.IsValid() && Args->TryGetStringField(Key, Value) && !Value.IsEmpty())
		{
			return Value;
		}
		return Fallback;
	}

	double ArgNum(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, double Fallback = 0.0)
	{
		double Value = Fallback;
		if (Args.IsValid())
		{
			Args->TryGetNumberField(Key, Value);
		}
		return Value;
	}

	bool ArgHas(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key)
	{
		return Args.IsValid() && Args->HasField(Key);
	}

	UMaterial* ResolveMaterial(const FString& Path, FString& OutError, FString& OutNote)
	{
		if (!Path.StartsWith(TEXT("/Game")))
		{
			OutError = TEXT("material path must start with /Game");
			return nullptr;
		}
		UMaterialInterface* Interface = LoadObject<UMaterialInterface>(nullptr, *Path);
		UMaterial* Material = Interface ? Interface->GetMaterial() : nullptr;
		if (!Material)
		{
			OutError = TEXT("material not found");
			return nullptr;
		}
		if (Material->GetPathName().StartsWith(TEXT("/Engine")))
		{
			OutError = TEXT("engine material cannot be edited; duplicate it into /Game first");
			return nullptr;
		}
		if (Interface != Material)
		{
			OutNote = TEXT("parent=") + Material->GetPathName();
		}
		return Material;
	}

	UClass* ExpressionClass(const FString& Name)
	{
		FString Clean = Name;
		if (Clean.Equals(TEXT("Lerp"), ESearchCase::IgnoreCase))
		{
			Clean = TEXT("LinearInterpolate");
		}
		else if (Clean.Equals(TEXT("Mask"), ESearchCase::IgnoreCase))
		{
			Clean = TEXT("ComponentMask");
		}
		else if (Clean.Equals(TEXT("Texture"), ESearchCase::IgnoreCase) || Clean.Equals(TEXT("TextureSample"), ESearchCase::IgnoreCase))
		{
			Clean = TEXT("TextureSampleParameter2D");
		}
		else if (Clean.Equals(TEXT("Scalar"), ESearchCase::IgnoreCase))
		{
			Clean = TEXT("ScalarParameter");
		}
		else if (Clean.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
		{
			Clean = TEXT("VectorParameter");
		}
		else if (Clean.Equals(TEXT("UV"), ESearchCase::IgnoreCase))
		{
			Clean = TEXT("TextureCoordinate");
		}
		if (!Clean.StartsWith(TEXT("MaterialExpression")))
		{
			Clean = TEXT("MaterialExpression") + Clean;
		}
		return LoadClass<UMaterialExpression>(nullptr, *(TEXT("/Script/Engine.") + Clean));
	}

	UMaterialExpression* FindExpr(UMaterial* Material, const FString& Desc)
	{
		if (!Material || Desc.IsEmpty())
		{
			return nullptr;
		}
		for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
		{
			if (Expression && Expression->Desc.Equals(Desc, ESearchCase::IgnoreCase))
			{
				return Expression;
			}
		}
		return nullptr;
	}

	EMaterialProperty PropertyByName(const FString& Name, bool& bOk)
	{
		bOk = true;
		const FString Key = Name.ToLower();
		if (Key == TEXT("basecolor") || Key == TEXT("diffuse")) return MP_BaseColor;
		if (Key == TEXT("emissive")) return MP_EmissiveColor;
		if (Key == TEXT("opacity")) return MP_Opacity;
		if (Key == TEXT("opacitymask")) return MP_OpacityMask;
		if (Key == TEXT("normal")) return MP_Normal;
		if (Key == TEXT("roughness")) return MP_Roughness;
		if (Key == TEXT("metallic")) return MP_Metallic;
		if (Key == TEXT("specular")) return MP_Specular;
		if (Key == TEXT("refraction")) return MP_Refraction;
		if (Key == TEXT("ambientocclusion") || Key == TEXT("ao")) return MP_AmbientOcclusion;
		if (Key == TEXT("worldpositionoffset") || Key == TEXT("wpo")) return MP_WorldPositionOffset;
		bOk = false;
		return MP_BaseColor;
	}

	void ApplyExpressionFields(UMaterialExpression* Expression, const TSharedPtr<FJsonObject>& Args)
	{
		if (!Expression)
		{
			return;
		}
		const FString Desc = ArgStr(Args, TEXT("desc"));
		if (!Desc.IsEmpty())
		{
			Expression->Desc = Desc;
		}
		if (UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(Expression))
		{
			Constant->R = static_cast<float>(ArgNum(Args, ArgHas(Args, TEXT("value")) ? TEXT("value") : TEXT("r")));
		}
		if (UMaterialExpressionConstant3Vector* Color = Cast<UMaterialExpressionConstant3Vector>(Expression))
		{
			Color->Constant = FLinearColor(
				static_cast<float>(ArgNum(Args, TEXT("r"), 1)),
				static_cast<float>(ArgNum(Args, TEXT("g"), 1)),
				static_cast<float>(ArgNum(Args, TEXT("b"), 1)),
				static_cast<float>(ArgNum(Args, TEXT("a"), 1)));
		}
		const FString Parameter = ArgStr(Args, TEXT("parameter"));
		if (UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expression))
		{
			if (!Parameter.IsEmpty())
			{
				Scalar->SetParameterName(*Parameter);
			}
			if (ArgHas(Args, TEXT("value")))
			{
				Scalar->DefaultValue = static_cast<float>(ArgNum(Args, TEXT("value")));
			}
		}
		if (UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Expression))
		{
			if (!Parameter.IsEmpty())
			{
				Vector->SetParameterName(*Parameter);
			}
			Vector->DefaultValue = FLinearColor(
				static_cast<float>(ArgNum(Args, TEXT("r"), 1)),
				static_cast<float>(ArgNum(Args, TEXT("g"), 1)),
				static_cast<float>(ArgNum(Args, TEXT("b"), 1)),
				static_cast<float>(ArgNum(Args, TEXT("a"), 1)));
		}
		if (UMaterialExpressionTextureSampleParameter2D* TextureParam = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression))
		{
			if (!Parameter.IsEmpty())
			{
				TextureParam->ParameterName = *Parameter;
			}
			const FString TexturePath = ArgStr(Args, TEXT("texture"));
			if (!TexturePath.IsEmpty())
			{
				if (UTexture* Texture = LoadObject<UTexture>(nullptr, *TexturePath))
				{
					TextureParam->Texture = Texture;
				}
			}
		}
		if (UMaterialExpressionComponentMask* Mask = Cast<UMaterialExpressionComponentMask>(Expression))
		{
			auto On = [&](const TCHAR* Key) -> int32
			{
				bool bValue = false;
				if (Args.IsValid())
				{
					Args->TryGetBoolField(Key, bValue);
				}
				return bValue ? 1 : 0;
			};
			if (!ArgHas(Args, TEXT("mask_r")) && !ArgHas(Args, TEXT("mask_g")) && !ArgHas(Args, TEXT("mask_b")) && !ArgHas(Args, TEXT("mask_a")))
			{
				Mask->R = 1;
				Mask->G = 1;
				Mask->B = 1;
			}
			if (ArgHas(Args, TEXT("mask_r"))) Mask->R = On(TEXT("mask_r"));
			if (ArgHas(Args, TEXT("mask_g"))) Mask->G = On(TEXT("mask_g"));
			if (ArgHas(Args, TEXT("mask_b"))) Mask->B = On(TEXT("mask_b"));
			if (ArgHas(Args, TEXT("mask_a"))) Mask->A = On(TEXT("mask_a"));
		}
		if (UMaterialExpressionCollectionParameter* CollectionParam = Cast<UMaterialExpressionCollectionParameter>(Expression))
		{
			const FString CollectionPath = ArgStr(Args, TEXT("collection"));
			UMaterialParameterCollection* Collection = CollectionPath.IsEmpty()
				? nullptr
				: LoadObject<UMaterialParameterCollection>(nullptr, *CollectionPath);
			if (Collection)
			{
				CollectionParam->Collection = Collection;
				CollectionParam->ParameterName = *Parameter;
				for (const FCollectionVectorParameter& Item : Collection->VectorParameters)
				{
					if (Item.ParameterName.ToString().Equals(Parameter, ESearchCase::IgnoreCase))
					{
						CollectionParam->ParameterId = Item.Id;
					}
				}
				for (const FCollectionScalarParameter& Item : Collection->ScalarParameters)
				{
					if (Item.ParameterName.ToString().Equals(Parameter, ESearchCase::IgnoreCase))
					{
						CollectionParam->ParameterId = Item.Id;
					}
				}
			}
		}
		if (UMaterialExpressionTransform* Transform = Cast<UMaterialExpressionTransform>(Expression))
		{
			auto Space = [](const FString& Name, bool bSource) -> int32
			{
				const FString Key = Name.ToLower();
				if (Key.Contains(TEXT("tangent"))) return bSource ? TRANSFORMSOURCE_Tangent : TRANSFORM_Tangent;
				if (Key.Contains(TEXT("local"))) return bSource ? TRANSFORMSOURCE_Local : TRANSFORM_Local;
				if (Key.Contains(TEXT("view"))) return bSource ? TRANSFORMSOURCE_View : TRANSFORM_View;
				return bSource ? TRANSFORMSOURCE_World : TRANSFORM_World;
			};
			Transform->TransformSourceType = static_cast<EMaterialVectorCoordTransformSource>(Space(ArgStr(Args, TEXT("source"), TEXT("world")), true));
			Transform->TransformType = static_cast<EMaterialVectorCoordTransform>(Space(ArgStr(Args, TEXT("dest"), TEXT("tangent")), false));
		}
	}

	UEdGraph* FindBlueprintGraph(UBlueprint* Blueprint, const FString& GraphName)
	{
		if (!Blueprint)
		{
			return nullptr;
		}
		auto Match = [&](UEdGraph* Graph)
		{
			return Graph && (GraphName.IsEmpty()
				? Graph->GetName().Contains(TEXT("EventGraph"))
				: Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase));
		};
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (Match(Graph))
			{
				return Graph;
			}
		}
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Match(Graph))
			{
				return Graph;
			}
		}
		if (GraphName.IsEmpty() && Blueprint->UbergraphPages.Num() > 0)
		{
			return Blueprint->UbergraphPages[0];
		}
		return nullptr;
	}

	void PlaceNode(UEdGraph* Graph, UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Args, const FString& Id)
	{
		Node->NodePosX = static_cast<int32>(ArgNum(Args, TEXT("x")));
		Node->NodePosY = static_cast<int32>(ArgNum(Args, TEXT("y")));
		Node->CreateNewGuid();
		Graph->AddNode(Node, false, false);
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		Node->NodeComment = Id;
	}

	UClass* ResolveClass(const FString& Path, UClass* Fallback)
	{
		if (Path.IsEmpty())
		{
			return Fallback;
		}
		if (UClass* Loaded = LoadClass<UObject>(nullptr, *Path))
		{
			return Loaded;
		}
		if (UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Path))
		{
			return Blueprint->GeneratedClass;
		}
		return nullptr;
	}

	FEdGraphPinType PinTypeFromName(const FString& TypeName, UClass* ObjectClass)
	{
		FEdGraphPinType PinType;
		const FString Key = TypeName.ToLower();
		if (Key == TEXT("bool") || Key == TEXT("boolean"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		}
		else if (Key == TEXT("int") || Key == TEXT("integer"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		}
		else if (Key == TEXT("float") || Key == TEXT("real") || Key == TEXT("double"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		}
		else if (Key == TEXT("string"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_String;
		}
		else if (Key == TEXT("name"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		}
		else if (Key == TEXT("vector"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
		}
		else if (Key == TEXT("rotator"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
		}
		else if (Key == TEXT("transform"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
		}
		else if (Key == TEXT("class"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
			PinType.PinSubCategoryObject = ObjectClass ? ObjectClass : AActor::StaticClass();
		}
		else
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			PinType.PinSubCategoryObject = ObjectClass ? ObjectClass : AActor::StaticClass();
		}
		return PinType;
	}

	UClass* ComponentClassFromName(const FString& Name)
	{
		const FString Key = Name.ToLower();
		if (Key == TEXT("sphere") || Key.Contains(TEXT("sphere"))) return USphereComponent::StaticClass();
		if (Key == TEXT("scene")) return USceneComponent::StaticClass();
		if (Key == TEXT("static_mesh") || Key.Contains(TEXT("staticmesh"))) return UStaticMeshComponent::StaticClass();
		if (Key == TEXT("box")) return LoadClass<UActorComponent>(nullptr, TEXT("/Script/Engine.BoxComponent"));
		if (Key == TEXT("capsule")) return LoadClass<UActorComponent>(nullptr, TEXT("/Script/Engine.CapsuleComponent"));
		if (Key == TEXT("camera")) return LoadClass<UActorComponent>(nullptr, TEXT("/Script/Engine.CameraComponent"));
		if (Key == TEXT("arrow")) return LoadClass<UActorComponent>(nullptr, TEXT("/Script/Engine.ArrowComponent"));
		if (Key == TEXT("point_light") || Key == TEXT("light")) return LoadClass<UActorComponent>(nullptr, TEXT("/Script/Engine.PointLightComponent"));
		if (Key == TEXT("audio")) return LoadClass<UActorComponent>(nullptr, TEXT("/Script/Engine.AudioComponent"));
		return ResolveClass(Name, nullptr);
	}

	UEdGraphNode* FindNode(UEdGraph* Graph, const FString& Id)
	{
		if (!Graph || Id.IsEmpty())
		{
			return nullptr;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeComment.Equals(Id, ESearchCase::IgnoreCase))
			{
				return Node;
			}
		}
		return nullptr;
	}

	UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& Name, EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}
		UEdGraphPin* Fallback = nullptr;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != Direction)
			{
				continue;
			}
			if (!Fallback)
			{
				Fallback = Pin;
			}
			if (!Name.IsEmpty() && Pin->PinName.ToString().Equals(Name, ESearchCase::IgnoreCase))
			{
				return Pin;
			}
		}
		return Name.IsEmpty() ? Fallback : nullptr;
	}

	FString PinList(const UEdGraphNode* Node)
	{
		FString Out;
		if (!Node)
		{
			return Out;
		}
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}
			Out += Pin->PinName.ToString();
			Out += Pin->Direction == EGPD_Input ? TEXT(":in ") : TEXT(":out ");
		}
		return Out;
	}

	FString EventFunctionName(const FString& Name)
	{
		const FString Key = Name.ToLower();
		if (Key == TEXT("begin_play") || Key == TEXT("beginplay")) return TEXT("ReceiveBeginPlay");
		if (Key == TEXT("tick")) return TEXT("ReceiveTick");
		if (Key == TEXT("begin_overlap") || Key == TEXT("overlap")) return TEXT("ReceiveActorBeginOverlap");
		if (Key == TEXT("end_overlap")) return TEXT("ReceiveActorEndOverlap");
		if (Key == TEXT("destroyed") || Key == TEXT("end_play")) return TEXT("ReceiveDestroyed");
		return Name;
	}

	FGuid FindBinding(UMovieScene* Movie, const FString& Label)
	{
		if (!Movie)
		{
			return FGuid();
		}
		for (int32 Index = 0; Index < Movie->GetPossessableCount(); ++Index)
		{
			FMovieScenePossessable& Possessable = Movie->GetPossessable(Index);
			if (Possessable.GetName().Equals(Label, ESearchCase::IgnoreCase))
			{
				return Possessable.GetGuid();
			}
		}
		return FGuid();
	}

	void SavePackage(UObject* Object)
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
}

FString FEditorPromptGraphs::ListMaterialExpressions(const FString& MaterialPath)
{
	FString Error;
	FString Note;
	UMaterial* Material = ResolveMaterial(MaterialPath, Error, Note);
	if (!Material)
	{
		return Error;
	}
	FString Out = Note.IsEmpty() ? FString() : Note + TEXT("\n");
	for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
	{
		if (!Expression)
		{
			continue;
		}
		Out += TEXT("desc=") + (Expression->Desc.IsEmpty() ? Expression->GetName() : Expression->Desc);
		Out += TEXT(" class=") + Expression->GetClass()->GetName() + TEXT("\n");
	}
	return Out.IsEmpty() ? TEXT("(no expressions)") : Out;
}

FString FEditorPromptGraphs::AddMaterialExpression(const FString& MaterialPath, const TSharedPtr<FJsonObject>& Args)
{
	FString Error;
	FString Note;
	UMaterial* Material = ResolveMaterial(MaterialPath, Error, Note);
	if (!Material)
	{
		return Error;
	}
	const FString ClassName = ArgStr(Args, TEXT("class"));
	UClass* Class = ExpressionClass(ClassName);
	if (!Class)
	{
		return TEXT("unknown expression class");
	}
	FString Desc = ArgStr(Args, TEXT("desc"));
	if (Desc.IsEmpty())
	{
		Desc = ClassName.IsEmpty() ? Class->GetName() : ClassName;
	}
	if (UMaterialExpression* Existing = FindExpr(Material, Desc))
	{
		ApplyExpressionFields(Existing, Args);
		UMaterialEditingLibrary::RecompileMaterial(Material);
		SavePackage(Material);
		return TEXT("expression exists desc=") + Desc + (Note.IsEmpty() ? FString() : TEXT("\n") + Note);
	}
	UMaterialExpression* Created = UMaterialEditingLibrary::CreateMaterialExpression(
		Material, Class, static_cast<int32>(ArgNum(Args, TEXT("x"), -200)), static_cast<int32>(ArgNum(Args, TEXT("y"))));
	if (!Created)
	{
		return TEXT("create expression failed");
	}
	Created->Desc = Desc;
	ApplyExpressionFields(Created, Args);
	UMaterialEditingLibrary::RecompileMaterial(Material);
	SavePackage(Material);
	FString Message = TEXT("added desc=") + Desc + TEXT(" class=") + Created->GetClass()->GetName();
	if (!Note.IsEmpty())
	{
		Message += TEXT("\n") + Note;
	}
	return Message;
}

FString FEditorPromptGraphs::ConnectMaterial(const FString& MaterialPath, const TSharedPtr<FJsonObject>& Args)
{
	FString Error;
	FString Note;
	UMaterial* Material = ResolveMaterial(MaterialPath, Error, Note);
	if (!Material)
	{
		return Error;
	}
	UMaterialExpression* From = FindExpr(Material, ArgStr(Args, TEXT("from")));
	if (!From)
	{
		return TEXT("from expression not found");
	}
	const FString PropertyName = ArgStr(Args, TEXT("property"));
	bool bLinked = false;
	if (!PropertyName.IsEmpty())
	{
		bool bProperty = false;
		const EMaterialProperty Property = PropertyByName(PropertyName, bProperty);
		if (!bProperty)
		{
			return TEXT("unknown material property");
		}
		bLinked = UMaterialEditingLibrary::ConnectMaterialProperty(From, ArgStr(Args, TEXT("from_pin")), Property);
	}
	else
	{
		UMaterialExpression* To = FindExpr(Material, ArgStr(Args, TEXT("to")));
		if (!To)
		{
			return TEXT("to expression not found");
		}
		bLinked = UMaterialEditingLibrary::ConnectMaterialExpressions(
			From, ArgStr(Args, TEXT("from_pin")), To, ArgStr(Args, TEXT("to_pin")));
	}
	if (!bLinked)
	{
		return TEXT("connect failed");
	}
	UMaterialEditingLibrary::RecompileMaterial(Material);
	SavePackage(Material);
	return TEXT("connected") + (Note.IsEmpty() ? FString() : TEXT("\n") + Note);
}

FString FEditorPromptGraphs::ListBlueprintNodes(const FString& Path, const FString& GraphName)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Path);
	UEdGraph* Graph = FindBlueprintGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return TEXT("blueprint event graph not found");
	}
	FString Out;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}
		Out += TEXT("id=") + (Node->NodeComment.IsEmpty() ? Node->GetName() : Node->NodeComment);
		Out += TEXT(" class=") + Node->GetClass()->GetName();
		Out += TEXT(" pins=") + PinList(Node) + TEXT("\n");
	}
	return Out.IsEmpty() ? TEXT("(no nodes)") : Out;
}

FString FEditorPromptGraphs::AddBlueprintNode(const FString& Path, const TSharedPtr<FJsonObject>& Args)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Path);
	UEdGraph* Graph = FindBlueprintGraph(Blueprint, ArgStr(Args, TEXT("graph")));
	if (!Blueprint || !Graph)
	{
		return TEXT("blueprint graph not found");
	}
	const FString Kind = ArgStr(Args, TEXT("kind"), TEXT("call")).ToLower();
	const FString Name = ArgStr(Args, TEXT("name"));
	const bool bNeedsName = Kind == TEXT("event") || Kind == TEXT("call") || Kind == TEXT("get") || Kind == TEXT("set")
		|| Kind == TEXT("custom_event") || Kind == TEXT("timeline");
	if (bNeedsName && Name.IsEmpty())
	{
		return TEXT("name is empty");
	}
	FString Id = ArgStr(Args, TEXT("id"));
	if (Id.IsEmpty())
	{
		Id = Name.IsEmpty() ? Kind : Name;
	}
	if (UEdGraphNode* Existing = FindNode(Graph, Id))
	{
		return TEXT("node exists id=") + Id + TEXT(" pins=") + PinList(Existing);
	}
	UClass* Parent = Blueprint->ParentClass ? Blueprint->ParentClass.Get() : AActor::StaticClass();
	UEdGraphNode* Created = nullptr;
	const UEdGraphSchema_K2* K2 = Cast<UEdGraphSchema_K2>(Graph->GetSchema());
	if (Kind == TEXT("branch"))
	{
		UK2Node_IfThenElse* Branch = NewObject<UK2Node_IfThenElse>(Graph);
		PlaceNode(Graph, Branch, Args, Id);
		Created = Branch;
	}
	else if (Kind == TEXT("sequence"))
	{
		UK2Node_ExecutionSequence* Sequence = NewObject<UK2Node_ExecutionSequence>(Graph);
		PlaceNode(Graph, Sequence, Args, Id);
		Created = Sequence;
	}
	else if (Kind == TEXT("self"))
	{
		UK2Node_Self* Self = NewObject<UK2Node_Self>(Graph);
		PlaceNode(Graph, Self, Args, Id);
		Created = Self;
	}
	else if (Kind == TEXT("cast"))
	{
		UClass* Target = ResolveClass(ArgStr(Args, TEXT("class")), nullptr);
		if (!Target)
		{
			return TEXT("cast class not found");
		}
		UK2Node_DynamicCast* CastNode = NewObject<UK2Node_DynamicCast>(Graph);
		CastNode->TargetType = Target;
		PlaceNode(Graph, CastNode, Args, Id);
		Created = CastNode;
	}
	else if (Kind == TEXT("get") || Kind == TEXT("set"))
	{
		if (!K2)
		{
			return TEXT("graph schema is not Blueprint");
		}
		UEdGraphNode* VarNode = Kind == TEXT("get")
			? static_cast<UEdGraphNode*>(K2->SpawnVariableGetNode(FVector2D(ArgNum(Args, TEXT("x")), ArgNum(Args, TEXT("y"))), Graph, *Name, Blueprint->SkeletonGeneratedClass))
			: static_cast<UEdGraphNode*>(K2->SpawnVariableSetNode(FVector2D(ArgNum(Args, TEXT("x")), ArgNum(Args, TEXT("y"))), Graph, *Name, Blueprint->SkeletonGeneratedClass));
		if (!VarNode)
		{
			return TEXT("variable not found");
		}
		VarNode->NodeComment = Id;
		Created = VarNode;
	}
	else if (Kind == TEXT("custom_event"))
	{
		UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(Graph);
		Event->CustomFunctionName = *Name;
		PlaceNode(Graph, Event, Args, Id);
		Created = Event;
	}
	else if (Kind == TEXT("spawn"))
	{
		UClass* SpawnClass = ResolveClass(ArgStr(Args, TEXT("class")), nullptr);
		if (!SpawnClass || !SpawnClass->IsChildOf(AActor::StaticClass()))
		{
			return TEXT("spawn class not found");
		}
		UK2Node_SpawnActor* Spawn = NewObject<UK2Node_SpawnActor>(Graph);
		PlaceNode(Graph, Spawn, Args, Id);
		if (UEdGraphPin* ClassPin = Spawn->GetBlueprintPin())
		{
			ClassPin->DefaultObject = SpawnClass;
			Spawn->PinDefaultValueChanged(ClassPin);
		}
		Created = Spawn;
	}
	else if (Kind == TEXT("timeline"))
	{
		if (!FBlueprintEditorUtils::AddNewTimeline(Blueprint, *Name))
		{
			return TEXT("timeline create failed");
		}
		UK2Node_Timeline* Timeline = NewObject<UK2Node_Timeline>(Graph);
		Timeline->TimelineName = *Name;
		PlaceNode(Graph, Timeline, Args, Id);
		Created = Timeline;
	}
	else if (Kind == TEXT("input"))
	{
		const FString ActionPath = ArgStr(Args, TEXT("action"));
		UObject* Action = LoadObject<UObject>(nullptr, *ActionPath);
		if (!FModuleManager::Get().LoadModule(TEXT("InputBlueprintNodes")))
		{
			return TEXT("Enhanced Input nodes are not available");
		}
		UClass* InputClass = FindObject<UClass>(nullptr, TEXT("/Script/InputBlueprintNodes.K2Node_EnhancedInputAction"));
		if (!Action || !InputClass)
		{
			return TEXT("input action not found");
		}
		UEdGraphNode* InputNode = NewObject<UEdGraphNode>(Graph, InputClass);
		if (FObjectProperty* Property = FindFProperty<FObjectProperty>(InputClass, TEXT("InputAction")))
		{
			Property->SetObjectPropertyValue_InContainer(InputNode, Action);
		}
		PlaceNode(Graph, InputNode, Args, Id);
		Created = InputNode;
	}
	else if (Kind == TEXT("delay") || Kind == TEXT("print") || Kind == TEXT("make_vector"))
	{
		UFunction* Function = nullptr;
		if (Kind == TEXT("delay"))
		{
			Function = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("Delay"));
		}
		else if (Kind == TEXT("print"))
		{
			Function = UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString"));
		}
		else
		{
			Function = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("MakeVector"));
		}
		if (!Function)
		{
			return TEXT("function not found");
		}
		UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
		Call->SetFromFunction(Function);
		PlaceNode(Graph, Call, Args, Id);
		if (ArgHas(Args, TEXT("value")))
		{
			const FString PinName = Kind == TEXT("delay") ? TEXT("Duration") : TEXT("InString");
			if (UEdGraphPin* Pin = FindPin(Call, PinName, EGPD_Input))
			{
				if (const UEdGraphSchema* Schema = Graph->GetSchema())
				{
					Schema->TrySetDefaultValue(*Pin, ArgStr(Args, TEXT("value")));
				}
			}
		}
		Created = Call;
	}
	else if (Kind == TEXT("event"))
	{
		const FString FunctionName = EventFunctionName(Name);
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
			{
				if (Event->EventReference.GetMemberName().ToString().Equals(FunctionName, ESearchCase::IgnoreCase))
				{
					Event->NodeComment = Id;
					return TEXT("node exists id=") + Id + TEXT(" pins=") + PinList(Event);
				}
			}
		}
		UFunction* Function = Parent->FindFunctionByName(*FunctionName);
		if (!Function)
		{
			Function = AActor::StaticClass()->FindFunctionByName(*FunctionName);
		}
		if (!Function)
		{
			return TEXT("event not found");
		}
		UK2Node_Event* Event = NewObject<UK2Node_Event>(Graph);
		Event->EventReference.SetExternalMember(Function->GetFName(), Function->GetOwnerClass());
		Event->bOverrideFunction = true;
		Event->NodePosX = static_cast<int32>(ArgNum(Args, TEXT("x")));
		Event->NodePosY = static_cast<int32>(ArgNum(Args, TEXT("y")));
		Event->CreateNewGuid();
		Graph->AddNode(Event, false, false);
		Event->PostPlacedNewNode();
		Event->AllocateDefaultPins();
		Event->NodeComment = Id;
		Created = Event;
	}
	else
	{
		const FString ClassPath = ArgStr(Args, TEXT("class"));
		UClass* Class = ClassPath.IsEmpty() ? Parent : LoadClass<UObject>(nullptr, *ClassPath);
		if (!Class)
		{
			Class = Parent;
		}
		UFunction* Function = Class->FindFunctionByName(*Name);
		if (!Function)
		{
			Function = AActor::StaticClass()->FindFunctionByName(*Name);
		}
		if (!Function)
		{
			Function = FindObject<UFunction>(nullptr, *Name);
		}
		if (!Function)
		{
			return TEXT("function not found");
		}
		UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
		Call->SetFromFunction(Function);
		Call->NodePosX = static_cast<int32>(ArgNum(Args, TEXT("x"), 300));
		Call->NodePosY = static_cast<int32>(ArgNum(Args, TEXT("y")));
		Call->CreateNewGuid();
		Graph->AddNode(Call, false, false);
		Call->PostPlacedNewNode();
		Call->AllocateDefaultPins();
		Call->NodeComment = Id;
		Created = Call;
	}
	if (!Created)
	{
		return TEXT("node was not created");
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SavePackage(Blueprint);
	return TEXT("added id=") + Id + TEXT(" pins=") + PinList(Created);
}

FString FEditorPromptGraphs::ConnectBlueprint(const FString& Path, const TSharedPtr<FJsonObject>& Args)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Path);
	UEdGraph* Graph = FindBlueprintGraph(Blueprint, ArgStr(Args, TEXT("graph")));
	if (!Graph)
	{
		return TEXT("blueprint event graph not found");
	}
	UEdGraphNode* From = FindNode(Graph, ArgStr(Args, TEXT("from")));
	UEdGraphNode* To = FindNode(Graph, ArgStr(Args, TEXT("to")));
	if (!From || !To)
	{
		return TEXT("node not found");
	}
	UEdGraphPin* FromPin = FindPin(From, ArgStr(Args, TEXT("from_pin")), EGPD_Output);
	UEdGraphPin* ToPin = FindPin(To, ArgStr(Args, TEXT("to_pin")), EGPD_Input);
	if (!FromPin || !ToPin)
	{
		return TEXT("pin not found from=") + PinList(From) + TEXT(" to=") + PinList(To);
	}
	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema || !Schema->TryCreateConnection(FromPin, ToPin))
	{
		return TEXT("connect failed from=") + PinList(From) + TEXT(" to=") + PinList(To);
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SavePackage(Blueprint);
	return TEXT("connected");
}

FString FEditorPromptGraphs::SetBlueprintPin(const FString& Path, const TSharedPtr<FJsonObject>& Args)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Path);
	UEdGraph* Graph = FindBlueprintGraph(Blueprint, ArgStr(Args, TEXT("graph")));
	UEdGraphNode* Node = FindNode(Graph, ArgStr(Args, TEXT("id")));
	if (!Node)
	{
		return TEXT("node not found");
	}
	UEdGraphPin* Pin = FindPin(Node, ArgStr(Args, TEXT("pin")), EGPD_Input);
	if (!Pin)
	{
		return TEXT("pin not found ") + PinList(Node);
	}
	const FString Value = ArgStr(Args, TEXT("value"));
	if (const UEdGraphSchema* Schema = Graph->GetSchema())
	{
		Schema->TrySetDefaultValue(*Pin, Value);
	}
	else
	{
		Pin->DefaultValue = Value;
	}
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	SavePackage(Blueprint);
	return TEXT("pin set");
}

FString FEditorPromptGraphs::ListBlueprintVariables(const FString& Path)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Path);
	if (!Blueprint)
	{
		return TEXT("blueprint not found");
	}
	FString Out;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		Out += Variable.VarName.ToString() + TEXT(" type=") + Variable.VarType.PinCategory.ToString();
		if (!Variable.DefaultValue.IsEmpty())
		{
			Out += TEXT(" default=") + Variable.DefaultValue;
		}
		Out += TEXT("\n");
	}
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node)
			{
				Out += TEXT("component=") + Node->GetVariableName().ToString();
				if (Node->ComponentClass)
				{
					Out += TEXT(" class=") + Node->ComponentClass->GetName();
				}
				Out += TEXT("\n");
			}
		}
	}
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph)
		{
			Out += TEXT("function=") + Graph->GetName() + TEXT("\n");
		}
	}
	return Out.IsEmpty() ? TEXT("(no variables)") : Out;
}

FString FEditorPromptGraphs::AddBlueprintVariable(const FString& Path, const TSharedPtr<FJsonObject>& Args)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Path);
	if (!Blueprint)
	{
		return TEXT("blueprint not found");
	}
	const FString Name = ArgStr(Args, TEXT("name"));
	if (Name.IsEmpty())
	{
		return TEXT("name is empty");
	}
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName.ToString().Equals(Name, ESearchCase::IgnoreCase))
		{
			return TEXT("variable exists ") + Name;
		}
	}
	UClass* ObjectClass = ResolveClass(ArgStr(Args, TEXT("class")), AActor::StaticClass());
	const FEdGraphPinType PinType = PinTypeFromName(ArgStr(Args, TEXT("type"), TEXT("float")), ObjectClass);
	if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, *Name, PinType, ArgStr(Args, TEXT("value"))))
	{
		return TEXT("add variable failed");
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SavePackage(Blueprint);
	return TEXT("variable ") + Name;
}

FString FEditorPromptGraphs::AddBlueprintComponent(const FString& Path, const TSharedPtr<FJsonObject>& Args)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Path);
	if (!Blueprint || !Blueprint->SimpleConstructionScript)
	{
		return TEXT("blueprint construction script not found");
	}
	UClass* ComponentClass = ComponentClassFromName(ArgStr(Args, TEXT("class")));
	if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
	{
		return TEXT("component class not found");
	}
	const FString Name = ArgStr(Args, TEXT("name"), ComponentClass->GetName());
	USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, *Name);
	if (!Node)
	{
		return TEXT("component create failed");
	}
	Blueprint->SimpleConstructionScript->AddNode(Node);
	if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Node->ComponentTemplate))
	{
		const FString Profile = ArgStr(Args, TEXT("profile"));
		if (!Profile.IsEmpty())
		{
			Primitive->SetCollisionProfileName(*Profile);
		}
	}
	if (USphereComponent* Sphere = Cast<USphereComponent>(Node->ComponentTemplate))
	{
		if (ArgHas(Args, TEXT("radius")))
		{
			Sphere->SetSphereRadius(static_cast<float>(ArgNum(Args, TEXT("radius"), 50)));
		}
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SavePackage(Blueprint);
	return TEXT("component ") + Node->GetVariableName().ToString();
}

FString FEditorPromptGraphs::AddBlueprintFunction(const FString& Path, const TSharedPtr<FJsonObject>& Args)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Path);
	if (!Blueprint)
	{
		return TEXT("blueprint not found");
	}
	const FString Name = ArgStr(Args, TEXT("name"));
	if (Name.IsEmpty())
	{
		return TEXT("name is empty");
	}
	if (FindBlueprintGraph(Blueprint, Name))
	{
		return TEXT("function exists ") + Name;
	}
	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, *Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	if (!Graph)
	{
		return TEXT("function graph failed");
	}
	FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SavePackage(Blueprint);
	return TEXT("function ") + Name;
}

FString FEditorPromptGraphs::ListLibraryAssets()
{
	FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(TEXT("/Game/Fab"));
	Filter.PackagePaths.Add(TEXT("/Game/Megascans"));
	Filter.PackagePaths.Add(TEXT("/Game/MSPresets"));
	TArray<FAssetData> Assets;
	Registry.Get().GetAssets(Filter, Assets);
	FString Out;
	int32 Shown = 0;
	for (const FAssetData& Asset : Assets)
	{
		if (Shown >= 40)
		{
			Out += TEXT("...\n");
			break;
		}
		const FString ClassName = Asset.AssetClassPath.GetAssetName().ToString();
		if (!ClassName.Contains(TEXT("StaticMesh")) && !ClassName.Contains(TEXT("Material"))
			&& !ClassName.Contains(TEXT("Texture")) && !ClassName.Contains(TEXT("Blueprint"))
			&& !ClassName.Contains(TEXT("World")))
		{
			continue;
		}
		Out += Asset.GetObjectPathString() + TEXT(" class=") + ClassName + TEXT("\n");
		++Shown;
	}
	if (Out.IsEmpty())
	{
		return TEXT("no Fab or Megascans assets in this project. open_asset_library, sign in, and add assets from there.");
	}
	return Out;
}

FString FEditorPromptGraphs::OpenAssetLibrary()
{
	FString Out;
	auto Open = [&](const TCHAR* ModuleName, const TCHAR* TabName)
	{
		if (!FModuleManager::Get().LoadModule(ModuleName))
		{
			return;
		}
		if (FGlobalTabmanager::Get()->TryInvokeTab(FName(TabName)).IsValid())
		{
			Out += FString(TabName) + TEXT(" opened\n");
		}
	};
	Open(TEXT("Fab"), TEXT("FabTab"));
	Open(TEXT("Bridge"), TEXT("BridgeTab"));
	if (Out.IsEmpty())
	{
		return TEXT("Fab and Bridge are not available in this editor. Enable the Fab plugin, then sign in to download Megascans.");
	}
	return Out + TEXT("Download needs an Epic sign-in inside that window. This tool cannot search or download by itself.");
}

FString FEditorPromptGraphs::AddSequenceKey(const FString& Path, const TSharedPtr<FJsonObject>& Args)
{
	ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *Path);
	UMovieScene* Movie = Sequence ? Sequence->GetMovieScene() : nullptr;
	if (!Movie)
	{
		return TEXT("sequence not found");
	}
	const FString Label = ArgStr(Args, TEXT("label"));
	const FGuid Guid = FindBinding(Movie, Label);
	if (!Guid.IsValid())
	{
		return TEXT("binding not found; sequence_bind_actor first");
	}
	Movie->Modify();
	UMovieScene3DTransformTrack* Track = Movie->FindTrack<UMovieScene3DTransformTrack>(Guid);
	if (!Track)
	{
		Track = Movie->AddTrack<UMovieScene3DTransformTrack>(Guid);
	}
	if (!Track)
	{
		return TEXT("transform track failed");
	}
	UMovieScene3DTransformSection* Section = nullptr;
	for (UMovieSceneSection* Existing : Track->GetAllSections())
	{
		Section = Cast<UMovieScene3DTransformSection>(Existing);
		if (Section)
		{
			break;
		}
	}
	if (!Section)
	{
		Section = Cast<UMovieScene3DTransformSection>(Track->CreateNewSection());
		if (!Section)
		{
			return TEXT("transform section failed");
		}
		Track->AddSection(*Section);
		Section->SetRange(TRange<FFrameNumber>::All());
	}
	FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
	TArrayView<FMovieSceneDoubleChannel*> Channels = Proxy.GetChannels<FMovieSceneDoubleChannel>();
	if (Channels.Num() < 3)
	{
		return TEXT("transform channels missing");
	}
	const FFrameNumber Frame(static_cast<int32>(ArgNum(Args, TEXT("frame"))));
	auto Key = [&](int32 Index, const TCHAR* Field)
	{
		if (ArgHas(Args, Field) && Channels.IsValidIndex(Index) && Channels[Index])
		{
			AddKeyToChannel(Channels[Index], Frame, ArgNum(Args, Field), EMovieSceneKeyInterpolation::Auto);
		}
	};
	Key(0, TEXT("x"));
	Key(1, TEXT("y"));
	Key(2, TEXT("z"));
	Key(3, TEXT("roll"));
	Key(4, TEXT("pitch"));
	Key(5, TEXT("yaw"));
	Key(6, TEXT("sx"));
	Key(7, TEXT("sy"));
	Key(8, TEXT("sz"));
	SavePackage(Sequence);
	return FString::Printf(TEXT("keyed %s frame=%d channels=%d"), *Label, Frame.Value, Channels.Num());
}

FString FEditorPromptGraphs::AddCameraCut(const FString& Path, const TSharedPtr<FJsonObject>& Args)
{
	ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *Path);
	UMovieScene* Movie = Sequence ? Sequence->GetMovieScene() : nullptr;
	if (!Movie)
	{
		return TEXT("sequence not found");
	}
	const FGuid Guid = FindBinding(Movie, ArgStr(Args, TEXT("label")));
	if (!Guid.IsValid())
	{
		return TEXT("binding not found; sequence_bind_actor first");
	}
	Movie->Modify();
	UMovieSceneCameraCutTrack* Track = Cast<UMovieSceneCameraCutTrack>(Movie->GetCameraCutTrack());
	if (!Track)
	{
		Track = Cast<UMovieSceneCameraCutTrack>(Movie->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
	}
	if (!Track)
	{
		return TEXT("camera cut track failed");
	}
	const int32 Start = static_cast<int32>(ArgNum(Args, TEXT("start")));
	const int32 End = static_cast<int32>(ArgNum(Args, TEXT("end"), Start + 100));
	UMovieSceneCameraCutSection* Section = Track->AddNewCameraCut(UE::MovieScene::FRelativeObjectBindingID(Guid), FFrameNumber(Start));
	if (!Section)
	{
		return TEXT("camera cut failed");
	}
	Section->SetRange(TRange<FFrameNumber>::Inclusive(FFrameNumber(Start), FFrameNumber(FMath::Max(Start, End))));
	SavePackage(Sequence);
	return FString::Printf(TEXT("camera cut %d-%d"), Start, End);
}
