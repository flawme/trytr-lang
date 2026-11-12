#ifndef TRYTR_SRC_VM_H
#define TRYTR_SRC_VM_H

#include "ir.h"

TrytrStatus trytr_vm_execute(
    const TrytrIRProgram *program,
    TrytrElementVisitor visitor,
    void *user_data
);

#endif
