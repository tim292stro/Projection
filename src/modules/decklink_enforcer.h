/* SPDX-License-Identifier: MIT */
#ifndef DECKLINK_ENFORCER_H
#define DECKLINK_ENFORCER_H

#include "metadata_extractor.h"

/**
 * @brief Enforce DeckLink Quad-Link 2SI output mapping at startup.
 *
 * Uses external command hook to keep engine in C while allowing
 * SDK-backed enforcement in a dedicated helper binary.
 *
 * Env vars:
 * - DECKLINK_SKIP_2SI_ENFORCE=1 (disable)
 * - DECKLINK_2SI_ENFORCER_CMD="/usr/local/bin/decklink_2si_enforcer"
 *
 * @return 0 on success, -1 on failure
 *
 * Operational implication:
 * - Failure should be treated by caller as degraded-output risk and may
 *   warrant fail-safe hold before playout.
 */
int decklink_enforce_quadlink_2si(void);

/**
 * @brief Optional hook: invoke external command to inject DeckLink HDR ancillary metadata.
 *
 * Env vars:
 * - DECKLINK_SKIP_HDR_ANCILLARY=1 (disable)
 * - DECKLINK_HDR_ANCILLARY_CMD="/usr/local/bin/decklink_hdr_ancillary_inject"
 *
 * The command is invoked with:
 * --file <path> --hdr <0|1> --primaries <name> --transfer <name> --matrix <name>
 *
 * @return 0 on success or skipped, -1 on command failure
 */
int decklink_inject_hdr_ancillary_for_media(const char* file_path, const ProjectionMediaMetadata* meta);

#endif
