# hello.s: a hand-written program obeying the codegen contract of
# toolchain.md 6, as the compiler would emit it for
#
#     fn i32 main() { println("hello, world!"); return 0; }
#
# in main.ft: PIE, RIP-relative data, @PLT calls into the runtime, a frame in
# every function, rsp a multiple of 16 before every call, al zeroed before
# every extern call, one .Lfile record for the module (item 3) even though no
# check refers to it. fort_entry receives the args slice by pointer, copies it
# into its frame and calls main.main (item 18). test/pipeline_test.sh
# assembles it with fort_rt.o and expects "hello, world!\n" and status 0.
        .text

        .globl  main.main
        .type   main.main, @function
        .p2align 4
main.main:
        push    %rbp
        mov     %rsp, %rbp
        mov     $1, %edi
        lea     .Lstr0(%rip), %rsi
        mov     $13, %edx
        xor     %eax, %eax
        call    fort_rt_print_str@PLT
        mov     $1, %edi
        mov     $10, %esi
        xor     %eax, %eax
        call    fort_rt_print_char@PLT
        xor     %eax, %eax
        pop     %rbp
        ret
        .size   main.main, .-main.main

        .globl  fort_entry
        .type   fort_entry, @function
        .p2align 4
fort_entry:
        push    %rbp
        mov     %rsp, %rbp
        sub     $16, %rsp
        mov     (%rdi), %rax
        mov     %rax, -16(%rbp)
        mov     8(%rdi), %rax
        mov     %rax, -8(%rbp)
        call    main.main
        leave
        ret
        .size   fort_entry, .-fort_entry

        .section .rodata
.Lstr0:
        .asciz  "hello, world!"
.Lfile0:
        .asciz  "main.ft"

        .section .note.GNU-stack,"",@progbits
