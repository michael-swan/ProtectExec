int protect_exec(uid_t uid, const char *fs_path, const char *mnt_path,
                 const char *cgroup_path, const char *exec_path,
                 char *const argv[], char *const envp[]);
