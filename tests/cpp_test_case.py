import subprocess
import tempfile
import unittest
import os
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
CPP_TEST_DIR = Path(__file__).resolve().parent


class CppTestCase(unittest.TestCase):
    def assert_cpp_test_passes(self, cpp_filename, extra_sources=()):
        source = CPP_TEST_DIR / cpp_filename
        if not source.is_file():
            self.fail(f"Missing C++ test file: {source}")
        extra_source_paths = [REPO_ROOT / extra_source for extra_source in extra_sources]
        for extra_source_path in extra_source_paths:
            if not extra_source_path.is_file():
                self.fail(f"Missing C++ source file: {extra_source_path}")
        extra_include_paths = []
        external_source_paths = []
        if self._uses_ocpp_protocol(extra_sources):
            esphome_paths = self._find_installed_esphome_paths()
            if esphome_paths is None:
                self.fail(
                    "C++ tests for ocpp/protocol.cpp require the real ESPHome JSON library. "
                    "Install the project dependencies from requirements.txt before running this test."
                )
            extra_include_paths.append(esphome_paths["site_root"])
            arduinojson_include_path = self._find_arduinojson_include_path(esphome_paths)
            if arduinojson_include_path is not None:
                extra_include_paths.append(arduinojson_include_path)
            external_source_paths.append(esphome_paths["json_util_cpp"])

        with tempfile.TemporaryDirectory() as tmpdir:
            binary = Path(tmpdir) / source.stem
            compile_result = subprocess.run(
                [
                    "c++",
                    "-std=c++20",
                    "-I",
                    str(CPP_TEST_DIR),
                    "-I",
                    str(REPO_ROOT),
                    *(arg for include_path in extra_include_paths for arg in ("-I", str(include_path))),
                    str(source),
                    *(str(extra_source_path) for extra_source_path in extra_source_paths),
                    *(str(external_source_path) for external_source_path in external_source_paths),
                    "-o",
                    str(binary),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            compile_error = self._process_failure_message("compile", compile_result)
            if compile_error:
                self.fail(compile_error)
            run_result = subprocess.run([str(binary)], cwd=REPO_ROOT, text=True, capture_output=True)
            run_error = self._process_failure_message("run", run_result)
            if run_error:
                self.fail(run_error)

    @staticmethod
    def _uses_ocpp_protocol(extra_sources):
        return "esphome/components/ocpp/protocol.cpp" in extra_sources

    @staticmethod
    def _find_installed_esphome_paths():
        import importlib.util

        original_path = list(sys.path)
        try:
            sys.path = [path for path in sys.path if Path(path or ".").resolve() != REPO_ROOT]
            spec = importlib.util.find_spec("esphome")
        finally:
            sys.path = original_path

        if spec is None or spec.origin is None:
            return None
        package_dir = Path(spec.origin).resolve().parent
        site_root = package_dir.parent
        json_util_h = package_dir / "components" / "json" / "json_util.h"
        json_util_cpp = package_dir / "components" / "json" / "json_util.cpp"
        if not json_util_h.is_file() or not json_util_cpp.is_file():
            return None
        return {
            "site_root": site_root,
            "json_util_cpp": json_util_cpp,
        }

    @staticmethod
    def _find_arduinojson_include_path(esphome_paths):
        explicit_path = os.environ.get("ARDUINOJSON_INCLUDE_DIR")
        if explicit_path:
            include_path = Path(explicit_path).expanduser().resolve()
            normalized_include_path = CppTestCase._arduinojson_include_path_from_directory(include_path)
            if normalized_include_path is not None:
                return normalized_include_path

        for search_root in CppTestCase._arduinojson_search_roots(esphome_paths):
            if not search_root.is_dir():
                continue
            for header in search_root.rglob("ArduinoJson.h"):
                normalized_include_path = CppTestCase._arduinojson_include_path_from_directory(header.parent)
                if normalized_include_path is not None:
                    return normalized_include_path
        return None

    @staticmethod
    def _arduinojson_include_path_from_directory(include_path):
        if (include_path / "src" / "ArduinoJson.h").is_file():
            return include_path / "src"
        if (include_path / "ArduinoJson.h").is_file():
            return include_path
        return None

    @staticmethod
    def _arduinojson_search_roots(esphome_paths):
        roots = [
            esphome_paths["site_root"],
            Path(sys.prefix),
            REPO_ROOT / ".pio",
            REPO_ROOT / ".esphome",
        ]
        platformio_core_dir = os.environ.get("PLATFORMIO_CORE_DIR")
        if platformio_core_dir:
            roots.append(Path(platformio_core_dir).expanduser())
        roots.append(Path.home() / ".platformio")
        roots.extend(CppTestCase._esphome_build_roots_from_compile_log())
        return roots

    @staticmethod
    def _esphome_build_roots_from_compile_log():
        compile_log = REPO_ROOT / "compile.txt"
        if not compile_log.is_file():
            return []
        roots = []
        for line in compile_log.read_text(errors="ignore").splitlines():
            marker = "/.esphome/build/"
            if marker not in line:
                continue
            before, _, after = line.partition(marker)
            if before:
                roots.append(Path(before + marker + after.split()[0].split("/.pioenvs/")[0]).parent)
        return roots

    def _process_failure_message(self, action, result):
        if result.returncode == 0:
            return None
        output = self._format_process_output(result)
        if output:
            if "ArduinoJson.h' file not found" in output or 'ArduinoJson.h" file not found' in output:
                output += (
                    "\n\nHint: ESPHome's json_util.h uses the real ArduinoJson library, but "
                    "the ESPHome Python package does not ship ArduinoJson.h. Install it into "
                    "a persistent PlatformIO location with:\n"
                    "  pio pkg install --global --library 'bblanchon/ArduinoJson@^7'\n"
                    "or set ARDUINOJSON_INCLUDE_DIR to the directory containing ArduinoJson.h."
                )
            return output
        return f"C++ test {action} failed with exit code {result.returncode} and produced no output."

    @staticmethod
    def _format_process_output(result):
        stdout = result.stdout.strip()
        stderr = result.stderr.strip()
        if stdout and stderr:
            return f"{stderr}\n\nstdout:\n{stdout}"
        return stderr or stdout
