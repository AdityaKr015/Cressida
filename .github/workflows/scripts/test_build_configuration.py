"""Exercise the release recipe with real slim-LTO static archives."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


TOOLS = ("just", "meson", "ninja", "gcc", "g++", "gcc-ar")
ROOT = Path(__file__).resolve().parents[3]


@unittest.skipUnless(all(shutil.which(tool) for tool in TOOLS), "GCC LTO build tools are unavailable")
class ReleaseConfigurationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cressida-release-check-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / "build-check-release"
        self.environment = os.environ.copy()
        self.environment.update(CC=shutil.which("gcc"), CXX=shutil.which("g++"), AR="ar")
        (self.root / "meson.build").write_text(
            "project('release-archive-check', 'cpp', default_options: ['cpp_std=c++23'])\n"
            "archive = static_library('archive-probe', 'probe.cpp', cpp_args: ['-fno-fat-lto-objects'])\n"
            "executable('cressida', 'main.cpp', link_with: archive)\n"
        )
        (self.root / "probe.cpp").write_text('extern "C" int archive_answer() { return 42; }\n')
        (self.root / "main.cpp").write_text(
            'extern "C" int archive_answer();\nint main() { return archive_answer() == 42 ? 0 : 1; }\n'
        )

    def run_command(self, *arguments):
        result = subprocess.run(
            arguments, cwd=self.root, env=self.environment, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60,
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        return result.stdout

    def build_release(self):
        return self.run_command(
            "just", "--justfile", str(ROOT / "justfile"), "--working-directory", str(self.root),
            "--set", "build-prefix", "build-check", "build", "release",
        )

    def test_generic_ar_selects_a_working_release_archiver(self):
        self.build_release()
        self.run_command(str(self.build / "cressida"))

    def test_reconfigure_repairs_archives_without_recompiling_objects(self):
        self.run_command(
            "meson", "setup", str(self.build), "--buildtype=release",
            "-Db_lto=true", "-Db_sanitize=none", "-Dcpp_std=c++23",
        )
        self.run_command("ninja", "-C", str(self.build), "libarchive-probe.a")
        compiled = self.build / "libarchive-probe.a.p/probe.cpp.o"
        timestamp = compiled.stat().st_mtime_ns
        self.build_release()
        self.run_command(str(self.build / "cressida"))
        self.assertEqual(compiled.stat().st_mtime_ns, timestamp)

    def test_custom_archiver_is_preserved(self):
        archiver = self.root / "custom archiver"
        trace = self.root / "archiver.log"
        archiver.write_text(
            '#!/bin/sh\nprintf "%s\\n" "$@" >> "$CRESSIDA_TEST_AR_LOG"\n'
            'exec "$CRESSIDA_TEST_AR" "$@"\n'
        )
        archiver.chmod(0o700)
        self.environment.update(
            AR=str(archiver), CRESSIDA_TEST_AR=shutil.which("gcc-ar"), CRESSIDA_TEST_AR_LOG=str(trace),
        )
        self.build_release()
        self.run_command(str(self.build / "cressida"))
        self.assertIn("libarchive-probe.a", trace.read_text())


if __name__ == "__main__":
    unittest.main()
