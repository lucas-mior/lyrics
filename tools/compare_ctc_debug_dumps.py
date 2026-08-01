#!/usr/bin/env python3

import argparse
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path


DEFAULT_SECONDS_TOLERANCE = 0.005
DEFAULT_SCORE_TOLERANCE = 0.0001


@dataclass
class DumpSection:
    keys: dict[str, str] = field(default_factory=dict)
    header: list[str] = field(default_factory=list)
    rows: list[dict[str, str]] = field(default_factory=list)


@dataclass
class ParsedDump:
    sections: dict[str, DumpSection] = field(default_factory=dict)


def unescape_text(text: str) -> str:
    result = []
    i = 0
    while i < len(text):
        char = text[i]
        if char != "\\":
            result.append(char)
            i += 1
            continue

        if i + 1 >= len(text):
            result.append("\\")
            i += 1
            continue

        code = text[i + 1]
        if code == "\\":
            result.append("\\")
            i += 2
        elif code == "t":
            result.append("\t")
            i += 2
        elif code == "n":
            result.append("\n")
            i += 2
        elif code == "r":
            result.append("\r")
            i += 2
        elif code == "x" and i + 3 < len(text):
            hex_text = text[i + 2:i + 4]
            try:
                result.append(chr(int(hex_text, 16)))
                i += 4
            except ValueError:
                result.append("\\x")
                i += 2
        else:
            result.append(code)
            i += 2

    return "".join(result)


def parse_dump(path: Path) -> ParsedDump:
    parsed = ParsedDump()
    section = None

    with path.open("r", encoding="utf-8") as file:
        for raw_line in file:
            line = raw_line.rstrip("\n")
            if not line:
                continue
            if line.startswith("#"):
                continue
            if line.startswith("[") and line.endswith("]"):
                name = unescape_text(line[1:-1])
                section = DumpSection()
                parsed.sections[name] = section
                continue
            if section is None:
                continue

            if "\t" in line:
                values = [unescape_text(value) for value in line.split("\t")]
                if not section.header:
                    section.header = values
                else:
                    row = {}
                    for i, name in enumerate(section.header):
                        row[name] = values[i] if i < len(values) else ""
                    section.rows.append(row)
                continue

            if "=" in line:
                key, value = line.split("=", 1)
                section.keys[unescape_text(key)] = unescape_text(value)

    return parsed


def section_text(dump: ParsedDump, name: str) -> str | None:
    section = dump.sections.get(name)
    if section is None:
        return None

    return section.keys.get("text")


def section_rows(dump: ParsedDump, name: str) -> list[dict[str, str]]:
    section = dump.sections.get(name)
    if section is None:
        return []

    return section.rows


def section_keys(dump: ParsedDump, name: str) -> dict[str, str]:
    section = dump.sections.get(name)
    if section is None:
        return {}

    return section.keys


def as_float(value: str | None, default: float = math.nan) -> float:
    if value is None:
        return default
    try:
        return float(value)
    except ValueError:
        return default


def as_int(value: str | None, default: int = -1) -> int:
    if value is None:
        return default
    try:
        return int(value)
    except ValueError:
        return default


def compare_key_values(
    lines: list[str],
    left: ParsedDump,
    right: ParsedDump,
    section_name: str,
    keys: list[str],
) -> int:
    mismatch_count = 0
    left_keys = section_keys(left, section_name)
    right_keys = section_keys(right, section_name)

    for key in keys:
        left_value = left_keys.get(key)
        right_value = right_keys.get(key)
        if left_value != right_value:
            lines.append(
                f"{section_name}.{key}: C={left_value!r} Python={right_value!r}"
            )
            mismatch_count += 1

    return mismatch_count


def compare_float_key_values(
    lines: list[str],
    left: ParsedDump,
    right: ParsedDump,
    section_name: str,
    keys: list[str],
    tolerance: float,
) -> int:
    mismatch_count = 0
    left_keys = section_keys(left, section_name)
    right_keys = section_keys(right, section_name)

    for key in keys:
        left_value = as_float(left_keys.get(key))
        right_value = as_float(right_keys.get(key))
        if math.isnan(left_value) or math.isnan(right_value):
            lines.append(
                f"{section_name}.{key}: C={left_keys.get(key)!r} "
                f"Python={right_keys.get(key)!r}"
            )
            mismatch_count += 1
        elif abs(left_value - right_value) > tolerance:
            lines.append(
                f"{section_name}.{key}: C={left_value:.9g} "
                f"Python={right_value:.9g} delta={left_value - right_value:.9g}"
            )
            mismatch_count += 1

    return mismatch_count


