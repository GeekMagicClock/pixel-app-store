Import("env")

from pathlib import Path
import subprocess
import sys


def _project_path(*parts):
    return Path(env.subst("$PROJECT_DIR")).joinpath(*parts)


def _build_path(*parts):
    return Path(env.subst("$BUILD_DIR")).joinpath(*parts)


script_path = _project_path("scripts", "embed_web_asset.py")
generated_dir = _build_path("esp-idf", "src", "generated")
assets = [
    ("f.html", "f_html_gz.h", "f_html_gz"),
    ("portal.html", "portal_html_gz.h", "portal_html_gz"),
    ("favicon.ico", "favicon_ico_gz.h", "favicon_ico_gz"),
]

generated_dir.mkdir(parents=True, exist_ok=True)

for input_name, output_name, symbol in assets:
    input_path = _project_path("webpages", input_name)
    output_path = generated_dir.joinpath(output_name)
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
            symbol,
        ],
        check=False,
    )
    if result.returncode != 0:
        raise SystemExit(result.returncode)
