#!/usr/bin/env python3
"""Check actual NSH echo results, not echoed input. Exit 2 means incomplete."""

import re
import sys
from pathlib import Path


def check_status(text, tags):
    text = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", text)
    expected = {tag.partition("=")[0]: int(tag.partition("=")[2] or "0")
                for tag in tags}
    tags = list(expected)
    results = []
    for line in text.splitlines(keepends=True):
        if not line.endswith("\n"):
            continue
        line = line.rstrip("\r\n")
        match = re.fullmatch(r"(WGCHK_[A-Za-z0-9_]+):([0-9]+)", line)
        if match and match[1] in expected:
            results.append((match[1], int(match[2])))
    if [tag for tag, _ in results] != tags[: len(results)]:
        return 1
    if any(status != expected[tag] for tag, status in results):
        return 1
    return 0 if len(results) == len(tags) else 2


if __name__ == "__main__":
    sys.exit(check_status(Path(sys.argv[1]).read_text(errors="replace"), sys.argv[2:]))
