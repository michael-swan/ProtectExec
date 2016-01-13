#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/loop.h>
#include <linux/sched.h>
#include <sched.h>
#include <alloca.h>
#include <signal.h>
#include <stdbool.h>
#include <mntent.h>
#include <syscall.h>

#include "config.h"
#include "losetup.h"
#include "dbg.h"

static int protect_exec_clone(void *data);
static bool valid_mntent(struct mntent *me);
static int pivot_root(const char *new_root, const char *put_old);

struct protect_exec_args {
    uid_t uid;
    const char *fs_path;
    const char *mnt_path;
    const char *cgroup_path;
    const char *exec_path;
    char *const *argv;
    char *const *envp;
};

// Assumptions:
//  1. 'squashfs' and 'loop' modules have been loaded (or are built into the kernel)
//  2. Loopback devices are found at /dev/loop*
//  3. Process has write access to the current working directory.
int protect_exec(uid_t uid, const char *fs_path, const char *mnt_path,
                 const char *cgroup_path, const char *exec_path,
                 char *const argv[], char *const envp[])
{
    int ret = -1;
    int loop_fd;

    // 0. Trivial input validation
    // Diallowed inputs:
    //   1. NULL pointers
    //   2. UID of 0 (corresponding with root)
    if(uid == 0 || fs_path == NULL || mnt_path == NULL || cgroup_path == NULL ||
       exec_path == NULL || argv == NULL || envp == NULL)
    {
        errno = EINVAL;
        debug("Input validation failed. (errno: %s)", clean_errno());
        goto error_0;
    }

    // 1. Link a loopback device to the SquashFS file
    char *loop_path = losetup(fs_path);

    if(loop_path == NULL)
    {
        debug("Loopback device assignment failed. (errno: %s)", clean_errno());
        goto error_0;
    }

    // 2. Mount that loopback device at /, tmpfs at /db, and all automatic `/etc/fstab` entries (relative to root path)
    // 2a. Mount SquashFS loopback device
    if(mount(loop_path, mnt_path, "squashfs", MS_RDONLY, NULL))
    {
        debug("mount(2) failed. (errno: %s)", clean_errno());
        goto error_1;
    }

    // 2b. Mount contents of /etc/fstab if it exists
    char fstab_path[4097];
    snprintf(fstab_path, sizeof(fstab_path), "%s/etc/fstab", mnt_path);
    FILE *me_file = setmntent(fstab_path, "r");

    if(me_file == NULL)
    {
        debug("setmntent(3) failed. (errno: %s)", clean_errno());
        debug("setmntent(\"%s\", \"r\")", fstab_path);
    }
    else
    {
        struct mntent *me;
        while((me = getmntent(me_file)))
        {
            // Filter mount entries
            if(!valid_mntent(me))
            {
                debug("Invalid mount entry found in root filesystem '/etc/fstab'.");
                continue;
            }

            // TODO: Sanitize me->mnt_dir: remove ".." substrings
            // TODO: Add support for mount options (after safely processing me->mnt_opts)
            // Concatenate me->mnt_dir with mnt_path
            char mnt_dir[4097];
            snprintf(mnt_dir, sizeof(mnt_dir), "%s%s", mnt_path, me->mnt_dir);

            // Mount valid fstab entry
            if(mount(me->mnt_fsname, mnt_dir, me->mnt_type, 0, NULL))
            {
                // '/etc/fstab' mount failure is not considered fatal
                debug("mount(2) failed while processing '/etc/fstab'. (errno: %s)", clean_errno());
            }
        }

        // Close mount entry file descriptor
        endmntent(me_file);
    }

    // 3. Perform `clone(2)`, detaching from certain namespaces
    // 3a. Allocate several pages for the `clone(2)` stack.
    size_t clone_stack_size = 4096 * 8;
    long page_size = sysconf(_SC_PAGESIZE);

    // When debugging, emit a warning if clone_stack_size is suboptimal.
    if(page_size == -1)
    {
        debug("sysconf(3) failed. (errno: %s)", clean_errno());
        debug("sysconf(_SC_PAGESIZE)");
    }
    else if((size_t) page_size < clone_stack_size && clone_stack_size % (size_t) page_size != 0)
    {
        debug("clone_stack_size is not a multiple of the system page size.");
    }
    else if((size_t) page_size > clone_stack_size)
    {
        debug("clone_stack_size is smaller than a system page.");
    }

    char *clone_stack = alloca(clone_stack_size);

    // 3b. Store the arguments to pass to `clone(2)` in a dynamically allocated struct
    struct protect_exec_args args;
    args.uid = uid;
    args.fs_path = fs_path;
    args.mnt_path = mnt_path;
    args.cgroup_path = cgroup_path;
    args.exec_path = exec_path;
    args.argv = argv;
    args.envp = envp;

    // 3c. Call `clone(2)` synchronously
    int status = 0;
    pid_t clone_pid = clone(protect_exec_clone, clone_stack + clone_stack_size,
                            CLONE_NAMESPACES | CLONE_VFORK | SIGCHLD, &args);

    if(clone_pid == -1)
    {
        debug("clone(2) failed. (errno: %s)", clean_errno());
        goto error_2;
    }

    debug("clone(2) completed.");

    if(waitpid(clone_pid, &status, 0) == -1)
    {
        debug("waitpid(2) failed. (errno: %s)", clean_errno());
        goto error_2;
    }
    else
    {
        debug("waitpid(2) completed.");

        if(status != 0)
        {
            debug("clone(2) and waitpid(2) completed with non-success status code. (status: %d)", WEXITSTATUS(status));
            goto error_2;
        }
    }

    ret = 0;

error_2:
    if(umount2(mnt_path, MNT_DETACH))
    {
        debug("umount2(2) failed. (errno: %s)", clean_errno());
        debug("umount2(\"%s\", MNT_DETACH)", mnt_path);
    }
error_1:
    loop_fd = open(loop_path, O_NONBLOCK);

    if(loop_fd == -1)
    {
        debug("open(2) failed. (errno: %s)", clean_errno());
        debug("open(\"%s\", O_NONBLOCK)", loop_path);
    }

    if(ioctl(loop_fd, LOOP_CLR_FD))
    {
        debug("ioctl(2) failed. (errno: %s)", clean_errno());
        debug("ioctl(%d, LOOP_CLR_FD)", loop_fd);
    }

    if(close(loop_fd))
    {
        debug("close(2) failed. (errno: %s)", clean_errno());
        debug("close(%d)", loop_fd);
    }

    free(loop_path);
error_0:
    return ret;
}

