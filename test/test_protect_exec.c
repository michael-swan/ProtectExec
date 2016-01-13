#include <unistd.h>
#include "protect_exec.h"

int main(void)
{
    // losetup("/home/mswan/ProtectExec/example");
    char *argv[] = { NULL };
    char *envp[] = { NULL };
    if(protect_exec(1000, "/home/mswan/ProtectExec/example.sqfs",
                    "/home/mswan/ProtectExec/mnt", "/mnt/cgroup/cg0/protect_exec",
                    "/program", argv, envp))
    {
        return 1;
    }
    return 0;
}
