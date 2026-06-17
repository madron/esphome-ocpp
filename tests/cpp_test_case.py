import subprocess
import tempfile
import unittest
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

        with tempfile.TemporaryDirectory() as tmpdir:
            binary = Path(tmpdir) / source.stem
            compile_result = subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-I",
                    str(CPP_TEST_DIR),
                    "-I",
                    str(REPO_ROOT),
                    str(source),
                    *(str(extra_source_path) for extra_source_path in extra_source_paths),
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

    def _process_failure_message(self, action, result):
        if result.returncode == 0:
            return None
        output = self._format_process_output(result)
        if output:
            return output
        return f"C++ test {action} failed with exit code {result.returncode} and produced no output."

    @staticmethod
    def _format_process_output(result):
        stdout = result.stdout.strip()
        stderr = result.stderr.strip()
        if stdout and stderr:
            return f"{stderr}\n\nstdout:\n{stdout}"
        return stderr or stdout