def token_ids(dump: ParsedDump) -> list[int]:
    ids = []
    for row in section_rows(dump, "target_tokens"):
        if row.get("is_star") == "1":
            continue
        ids.append(as_int(row.get("token_id")))

    return ids


def row_field(row: dict[str, str], field_name: str, default: str = "") -> str:
    return row.get(field_name, default)


def compare_token_ids(lines: list[str], left: ParsedDump, right: ParsedDump) -> int:
    left_ids = token_ids(left)
    right_ids = token_ids(right)
    mismatch_count = 0

    if left_ids == right_ids:
        return 0

    mismatch_count += 1
    limit = min(len(left_ids), len(right_ids))
    first = None
    for i in range(limit):
        if left_ids[i] != right_ids[i]:
            first = i
            break
    if first is None and len(left_ids) != len(right_ids):
        first = limit

    lines.append(
        f"target_tokens.token_id: C count={len(left_ids)} "
        f"Python count={len(right_ids)} first_diff={first}"
    )
    if first is not None:
        lines.append(
            f"target_tokens[{first}]: "
            f"C={left_ids[first] if first < len(left_ids) else None!r} "
            f"Python={right_ids[first] if first < len(right_ids) else None!r}"
        )

    return mismatch_count


def compare_segments(
    lines: list[str],
    left: ParsedDump,
    right: ParsedDump,
    seconds_tolerance: float,
    score_tolerance: float,
) -> int:
    left_rows = section_rows(left, "merged_path_segments")
    right_rows = section_rows(right, "merged_path_segments")
    count = min(len(left_rows), len(right_rows))

    for i in range(count):
        left_row = left_rows[i]
        right_row = right_rows[i]
        fields = ["token_id", "token_text", "start_frame", "end_frame", "is_blank", "is_star"]
        for field_name in fields:
            if row_field(left_row, field_name) != row_field(right_row, field_name):
                lines.append(
                    f"merged_path_segments[{i}].{field_name}: "
                    f"C={row_field(left_row, field_name)!r} "
                    f"Python={row_field(right_row, field_name)!r}"
                )
                return 1

        for field_name in ["start_seconds", "end_seconds"]:
            left_value = as_float(left_row.get(field_name))
            right_value = as_float(right_row.get(field_name))
            if abs(left_value - right_value) > seconds_tolerance:
                lines.append(
                    f"merged_path_segments[{i}].{field_name}: "
                    f"C={left_value:.9g} Python={right_value:.9g} "
                    f"delta={left_value - right_value:.9g}"
                )
                return 1

        left_score = as_float(left_row.get("score"))
        right_score = as_float(right_row.get("score"))
        if abs(left_score - right_score) > score_tolerance:
            lines.append(
                f"merged_path_segments[{i}].score: C={left_score:.9g} "
                f"Python={right_score:.9g} delta={left_score - right_score:.9g}"
            )
            return 1

    if len(left_rows) != len(right_rows):
        lines.append(
            f"merged_path_segments count: C={len(left_rows)} "
            f"Python={len(right_rows)}"
        )
        return 1

    return 0


def word_text(row: dict[str, str]) -> str:
    return row.get("text", "")


