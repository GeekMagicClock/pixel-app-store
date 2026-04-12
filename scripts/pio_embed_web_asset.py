Import("env")

from pathlib import Path
import subprocess
import sys


def _project_path(*parts):
    return Path(env.subst("$PROJECT_DIR")).joinpath(*parts)


def _build_path(*parts):
    return Path(env.subst("$BUILD_DIR")).joinpath(*parts)


input_path = _project_path("webpages", "f.html")
script_path = _project_path("scripts", "embed_web_asset.py")
output_path = _build_path("esp-idf", "src", "generated", "f_html_gz.h")

output_path.parent.mkdir(parents=True, exist_ok=True)

print(f"Embedding web asset: {input_path} -> {output_path}")
result = subprocess.run(
    [
        sys.executable,
        str(script_path),
        "--input",
        str(input_path),
        "--output",
        str(output_path),
        "--symbol",
        "f_html_gz",
    ],
    check=False,
)
if result.returncode != 0:
    raise SystemExit(result.returncode)