static int protect_exec_clone(void *data)
{
    struct protect_exec_args *args = data;

    // 4. Join the specified cgroup
    // 4a. Acquire a file descriptor to the cgroups 'tasks' file
    int cgroup_dir_fd = open(args->cgroup_path, O_DIRECTORY|O_RDONLY|O_CLOEXEC);
    if(cgroup_dir_fd == -1)
    {
        debug("open(2) failed. (errno: %s)", clean_errno());
        debug("open(\"%s\", O_DIRECTORY|O_RDONLY|O_CLOEXEC)", args->cgroup_path);
        return -1;
    }

    int cgroup_tasks_fd = openat(cgroup_dir_fd, "tasks", O_RDWR|O_CLOEXEC);
    if(cgroup_tasks_fd == -1)
    {
        debug("openat(2) failed. (errno: %s)", clean_errno());
        debug("openat(%d, \"tasks\", O_RDWR|O_CLOEXEC)", cgroup_dir_fd);
        return -1;
    }

    // 4b. Convert the current process' pid into a string
    char pid_string[11];
    snprintf(pid_string, sizeof(pid_string), "%d", getpid());

    // 4c. Write the current process' pid to the tasks file
    size_t pid_string_size = strnlen(pid_string, 10);
    int ret = write(cgroup_tasks_fd, pid_string, pid_string_size);

    if((size_t) ret != pid_string_size)
    {
        debug("write(2) failed. (errno: %s)", clean_errno());
        debug("write(%d, \"%s\", %lu)", cgroup_tasks_fd, pid_string, pid_string_size);
        return -1;
    }

    // 5. `pivot_root(2)`'s into the new root, overlaying the old root onto the new root
    // 5a. Acquire file descriptors for both the old and the new root
    int old_root_fd = open("/", O_DIRECTORY|O_RDONLY|O_CLOEXEC);
    if(old_root_fd == -1)
    {
        debug("open(2) failed. (errno: %s)", clean_errno());
        debug("open(\"/\", O_DIRECTORY|O_RDONLY|O_CLOEXEC)");
        return -1;
    }

    int new_root_fd = open(args->mnt_path, O_DIRECTORY|O_RDONLY|O_CLOEXEC);
    if(new_root_fd == -1)
    {
        debug("open(2) failed. (errno: %s)", clean_errno());
        debug("open(\"%s\", O_DIRECTORY|O_RDONLY|O_CLOEXEC)", args->mnt_path);
        return -1;
    }

    // 5b. Perform `pivot_root(2)`
    if(fchdir(new_root_fd))
    {
        debug("fchdir(2) failed. (errno: %s)", clean_errno());
        debug("fchdir(%d)", new_root_fd);
        return -1;
    }

    if(pivot_root(".", "."))
    {
        debug("pivot_root(2) failed. (errno: %s)", clean_errno());
        debug("pivot_root(\".\", \".\")");
        return -1;
    }

    // 5c. Unmount the old root
    if(fchdir(old_root_fd))
    {
        debug("fchdir(2) failed. (errno: %s)", clean_errno());
        debug("fchdir(%d)", old_root_fd);
        return -1;
    }

    if(umount2(".", MNT_DETACH))
    {
        debug("umount2(2) failed. (errno: %s)", clean_errno());
        debug("umount2(\".\", MNT_DETACH)");
        return -1;
    }

    // 5d. Change current working directory to the new root
    if(fchdir(new_root_fd))
    {
        debug("fchdir(2) failed. (errno: %s)", clean_errno());
        debug("fchdir(%d)", new_root_fd);
        return -1;
    }
    
    // 6. Change the namespaces to their desired configuration (e.g. unmount everything not from step 2, remove all interfaces)
    // TODO: Figure out what to change and how to change it.
    
    // 7. Perform `setuid(2)` with the specified UID
    if(setuid(args->uid))
    {
        debug("setuid(2) failed. (errno: %s)", clean_errno());
        debug("setuid(%d)", args->uid);
        return -1;
    }

    // 8. `execve(2)` the specified program
    execve(args->exec_path, args->argv, args->envp);

    // NOTE: The following is only reached upon the failure of `execve(2)`
    debug("execve(2) failed. (errno: %s)", clean_errno());
    debug("execve(\"%s\", %p, %p)", args->exec_path, args->argv, args->envp);

    return -1;
}

static bool valid_mntent(struct mntent *me)
{
    char *ty = me->mnt_type;
    bool valid_mnt_type = !(strcmp(ty, "proc") && strcmp(ty, "tmpfs") && strcmp(ty, "sysfs"));

    // TODO: Add further validation.

    return valid_mnt_type;
}

static int pivot_root(const char *new_root, const char *put_old)
{
    return syscall(__NR_pivot_root, new_root, put_old);
}
