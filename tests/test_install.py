"""Install to a temporary prefix, relocate it, and build a separate C consumer."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(*arguments):
    subprocess.run(arguments, check=True, timeout=60)


with tempfile.TemporaryDirectory(prefix="trytr-install-") as directory:
    root = Path(directory)
    original = root / "original prefix"
    relocated = root / "relocated prefix"
    run("cmake", "--install", sys.argv[1], "--prefix", str(original))
    shutil.move(original, relocated)
    run(str(relocated / "bin/trytr"), "--version")
    source = root / "consumer"
    source.mkdir()
    (source / "CMakeLists.txt").write_text("""
cmake_minimum_required(VERSION 3.20)
project(consumer LANGUAGES C)
find_package(Trytr 0.1 CONFIG REQUIRED)
add_executable(consumer main.c)
target_compile_features(consumer PRIVATE c_std_23)
target_link_libraries(consumer PRIVATE trytr::core trytr::proof)
""")
    (source / "main.c").write_text(r"""
#include <trytr/stream.h>
#include <trytr/html.h>
#include <trytr/algebra.h>
#include <stdlib.h>
int main(void) {
    TrytrStream *stream = NULL;
    if (trytr_stream_create(NULL, &stream) != TRYTR_STATUS_OK) return 1;
    TrytrSnapshot *snapshot = trytr_stream_acquire_snapshot(stream);
    char *html = NULL;
    size_t length = 0;
    const TrytrStatus status = trytr_html_render(trytr_snapshot_document(snapshot), &html, &length);
    free(html);
    trytr_snapshot_release(snapshot);
    trytr_stream_destroy(stream);
    if (status != TRYTR_STATUS_OK || length == 0) return 2;
    const TrytrAlgebraResult result = trytr_algebra_check_rational("x/x=1");
    return result.status == TRYTR_ALGEBRA_CONDITIONAL && result.condition_count == 1 ? 0 : 3;
}
""")
    build = root / "consumer-build"
    run("cmake", "-S", str(source), "-B", str(build), f"-DCMAKE_PREFIX_PATH={relocated}")
    run("cmake", "--build", str(build))
    run(str(build / "consumer"))
