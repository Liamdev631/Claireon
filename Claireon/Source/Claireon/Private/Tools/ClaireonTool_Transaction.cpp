// Copyright (c) 2026 The Claireon Contributors
// SPDX-License-Identifier: MIT

#include "Tools/ClaireonTool_Transaction.h"
#include "ClaireonLog.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Editor/TransBuffer.h"

using FToolResult = IClaireonTool::FToolResult;

// Static member initialization
bool ClaireonTool_Transaction::bGroupActive = false;
FString ClaireonTool_Transaction::ActiveGroupLabel;

// ============================================================================
// Tool Identity
// ============================================================================

FString ClaireonTool_Transaction::GetName() const
{
	return TEXT("claireon.transaction");
}

FString ClaireonTool_Transaction::GetDescription() const
{
	return TEXT("Undo/redo transactions, view history, and group multiple operations into a single undo step. "
				"Use 'undo'/'redo' to step through history, 'history' to list recent transactions, "
				"and 'begin_group'/'end_group' to batch operations atomically.");
}

FString ClaireonTool_Transaction::GetFullDescription() const
{
	return TEXT("Manage the editor's undo/redo transaction system.\n\n"
				"Operations:\n"
				"- undo: Undo the last N transactions. Returns the descriptions of undone entries.\n"
				"- redo: Redo the last N undone transactions.\n"
				"- history: List recent transactions. Use filter='claireon' to show only [Claireon]-prefixed entries.\n"
				"- begin_group: Start a transaction group. All subsequent tool calls are grouped into one undo step.\n"
				"- end_group: End the active group. The group appears as a single entry in undo history.\n"
				"- rollback_group: Cancel the active group and undo everything in it.\n\n"
				"Examples:\n"
				"  {\"operation\": \"undo\", \"count\": 1}\n"
				"  {\"operation\": \"redo\", \"count\": 3}\n"
				"  {\"operation\": \"history\", \"count\": 20, \"filter\": \"claireon\"}\n"
				"  {\"operation\": \"begin_group\", \"label\": \"Populate enemy DataTable\"}\n"
				"  {\"operation\": \"end_group\"}\n"
				"  {\"operation\": \"rollback_group\"}");
}

// ============================================================================
// Input Schema
// ============================================================================

TSharedPtr<FJsonObject> ClaireonTool_Transaction::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	// operation (required)
	TSharedPtr<FJsonObject> OperationProp = MakeShared<FJsonObject>();
	OperationProp->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> OperationEnum;
	OperationEnum.Add(MakeShared<FJsonValueString>(TEXT("undo")));
	OperationEnum.Add(MakeShared<FJsonValueString>(TEXT("redo")));
	OperationEnum.Add(MakeShared<FJsonValueString>(TEXT("history")));
	OperationEnum.Add(MakeShared<FJsonValueString>(TEXT("begin_group")));
	OperationEnum.Add(MakeShared<FJsonValueString>(TEXT("end_group")));
	OperationEnum.Add(MakeShared<FJsonValueString>(TEXT("rollback_group")));
	OperationProp->SetArrayField(TEXT("enum"), OperationEnum);
	OperationProp->SetStringField(TEXT("description"), TEXT("The transaction operation to perform."));
	Properties->SetObjectField(TEXT("operation"), OperationProp);

	// count (optional)
	TSharedPtr<FJsonObject> CountProp = MakeShared<FJsonObject>();
	CountProp->SetStringField(TEXT("type"), TEXT("integer"));
	CountProp->SetStringField(TEXT("description"),
		TEXT("For undo/redo: number of transactions to undo/redo (default 1). "
			 "For history: number of entries to return (default 20, max 100)."));
	Properties->SetObjectField(TEXT("count"), CountProp);

	// label (required for begin_group)
	TSharedPtr<FJsonObject> LabelProp = MakeShared<FJsonObject>();
	LabelProp->SetStringField(TEXT("type"), TEXT("string"));
	LabelProp->SetStringField(TEXT("description"),
		TEXT("Descriptive label for the group. Required for begin_group. "
			 "The [Claireon] prefix is prepended automatically."));
	Properties->SetObjectField(TEXT("label"), LabelProp);

	// filter (optional for history)
	TSharedPtr<FJsonObject> FilterProp = MakeShared<FJsonObject>();
	FilterProp->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> FilterEnum;
	FilterEnum.Add(MakeShared<FJsonValueString>(TEXT("all")));
	FilterEnum.Add(MakeShared<FJsonValueString>(TEXT("claireon")));
	FilterProp->SetArrayField(TEXT("enum"), FilterEnum);
	FilterProp->SetStringField(TEXT("description"),
		TEXT("For history: 'all' returns all entries, 'claireon' returns only [Claireon]-prefixed entries. Default: 'all'."));
	Properties->SetObjectField(TEXT("filter"), FilterProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("operation")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

// ============================================================================
// Execute -- dispatch to operation handlers
// ============================================================================

FToolResult ClaireonTool_Transaction::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	FString Operation;
	if (!Arguments->TryGetStringField(TEXT("operation"), Operation) || Operation.IsEmpty())
	{
		return MakeErrorResult(TEXT("Missing required parameter: operation"));
	}

	if (Operation == TEXT("undo"))
		return Operation_Undo(Arguments);
	if (Operation == TEXT("redo"))
		return Operation_Redo(Arguments);
	if (Operation == TEXT("history"))
		return Operation_History(Arguments);
	if (Operation == TEXT("begin_group"))
		return Operation_BeginGroup(Arguments);
	if (Operation == TEXT("end_group"))
		return Operation_EndGroup(Arguments);
	if (Operation == TEXT("rollback_group"))
		return Operation_RollbackGroup(Arguments);

	return MakeErrorResult(FString::Printf(TEXT("Unknown operation: %s"), *Operation));
}

