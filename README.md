# ProtectExec

`protect_exec(3)` is intended to provide a single execve-like function that accepts the following arguments:

 - SquashFS file path
 - Path to mount SquashFS file (a.k.a. root path)
 - Path to the root of the desired cgroup filesystem
 - UID in which to execute the program
 - Arguments to pass to `execve(2)`

and performs the following steps:

 1. Link a loopback device to the SquashFS file
 2. Mount that loopback device at /, tmpfs at /db, and all automatic `/etc/fstab` entries (relative to root path)
 3. Call `clone(2)` with `CLONE_NEWNS`, `CLONE_NEWNET`, `CLONE_NEWIPC`, and `CLONE_NEWUTS` options set
 4. Join the specified cgroup (Within the child, write the current pid into the cgroup tasks file)
 5. `chroot(2)`'s into that new root
 6. Change the namespaces to their desired configuration (e.g. unmount everything not from step 2, remove all interfaces)
 7. Perform `setuid(2)` with the specified UID
 8. `execve(2)` the specified program
