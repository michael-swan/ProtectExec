#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/loop.h>
#include <linux/sched.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <mntent.h>

#include "config.h"
#include "losetup.h"
#include "dbg.h"

static int protect_exec_clone(void *data);
static bool valid_mntent(struct mntent *me);

struct protect_exec_args {
    uid_t uid;
    char *fs_path;
    char *mnt_path;
    char *cgroup_path;
    char *exec_path;
    char **argv;
    char **envp;
};

// Assumptions:
//  1. 'squashfs' and 'loop' modules have been loaded (or are built into the kernel)
//  2. Loopback devices are found at /dev/loop*
//  3. Process has write access to the current working directory.
int protect_exec(uid_t uid, const char *fs_path, const char *mnt_path,
                 const char *cgroup_path, const char *exec_path,
                 char *const argv[], char *const envp[])
{
    // 0. Trivial input validation
    // Diallowed inputs:
    //   1. NULL pointers
    //   2. UID of 0 (corresponding with root)
    if(uid == 0 || fs_path == NULL || mnt_path == NULL || cgroup_path == NULL ||
       exec_path == NULL || argv == NULL || envp == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    // 1. Link a loopback device to the SquashFS file
    char *loop_path = losetup(fs_path);

    if(loop_path == NULL)
    {
        return -1;
    }

    // 2. Mount that loopback device at /, tmpfs at /db, and all automatic `/etc/fstab` entries (relative to root path)
    // 2a. Mount SquashFS loopback device
    if(mount(loop_path, mnt_path, "squashfs", MS_RDONLY, NULL))
    {
        return -1;
    }

    // 2b. Mount contents of /etc/fstab if it exists
    FILE *me_file;
    {
        char fstab_path[4097];
        snprintf(fstab_path, sizeof(fstab_path), "%s/etc/fstab", mnt_path);
        me_file = setmntent(fstab_path, "r");
    }

    if(me_file != NULL)
    {
        struct mntent *me;
        while(me = getmntent(me_file))
        {
            // Filter mount entries
            if(!valid_mntent(me))
            {
                continue;
            }

            // TODO: Cleanup me->mnt_dir: remove ".." substrings and concatenate with mnt_path
            // TODO: Add support for mount options (after safely processing me->mnt_opts)

            // Mount valid fstab entry
            mount(me->mnt_fsname, me->mnt_dir, me->mnt_type, 0, NULL);
        }
    }

    size_t clone_stack_size = sysconf(_SC_PAGESIZE);
    void *clone_stack = alloca(clone_stack_size);

    struct protect_exec_args *args = alloca(sizeof(struct protect_exec_args));
    args->uid = uid;
    args->fs_path = fs_path;
    args->mnt_path = mnt_path;
    args->cgroup_path = cgroup_path;
    args->exec_path = exec_path;
    args->argv = argv;
    args->envp = envp;

    // 3. Call `clone(2)`, detaching from certain namespaces
    int status;
    pid_t clone_pid = clone(protect_exec_clone, clone_stack + clone_stack_size,
                            CLONE_NAMESPACES | CLONE_VFORK | SIGCHLD, args);

    if(clone_pid == -1 || waitpid(clone_pid, &status, 0) == -1 || status != 0)
    {
        return -1;
    }

    return 0;
}

// TODO:
//   1. Conceive a convention for returning errors from a clone(2) function.
static int protect_exec_clone(void *data)
{
    struct protect_exec_args *args = data;

    // 4. Join the specified cgroup
    // 4a. Acquire a file descriptor to the cgroups 'tasks' file
    int cgroup_dir_fd = open(args->cgroup_path, O_DIRECTORY|O_RDONLY|O_CLOEXEC);
    if(cgroup_dir_fd == -1)
    {
        return -1;
    }

    int cgroup_tasks_fd = openat(cgroup_dir_fd, "tasks", O_RDWR|O_CLOEXEC);
    if(cgroup_tasks_fd == -1)
    {
        return -1;
    }

    // 4b. Convert the current process' pid into a string
    char pid_string[11];
    snprintf(pid_string, sizeof(pid_string), "%d", getpid());

    // 4c. Write the current process' pid to the tasks file
    size_t pid_string_size = strnlen(pid_string, 10);
    int ret = write(cgroup_tasks_fd, pid_string, pid_string_size);

    if(ret != pid_string_size)
    {
        return -1;
    }

    // 5. `pivot_root(2)`'s into the new root, overlaying the old root onto the new root
    // 5a. Acquire file descriptors for both the old and the new root
    int old_root_fd = open("/", O_DIRECTORY|O_RDONLY|O_CLOEXEC);
    if(old_root_fd == -1)
    {
        return -1;
    }

    int new_root_fd = open(args->mnt_path, O_DIRECTORY|O_RDONLY|O_CLOEXEC);
    if(new_root_fd == -1)
    {
        return -1;
    }

    // 5b. Perform `pivot_root(2)`
    if(fchdir(new_root_fd))
    {
        return -1;
    }

    if(pivot_root(".", "."))
    {
        return -1;
    }

    // 5c. Unmount the old root
    if(fchdir(old_root_fd))
    {
        return -1;
    }

    if(umount2(".", MNT_DETACH))
    {
        return -1;
    }

    // 5d. Change current working directory to the new root
    if(fchdir(new_root_fd))
    {
        return -1;
    }
    
    // 6. Change the namespaces to their desired configuration (e.g. unmount everything not from step 2, remove all interfaces)
    // TODO: Figure out what to change and how to change it.
    
    // 7. Perform `setuid(2)` with the specified UID
    if(setuid(args->uid))
    {
        return -1;
    }

    // 8. `execve(2)` the specified program
    execve(args->exec_path, args->argv, args->envp);

    // NOTE: The following is only reached upon the failure of `execve(2)`
    return -1;
}

static bool valid_mntent(struct mntent *me)
{
    char *ty = me->mnt_type;
    bool valid_mnt_type = !(strcmp(ty, "proc") && strcmp(ty, "tmpfs") && strcmp(ty, "sysfs"));

    // TODO: Add further validation.

    return valid_mnt_type;
}
