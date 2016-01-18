#define _GNU_SOURCE
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "dbg.h"
#include "protect_exec.h"

#define CGROUP_NAME      "protect_exec_simple_test_cgroup"
#define CGROUP_ROOT_PATH ("/tmp/" CGROUP_NAME)
#define EXEC_PATH        "/executable_program"
#define ROOT_MNT_PATH    "/tmp/protect_exec_test_simple_mnt"

static int chdir_exec(void);
static void usage(void);

int main(int argc, char **argv)
{
	char *cgroup_path;
	int ret = 1;

	// UID must be specified as the first command-line argument
	if(argc < 2)
	{
		log_err("UID not specified in command-line arguments.");
		usage();
		goto error_0;
	}

	// Set the current working directory to the directory containing this
	// executable.
	if(chdir_exec())
	{
		log_err("Test failed. Could not make the current working directory match the current executable's directory.");
		goto error_0;
	}

	int cgroup_path_specified = argc > 2 && argv[2];

	// If a second command-line argument has been given, treat that as a
	// path to the root of an existing Control Group filesystem.
	// Otherwise, mount a new Control Group filesystem.
	if(cgroup_path_specified)
	{
		cgroup_path = argv[2];
	}
	else
	{
		cgroup_path = CGROUP_ROOT_PATH;

		if(mkdir(cgroup_path, 0644))
		{
			log_err("Test failed. mkdir(2) failed.");
			log_err("mkdir(\"%s\", 0644)", cgroup_path);
			goto error_0;
		}

		if(mount(CGROUP_NAME, cgroup_path, "cgroup", MS_RDONLY, "cpu"))
		{
			log_err("Test failed. mount(2) failed.");
			debug("mount(\"%s\", \"%s\", \"cgroup\", MS_RDONLY, \"cpu\")", CGROUP_NAME, cgroup_path);
			goto error_1;
		}

	}

	uid_t uid = (uid_t) atoi(argv[1]);
	const char *mnt_path = ROOT_MNT_PATH;
	const char *exec_path = EXEC_PATH;
	char *const exec_argv[] = { (char *const) exec_path, NULL };
	char *const exec_envp[] = { NULL };

	// Calculate path of SquashFS filesystem.
	char fs_path[PATH_MAX];
	char cwd_path[PATH_MAX];
	getcwd(cwd_path, sizeof(cwd_path));
	snprintf(fs_path, sizeof(fs_path), "%s/root.sqsh", cwd_path);

	if(mkdir(mnt_path, 0644))
	{
		log_err("Test failed. mkdir(2) failed.");
		log_err("mkdir(\"%s\", 0644)", cgroup_path);
		goto error_2;
	}

	// Call protect_exec(3)
	if(protect_exec(uid, fs_path, mnt_path, cgroup_path, exec_path, exec_argv, exec_envp))
	{
		log_err("Test failed. protect_exec(3) failed.");
		goto error_3;
	}

	ret = 0;

error_3:
	if(rmdir(mnt_path))
	{
		log_err("rmdir(2) failed.");
		log_err("rmdir(\"%s\")", mnt_path);
	}
error_2:
	if(!cgroup_path_specified && umount2(cgroup_path, MNT_DETACH))
	{
		log_err("mount2(2) failed.");
		log_err("mount2(\"%s\", MNT_DETACH)", cgroup_path);
	}
error_1:
	if(!cgroup_path_specified && rmdir(cgroup_path))
	{
		log_err("rmdir(2) failed.");
		log_err("rmdir(\"%s\")", cgroup_path);
	}
error_0:
	return ret;
}

static void usage(void)
{
	puts("USAGE: test_simple UID [CGROUP_PATH]");
}

// Description:
//   Change the current working directory to the directory containing the
//   current process' executable.
// Returns:
//   0 on success, -1 on failure.
static int chdir_exec(void)
{
	char program_path[4096];

	if(readlink("/proc/self/exe", program_path, sizeof(program_path)) == -1)
	{
		debug("readlink(2) failed.");
		debug("readlink(\"/proc/self/exe\", program_path, sizeof(program_path))");
		return -1;
	}
	
	if(chdir(dirname(program_path)))
	{
		debug("chdir(2) failed.");
		debug("chdir(\"%s\")", program_path);
		return -1;
	}

	return 0;
}
