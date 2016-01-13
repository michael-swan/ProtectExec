#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <alloca.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <linux/loop.h>
#include <sys/ioctl.h>
#include "dbg.h"

// Checking capabilities should be done as a robustness measures.
// mknod(2) should be called if all loopback nodes in /dev have been
// exhausted.
    // #include <sys/capability.h>
    // cap_t cap = cap_get_proc();
    // bool can_mknod = cap_get_flag(cap, CAP_MKNOD, ... );

static char *assign_loopback(const char *filename, int dir_fd, DIR *dir);
static char *fd_path(int fd);

// Description:
//   Choose an available loopback device within /dev to link to a given file.
// Parameters:
//   filename - Full path of file to link to a loopback device
// Return:
//   NULL on error, non-NULL on success
char *losetup(const char *filename)
{
    const char *dev_dir_path = "/dev/loop";

    int dev_dir_fd = open(dev_dir_path, O_DIRECTORY|O_RDONLY|O_CLOEXEC);
    DIR *dev_dir = opendir(dev_dir_path);
    
    char *loopback_device_path = assign_loopback(
        filename,
        dev_dir_fd,
        dev_dir
    );

    close(dev_dir_fd);
    closedir(dev_dir);

    return loopback_device_path;
}

// Description:
//   Find an unassigned loopback device within a given directory and assign a
//   given file to it.
// Return:
//   Full path of loopback device file as a string.
//   NULL on error, non-NULL on success
// TODO Wishlist:
//   1. Create new loopback devices if fewer than max_loop nodes have been made
//      and more are needed. (Check CAP_MKNOD first and give helpful error
//      otherwise.)
static char *assign_loopback(const char *filename, int dir_fd, DIR *dir)
{
    struct dirent *de;
    struct stat device_stat;

    de = readdir(dir);

    if(de == NULL)
    {
        debug("Directory entries exhausted without finding an unallocated loopback device.");
        return NULL;
    }

    if(de->d_type != DT_BLK ||
       fstatat(dir_fd, de->d_name, &device_stat, 0) == -1 ||
       major(device_stat.st_rdev) != 7)
    {
        debug("Ignoring non-loopback file. (filename: \"%s\")", de->d_name);
        return assign_loopback(filename, dir_fd, dir);
    }

    // Open the loopback device
    struct loop_info info;
    int loop_fd = openat(dir_fd, de->d_name, O_RDONLY|O_CLOEXEC);

    if(loop_fd == -1)
    {
        debug("openat(2) failed. (errno: %s)", clean_errno());
        debug("openat(%d, \"%s\", O_RDONLY|O_CLOEXEC)", dir_fd, de->d_name);
    }
    else
    {
        if(ioctl(loop_fd, LOOP_GET_STATUS, &info) == -1)
        {
            char *loop_fd_path = fd_path(loop_fd);
            debug("Vacant loopback device found. (filename: \"%s\")", loop_fd_path);
            free(loop_fd_path);

            // Open the source file to assign to the open loopback device
            int file_fd = open(filename, O_RDONLY|O_CLOEXEC);

            if(file_fd == -1)
            {
                debug("open(2) failed. (errno: %s)", clean_errno());
                debug("open(\"%s\", O_RDONLY|O_CLOEXEC)", filename);

                if(close(loop_fd))
                {
                    debug("close(2) failed. (errno: %s)", clean_errno());
                    debug("close(%d)", loop_fd);
                }

                return NULL;
            }

            int ret_ioctl = ioctl(loop_fd, LOOP_SET_FD, file_fd);

            if(ret_ioctl)
            {
                debug("ioctl(2) failed. (errno: %s)", clean_errno());
                debug("ioctl(%d, LOOP_SET_FD, %d)", loop_fd, file_fd);
            }

            int ret_file_close = close(file_fd);
            
            if(ret_file_close)
            {
                debug("close(2) failed. (errno: %s)", clean_errno());
                debug("close(%d)", file_fd);
            }

            if(!ret_ioctl && !ret_file_close)
            {
                char *loop_path = fd_path(loop_fd);

                if(close(loop_fd))
                {
                    if(loop_path != NULL)
                    {
                        free(loop_path);
                    }

                    return NULL;
                }

                return loop_path;
            }
        }

        if(close(loop_fd))
        {
            debug("close(2) failed. (errno: %s)", clean_errno());
            debug("close(%d)", loop_fd);
            return NULL;
        }
    }
    
    return assign_loopback(filename, dir_fd, dir);
}

static char *fd_path(int fd)
{
    char link_path[4097];
    char proc_path[33];
    
    // Calculate path to procfs file descriptor link for file descriptor
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/fd/%d", getpid(), fd);
    
    // Null-terminate link_path
    link_path[4096] = 0;
    
    // Read procfs file descriptor link path into buffer
    ssize_t link_path_size = readlink(proc_path, link_path, 4096);

    if(link_path_size == -1)
    {
        debug("readlink(2) failed. (errno: %s)", clean_errno());
        debug("readlink(\"%s\", \"%s\", 4096)", proc_path, link_path);
        return NULL;
    }

    // Copy file descriptor path to the heap
    char *path = malloc(link_path_size + 1);

    if(path == NULL)
    {
        debug("malloc(3) failed. (errno: %s)", clean_errno());
        debug("malloc(%ld)", link_path_size + 1);
        return NULL;
    }

    memcpy(path, link_path, link_path_size);
    path[link_path_size] = 0;

    return path;
}
