/*
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * libc_init_static.c
 *
 * The program startup function __libc_init() defined here is
 * used for static executables only (i.e. those that don't depend
 * on shared libraries). It is called from arch-$ARCH/bionic/crtbegin_static.S
 * which is directly invoked by the kernel when the program is launched.
 *
 * The 'structors' parameter contains pointers to various initializer
 * arrays that must be run before the program's 'main' routine is launched.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <elf.h>
#include "pthread_internal.h"
#include "atexit.h"
#include "libc_init_common.h"

#include <bionic_tls.h>
#include <errno.h>
#include <sys/mman.h>

// Returns the address of the page containing address 'x'.
#define PAGE_START(x)  ((x) & PAGE_MASK)

// Returns the address of the next page after address 'x', unless 'x' is
// itself at the start of a page.
#define PAGE_END(x)    PAGE_START((x) + (PAGE_SIZE-1))

static void call_array(void(**list)())
{
    // First element is -1, list is null-terminated
    while (*++list) {
        (*list)();
    }
}

/*
 * Find the value of the AT_* variable passed to us by the kernel.
 */
static unsigned find_aux(unsigned *vecs, unsigned type) {
    while (vecs[0]) {
        if (vecs[0] == type) {
            return vecs[1];
        }
        vecs += 2;
    }

    return 0; // should never happen
}

static void apply_gnu_relro(unsigned *vecs) {
    Elf32_Phdr *phdr_start;
    unsigned phdr_ct;
    Elf32_Phdr *phdr;

    phdr_start = (Elf32_Phdr *) find_aux(vecs, AT_PHDR);
    phdr_ct    = find_aux(vecs, AT_PHNUM);

    for (phdr = phdr_start; phdr < (phdr_start + phdr_ct); phdr++) {
        if (phdr->p_type != PT_GNU_RELRO)
            continue;

        Elf32_Addr seg_page_start = PAGE_START(phdr->p_vaddr);
        Elf32_Addr seg_page_end   = PAGE_END(phdr->p_vaddr + phdr->p_memsz);

        // Check return value here? What do we do if we fail?
        mprotect((void *) seg_page_start,
                 seg_page_end - seg_page_start,
                 PROT_READ);
    }
}

__noreturn void __libc_init(uintptr_t *elfdata,
                       void (*onexit)(void),
                       int (*slingshot)(int, char**, char**),
                       structors_array_t const * const structors)
{
    int  argc;
    char **argv, **envp;
    unsigned *vecs;

    __libc_init_tls(NULL);

    /* Initialize the C runtime environment */
    __libc_init_common(elfdata);

    /* Several Linux ABIs don't pass the onexit pointer, and the ones that
     * do never use it.  Therefore, we ignore it.
     */

    /* pre-init array. */
    call_array(structors->preinit_array);

    // call static constructors
    call_array(structors->init_array);

    argc = (int) *elfdata;
    argv = (char**)(elfdata + 1);
    envp = argv + argc + 1;

    // The auxiliary vector is at the end of the environment block
    vecs = (unsigned *) envp;
    while (vecs[0] != 0) {
        vecs++;
    }
    /* The end of the environment block is marked by two NULL pointers */
    vecs++;

    /* The executable may have its own destructors listed in its .fini_array
     * so we need to ensure that these are called when the program exits
     * normally.
     */
    if (structors->fini_array)
        __cxa_atexit(__libc_fini,structors->fini_array,NULL);

    apply_gnu_relro(vecs);
    exit(slingshot(argc, argv, envp));
}
