// Account-free macOS SDK lifecycle regression. The plugin is supplied locally.
#include <dlfcn.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

template<class T> T symbol(void *handle, const char *name)
{
    auto ptr = dlsym(handle, name);
    if (!ptr) throw std::runtime_error(std::string("Missing symbol: ") + name);
    return reinterpret_cast<T>(ptr);
}

int main(int argc, char **argv)
{
    // plugin, fresh state directory, Studio certificate directory, baseline|stop|stop-immediate
    if (argc != 5) return 2;
    const std::string mode = argv[4];
    if (mode != "baseline" && mode != "stop" && mode != "stop-immediate") return 2;
    void *lib = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL);
    if (!lib) { std::cerr << dlerror() << '\n'; return 3; }
    auto version = symbol<std::string(*)()>(lib, "bambu_network_get_version");
    auto create = symbol<void*(*)(std::string)>(lib, "bambu_network_create_agent");
    auto config = symbol<int(*)(void*, std::string)>(lib, "bambu_network_set_config_dir");
    auto log = symbol<int(*)(void*)>(lib, "bambu_network_init_log");
    auto cert = symbol<int(*)(void*, std::string, std::string)>(lib, "bambu_network_set_cert_file");
    auto country = symbol<int(*)(void*, std::string)>(lib, "bambu_network_set_country_code");
    auto start = symbol<int(*)(void*)>(lib, "bambu_network_start");
    auto discovery = symbol<bool(*)(void*, bool, bool)>(lib, "bambu_network_start_discovery");
    auto destroy = symbol<int(*)(void*)>(lib, "bambu_network_destroy_agent");
    std::cout << "plugin_version=" << version() << std::endl;
    void *agent = create(argv[2]);
    if (!agent) return 4;
    if (config(agent, argv[2]) || log(agent) || cert(agent, argv[3], "slicer_base64.cer") ||
        country(agent, "US") || start(agent)) return 5;
    // Same passive discovery mode used by Studio; no login or printer commands.
    if (!discovery(agent, true, false)) return 6;
    if (mode != "stop-immediate")
        std::this_thread::sleep_for(std::chrono::seconds(2));
    if (mode != "baseline") {
        if (!discovery(agent, false, false)) return 7;
        std::cout << "discovery_stopped" << std::endl;
    }
    std::cout << "destroy_begin" << std::endl;
    if (destroy(agent)) return 8;
    std::cout << "destroy_returned" << std::endl;
    // Guard Malloc faults if a receiver accesses the freed discovery object.
    std::this_thread::sleep_for(std::chrono::seconds(6));
    std::cout << "post_destroy_wait_passed" << std::endl;
    dlclose(lib);
    return 0;
}
