#!/usr/bin/env python3
"""Generate an .rst document from the ProcessCancellation tests."""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, List


@dataclass
class TestCase:
    name: str
    description: str
    code: List[str]
    start_line: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate an .rst document that mirrors the ProcessCancellation tests"
    )
    parser.add_argument(
        "--input",
        default="test/test_process_cancellation.cpp",
        type=Path,
        help="Path to the test source file",
    )
    parser.add_argument(
        "--output",
        default="docs/process_cancellation_tests.rst",
        type=Path,
        help="Destination .rst file",
    )
    parser.add_argument(
        "--suite",
        default="ProcessCancellation",
        help="Name of the GoogleTest fixture to scan",
    )
    parser.add_argument(
        "--title",
        default="Process Cancellation Examples",
        help="Top level title for the generated document",
    )
    parser.add_argument(
        "--repo",
        default="https://github.com/pgit/asio-coro",
        help="Base GitHub repository URL",
    )
    parser.add_argument(
        "--branch",
        default="master",
        help="Git reference (branch/tag/commit) to use in source links",
    )
    return parser.parse_args()


def gather_comment(lines: List[str], start_index: int) -> str:
    idx = start_index - 1
    # skip blank lines between the comment block and TEST_F
    while idx >= 0 and not lines[idx].strip():
        idx -= 1

    collected: List[tuple[str, str]] = []
    while idx >= 0 and lines[idx].lstrip().startswith("//"):
        raw = lines[idx].split("//", 1)[1]
        text = raw.rstrip()
        if text.startswith(" ") and not text.startswith("    "):
            text = text[1:]
        stripped = text.strip()
        if stripped and set(stripped) <= {"-", "="}:
            text = ""
            stripped = ""
        collected.append((text, stripped))
        idx -= 1
        # swallow additional blank lines between comment paragraphs
        while idx >= 0 and not lines[idx].strip():
            idx -= 1

    cleaned: List[str] = []
    for text, stripped in reversed(collected):
        if not stripped:
            if cleaned and cleaned[-1] != "":
                cleaned.append("")
        else:
            cleaned.append(text)
    return "\n".join(cleaned).strip()


def gather_code(lines: List[str], start_index: int) -> List[str]:
    code: List[str] = []
    depth = 0
    idx = start_index
    seen_body = False
    while idx < len(lines):
        line = lines[idx]
        code.append(line.rstrip())
        delta = line.count("{") - line.count("}")
        depth += delta
        if delta > 0:
            seen_body = True
        if seen_body and depth == 0:
            break
        idx += 1
    return code


def iter_tests(lines: List[str], suite: str) -> Iterator[TestCase]:
    pattern = re.compile(rf"^\s*TEST_F\(\s*{suite}\s*,\s*([^)]+)\)")
    for idx, line in enumerate(lines):
        match = pattern.match(line)
        if not match:
            continue
        name = match.group(1).strip()
        description = gather_comment(lines, idx)
        code = gather_code(lines, idx)
        yield TestCase(name=name, description=description, code=code, start_line=idx + 1)


def build_github_url(repo: str, ref: str, source_path: Path, line: int) -> str:
    rel = source_path.as_posix()
    return f"{repo}/blob/{ref}/{rel}#L{line}"


def build_document(
    title: str,
    tests: List[TestCase],
    source_path: Path,
    repo: str,
    ref: str,
) -> str:
    if not tests:
        raise SystemExit("No tests were found; did the fixture name change?")

    lines: List[str] = []
    lines.append(title)
    lines.append("=" * len(title))
    lines.append("")
    lines.append(
        "This file was generated automatically from ``test/test_process_cancellation.cpp``."
    )
    lines.append("")

    for test in tests:
        lines.append(test.name)
        lines.append("-" * len(test.name))
        lines.append("")
        if test.description:
            lines.append(test.description)
            lines.append("")
        else:
            lines.append("(No description provided.)")
            lines.append("")

        github_url = build_github_url(repo, ref, source_path, test.start_line)
        lines.append(f"`View source on GitHub <{github_url}>`_")
        lines.append("")

        lines.append(".. code-block:: cpp")
        lines.append("")
        for code_line in test.code:
            if code_line:
                lines.append(f"   {code_line}")
            else:
                lines.append("")
        lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def main() -> None:
    args = parse_args()
    source_text = args.input.read_text(encoding="utf-8").splitlines()
    tests = list(iter_tests(source_text, args.suite))
    document = build_document(args.title, tests, args.input, args.repo, args.branch)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(document, encoding="utf-8")
    print(f"Wrote {args.output} with {len(tests)} sections.")


if __name__ == "__main__":
    main()