// ============================================================================
// Operation: undo
// ============================================================================

FToolResult ClaireonTool_Transaction::Operation_Undo(const TSharedPtr<FJsonObject>& Arguments)
{
	if (!GEditor)
	{
		return MakeErrorResult(TEXT("GEditor is not available"));
	}

	int32 Count = 1;
	if (Arguments->HasField(TEXT("count")))
	{
		Count = FMath::Max(1, static_cast<int32>(Arguments->GetNumberField(TEXT("count"))));
	}

	TArray<TSharedPtr<FJsonValue>> UndoneNames;
	int32 UndoneCount = 0;

	for (int32 i = 0; i < Count; ++i)
	{
		if (!GEditor->UndoTransaction())
		{
			break;
		}
		++UndoneCount;

		// Get the description of what was just undone from the redo buffer position
		FString Description = TEXT("(unknown)");
		if (UTransBuffer* TransBuffer = Cast<UTransBuffer>(GEditor->Trans))
		{
			// After undo, the undone transaction is now at UndoBuffer[UndoBuffer.Num() - UndoCount]
			// where UndoCount tracks how many items have been undone
			int32 UndoBufferNum = TransBuffer->GetUndoCount();
			int32 BufferSize = TransBuffer->GetQueueLength();
			int32 UndoneIndex = BufferSize - UndoBufferNum;
			if (UndoneIndex >= 0 && UndoneIndex < BufferSize)
			{
				const FTransaction* Transaction = TransBuffer->GetTransaction(UndoneIndex);
				if (Transaction)
				{
					Description = Transaction->GetTitle().ToString();
				}
			}
		}
		UndoneNames.Add(MakeShared<FJsonValueString>(Description));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("undone_count"), UndoneCount);
	Result->SetArrayField(TEXT("transactions"), UndoneNames);

	return MakeSuccessResult(Result, FString::Printf(TEXT("Undid %d transaction(s)"), UndoneCount));
}

// ============================================================================
// Operation: redo
// ============================================================================

FToolResult ClaireonTool_Transaction::Operation_Redo(const TSharedPtr<FJsonObject>& Arguments)
{
	if (!GEditor)
	{
		return MakeErrorResult(TEXT("GEditor is not available"));
	}

	int32 Count = 1;
	if (Arguments->HasField(TEXT("count")))
	{
		Count = FMath::Max(1, static_cast<int32>(Arguments->GetNumberField(TEXT("count"))));
	}

	TArray<TSharedPtr<FJsonValue>> RedoneNames;
	int32 RedoneCount = 0;

	for (int32 i = 0; i < Count; ++i)
	{
		// Before redo, get the description of what will be redone
		FString Description = TEXT("(unknown)");
		if (UTransBuffer* TransBuffer = Cast<UTransBuffer>(GEditor->Trans))
		{
			int32 UndoCount = TransBuffer->GetUndoCount();
			if (UndoCount > 0)
			{
				int32 BufferSize = TransBuffer->GetQueueLength();
				int32 RedoIndex = BufferSize - UndoCount;
				if (RedoIndex >= 0 && RedoIndex < BufferSize)
				{
					const FTransaction* Transaction = TransBuffer->GetTransaction(RedoIndex);
					if (Transaction)
					{
						Description = Transaction->GetTitle().ToString();
					}
				}
			}
		}

		if (!GEditor->RedoTransaction())
		{
			break;
		}
		++RedoneCount;
		RedoneNames.Add(MakeShared<FJsonValueString>(Description));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("redone_count"), RedoneCount);
	Result->SetArrayField(TEXT("transactions"), RedoneNames);

	return MakeSuccessResult(Result, FString::Printf(TEXT("Redid %d transaction(s)"), RedoneCount));
}

// ============================================================================
// Operation: history
// ============================================================================

