fort is a systems programming language that looks quite like C but improves in a few areas: const by
default, sized arrays, modules, etc. Your goal is to build on the design captured in notes directory
and lead a swarm of agents to implement a compiler. Some initial requirements:
- Target: x86 64-bit Linux.
- Produce assembly and then generate object code and link using clang/GCC.
- Write in C so that the compiler is eventually written in fort.
- Write a lot of tests: aim for a 3x coverage ratio.

Let's start by completing the language design and bringing it to a point where you are ready to
implement the compiler. We will then work on an implementation strategy together.

