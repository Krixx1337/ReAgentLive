"""The Unreal pack's engine offsets live in engine_layout.h only.

Fails when a pack file defines a constant named like an engine offset with a
literal value, or uses one of the names engine_layout.h replaced.
"""
import pathlib
import re
import sys

PACK = pathlib.Path(__file__).resolve().parents[2] / "src" / "reagentlive" / "packs" / "ue"
HOMES = {"engine_layout.h", "engine_layout_sdk.h", "globals.h", "name_pool.h"}
DEFINITION = re.compile(r"constexpr\s+[\w:]+\s+(\w*(?:Offset|Slot))\s*=\s*(?:0x[0-9A-Fa-f]+|[0-9]+)")
GONE = re.compile(r"\b(ObjectIdentityOffset|InterfacesOffset|MapLayoutOffset|SetLayoutOffset|ScriptOffset|"
                  r"FrameNodeOffset|FrameLocalsOffset|FunctionReturnOffset)\b")
# Named like offsets, but not engine layout: bit counts and request bounds.
ALLOWED = {"BlockOffsetBits"}


def main() -> int:
    problems = []
    for path in sorted(PACK.rglob("*")):
        if path.suffix not in {".h", ".cpp"} or path.name in HOMES:
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            where = f"{path.relative_to(PACK)}:{number}"
            match = DEFINITION.search(line)
            if match and match.group(1) not in ALLOWED:
                problems.append(f"{where}: engine offset {match.group(1)} outside engine_layout.h")
            if GONE.search(line):
                problems.append(f"{where}: {GONE.search(line).group(1)} moved to engine_layout.h")
    for problem in problems:
        print(problem)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
