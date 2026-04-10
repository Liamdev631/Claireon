// Copyright (c) 2026 The Claireon Contributors
// SPDX-License-Identifier: MIT

#pragma once
#include "CoreMinimal.h"

namespace ClaireonPathResolver
{
	/** Classification of what type of Unreal path was resolved. */
	enum class EPathKind : uint8
	{
		/** Standard asset package path (e.g. /Game/Characters/BP_Player) */
		PackagePath,
		/** Native class reference (e.g. /Script/Engine.Actor) */
		NativeClassPath,
	};

	struct FResolvedPath
	{
		// The normalized path (e.g. /Game/Characters/BP_Player or /Script/Engine.Actor)
		FString Path;

		// Classification of what type of path was resolved
		EPathKind Kind = EPathKind::PackagePath;

		// If the input had a _C suffix that was stripped, this is true (only relevant for PackagePath kind)
		bool bIsClassReference = false;

		// The original input for error reporting
		FString OriginalInput;

		// Human-readable description of which normalization steps were applied (for debugging)
		FString NormalizationTrace;
	};

	struct FResolveResult
	{
		// Whether resolution succeeded
		bool bSuccess = false;

		// The resolved path (only valid if bSuccess is true)
		FResolvedPath ResolvedPath;

		// Error message if resolution failed (empty on success)
		FString Error;
	};

	/**
	 * Resolve any user-provided path into a canonical Unreal path.
	 * Returns a result struct containing success/failure, the resolved path, and any error.
	 *
	 * Threading: Must be called on the game thread if _C disambiguation via
	 * AssetRegistry lookup is needed. Off-thread calls will do best-effort
	 * _C stripping without registry validation.
	 */
	CLAIREON_API FResolveResult Resolve(const FString& InPath);
}
