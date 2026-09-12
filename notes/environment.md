# fort environment: the VM, the build and the shared folder

This document holds every fact about the machine the project builds on: the Vagrant VM, the
`tools/vm` wrapper, the VirtualBox shared folder, the cross toolchain, and the CMake presets and
targets. It is not normative about the language; `notes/decisions.md` and `notes/grammar.md` win
over it (D1.2).

The headings below are empty. T-098 created them; T-099 moves the text of the `## Environment`
section of `AGENTS.md` and of the build half of `## Build and test` into them, one bullet at a
time and without a rewrite. Each heading names what it takes.

## 1. The VM

The Vagrant box, `tools/vm` and its subcommands, `FORT_VM_DIR`, `FORT_VM_CPUS`, `FORT_VM_MEMORY`,
one VM for each ticket, the cost of a shared VM, and the recovery after the host sleeps.

## 2. The shared folder

`/vagrant`, the guest `/tmp` that is not the host's, the stale `mmap` pages, the stale mtime that
makes ninja print "no work to do", and how to recover from each.

## 3. Provisioning

`tools/provision.sh`, the packages it installs, `QEMU_LD_PREFIX`, apport and `kernel.core_pattern`,
and the git symlink that makes a worktree's `.git` file resolve in the guest.

## 4. The cross toolchain

The x86-64 Linux target, the arm64 host compiler, `--cc` and `--target`, `qemu-x86_64`, and the
`qemu: uncaught target signal` line that a harness strips from stderr.

## 5. The build

The CMake presets, the targets, the build directories, the `gcc` preset that no gate runs, the
`find_program` cache trap, and the stage1, stage2 and stage3 binaries.

## 6. The host side

What runs on the host and not in the guest: git, the VS Code extension, the ssh `ControlPath`
length limit on macOS, and `tools/vm run` as the one path from the host to the guest.
