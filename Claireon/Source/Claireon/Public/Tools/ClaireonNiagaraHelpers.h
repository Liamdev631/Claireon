// Copyright (c) 2026 The Claireon Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"

class UNiagaraSystem;
class UNiagaraRendererProperties;
class UNiagaraNodeOutput;
class UNiagaraNodeFunctionCall;
class UNiagaraScript;
struct FNiagaraEmitterHandle;
struct FVersionedNiagaraEmitterData;

/**
 * Helper functions for Niagara MCP tools.
 * Provides asset loading, structure formatting, class resolution, and property setting.
 */
namespace ClaireonNiagaraHelpers
{
	/** Load a UNiagaraSystem from an asset path. Returns nullptr and fills OutError on failure. */
	UNiagaraSystem* LoadNiagaraSystemAsset(const FString& AssetPath, FString& OutError);

	/** Format the full structure of a Niagara System as human-readable text. */
	FString FormatNiagaraSystemStructure(const UNiagaraSystem* System, bool bFullDetail = true);

	/** Format a single emitter's structure. System pointer enables module stack listing. */
	FString FormatEmitterStructure(const UNiagaraSystem* System, const FNiagaraEmitterHandle& EmitterHandle, int32 EmitterIndex, bool bFullDetail = true);

	/** Format a renderer's properties as text. */
	FString FormatRendererProperties(const UNiagaraRendererProperties* Renderer, int32 RendererIndex, const FString& Indent = TEXT("    "));

	/** Format user-exposed parameters from the system. */
	FString FormatUserParameters(const UNiagaraSystem* System);

	/** Resolve a shorthand renderer type name to a UClass. Returns nullptr and fills OutError on failure. */
	UClass* ResolveRendererClass(const FString& TypeName, FString& OutError);

	/** Get a human-readable name for a renderer class (e.g. "Sprite Renderer"). */
	FString GetRendererTypeName(const UNiagaraRendererProperties* Renderer);

	/** Set a property on a UObject via reflection. Returns true on success. */
	bool SetObjectProperty(UObject* Object, const FString& PropertyName, const FString& PropertyValue, FString& OutError);

	/** Format properties of a UObject (non-default, non-transient) as indented text. */
	FString FormatObjectProperties(const UObject* Object, const FString& Indent);

	// ========================================================================
	// Stack Resolution + Graph Traversal (Stage 001)
	// ========================================================================

	/** Map human-readable stack name to ENiagaraScriptUsage. Returns false if name is invalid. */
	bool ResolveStackName(const FString& StackName, ENiagaraScriptUsage& OutUsage, FString& OutError);

	/** Navigate from system + emitter index + stack to the output node. Returns nullptr on error. */
	UNiagaraNodeOutput* GetStackOutputNode(UNiagaraSystem* System, int32 EmitterIndex, ENiagaraScriptUsage Usage, FString& OutError);

	/** Get ordered module function call nodes from a stack. Returns false on error. */
	bool GetOrderedModuleNodes(UNiagaraSystem* System, int32 EmitterIndex, ENiagaraScriptUsage Usage, TArray<UNiagaraNodeFunctionCall*>& OutModuleNodes, FString& OutError);

	/** Resolve a module name (full path or short name) to a UNiagaraScript*. Returns nullptr on error. */
	UNiagaraScript* ResolveModuleScript(const FString& ModuleNameOrPath, FString& OutError);

	/** Format a module node's info (name + key inputs) as a string for status output. */
	FString FormatModuleInfo(UNiagaraNodeFunctionCall* ModuleNode, bool bIncludeInputs = true);
}
