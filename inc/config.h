#ifndef _PROTECT_EXEC_CONFIG_H
#define _PROTECT_EXEC_CONFIG_H

#include <sched.h>

// Stack space for pre-execve clone functions to execute within.
// Default: 1MB
#define CLONE_STACK_SIZE (1<<20)

// Namespaces for clone process to detach from
#define CLONE_NAMESPACES (CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWNET)

// Path of directory to check for loopback device files
#define LOOPBACK_DEV_DIR "/dev/loop"

#endif
