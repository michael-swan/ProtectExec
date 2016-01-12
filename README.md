# ProtectExec

`protect_exec(3)` is intended to provide a single execve-like function that accepts the following arguments:

 - SquashFS file path
 - Path to mount SquashFS file (a.k.a. root path)
 - Path to the root of the desired cgroup filesystem
 - UID in which to execute the program
 - Arguments to pass to `execve(2)`

and performs the following steps:

 1. Link a loopback device to the SquashFS file
 2. Mount that loopback device at /, tmpfs at /db, and all automatic `/etc/fstab` entries (relative to new root path)
 3. Call `clone(2)` with `CLONE_NEWNS`, `CLONE_NEWNET`, `CLONE_NEWIPC`, and `CLONE_NEWUTS` options set
 4. Join the specified cgroup
 5. `pivot_root(2)`'s into the new root
 6. Change the namespaces to their desired configuration (e.g. unmount everything not from step 2, remove all interfaces)
 7. Perform `setuid(2)` with the specified UID
 8. `execve(2)` the specified program

`protect_exec(3)` must be called by a process with a user which has the following capabilities:

 1. CAP_SYS_ADMIN
 2. CAP_SYS_CHROOT
 3. CAP_SETUID

and write access to the cgroup `tasks` file. In any case, root meets these requirements and is likely the simplest option.

Presently, there is no means of specifying which device nodes should be created besides specifying a devtmpfs in `/etc/fstab`. We could later add support for a node table like CPIO called `/etc/nodtab` or similar.