FToolResult ClaireonTool_Transaction::Operation_History(const TSharedPtr<FJsonObject>& Arguments)
{
	if (!GEditor)
	{
		return MakeErrorResult(TEXT("GEditor is not available"));
	}

	UTransBuffer* TransBuffer = Cast<UTransBuffer>(GEditor->Trans);
	if (!TransBuffer)
	{
		return MakeErrorResult(TEXT("Failed to access transaction buffer"));
	}

	int32 Count = 20;
	if (Arguments->HasField(TEXT("count")))
	{
		Count = FMath::Clamp(static_cast<int32>(Arguments->GetNumberField(TEXT("count"))), 1, 100);
	}

	FString Filter = TEXT("all");
	Arguments->TryGetStringField(TEXT("filter"), Filter);
	bool bFilterClaireon = Filter == TEXT("claireon");

	int32 BufferSize = TransBuffer->GetQueueLength();
	int32 UndoCount = TransBuffer->GetUndoCount();

	TArray<TSharedPtr<FJsonValue>> Entries;

	// Iterate from most recent to oldest
	for (int32 i = BufferSize - 1; i >= 0 && Entries.Num() < Count; --i)
	{
		const FTransaction* Transaction = TransBuffer->GetTransaction(i);
		if (!Transaction)
		{
			continue;
		}

		FString Description = Transaction->GetTitle().ToString();

		if (bFilterClaireon && !Description.StartsWith(TEXT("[Claireon]")))
		{
			continue;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("index"), i);
		Entry->SetStringField(TEXT("description"), Description);

		// Mark whether this entry is in the "undone" region (available for redo)
		bool bIsUndone = i >= (BufferSize - UndoCount);
		Entry->SetBoolField(TEXT("is_undone"), bIsUndone);

		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("entries"), Entries);
	Result->SetNumberField(TEXT("total_in_buffer"), BufferSize);
	Result->SetNumberField(TEXT("undo_count"), UndoCount);

	return MakeSuccessResult(Result, FString::Printf(TEXT("Returned %d history entries (%d total in buffer)"), Entries.Num(), BufferSize));
}

// ============================================================================
// Operation: begin_group
// ============================================================================

FToolResult ClaireonTool_Transaction::Operation_BeginGroup(const TSharedPtr<FJsonObject>& Arguments)
{
	if (!GEditor)
	{
		return MakeErrorResult(TEXT("GEditor is not available"));
	}

	if (bGroupActive)
	{
		return MakeErrorResult(FString::Printf(TEXT("A group is already active: '%s'. Call end_group or rollback_group first."), *ActiveGroupLabel));
	}

	FString Label;
	if (!Arguments->TryGetStringField(TEXT("label"), Label) || Label.IsEmpty())
	{
		return MakeErrorResult(TEXT("Missing required parameter: label (required for begin_group)"));
	}

	FString FullLabel = FString::Printf(TEXT("[Claireon] %s"), *Label);
	GEditor->BeginTransaction(FText::FromString(FullLabel));

	bGroupActive = true;
	ActiveGroupLabel = Label;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("group_started"), true);
	Result->SetStringField(TEXT("label"), FullLabel);

	return MakeSuccessResult(Result, FString::Printf(TEXT("Started transaction group: %s"), *FullLabel));
}

// ============================================================================
// Operation: end_group
// ============================================================================

FToolResult ClaireonTool_Transaction::Operation_EndGroup(const TSharedPtr<FJsonObject>& Arguments)
{
	if (!GEditor)
	{
		return MakeErrorResult(TEXT("GEditor is not available"));
	}

	if (!bGroupActive)
	{
		return MakeErrorResult(TEXT("No active group to end."));
	}

	GEditor->EndTransaction();

	FString EndedLabel = ActiveGroupLabel;
	bGroupActive = false;
	ActiveGroupLabel.Empty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("group_ended"), true);

	return MakeSuccessResult(Result, FString::Printf(TEXT("Ended transaction group: %s"), *EndedLabel));
}

// ============================================================================
// Operation: rollback_group
// ============================================================================

FToolResult ClaireonTool_Transaction::Operation_RollbackGroup(const TSharedPtr<FJsonObject>& Arguments)
{
	if (!GEditor)
	{
		return MakeErrorResult(TEXT("GEditor is not available"));
	}

	if (!bGroupActive)
	{
		return MakeErrorResult(TEXT("No active group to rollback."));
	}

	FString RolledBackLabel = ActiveGroupLabel;

	// Close the group first, then undo it
	GEditor->EndTransaction();
	GEditor->UndoTransaction();

	bGroupActive = false;
	ActiveGroupLabel.Empty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("group_rolled_back"), true);

	return MakeSuccessResult(Result, FString::Printf(TEXT("Rolled back transaction group: %s"), *RolledBackLabel));
}

// ============================================================================
// ResetGroupState
// ============================================================================

void ClaireonTool_Transaction::ResetGroupState()
{
	if (bGroupActive)
	{
		if (GEditor)
		{
			GEditor->EndTransaction();
		}
		UE_LOG(LogClaireon, Warning, TEXT("[MCP] Closing leaked transaction group: '%s'"), *ActiveGroupLabel);
	}
	bGroupActive = false;
	ActiveGroupLabel.Empty();
}
