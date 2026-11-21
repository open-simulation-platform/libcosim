"""Sphinx configuration for libcosim."""

import os
from pathlib import Path

project = "libcosim"
release = (Path(__file__).parent.parent / "version.txt").read_text(
    encoding="utf-8"
).strip()
extensions = ["breathe"]
html_theme = "furo"
html_static_path = ["_static"]
html_css_files = ["stack.css"]
primary_domain = "cpp"

breathe_projects = {"libcosim": os.environ.get("LIBCOSIM_DOXYGEN_XML", "")}
breathe_default_project = "libcosim"

exclude_patterns = []
