#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Flexible media validator for Projection ingestion requirements.

Focuses on required streams, cue signaling, and minimum metadata inclusions,
without over-constraining codec/resolution internals.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

REQUIRED_METADATA_KEYS = ["title", "projection:av_sync_offset_ms"]
RECOMMENDED_CUES = ["[FEATURE]", "[CREDITS]"]


def run_ffprobe(path: Path) -> dict:
    # Use ffprobe JSON output as the single source of truth for stream/tag
    # inspection to keep validator behavior aligned with deployed FFmpeg stack.
    cmd = [
        "ffprobe",
        "-v",
        "error",
        "-show_streams",
        "-show_chapters",
        "-show_format",
        "-of",
        "json",
        str(path),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "ffprobe failed")
    return json.loads(proc.stdout)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Projection media signaling minimums")
    parser.add_argument("media", type=Path, help="Path to media file")
    parser.add_argument(
        "--allow-missing-av-offset",
        action="store_true",
        help="Treat missing projection:av_sync_offset_ms as warning instead of failure",
    )
    parser.add_argument(
        "--require-feature-credits",
        action="store_true",
        help="Require both [FEATURE] and [CREDITS] chapter cues",
    )
    args = parser.parse_args()

    if not args.media.exists():
        print(f"FAIL: file does not exist: {args.media}")
        return 2

    try:
        data = run_ffprobe(args.media)
    except Exception as exc:
        print(f"FAIL: ffprobe error: {exc}")
        return 2

    streams = data.get("streams", [])
    format_tags = (data.get("format", {}) or {}).get("tags", {}) or {}
    chapters = data.get("chapters", [])

    failures: list[str] = []
    warnings: list[str] = []

    video_streams = [s for s in streams if s.get("codec_type") == "video"]
    audio_streams = [s for s in streams if s.get("codec_type") == "audio"]

    if not video_streams:
        failures.append("Missing required video stream")
    if not audio_streams:
        failures.append("Missing required audio stream")

    if audio_streams:
        # 48 kHz is operational target but not always a hard ingest blocker in
        # tolerance mode, so this stays a warning.
        sr_values = {str(s.get("sample_rate", "")) for s in audio_streams}
        if "48000" not in sr_values:
            warnings.append("No 48 kHz audio stream found (engine target path is 48 kHz)")

    # Metadata policy supports one strict-required key (title) and one
    # conditionally strict key (projection AV offset) via CLI override.
    for key in REQUIRED_METADATA_KEYS:
        if key not in format_tags or not str(format_tags.get(key, "")).strip():
            if key == "projection:av_sync_offset_ms" and args.allow_missing_av_offset:
                warnings.append("Missing projection:av_sync_offset_ms (allowed by flag)")
            else:
                failures.append(f"Missing required metadata key: {key}")

    chapter_titles = []
    for ch in chapters:
        tags = (ch.get("tags") or {})
        title = str(tags.get("title", "")).strip()
        if title:
            chapter_titles.append(title)

    if not chapter_titles:
        # Cue signaling is considered required for automation reliability.
        failures.append("Missing required cue signaling: no chapter cues found")

    # Cue strictness is caller-controlled:
    # - strict mode: FEATURE/CREDITS are mandatory
    # - default mode: they are recommendations only
    if args.require_feature_credits:
        for cue in RECOMMENDED_CUES:
            if cue not in chapter_titles:
                failures.append(f"Required cue missing: {cue}")
    else:
        for cue in RECOMMENDED_CUES:
            if cue not in chapter_titles:
                warnings.append(f"Recommended cue missing: {cue}")

    if failures:
        print("FAIL")
        for item in failures:
            print(f"  - {item}")
        if warnings:
            print("WARN")
            for item in warnings:
                print(f"  - {item}")
        return 1

    print("PASS")
    if warnings:
        print("WARN")
        for item in warnings:
            print(f"  - {item}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
