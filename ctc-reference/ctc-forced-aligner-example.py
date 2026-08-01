#!/usr/bin/python

import argparse
import math
import os
import re

import torch

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
ALIGNMENT_MODEL = "MahmoudAshraf/mms-300m-1130-forced-aligner"
LANGUAGE = "eng"
SPLIT_SIZE = "word"
STAR_FREQUENCY = "edges"


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


def dump_escape(text):
    if text is None:
        text = "<null>"

    escaped = []
    for char in text:
        if char == "\\":
            escaped.append("\\\\")
        elif char == "\t":
            escaped.append("\\t")
        elif char == "\n":
            escaped.append("\\n")
        elif char == "\r":
            escaped.append("\\r")
        elif ord(char) < 0x20 or ord(char) == 0x7F:
            escaped.append(f"\\x{ord(char):02X}")
        else:
            escaped.append(char)

    return "".join(escaped)


class DebugDumpWriter:
    def __init__(self, path):
        self.path = path
        self.file = open(path, "w", encoding="utf-8")

    def close(self):
        self.file.close()

    def write(self, text):
        self.file.write(text)

    def section(self, name):
        self.write(f"\n[{dump_escape(name)}]\n")

    def key(self, name, value):
        self.write(f"{dump_escape(name)}={dump_escape(str(value))}\n")

    def text_section(self, name, text):
        self.section(name)
        self.key("text", text)


def tokenizer_dictionary(tokenizer):
    dictionary = tokenizer.get_vocab()
    if len(dictionary) > tokenizer.vocab_size:
        raise ValueError(
            "Tokenizer vocabulary contains more tokens than expected. "
            "Please open an issue on Github to report this and include the "
            "model name."
        )

    dictionary = {key.lower(): value for key, value in dictionary.items()}
    dictionary["<star>"] = len(dictionary)

    return dictionary


def flatten_target_tokens(tokens_starred, tokenizer):
    dictionary = tokenizer_dictionary(tokenizer)
    flattened = []

    for starred_index, token_group in enumerate(tokens_starred):
        for token in token_group.split(" "):
            if token in dictionary:
                flattened.append(
                    {
                        "token_id": dictionary[token],
                        "token_text": token,
                        "starred_text_index": starred_index,
                        "is_star": int(token == "<star>"),
                    }
                )

    return flattened


def write_config_dump(
    writer,
    audio_path,
    text_path,
    lrc_path,
    device,
    dtype,
    batch_size,
    window_size,
    context_size,
):
    writer.section("config")
    writer.key("ctc_model_path", ALIGNMENT_MODEL)
    writer.key("tokenizer_path", ALIGNMENT_MODEL)
    writer.key("vocals_path", audio_path)
    writer.key("lyrics_path", text_path)
    writer.key("output_path", lrc_path)
    writer.key("split_size", SPLIT_SIZE)
    writer.key("star_frequency", STAR_FREQUENCY)
    writer.key("romanization", "uroman")
    writer.key("language", LANGUAGE)
    writer.key("device", device)
    writer.key("compute_dtype", str(dtype).replace("torch.", ""))
    writer.key("batch_size", batch_size)
    writer.key("window_seconds", window_size)
    writer.key("context_seconds", context_size)


def write_target_tokens_dump(writer, tokens_starred, tokenizer):
    target_tokens = flatten_target_tokens(tokens_starred, tokenizer)

    writer.section("target_tokens")
    writer.write("index\ttoken_id\ttoken_text\tstarred_text_index\tis_star\n")
    for index, token in enumerate(target_tokens):
        writer.write(
            f"{index}\t{token['token_id']}\t"
            f"{dump_escape(token['token_text'])}\t"
            f"{token['starred_text_index']}\t{token['is_star']}\n"
        )


def write_frames_dump(
    writer,
    audio_waveform,
    emissions,
    stride,
    model,
    tokenizer,
    window_size,
    context_size,
):
    audio_sample_count = int(audio_waveform.numel())
    ratio = int(model.config.inputs_to_logits_ratio)
    window_samples = int(window_size*SAMPLE_RATE)
    context_samples = int(context_size*SAMPLE_RATE)
    if audio_sample_count < window_samples:
        chunk_count = 1
        row_sample_count = audio_sample_count
    else:
        chunk_count = math.ceil(audio_sample_count/window_samples)
        row_sample_count = window_samples + 2*context_samples

    writer.section("frames")
    writer.key("audio_sample_rate", SAMPLE_RATE)
    writer.key("audio_sample_count", audio_sample_count)
    writer.key("model_sample_rate", SAMPLE_RATE)
    writer.key("inputs_to_logits_ratio", ratio)
    writer.key("stride_ms", stride)
    writer.key("frame_duration_seconds", stride/1000.0)
    writer.key("emission_frame_count", int(emissions.size(0)))
    writer.key("emission_vocabulary_size", int(emissions.size(1)) - 1)
    writer.key("tokenizer_token_count", int(tokenizer.vocab_size))
    dictionary = tokenizer_dictionary(tokenizer)
    blank_id = dictionary.get("<blank>", tokenizer.pad_token_id)
    writer.key("blank_token_id", int(blank_id))
    writer.key("star_token_id", int(emissions.size(1)) - 1)
    writer.key("chunk_count", chunk_count)
    writer.key("row_sample_count", row_sample_count)
    writer.key("window_seconds", window_size)
    writer.key("context_seconds", context_size)


