"""Shared scenario generation and JSON serialization for fuzz tests."""

import json
import random


# MOTIONS = ("w", "W", "b", "B", "e", "E")
# MOTIONS = ("h", "j", "k", "l")
# MOTIONS = ("0", "$", "G", "gg",)

__MOTIONS = ("h", "j", "k", "l", "w", "W", "b", "B", "e", "E", "0", "$", "G", "gg")
__OPERATORS = ("d", "c", "y")
# "p", "P"

MOTIONS = ()

for op in __OPERATORS:
    for motion in __MOTIONS:
        MOTIONS = MOTIONS + (op+motion+"p",)
        MOTIONS = MOTIONS + (op+motion+"P",)

WORDS = ("alpha", "Beta", "x9", "vim", "motion", "Z")
SYMBOLS = (".", ",", "_", "-", "+", "#", "!", ":")


def generate_scenarios(seed: int, text_count: int) -> list[dict[str, object]]:
    rng = random.Random(seed)
    scenarios = []
    for text_index in range(text_count):
        line_count = rng.randint(1, 6)
        lines = []
        for _ in range(line_count):
            if rng.random() < 0.2:
                lines.append("")
                continue
            parts = []
            for _ in range(rng.randint(2, 8)):
                parts.append(rng.choice(WORDS + SYMBOLS))
                if rng.random() < 0.65:
                    parts.append(" " * rng.randint(1, 3))
            lines.append("".join(parts).rstrip())
        text = "\r\n".join(lines) or "x"
        positions = [
            index
            for index, character in enumerate(text)
            if character not in ("\r", "\n")
        ]
        if not positions:
            positions = [0]
        caret = rng.choice(positions)
        for motion in MOTIONS:
            count = rng.randint(2, 6) if rng.random() < 0.5 else None
            keys = list(motion) if count is None else list(str(count) + motion)
            count_id = "single" if count is None else str(count)
            scenarios.append(
                {
                    "id": f"seed-{seed}-text-{text_index}-motion-{motion}-count-{count_id}",
                    "initial_text": text,
                    "initial_caret": caret,
                    "keys": keys,
                    "expected_text": text,
                    "expected_caret": 0,
                }
            )
    return scenarios


def write_json(path, value) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
