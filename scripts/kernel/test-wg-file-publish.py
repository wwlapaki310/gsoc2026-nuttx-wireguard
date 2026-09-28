#!/usr/bin/env python3
"""Fault-test the actual apps file publisher on a host, not a reimplementation.

Models the destructive NuttX VFS rename failure. This does not emulate SmartFS
media or prove persistence across power loss. Requires a host C compiler.
"""
import pathlib
import subprocess
import sys
import tempfile

source = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else
                      "/opt/apps/system/wg/wg_main.c").read_text()
start = source.index("static int wg_replace_file(")
end = source.index("\n}\n", start) + 3
function = source[start:end]
start = source.index("static FAR FILE *wg_open_temp(")
end = source.index("\n}\n", start) + 3
function = source[start:end] + "\n" + function
start = source.index("static int wg_record_private_key(")
end = source.index("\n}\n", start) + 3
function += "\n" + source[start:end]
fixture = pathlib.Path(__file__).with_name("wg-file-publish-test.c").read_text()
with tempfile.TemporaryDirectory(prefix="wg-publish-") as directory:
    root = pathlib.Path(directory)
    unit = root / "test.c"
    unit.write_text(fixture.replace("/* PUBLISHER_UNDER_TEST */", function))
    binary = root / "test"
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    str(unit), "-o", str(binary)], check=True)
    for case in range(11):
        work = root / str(case)
        work.mkdir()
        subprocess.run([str(binary), str(case)], cwd=work, check=True)
    # Negative control: deliberately ignore a failed publish. The same oracle
    # must reject this, rather than merely observing that the command ran.
    mutant = function.replace("if (rename(tmp, path) != 0)",
                              "if (rename(tmp, path) != 0 && false)")
    assert mutant != function
    unit.write_text(fixture.replace("/* PUBLISHER_UNDER_TEST */", mutant))
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    str(unit), "-o", str(binary)], check=True)
    work = root / "mutant"
    work.mkdir()
    result = subprocess.run([str(binary), "2"], cwd=work, capture_output=True)
    assert result.returncode != 0, "oracle accepted a destructive publisher"
    print("PASS: oracle rejects ignored-rename-failure mutation")
print("PASS: 11 actual-publisher cases and exclusive temporary files "
      "(host, not power-loss proof)")
