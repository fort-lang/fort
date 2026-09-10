# abort.s: a hand-written program obeying the codegen contract of
# toolchain.md 6, as the compiler would emit it in checked mode for
#
#     fn i32 main() {
#         println("before");
#         i32[3] a = {};
#         mut i64 i = 5;
#         return a[i];
#     }
#
# in abort.ft, with the "[" of a[i] at line 12, column 14. The bounds check
# (item 12) is one unsigned compare against the immediate length and a branch
# to an out-of-line stub (item 10) that loads the index, the length, .Lfile0
# and the position, zeroes al (item 7), calls fort_rt_fail_bounds@PLT and is
# followed by ud2.
# test/pipeline_test.sh expects "before\n" on stdout, the D11.4 line
# "abort.ft:12:14: runtime error: index 5 out of range for length 3" on
# stderr, and SIGABRT (status 134 under a shell).
        .text

        .globl  main.main
        .type   main.main, @function
        .p2align 4
main.main:
        push    %rbp
        mov     %rsp, %rbp
        sub     $16, %rsp
        mov     $1, %edi
        lea     .Lstr0(%rip), %rsi
        mov     $6, %edx
        xor     %eax, %eax
        call    fort_rt_print_str@PLT
        mov     $1, %edi
        mov     $10, %esi
        xor     %eax, %eax
        call    fort_rt_print_char@PLT
        movl    $0, -12(%rbp)
        movl    $0, -8(%rbp)
        movl    $0, -4(%rbp)
        mov     $5, %rax
        cmp     $3, %rax
        jae     .Lfail0
        mov     -12(%rbp,%rax,4), %eax
        leave
        ret
.Lfail0:
        mov     %rax, %rdi
        mov     $3, %esi
        lea     .Lfile0(%rip), %rdx
        mov     $12, %ecx
        mov     $14, %r8d
        xor     %eax, %eax
        call    fort_rt_fail_bounds@PLT
        ud2
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
        .asciz  "before"
.Lfile0:
        .asciz  "abort.ft"

        .section .note.GNU-stack,"",@progbits
