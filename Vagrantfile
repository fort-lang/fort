# -*- mode: ruby -*-
# vi: set ft=ruby :
#
# The fort development VM: Ubuntu 24.04 arm64 on VirtualBox with the native and
# x86-64 cross toolchains installed by tools/provision.sh (see
# notes/environment.md 1).
#
# Do not run `vagrant` directly. Run it only through `tools/vm`, which points
# VAGRANT_CWD at the VM directory ($FORT_VM_DIR, or else the main checkout) so
# every worktree shares one VM, and which keeps the cached ssh configuration
# in sync.
#
# Environment variables read at `vagrant up`:
#   FORT_VM_CPUS    number of virtual CPUs (default 6)
#   FORT_VM_MEMORY  memory in MiB (default 8192)
#   FORT_VM_SLOT    a second VM from this directory (set by tools/vm; default 1)

Vagrant.configure("2") do |config|
  config.vm.box = "bento/ubuntu-24.04"
  config.vm.box_version = ">= 202510.26.0"
  config.vm.hostname = "fort"

  # The directory containing this Vagrantfile (the repository root, including
  # .git/ and .worktrees/) is /vagrant in the guest. Worktree .git files hold
  # absolute host paths, so tools/provision.sh symlinks the host path to
  # /vagrant inside the guest to make git work there too.
  host_repo = File.realpath(File.dirname(__FILE__))
  config.vm.synced_folder ".", "/vagrant"

  # Do not persist the vboxsf mount into /etc/fstab: with persistence on, the
  # share is mounted at boot and again at `vagrant up`, stacking two
  # superblocks with independent page caches over the same files.
  config.vm.allow_fstab_modification = false

  config.vm.provider "virtualbox" do |vb|
    # One VirtualBox machine per VM directory and slot: fort-dev-<directory
    # name>, e.g. fort-dev-fort for the main checkout, so a VM brought up from a
    # worktree (FORT_VM_DIR) does not collide with the main checkout's; and
    # fort-dev-<directory name>-<slot> for a second VM from the same directory
    # (FORT_VM_SLOT, set by tools/vm), which mounts the same tree and differs
    # only in its state directory and its name.
    slot = ENV["FORT_VM_SLOT"].to_s
    slot = "1" if slot.empty?
    vb.name = slot == "1" ? "fort-dev-#{File.basename(host_repo)}" :
                            "fort-dev-#{File.basename(host_repo)}-#{slot}"
    vb.cpus = (ENV["FORT_VM_CPUS"] || "6").to_i
    vb.memory = (ENV["FORT_VM_MEMORY"] || "8192").to_i
    # No audio device: the driver hangs the VM when the Mac wakes from sleep.
    vb.customize ["modifyvm", :id, "--audio-driver", "none"]
  end

  config.vm.provision "shell",
    path: "tools/provision.sh",
    env: { "FORT_HOST_REPO" => host_repo }
end
