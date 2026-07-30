#!/usr/bin/python

import torch

from ctc_forced_aligner import (
    load_audio,
    load_alignment_model,
    generate_emissions,
    preprocess_text,
    get_alignments,
    get_spans,
    postprocess_results
)

def generate_lrc_with_ctc():
    audio_path = "maxwell_vocals.opus"
    text_path = "maxwell.txt"
    language = "eng" # ISO-639-3 code

    # 1. Load the dedicated Wav2Vec2/MMS CTC Alignment Model
    device = "cuda" if torch.cuda.is_available() else "cpu"
    print("Loading CTC Alignment Model...")
    alignment_model, alignment_tokenizer = load_alignment_model(
        device,
        dtype=torch.float16 if device == "cuda" else torch.float32,
    )

    # 2. Load audio and text
    audio_waveform = load_audio(audio_path, alignment_model.dtype, alignment_model.device)
    with open(text_path, "r", encoding="utf-8") as f:
        text = f.read().strip()

    # 3. Generate acoustic frame emissions (the neural network's raw phoneme probabilities)
    print("Generating frame emissions...")
    emissions, stride = generate_emissions(alignment_model, audio_waveform, batch_size=16)

    # 4. Run the forced alignment algorithm
    print("Aligning text to audio...")
    tokens_starred, text_starred = preprocess_text(text, romanize=True, language=language)
    segments, scores, blank_token = get_alignments(emissions, tokens_starred, alignment_tokenizer)

    # Extract precise word-level or sentence-level timing spans
    spans = get_spans(tokens_starred, segments, blank_token)

    # 5. Format and save to .lrc
    print("Writing LRC file...")

    # Process the raw token spans into clean words and exact seconds
    word_segments = postprocess_results(text_starred, spans, stride, scores)

    # Split the original text back into its individual lines
    original_lines = text.split('\n')

    with open("maxwell.lrc", "w", encoding="utf-8") as f:
        word_idx = 0

        for line_str in original_lines:
            clean_line = line_str.strip()

            # Keep blank lines from the original text format
            if not clean_line:
                f.write("\n")
                continue

            # Filter out punctuation to safely match against the aligned words
            line_chars = "".join(char.lower() for char in clean_line if char.isalnum())
            if not line_chars:
                continue

            if word_idx < len(word_segments):
                # The start time of the line is the start time of its first word
                start_time = word_segments[word_idx]["start"]

                minutes = int(start_time // 60)
                seconds = start_time % 60

                # Format exactly as requested (e.g., [00:11.98]Finished with my woman)
                timestamp = f"[{minutes:02d}:{seconds:05.2f}]"
                f.write(f"{timestamp}{clean_line}\n")

                # Consume words from the aligner output until we've rebuilt this line
                accumulated_chars = ""
                while word_idx < len(word_segments) and len(accumulated_chars) < len(line_chars):
                    word_text = word_segments[word_idx]["text"]
                    accumulated_chars += "".join(char.lower() for char in word_text if char.isalnum())
                    word_idx += 1

    print("Done! Dedicated CTC alignment complete.")

if __name__ == "__main__":
    generate_lrc_with_ctc()
