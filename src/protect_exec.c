#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/loop.h>

// #include "config.h"
#include "protect_exec.h"
#include "dbg.h"

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
int protect_exec(uid_t uid, const char *fs_path, const char *mnt_path,
                 const char *cgroup_path, const char *exec_path,
                 char *const argv[], char *const envp[])
{
    // losetup(fs_path);
    // 0. Trivial input validation
    // NULL pointers are disallowed in inputs
    // UID of 0 (corresponding with root) is disallowed in inputs
    /*if(uid == 0 || fs_path == NULL || mnt_path == NULL || cgroup_path == NULL ||
       exec_path == NULL || argv == NULL || envp == NULL)
    {
        return -EINVAL;
    }*/
    // 1. Link a loopback device to the SquashFS file
    // 1a. Find an unoccupied loopback device (create more loopback devices if needed)
    // int getdents64
    /*DIR *dev_dir = opendir("/dev");
	struct dirent *file;
	while(file = readdir(root_dir))
	{
        if(file.d_type == DT_BLK || file.d_type == DT_UNKNOWN) {
            dev_t dev;
            char path[264];
            struct stat device_stat;

            sprintf(path, "/dev/%s", file.d_name);

            // Get device file 'stat' and ignore failure
            if(stat(path, &device_stat)) {
                continue;
            }

            dev = device_stat.st_rdev;

            if(MAJOR(dev) == 7) {
                // Loopback device
                int fd = open(path, O_RDONLY|O_CLOEXEC);
                struct loop_info info;
                if(fd != -1 && ioctl(fd, LOOP_GET_STATUS, &info) == -1) {
                    // Available loopback device
                    
                }
            }
        }
	}
    open();
    int fd = open(fs_path, O_RDONLY|O_CLOEXEC);*/
    // 2. Mount that loopback device at /, tmpfs at /db, and all automatic `/etc/fstab` entries (relative to root path)
    // 3. Call `clone(2)` with `CLONE_NEWNS`, `CLONE_NEWNET`, `CLONE_NEWIPC`, and `CLONE_NEWUTS` options set
    // 4. Join the specified cgroup
    // 5. `chroot(2)`'s into that new root
    // 6. Change the namespaces to their desired configuration (e.g. unmount everything not from step 2, remove all interfaces)
    // 7. Perform `setuid(2)` with the specified UID
    // 8. `execve(2)` the specified program

    return 0;
}
