#include "NightSkyGameState.h"

// Portable battle checkpoints. Not implemented yet: the rollback snapshot in
// SaveGameState/LoadGameState is process-local and cannot be used here as is.

TArray<uint8> ANightSkyGameState::ExportCheckpoint()
{
	return {};
}

EBattleCheckpointResult ANightSkyGameState::ImportCheckpoint(const TArray<uint8>& Checkpoint)
{
	return EBattleCheckpointResult::Malformed;
}
