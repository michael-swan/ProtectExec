#include <unistd.h>
#include "losetup.h"

int main(void)
{
    // losetup("/home/mswan/ProtectExec/example");
    char *argv[] = { NULL };
    char *envp[] = { NULL };
    if(protect_exec(1000, "/home/mswan/ProtectExec/example.sqfs",
                    "/home/mswan/ProtectExec/mnt", "/",
                    "/program", argv, envp))
    {
        return 1;
    }
    return 0;
}
