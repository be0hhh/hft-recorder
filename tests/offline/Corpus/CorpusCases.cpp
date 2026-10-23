#include "CorpusFixture.hpp"

namespace {
#include "CorpusHealthCases.ipp"
#include "CorpusCursorCases.ipp"
#include "CorpusLifecycleCases.ipp"
#include "CorpusRoundtripCases.ipp"
#include "CorpusQuantityCases.ipp"
}
int main(int argc,char** argv) {
  const cxet::testing::Case cases[]{
    cxet::testing::Case{"recorder.multishard_lifecycle_uses_global_merge_not_lookahead",multishardLifecycleMerge},
    cxet::testing::Case{"recorder.multishard_stale_generation_refused_before_emission",multishardStaleGenerationFailsBeforeEmission},
    cxet::testing::Case{"recorder.native_quantity_authority_roundtrip_preserves_raw_and_health_identity",nativeQuantityAuthorityRoundtrip},
    cxet::testing::Case{"recorder.malformed_quantity_authority_is_not_recordable",malformedQuantityAuthorityIsNotRecordable},
    cxet::testing::Case{"recorder.native_health_cannot_apply_future_sibling_selection",nativeHealthSampleCannotApplyFutureSiblingSelection},
    cxet::testing::Case{"recorder.native_recovery_advances_actual_generation",nativeRecoveryAdvancesOnlyActualGeneration},
    cxet::testing::Case{"recorder.native_health_preserves_membership_and_hydrates_interval",nativeHealthPreservesMembershipAndHydratesInterval},
    cxet::testing::Case{"recorder.source_id_rejects_foreign_native_listing",sourceIdCannotChangeNativeListing},
    cxet::testing::Case{"recorder.captured_health_fences_preserved_capture_loss_refused",capturedHealthIsAnAvailabilityFence},
    cxet::testing::Case{"recorder.health_coverage_and_interval_start_are_exact",healthCoverageAndIntervalStart},
    cxet::testing::Case{"recorder.depth_state_gap_is_availability_not_price",depthStateGapIsNotAReplayPrice},
    cxet::testing::Case{"recorder.lifecycle_interval_start_uses_effective_membership_metadata",lifecycleSelectionStartsWithEffectiveMetadata},
    cxet::testing::Case{"recorder.lifecycle_native_readd_keeps_shared_generation_and_sibling_epoch",lifecycleReaddPreservesSharedSourceGeneration},
    cxet::testing::Case{"recorder.lifecycle_catalog_registration_keeps_dormant_future_ids",lifecycleRegistersDormantCatalogPrefix},
    cxet::testing::Case{"recorder.lifecycle_loss_refuses_unrelated_source_selection",lifecycleLossRejectsEverySelection},
    cxet::testing::Case{"recorder.lifecycle_remove_readd_retains_real_boundaries",lifecycleRemoveAndReadd},
    cxet::testing::Case{"recorder.lifecycle_new_listing_appends_stable_source_catalog",lifecycleAddsStableCatalogSource},
    cxet::testing::Case{"recorder.lifecycle_partial_remove_preserves_remaining_feed",lifecyclePartialRemovalKeepsOtherChannels},
    cxet::testing::Case{"recorder.whole_corpus_source_subset_ignores_foreign_gap",selectedCorpusSourcesIgnoreForeignGaps},
    cxet::testing::Case{"recorder.whole_corpus_cursor_merges_sources_without_record_vectors",wholeCorpusCursor},
    cxet::testing::Case{"recorder.compressed_all_channels_preserve_every_record_byte",allChannelsExactBytes},
    cxet::testing::Case{"recorder.compressed_cursor_bounded_multiblock_arrival_merge",compressedCursor},
    cxet::testing::Case{"recorder.cursor_refuses_corruption_before_emission",cursorFailsBeforeEmission},
    cxet::testing::Case{"recorder.uuid_tail_roundtrip_retains_zero_and_max",uuidTailRoundtrip},
    cxet::testing::Case{"recorder.uuid_tail_rejects_wrong_shape_channel_and_schema",uuidTailInvalidBoundary},
    cxet::testing::Case{"recorder.binary_roundtrip_retains_identity_and_values",roundtrip},
    cxet::testing::Case{"recorder.selection_requires_exact_source_identity",identity},
    cxet::testing::Case{"recorder.writer_rejects_foreign_generation_without_append",generation},
    cxet::testing::Case{"recorder.selection_receive_interval_is_half_open",interval},
    cxet::testing::Case{"recorder.selection_refuses_missing_required_channel",requiredChannel},
    cxet::testing::Case{"recorder.selection_preserves_gap_evidence_and_refuses_replay",gap},
    cxet::testing::Case{"recorder.reader_refuses_manifest_crc_and_truncated_segment",corruption},
    cxet::testing::Case{"recorder.final_drain_uses_reserved_capacity_below_hard_ceiling",finalDrain},
  }; return cxet::testing::runCases(argc,argv,cases);
}
