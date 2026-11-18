#include "pdf_render.h"

int main(void)
{
    return trytr_pdf_run_chromium("/unused/document.html", "/unused/output.pdf", "/unused/profile") ? 1 : 0;
}