def compare_word_spans(
    lines: list[str],
    left: ParsedDump,
    right: ParsedDump,
    seconds_tolerance: float,
    score_tolerance: float,
    max_word_deltas: int,
) -> int:
    left_rows = section_rows(left, "word_spans_after_padding")
    right_rows = section_rows(right, "word_spans_after_padding")
    mismatch_count = 0
    delta_lines = []
    count = min(len(left_rows), len(right_rows))

    if len(left_rows) != len(right_rows):
        lines.append(
            f"word_spans_after_padding count: C={len(left_rows)} "
            f"Python={len(right_rows)}"
        )
        mismatch_count += 1

    for i in range(count):
        left_row = left_rows[i]
        right_row = right_rows[i]
        left_start = as_float(left_row.get("start_seconds"))
        right_start = as_float(right_row.get("start_seconds"))
        left_end = as_float(left_row.get("end_seconds"))
        right_end = as_float(right_row.get("end_seconds"))
        left_score = as_float(left_row.get("score"))
        right_score = as_float(right_row.get("score"))
        start_delta = left_start - right_start
        end_delta = left_end - right_end
        score_delta = left_score - right_score

        if abs(start_delta) > seconds_tolerance or abs(end_delta) > seconds_tolerance:
            if mismatch_count == 0:
                lines.append(
                    f"word_spans_after_padding[{i}]: "
                    f"C={word_text(left_row)!r} {left_start:.9g}-{left_end:.9g} "
                    f"Python={word_text(right_row)!r} "
                    f"{right_start:.9g}-{right_end:.9g}"
                )
            mismatch_count += 1

        if abs(score_delta) > score_tolerance:
            mismatch_count += 1

        if len(delta_lines) < max_word_deltas:
            delta_lines.append(
                f"word {i}: C={word_text(left_row)!r} "
                f"Python={word_text(right_row)!r} "
                f"start_delta={start_delta:.9g} end_delta={end_delta:.9g}"
            )

    if delta_lines:
        lines.append("per-word timestamp deltas:")
        lines.extend(delta_lines)

    return mismatch_count


def compare_dumps(
    c_dump: ParsedDump,
    python_dump: ParsedDump,
    seconds_tolerance: float,
    score_tolerance: float,
    max_word_deltas: int,
) -> tuple[int, list[str]]:
    lines = []
    mismatch_count = 0

    mismatch_count += compare_key_values(
        lines,
        c_dump,
        python_dump,
        "config",
        ["split_size", "star_frequency", "language"],
    )

    c_text = section_text(c_dump, "normalized_text")
    py_text = section_text(python_dump, "normalized_text")
    if c_text != py_text:
        lines.append(f"normalized_text: C={c_text!r} Python={py_text!r}")
        mismatch_count += 1

    c_target = section_text(c_dump, "target_text")
    py_target = section_text(python_dump, "target_text")
    if c_target != py_target:
        lines.append(f"target_text: C={c_target!r} Python={py_target!r}")
        mismatch_count += 1

    mismatch_count += compare_token_ids(lines, c_dump, python_dump)
    mismatch_count += compare_key_values(
        lines,
        c_dump,
        python_dump,
        "frames",
        ["audio_sample_rate", "model_sample_rate", "inputs_to_logits_ratio"],
    )
    mismatch_count += compare_float_key_values(
        lines,
        c_dump,
        python_dump,
        "frames",
        ["stride_ms", "frame_duration_seconds", "emission_frame_count"],
        seconds_tolerance,
    )
    mismatch_count += compare_segments(
        lines,
        c_dump,
        python_dump,
        seconds_tolerance,
        score_tolerance,
    )
    mismatch_count += compare_word_spans(
        lines,
        c_dump,
        python_dump,
        seconds_tolerance,
        score_tolerance,
        max_word_deltas,
    )

    if mismatch_count == 0:
        lines.append("Dumps match within tolerance.")

    return mismatch_count, lines


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("c_dump", help="dump produced by gen_lrc or gen_lrc_raw")
    parser.add_argument("python_dump", help="dump produced by the Python reference")
    parser.add_argument(
        "--seconds-tolerance",
        type=float,
        default=DEFAULT_SECONDS_TOLERANCE,
        help="allowed timestamp delta in seconds",
    )
    parser.add_argument(
        "--score-tolerance",
        type=float,
        default=DEFAULT_SCORE_TOLERANCE,
        help="allowed score delta",
    )
    parser.add_argument(
        "--max-word-deltas",
        type=int,
        default=40,
        help="maximum number of per-word delta rows to print",
    )

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    c_dump = parse_dump(Path(args.c_dump))
    python_dump = parse_dump(Path(args.python_dump))
    mismatch_count, lines = compare_dumps(
        c_dump,
        python_dump,
        args.seconds_tolerance,
        args.score_tolerance,
        args.max_word_deltas,
    )

    for line in lines:
        print(line)

    if mismatch_count != 0:
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
