#!/usr/bin/env python3

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import compare_ctc_debug_dumps as compare


DUMP_TEMPLATE = """# lrc-ctc-parity-dump-v1

[config]
split_size=word
star_frequency=edges
language=eng

[normalized_text]
text=hello world

[target_text]
text=<star> h e l l o w o r l d <star>

[target_tokens]
index\ttoken_id\ttoken_text\tstarred_text_index\tis_star
0\t3\t<star>\t0\t1
1\t10\th\t1\t0
2\t11\te\t1\t0

[frames]
audio_sample_rate=16000
model_sample_rate=16000
inputs_to_logits_ratio=320
stride_ms=20
frame_duration_seconds=0.02
emission_frame_count=5

[merged_path_segments]
index\ttoken_index\ttoken_id\ttoken_text\tstart_frame\tend_frame\tstart_seconds\tend_seconds\tscore\tis_blank\tis_star
0\t-1\t0\t<blank>\t0\t1\t0\t0.02\t-0.1\t1\t0
1\t-1\t10\th\t1\t2\t0.02\t0.04\t-0.2\t0\t0

[word_spans_after_padding]
index\ttext\tstart_seconds\tend_seconds\tscore
0\thello\t0\t0.2\t-0.3
"""


def write_dump(directory: Path, name: str, text: str) -> Path:
    path = directory / name
    path.write_text(text, encoding="utf-8")
    return path


def test_matching_dumps():
    with tempfile.TemporaryDirectory() as tmp:
        directory = Path(tmp)
        c_path = write_dump(directory, "c.dump", DUMP_TEMPLATE)
        py_path = write_dump(directory, "py.dump", DUMP_TEMPLATE)

        mismatch_count, lines = compare.compare_dumps(
            compare.parse_dump(c_path),
            compare.parse_dump(py_path),
            0.005,
            0.0001,
            10,
        )

        assert mismatch_count == 0
        assert lines[-1] == "Dumps match within tolerance."


def test_first_word_delta_is_reported():
    python_dump = DUMP_TEMPLATE.replace(
        "0\thello\t0\t0.2\t-0.3",
        "0\thello\t0.04\t0.22\t-0.3",
    )

    with tempfile.TemporaryDirectory() as tmp:
        directory = Path(tmp)
        c_path = write_dump(directory, "c.dump", DUMP_TEMPLATE)
        py_path = write_dump(directory, "py.dump", python_dump)

        mismatch_count, lines = compare.compare_dumps(
            compare.parse_dump(c_path),
            compare.parse_dump(py_path),
            0.005,
            0.0001,
            10,
        )

        assert mismatch_count > 0
        assert any("word_spans_after_padding[0]" in line for line in lines)
        assert any("start_delta=-0.04" in line for line in lines)


def test_target_token_mismatch_is_reported():
    python_dump = DUMP_TEMPLATE.replace("2\t11\te", "2\t12\te")

    with tempfile.TemporaryDirectory() as tmp:
        directory = Path(tmp)
        c_path = write_dump(directory, "c.dump", DUMP_TEMPLATE)
        py_path = write_dump(directory, "py.dump", python_dump)

        mismatch_count, lines = compare.compare_dumps(
            compare.parse_dump(c_path),
            compare.parse_dump(py_path),
            0.005,
            0.0001,
            10,
        )

        assert mismatch_count > 0
        assert any("target_tokens.token_id" in line for line in lines)
        assert any("target_tokens[1]" in line for line in lines)


def main():
    test_matching_dumps()
    test_first_word_delta_is_reported()
    test_target_token_mismatch_is_reported()


if __name__ == "__main__":
    main()
