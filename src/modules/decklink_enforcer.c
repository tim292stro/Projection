/* SPDX-License-Identifier: MIT */
#include "decklink_enforcer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int decklink_enforce_quadlink_2si(void) {
    const char* skip = getenv("DECKLINK_SKIP_2SI_ENFORCE");
    if (skip && skip[0] == '1') {
        fprintf(stderr, "[DECKLINK] Quad-Link 2SI enforcement skipped by environment\n");
        return 0;
    }

    // External helper hook keeps SDK-specific control out of main runtime.
    // Implication: enforcement can evolve independently, but command presence
    // becomes a deployment prerequisite for strict output guarantees.
    const char* cmd = getenv("DECKLINK_2SI_ENFORCER_CMD");
    if (!cmd || !cmd[0]) {
        cmd = "decklink_2si_enforcer";
    }

    fprintf(stderr, "[DECKLINK] Enforcing Quad-Link 2SI using command: %s\n", cmd);
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "[DECKLINK] Quad-Link 2SI enforcement failed (rc=%d)\n", rc);
        return -1;
    }

    fprintf(stderr, "[DECKLINK] Quad-Link 2SI enforcement successful\n");
    return 0;
}

int decklink_inject_hdr_ancillary_for_media(const char* file_path, const ProjectionMediaMetadata* meta) {
    const char* skip = getenv("DECKLINK_SKIP_HDR_ANCILLARY");
    if (skip && skip[0] == '1') {
        fprintf(stderr, "[DECKLINK] HDR ancillary injection skipped by environment\n");
        return 0;
    }

    if (!meta) {
        fprintf(stderr, "[DECKLINK] HDR ancillary injection skipped: no metadata context\n");
        return 0;
    }

    // Injection gate decision: only run helper when metadata indicates HDR.
    // Implication: SDR playback avoids unnecessary ancillary writes.
    if (!meta->hdr_signaled) {
        fprintf(stderr, "[DECKLINK] HDR ancillary injection not required for SDR content\n");
        return 0;
    }

    const char* cmd = getenv("DECKLINK_HDR_ANCILLARY_CMD");
    if (!cmd || !cmd[0]) {
        fprintf(stderr, "[DECKLINK] HDR ancillary command not configured (set DECKLINK_HDR_ANCILLARY_CMD)\n");
        return 0;
    }

    // Shell-hardening guard: command path must be bare executable token.
    // Implication: blocks argument smuggling via environment override.
    if (strchr(cmd, ' ') || strchr(cmd, '\t')) {
        fprintf(stderr, "[DECKLINK] Invalid DECKLINK_HDR_ANCILLARY_CMD (must be executable path without shell args)\n");
        return -1;
    }

    char hdr_value[8] = {0};
    snprintf(hdr_value, sizeof(hdr_value), "%d", meta->hdr_signaled ? 1 : 0);

    const char* primaries = (meta->color_primaries[0] ? meta->color_primaries : "unspecified");
    const char* transfer = (meta->color_transfer[0] ? meta->color_transfer : "unspecified");
    const char* matrix = (meta->color_space[0] ? meta->color_space : "unspecified");
    const char* media_path = (file_path && file_path[0]) ? file_path : "";

    fprintf(stderr,
            "[DECKLINK] Injecting HDR ancillary metadata (cmd=%s hdr=%s primaries=%s transfer=%s matrix=%s)\n",
            cmd,
            hdr_value,
            primaries,
            transfer,
            matrix);

    // Use fork/exec directly to avoid shell evaluation and preserve explicit
    // argument boundaries for metadata values.
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[DECKLINK] Failed to fork for HDR ancillary command: %s\n", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        execlp(cmd,
               cmd,
               "--file", media_path,
               "--hdr", hdr_value,
               "--primaries", primaries,
               "--transfer", transfer,
               "--matrix", matrix,
               (char*)NULL);
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "[DECKLINK] Failed waiting for HDR ancillary command: %s\n", strerror(errno));
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        int rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        fprintf(stderr, "[DECKLINK] HDR ancillary metadata injection failed (rc=%d)\n", rc);
        return -1;
    }

    fprintf(stderr, "[DECKLINK] HDR ancillary metadata injection successful\n");
    return 0;
}
