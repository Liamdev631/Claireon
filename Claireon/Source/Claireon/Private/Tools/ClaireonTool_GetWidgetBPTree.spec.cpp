// Copyright (c) 2026 The Claireon Contributors
// SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "Tools/ClaireonTool_GetWidgetBPTree.h"
#include "Tools/ClaireonTool_EditWidgetBP.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGetWidgetBPTreeTest_BasicRead,
	"Claireon.GetWidgetBPTree.BasicRead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGetWidgetBPTreeTest_BasicRead::RunTest(const FString& Parameters)
{
	const FString PackageName = TEXT("/Game/__MCPTests/WBP_ReadTest");
	const FString AssetName = TEXT("WBP_ReadTest");

	// --- Setup: delete any leftover asset from a prior failed run ---
	{
		UPackage* ExistingPkg = FindPackage(nullptr, *PackageName);
		if (ExistingPkg)
		{
			TArray<UObject*> ObjectsInPkg;
			GetObjectsWithOuter(ExistingPkg, ObjectsInPkg, false);
			for (UObject* Obj : ObjectsInPkg)
			{
				if (Obj)
				{
					Obj->ClearFlags(RF_Standalone);
				}
			}
			ExistingPkg->ClearFlags(RF_Standalone);
		}
	}

	// --- Create a test Widget Blueprint with a CanvasPanel root and a TextBlock child ---
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		AddError(TEXT("Failed to create test package"));
		return false;
	}

	UWidgetBlueprint* WBP = CastChecked<UWidgetBlueprint>(
		FKismetEditorUtilities::CreateBlueprint(
			UUserWidget::StaticClass(),
			Package,
			FName(*AssetName),
			BPTYPE_Normal,
			UWidgetBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			NAME_None));

	if (!WBP)
	{
		AddError(TEXT("Failed to create test Widget Blueprint"));
		return false;
	}

	if (!WBP->WidgetTree)
	{
		AddError(TEXT("Widget Blueprint has no WidgetTree"));
		return false;
	}

	// Build a simple hierarchy: CanvasPanel (root) -> TextBlock
	UCanvasPanel* Root = WBP->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("TestCanvas"));
	if (!Root)
	{
		AddError(TEXT("Failed to create root CanvasPanel"));
		return false;
	}
	WBP->WidgetTree->RootWidget = Root;

	UTextBlock* Text = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TestText"));
	if (!Text)
	{
		AddError(TEXT("Failed to create TestText TextBlock"));
		return false;
	}
	Root->AddChild(Text);

	// Notify asset registry so LoadObject can find the in-memory asset
	FAssetRegistryModule::AssetCreated(WBP);
	Package->MarkPackageDirty();

	// --- Execute the tool ---
	ClaireonTool_GetWidgetBPTree Tool;

	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("asset_path"), PackageName);

	auto Result = Tool.Execute(Args);

	if (Result.bIsError)
	{
		AddError(FString::Printf(TEXT("GetWidgetBPTree returned an error: %s"), *Result.GetContentAsString()));
		return false;
	}

	AddInfo(TEXT("GetWidgetBPTree succeeded without error"));

	// --- Parse the result as JSON ---
	FString ResultText = Result.GetContentAsString();
	TSharedPtr<FJsonObject> ResultJson;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResultText);
	if (!FJsonSerializer::Deserialize(Reader, ResultJson) || !ResultJson.IsValid())
	{
		AddError(FString::Printf(TEXT("Failed to parse result as JSON: %s"), *ResultText));
		return false;
	}

	// --- Verify "root" object is present ---
	const TSharedPtr<FJsonObject>* RootObjPtr = nullptr;
	if (!ResultJson->TryGetObjectField(TEXT("root"), RootObjPtr) || !RootObjPtr || !(*RootObjPtr).IsValid())
	{
		AddError(TEXT("Result JSON is missing 'root' object"));
		return false;
	}
	const TSharedPtr<FJsonObject>& RootObj = *RootObjPtr;

	// --- Verify root has "name" field ---
	FString RootName;
	if (!RootObj->TryGetStringField(TEXT("name"), RootName))
	{
		AddError(TEXT("Root widget is missing 'name' field"));
		return false;
	}
	AddInfo(FString::Printf(TEXT("Root widget name: %s"), *RootName));

	// --- Verify root has "class" field ---
	FString RootClass;
	if (!RootObj->TryGetStringField(TEXT("class"), RootClass))
	{
		AddError(TEXT("Root widget is missing 'class' field"));
		return false;
	}
	AddInfo(FString::Printf(TEXT("Root widget class: %s"), *RootClass));

	TestTrue("Root class should be CanvasPanel", RootClass.Contains(TEXT("CanvasPanel")));

	// --- Verify root has "children" array containing "TestText" ---
	const TArray<TSharedPtr<FJsonValue>>* ChildrenArray = nullptr;
	if (!RootObj->TryGetArrayField(TEXT("children"), ChildrenArray) || !ChildrenArray)
	{
		AddError(TEXT("Root widget is missing 'children' array"));
		return false;
	}

	bool bFoundTestText = false;
	for (const TSharedPtr<FJsonValue>& ChildValue : *ChildrenArray)
	{
		const TSharedPtr<FJsonObject>* ChildObjPtr = nullptr;
		if (!ChildValue->TryGetObject(ChildObjPtr) || !ChildObjPtr)
		{
			continue;
		}

		FString ChildName;
		if ((*ChildObjPtr)->TryGetStringField(TEXT("name"), ChildName) && ChildName == TEXT("TestText"))
		{
			bFoundTestText = true;
			break;
		}
	}

	TestTrue("Children array should contain 'TestText'", bFoundTestText);

	// --- Cleanup: mark the in-memory objects for GC ---
	WBP->ClearFlags(RF_Standalone);
	Package->ClearFlags(RF_Standalone);

	AddInfo(TEXT("GetWidgetBPTree.BasicRead passed"));
	return true;
}