def segment_score(scores, start_frame, end_frame):
    if hasattr(scores, "squeeze"):
        scores = scores.squeeze()
    score_slice = scores[start_frame:end_frame]
    if hasattr(score_slice, "mean"):
        return float(score_slice.mean())

    return 0.0


def token_id_for_label(label, dictionary, blank_token_id, star_token_id):
    if label == "<star>":
        return star_token_id
    if label in dictionary:
        return dictionary[label]

    return blank_token_id


def write_segments_dump(writer, segments, scores, tokenizer, blank_token, stride):
    dictionary = tokenizer_dictionary(tokenizer)
    blank_token_id = dictionary.get("<blank>", tokenizer.pad_token_id)
    star_token_id = len(dictionary) - 1

    writer.section("merged_path_segments")
    writer.write(
        "index\ttoken_index\ttoken_id\ttoken_text\tstart_frame\t"
        "end_frame\tstart_seconds\tend_seconds\tscore\tis_blank\tis_star\n"
    )
    for index, segment in enumerate(segments):
        start_frame = int(segment.start)
        end_frame = int(segment.end) + 1
        token_id = token_id_for_label(
            segment.label,
            dictionary,
            blank_token_id,
            star_token_id,
        )
        is_blank = int(segment.label == blank_token)
        is_star = int(segment.label == "<star>")
        writer.write(
            f"{index}\t-1\t{token_id}\t{dump_escape(segment.label)}\t"
            f"{start_frame}\t{end_frame}\t"
            f"{start_frame*stride/1000.0:.9g}\t"
            f"{end_frame*stride/1000.0:.9g}\t"
            f"{segment_score(scores, start_frame, end_frame):.9g}\t"
            f"{is_blank}\t{is_star}\n"
        )


def write_word_spans_after_padding_dump(writer, word_segments):
    writer.section("word_spans_after_padding")
    writer.write("index\ttext\tstart_seconds\tend_seconds\tscore\n")
    for index, word in enumerate(word_segments):
        writer.write(
            f"{index}\t{dump_escape(word['text'])}\t"
            f"{float(word['start']):.9g}\t{float(word['end']):.9g}\t"
            f"{float(word['score']):.9g}\n"
        )


def write_debug_dump(
    path,
    audio_path,
    text_path,
    lrc_path,
    device,
    dtype,
    batch_size,
    window_size,
    context_size,
    tokens_starred,
    audio_waveform,
    emissions,
    stride,
    model,
    tokenizer,
    segments,
    scores,
    blank_token,
    word_segments,
):
    writer = DebugDumpWriter(path)
    try:
        writer.write("# lrc-ctc-parity-dump-v1\n")
        write_config_dump(
            writer,
            audio_path,
            text_path,
            lrc_path,
            device,
            dtype,
            batch_size,
            window_size,
            context_size,
        )
        normalized_words = [
            token.replace(" ", "")
            for token in tokens_starred
            if token != "<star>"
        ]
        writer.text_section("normalized_text", " ".join(normalized_words))
        target_tokens = [token for token in tokens_starred if token != "<star>"]
        writer.text_section("target_text", " ".join(target_tokens))
        write_target_tokens_dump(writer, tokens_starred, tokenizer)
        write_frames_dump(
            writer,
            audio_waveform,
            emissions,
            stride,
            model,
            tokenizer,
            window_size,
            context_size,
        )
        write_segments_dump(writer, segments, scores, tokenizer, blank_token, stride)
        write_word_spans_after_padding_dump(writer, word_segments)
    finally:
        writer.close()


def generate_lrc_with_ctc(music, debug_dump_path=None):
    audio_path = music
    base_name = os.path.splitext(music)[0]

    text_path = f"{base_name}.txt"
    lrc_path = f"{base_name}.lrc"
    language = LANGUAGE
    batch_size = 16
    window_size = 30
    context_size = 2

    device = "cuda" if torch.cuda.is_available() else "cpu"
    dtype = torch.float16 if device == "cuda" else torch.float32

    print("Loading CTC Alignment Model...")
    alignment_model, alignment_tokenizer = load_alignment_model(
        device,
        dtype=dtype,
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
        batch_size=batch_size,
    )

    print("Preparing transcript...")
    tokens_starred, text_starred = preprocess_text(
        alignment_text,
        romanize=True,
        language=language,
        split_size=SPLIT_SIZE,
        star_frequency=STAR_FREQUENCY,
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

    if debug_dump_path:
        write_debug_dump(
            debug_dump_path,
            audio_path,
            text_path,
            lrc_path,
            device,
            dtype,
            batch_size,
            window_size,
            context_size,
            tokens_starred,
            audio_waveform,
            emissions,
            stride,
            alignment_model,
            alignment_tokenizer,
            segments,
            scores,
            blank_token,
            word_segments,
        )

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


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("music", help="audio file whose base name has a .txt file")
    parser.add_argument(
        "--debug-dump",
        help="write Python CTC parity debug dump to this path",
    )

    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    generate_lrc_with_ctc(args.music, args.debug_dump)
