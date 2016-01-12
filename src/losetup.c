#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/loop.h>
#include "losetup.h"

// Checking capabilities should be done as a robustness measures.
// mknod(2) should be called if all loopback nodes in /dev have been
// exhausted.
    // #include <sys/capability.h>
    // cap_t cap = cap_get_proc();
    // bool can_mknod = cap_get_flag(cap, CAP_MKNOD, ... );

static int assign_loopback(const char *filename, int dir_fd, DIR *dir);

// Description:
//   Choose an available loopback device within /dev to link to a given file.
// Parameters:
//   filename - Full path of file to link to a loopback device
// Return:
//   NULL on error, non-NULL on success
char *losetup(const char *filename)
{
    const char *dev_dir_path = "/dev/loop";

    return assign_loopback(
        filename,
        open(dev_dir_path, O_RDONLY|O_CLOEXEC),
        opendir(dev_dir_path)
    );
}

// Description:
//   Find the next available loopback device within a given directory.
// Return:
//   File descripto of loop
//   NULL on error, non-NULL on success
// TODO Wishlist:
//   1. Create new loopback devices if fewer than max_loop nodes have been made and more are needed. (Check CAP_MKNOD first and give helpful error otherwise.)
static char *assign_loopback(const char *filename, int dir_fd, DIR *dir)
{
    struct dirent *de;
    struct stat device_stat;

    de = readdir(dir);

    if(de == NULL)
    {
        return NULL;
    }

    if(de->d_type != DT_BLK ||
       fstatat(dir_fd, de->d_name, &device_stat, 0) == -1 ||
       major(device_stat.st_rdev) != 7)
    {
        return assign_loopback(filename, dir_fd, dir);
    }

    // Loopback device
    struct loop_info info;
    int loop_fd = openat(dir_fd, de->d_name, O_RDONLY|O_CLOEXEC);
    if(loop_fd != -1 && ioctl(loop_fd, LOOP_GET_STATUS, &info) == -1)
    {
        // Available loopback device
        int file_fd = open(filename, O_RDONLY|O_CLOEXEC);

        if(file_fd == -1)
        {
            close(loop_fd);
            return NULL;
        }

        int ret_ioctl = ioctl(loop_fd, LOOP_SET_FD, file_fd);
        int ret_file_close = close(file_fd);
        int ret_loop_close = close(loop_fd);
        
        if(ret_ioctl || ret_file_close || ret_loop_close)
        {
            return NULL;
        }
        else
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
        return NULL;
    }
    
    return assign_loopback(filename, dir_fd, dir);
}

static char *fd_path(int fd)
{
    char link_path[4097];
    char proc_path[31];
    
    // Calculate path to procfs file descriptor link for file descriptor
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/fd/%d", getpid(), fd);
    
    // Null-terminate link_path
    link_path[4096] = 0;
    
    // Read procfs file descriptor link path into buffer
    ssize_t link_path_size = readlink(proc_path, link_path, 4096);

    // Copy file descriptor path to the heap
    char *path = malloc(link_path_size + 1);

    if(path == NULL)
    {
        return NULL;
    }

    memcpy(path, link_path, link_path_size);
    path[link_path_size] = 0;

    return path;
}
