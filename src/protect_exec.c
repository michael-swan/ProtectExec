#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sched.h>
#include <unistd.h>
#include <linux/loop.h>

// #include "config.h"
#include "protect_exec.h"
#include "dbg.h"

// Statically allocate stack space for clone
static char clone_stack[CLONE_STACK_SIZE];

// Parameters: 
// Return: -1 on error, file descriptor (>0) on success 
static int losetup(const char *fs_path) {
    int status;
    pid_t child_pid;
    
    // Create child stdout pipe
    int stdout_pipe[2] = { -1, -1 };
    if(pipe(stdout_pipe) < 0) {
        // 'pipe' creation failed
        return -1;
    }
    // Start a fork
    child_pid = fork();

    int ret = -5555;
    if(child_pid == 0) {
        // Connect pipe - child side
        dup2(stdout_pipe[1], 1);
        // Execute 'losetup' within the child
        char *const args[] = { "losetup", "-f", fs_path, NULL };
        char *const envp[] = { NULL };
        ret = execve("/sbin/losetup", args, envp);
        exit(0);
    } else {
        // Wait for 'losetup' command to complete
        // waitpid(child_pid, &status, options);
        pid_t p = wait(&status);
        while(p != child_pid && p != -1) {
            p = wait(&status);
        }

        char buf[4097];
        int c = read(stdout_pipe[0], buf, 4096);
        while(c != 0 && c != -1) {
            buf[c] = 0;
            printf("PRINTING: %s\n", buf);
            c = read(stdout_pipe[0], buf, 4096);
        }

        if(c == -1) {
            return -1;
        }

        if(p == child_pid) {
            return 0;
        } else {
            return -1;
        }

    }

    return -1;
}

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
    mount(loop_path, mnt_path, "squashfs", MS_NOSUID, NULL);
    // 2b. Mount contents of /etc/fstab
    FILE *me_file = setmntent("/etc/fstab", "r");
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
    
    // 3. Call `clone(2)`, detaching from certain namespaces
    clone(protect_exec_clone, clone_stack + CLONE_STACK_SIZE, CLONE_NAMESPACES, NULL);

    return 0;
}

// TODO:
//   1. Conceive a convention for returning errors from a clone(2) function.
static int protect_exec_clone(void *data)
{
    // 4. Join the specified cgroup
    // 4a. Acquire a file descriptor to the cgroups 'tasks' file
    int cgroup_dir_fd = open(cgroup_path, O_DIRECTORY|O_RDONLY|O_CLOEXEC);
    if(cgroup_dir_fd == -1)
    {
        return -1;
    }

    int cgroup_tasks_fd = openat(cgroup_dir_fd, "tasks", O_RDONLY|O_CLOEXEC);
    if(cgroup_tasks_fd == -1)
    {
        return -1;
    }

    // 4b. Convert the current process' pid into a string
    char pid_string[11];
    snprintf(pid_string, sizeof(pid_string), "%d", getpid());

    // 4c. Write the current process' pid to the tasks file
    size_t pid_string_size = strnlen(pid_string, 10);

    if(write(cgroup_tasks_fd, pid_string, pid_string_size) != pid_string_size)
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

    int new_root_fd = open(mnt_path, O_DIRECTORY|O_RDONLY|O_CLOEXEC);
    if(new_root_fd == -1)
    {
        return -1;
    }

    // 5b. Perform `pivot_root(2)`
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
    if(setuid(uid))
    {
        return -1;
    }

    // 8. `execve(2)` the specified program
    execve(exec_path, argv, envp);

    // NOTE: The following is only reached upon the failure of `execve(2)`
    return -1;
}

static bool valid_mntent(struct mntent *me)
{
    char *ty = me->mnt_type;
    bool valid_mnt_type = !(strcmp(ty, "proc") && strcmp(ty, "tmpfs") && strcmp(ty, "sysfs"));

    return valid_mnt_type;
}