// ===========================================================================
// Test: MVVMBindingsInTree
// Verify include_mvvm_bindings flag controls MVVM data in tree output.
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGetWidgetBPTreeTest_MVVMBindingsInTree,
	"Claireon.GetWidgetBPTree.MVVMBindingsInTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGetWidgetBPTreeTest_MVVMBindingsInTree::RunTest(const FString& Parameters)
{
	const FString AssetPath = TEXT("/Game/__MCPTests/WBP_MVVMTreeTest");

	// --- Step 1: Create WBP and add MVVM viewmodel using EditWidgetBP ---
	ClaireonTool_EditWidgetBP EditTool;

	// Create WBP
	TSharedPtr<FJsonObject> CreateArgs = MakeShared<FJsonObject>();
	CreateArgs->SetStringField(TEXT("operation"), TEXT("create"));
	TSharedPtr<FJsonObject> CreateParams = MakeShared<FJsonObject>();
	CreateParams->SetStringField(TEXT("asset_path"), AssetPath);
	CreateParams->SetStringField(TEXT("root_widget_class"), TEXT("CanvasPanel"));
	CreateArgs->SetObjectField(TEXT("params"), CreateParams);

	auto CreateResult = EditTool.Execute(CreateArgs);
	if (CreateResult.bIsError)
	{
		AddError(FString::Printf(TEXT("Failed to create WBP: %s"), *CreateResult.GetContentAsString()));
		return false;
	}

	// Parse session_id
	FString SessionId;
	{
		TSharedPtr<FJsonObject> Json;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CreateResult.GetContentAsString());
		if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
		{
			Json->TryGetStringField(TEXT("session_id"), SessionId);
		}
	}
	if (SessionId.IsEmpty())
	{
		AddError(TEXT("No session_id in create response"));
		return false;
	}

	// Helper to execute operations
	auto ExecOp = [&](const FString& Op, const TSharedPtr<FJsonObject>& Params) -> IClaireonTool::FToolResult
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("operation"), Op);
		Args->SetStringField(TEXT("session_id"), SessionId);
		Args->SetObjectField(TEXT("params"), Params);
		return EditTool.Execute(Args);
	};

	// Add viewmodel
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("viewmodel_name"), TEXT("TreeTestVM"));
		Params->SetStringField(TEXT("viewmodel_class"), TEXT("MVVMViewModelBase"));

		auto Result = ExecOp(TEXT("add_mvvm_viewmodel"), Params);
		TestFalse("add_mvvm_viewmodel should succeed", Result.bIsError);
		if (Result.bIsError)
		{
			AddError(FString::Printf(TEXT("Failed to add viewmodel: %s"), *Result.GetContentAsString()));
			ExecOp(TEXT("close"), MakeShared<FJsonObject>());
			return false;
		}
	}

	// Save (compile) and close
	{
		auto Result = ExecOp(TEXT("compile"), MakeShared<FJsonObject>());
		TestFalse("compile should succeed", Result.bIsError);
	}

	// Save
	{
		auto Result = ExecOp(TEXT("save"), MakeShared<FJsonObject>());
		if (Result.bIsError)
		{
			AddInfo(FString::Printf(TEXT("save returned: %s (non-fatal)"), *Result.GetContentAsString()));
		}
	}

	// Close session
	ExecOp(TEXT("close"), MakeShared<FJsonObject>());

	// --- Step 2: Call get_widget_tree WITH include_mvvm_bindings=true ---
	ClaireonTool_GetWidgetBPTree TreeTool;
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), AssetPath);
		Args->SetBoolField(TEXT("include_mvvm_bindings"), true);

		auto Result = TreeTool.Execute(Args);
		TestFalse("get_widget_tree with include_mvvm_bindings=true should succeed", Result.bIsError);

		if (!Result.bIsError)
		{
			TSharedPtr<FJsonObject> Json;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.GetContentAsString());
			FJsonSerializer::Deserialize(Reader, Json);

			if (Json.IsValid())
			{
				const TArray<TSharedPtr<FJsonValue>>* VMArray = nullptr;
				bool bHasViewModels = Json->TryGetArrayField(TEXT("mvvm_viewmodels"), VMArray);
				TestTrue("Response should contain 'mvvm_viewmodels' array", bHasViewModels);

				if (bHasViewModels && VMArray)
				{
					TestTrue("mvvm_viewmodels array should have at least 1 entry", VMArray->Num() >= 1);
					AddInfo(FString::Printf(TEXT("mvvm_viewmodels count: %d"), VMArray->Num()));
				}
			}
			else
			{
				AddError(TEXT("Failed to parse tree result as JSON"));
			}
		}
	}

	// --- Step 3: Call get_widget_tree WITHOUT include_mvvm_bindings ---
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("asset_path"), AssetPath);
		// include_mvvm_bindings defaults to false

		auto Result = TreeTool.Execute(Args);
		TestFalse("get_widget_tree without include_mvvm_bindings should succeed", Result.bIsError);

		if (!Result.bIsError)
		{
			TSharedPtr<FJsonObject> Json;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.GetContentAsString());
			FJsonSerializer::Deserialize(Reader, Json);

			if (Json.IsValid())
			{
				const TArray<TSharedPtr<FJsonValue>>* VMArray = nullptr;
				bool bHasViewModels = Json->TryGetArrayField(TEXT("mvvm_viewmodels"), VMArray);
				TestFalse("Response should NOT contain 'mvvm_viewmodels' when flag is false", bHasViewModels);
				AddInfo(TEXT("Verified mvvm_viewmodels absent when include_mvvm_bindings is false"));
			}
			else
			{
				AddError(TEXT("Failed to parse tree result as JSON"));
			}
		}
	}

	AddInfo(TEXT("GetWidgetBPTree.MVVMBindingsInTree passed"));
	return true;
}
