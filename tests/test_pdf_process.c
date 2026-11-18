#include "pdf_render.h"

int main(int argc, char **argv)
{
    return trytr_pdf_run_chromium(argc > 1 ? argv[1] : "/unused/document.html",
        "/unused/output.pdf", "/unused/profile") ? 1 : 0;
}
