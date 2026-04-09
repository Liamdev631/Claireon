// Copyright (c) 2026 The Claireon Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "Tools/IClaireonTool.h"

/**
 * MCP tool for transaction management: undo, redo, history, and grouping.
 * Provides AI clients with access to Unreal's undo/redo system.
 */
class ClaireonTool_Transaction : public IClaireonTool
{
public:
	virtual FString GetName() const override;
	virtual FString GetDescription() const override;
	virtual FString GetFullDescription() const override;
	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresNoPIE() const override { return true; }

	/** Reset group state. Called from FClaireonServer::HandleInitialized() and module shutdown. */
	static void ResetGroupState();

private:
	// Group state -- safe as static because FClaireonServer is single-instance, single-session.
	static bool bGroupActive;
	static FString ActiveGroupLabel;

	// Operation handlers
	FToolResult Operation_Undo(const TSharedPtr<FJsonObject>& Arguments);
	FToolResult Operation_Redo(const TSharedPtr<FJsonObject>& Arguments);
	FToolResult Operation_History(const TSharedPtr<FJsonObject>& Arguments);
	FToolResult Operation_BeginGroup(const TSharedPtr<FJsonObject>& Arguments);
	FToolResult Operation_EndGroup(const TSharedPtr<FJsonObject>& Arguments);
	FToolResult Operation_RollbackGroup(const TSharedPtr<FJsonObject>& Arguments);
};
