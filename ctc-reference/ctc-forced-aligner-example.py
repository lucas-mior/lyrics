#!/usr/bin/python

import re
import torch
import sys
import os

from ctc_forced_aligner import (
    load_audio,
    load_alignment_model,
    generate_emissions,
    preprocess_text,
    get_alignments,
    get_spans,
    postprocess_results,
)


SAMPLE_RATE = 16000


def ctc_target_stats(tokens, tokenizer):
    dictionary = tokenizer.get_vocab()
    dictionary = {key.lower(): value for key, value in dictionary.items()}
    dictionary["<star>"] = len(dictionary)

    token_indices = [
        dictionary[token]
        for token in " ".join(tokens).split(" ")
        if token in dictionary
    ]

    repeats = sum(
        1
        for previous, current in zip(token_indices, token_indices[1:])
        if previous == current
    )

    return len(token_indices), repeats, len(token_indices) + repeats


def clean_alignment_lines(lines):
    cleaned = []

    for line in lines:
        line = line.strip()

        if not line:
            continue

        # Skip common lyric-file section markers like [Verse 1], [Chorus], etc.
        if re.fullmatch(r"[\[(].*[\])]", line):
            continue

        cleaned.append(line)

    return cleaned


def lrc_timestamp(seconds):
    minutes = int(seconds // 60)
    seconds = seconds % 60
    return f"[{minutes:02d}:{seconds:05.2f}]"


def normalized_chars(text):
    return "".join(char.lower() for char in text if char.isalnum())


def generate_lrc_with_ctc(music):
    audio_path = music
    base_name = os.path.splitext(music)[0]

    text_path = f"{base_name}.txt"
    lrc_path = f"{base_name}.lrc"
    language = "eng"

    device = "cuda" if torch.cuda.is_available() else "cpu"

    print("Loading CTC Alignment Model...")
    alignment_model, alignment_tokenizer = load_alignment_model(
        device,
        dtype=torch.float16 if device == "cuda" else torch.float32,
    )

    audio_waveform = load_audio(
        audio_path,
        alignment_model.dtype,
        alignment_model.device,
    )

    with open(text_path, "r", encoding="utf-8") as f:
        original_lines = [line.rstrip("\n") for line in f]

    alignment_lines = clean_alignment_lines(original_lines)
    alignment_text = " ".join(alignment_lines).strip()

    if not alignment_text:
        raise RuntimeError("No alignable lyric text found.")

    print("Generating frame emissions...")
    emissions, stride = generate_emissions(
        alignment_model,
        audio_waveform,
        batch_size=16,
    )

    print("Preparing transcript...")
    tokens_starred, text_starred = preprocess_text(
        alignment_text,
        romanize=True,
        language=language,
        split_size="word",
        star_frequency="edges",
    )

    target_len, repeats, min_frames = ctc_target_stats(
        tokens_starred,
        alignment_tokenizer,
    )

    audio_seconds = audio_waveform.numel() / SAMPLE_RATE
    available_frames = emissions.size(0)
    required_seconds = min_frames * stride / 1000.0

    print(f"Decoded audio: {audio_seconds:.2f}s")
    print(f"CTC frames: {available_frames}")
    print(f"Target labels: {target_len}")
    print(f"Repeated-label penalty: {repeats}")
    print(f"Minimum required frames: {min_frames}")
    print(f"Approx. minimum required audio: {required_seconds:.2f}s")

    if min_frames > available_frames:
        raise RuntimeError(
            "Transcript is too long for this audio. "
            f"Need at least {min_frames} CTC frames "
            f"({required_seconds:.2f}s), but audio produced only "
            f"{available_frames} frames ({audio_seconds:.2f}s). "
            "Use the matching lyric excerpt, remove non-sung text, or align "
            "a longer audio file."
        )

    print("Aligning text to audio...")
    segments, scores, blank_token = get_alignments(
        emissions,
        tokens_starred,
        alignment_tokenizer,
    )

    spans = get_spans(tokens_starred, segments, blank_token)
    word_segments = postprocess_results(text_starred, spans, stride, scores)

    print("Writing LRC file...")

    with open(lrc_path, "w", encoding="utf-8") as f:
        word_idx = 0

        for line_str in original_lines:
            clean_line = line_str.strip()

            if not clean_line:
                f.write("\n")
                continue

            # Keep section headers out of alignment output.
            if re.fullmatch(r"[\[(].*[\])]", clean_line):
                continue

            line_chars = normalized_chars(clean_line)
            if not line_chars:
                continue

            if word_idx >= len(word_segments):
                break

            start_time = word_segments[word_idx]["start"]
            f.write(f"{lrc_timestamp(start_time)}{clean_line}\n")

            accumulated_chars = ""
            while (
                word_idx < len(word_segments)
                and len(accumulated_chars) < len(line_chars)
            ):
                word_text = word_segments[word_idx]["text"]
                accumulated_chars += normalized_chars(word_text)
                word_idx += 1

    print("Done! Dedicated CTC alignment complete.")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <music>")
        exit(1)
    generate_lrc_with_ctc(sys.argv[1])
