#include "PluginHost.h"
#include <cstdlib>
#include <cstring>

int main(int argc, char* argv[]) {
    uint32_t slotId = 0;
    std::string pipeName, shmName, pluginPath;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--slot=", 7) == 0)
            slotId = static_cast<uint32_t>(atoi(argv[i] + 7));
        else if (strncmp(argv[i], "--pipe=", 7) == 0)
            pipeName = argv[i] + 7;
        else if (strncmp(argv[i], "--shm=", 6) == 0)
            shmName = argv[i] + 6;
        else if (strncmp(argv[i], "--plugin=", 9) == 0)
            pluginPath = argv[i] + 9;
    }

    if (pipeName.empty() || shmName.empty() || pluginPath.empty())
        return 1;

    PluginHost host(slotId, pipeName, shmName, pluginPath);
    return host.run();
}
