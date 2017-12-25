#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <sys/time.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <getopt.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>
#include <csignal>
#include "configEval.hpp"

extern "C" {
#include <pthread.h>
}

// DPDK!
#ifdef WITH_DPDK
extern "C" {
#include <rte_config.h>
#include <rte_common.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ring.h>
}
#include "network_dpdk.h"
#endif
#include "errors.h"
#include "buffer.h"

#include "Config.hpp"
#include "util.h"
#include "version.h"
#include "networkOutputSim.hpp"
#include "SampleProcess.hpp"
#include "json.hpp"
#include "restServer.hpp"
#include "kotekanMode.hpp"
#include "timer.hpp"
#include "fpga_header_functions.h"
#include "gpsTime.h"

#ifdef WITH_HSA
#include "hsaBase.h"
#endif
#ifdef WITH_OPENCL
    #include "clProcess.hpp"
#endif

using json = nlohmann::json;

kotekanMode * kotekan_mode = nullptr;
bool running = false;
std::mutex kotekan_state_lock;
volatile std::sig_atomic_t sig_value = 0;

void signal_handler(int signal)
{
    sig_value = signal;
}

void print_help() {
    printf("usage: kotekan [opts]\n\n");
    printf("Options:\n");
    printf("    --config (-c) [file]            The local JSON config file to use\n");
    printf("    --gps-time (-g)                 Used with -c, try to get GPS time (CHIME only)\n\n");
}

#ifdef WITH_DPDK
void dpdk_setup() {

    char  arg0[] = "./kotekan";
    char  arg1[] = "-n";
    char  arg2[] = "4";
    char  arg3[] = "-c";
#ifdef DPDK_VDIF_MODE
    char  arg4[] = "F";//"FF";
#else
     char  arg4[] = "F";
#endif
    char  arg5[] = "-m";
    char  arg6[] = "256";
    char* argv2[] = { &arg0[0], &arg1[0], &arg2[0], &arg3[0], &arg4[0], &arg5[0], &arg6[0], NULL };
    int   argc2   = (int)(sizeof(argv2) / sizeof(argv2[0])) - 1;

    /* Initialize the Environment Abstraction Layer (EAL). */
    int ret2 = rte_eal_init(argc2, argv2);
    if (ret2 < 0)
        exit(EXIT_FAILURE);
}
#endif

std::string exec(const std::string &cmd) {
    std::array<char, 256> buffer;
    std::string result;
    std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) throw std::runtime_error("popen() for the command " + cmd + " failed!");
    while (!feof(pipe.get())) {
        if (fgets(buffer.data(), 256, pipe.get()) != NULL)
            result += buffer.data();
    }
    return result;
}

void update_log_levels(Config &config) {
    // Adjust the log level
    int log_level = config.get_int("/", "log_level");

    log_level_warn = 0;
    log_level_debug = 0;
    log_level_info = 0;
    switch (log_level) {
        case 3:
            log_level_debug = 1;
        case 2:
            log_level_info = 1;
        case 1:
            log_level_warn = 1;
        default:
            break;
    }
}

void set_gps_time(Config &config) {
    if (config.exists("/", "gps_time") &&
        !config.exists("/gps_time", "error") &&
        config.exists("/gps_time", "frame0_nano")) {

        uint64_t frame0 = config.get_uint64("/gps_time", "frame0_nano");
        set_global_gps_time(frame0);
        INFO("Set FPGA frame 0 time to %" PRIu64 " nanoseconds since Unix Epoch\n", frame0);
    } else {
        if (config.exists("/gps_time", "error")) {
            string error_message = config.get_string("/gps_time", "error");
            ERROR("*****\nGPS time lookup failed with reason: \n %s\n ******\n",
                  error_message.c_str());
        } else {
            WARN("No GPS time set, using system clock.");
        }
    }
}

int start_new_kotekan_mode(Config &config) {

    timer dummytimer; //Strange linker error; required to build
    time_interval dummyinterval; //Strange linker error; required to build
    stream_id_t dummy_stream_id; //More weird Linker stuff
    uint32_t dummy_bin = bin_number(&dummy_stream_id, 1);

    config.dump_config();
    update_log_levels(config);
    set_gps_time(config);

    kotekan_mode = new kotekanMode(config);

    kotekan_mode->initalize_processes();
    kotekan_mode->start_processes();
    running = true;

    return 0;
}

int main(int argc, char ** argv) {
#ifdef WITH_DPDK
    dpdk_setup();
#endif

#ifdef WITH_HSA
    kotekan_hsa_start();
#endif
    json config_json;

    std::signal(SIGINT, signal_handler);

    int opt_val = 0;
    char * config_file_name = (char *)"none";
    int log_options = LOG_CONS | LOG_PID | LOG_NDELAY | LOG_PERROR;
    bool opt_d_set = false;
    bool gps_time = false;

    for (;;) {
        static struct option long_options[] = {
            {"config", required_argument, 0, 'c'},
            {"config-deamon", required_argument, 0, 'd'},
            {"gps-time", no_argument, 0, 'g'},
            {"help", no_argument, 0, 'h'},
            {0, 0, 0, 0}
        };

        int option_index = 0;

        opt_val = getopt_long (argc, argv, "ghc:d:",
                               long_options, &option_index);

        // End of args
        if (opt_val == -1) {
            break;
        }

        switch (opt_val) {
            case 'h':
                print_help();
                return 0;
                break;
            case 'c':
                config_file_name = strdup(optarg);
                break;
            case 'd':
                config_file_name = strdup(optarg);
                opt_d_set = true;
            case 'g':
                gps_time = true;
                break;
            default:
                printf("Invalid option, run with -h to see options");
                return -1;
                break;
        }
    }

    openlog ("kotekan", log_options, LOG_LOCAL1);

    // Load configuration file.
    //INFO("Kotekan starting with config file %s", config_file_name);
    const char git_hash[] = GIT_COMMIT_HASH;
    const char git_branch[] = GIT_BRANCH;
    INFO("Kotekan %f starting build: %s, on branch: %s",
            KOTEKAN_VERSION, git_hash, git_branch);

    Config config;

    restServer *rest_server = get_rest_server();
    rest_server->start();

    if (string(config_file_name) != "none") {
        // TODO should be in a try catch block, to make failures cleaner.
        std::lock_guard<std::mutex> lock(kotekan_state_lock);
        INFO("Opening config file %s", config_file_name);
        //config.parse_file(config_file_name, 0);
        
        string exec_path;
        if (gps_time) {
            INFO("Getting GPS time from ch_master, this might take some time...");
            exec_path = "python ../../scripts/gps_yaml_to_json.py " + std::string(config_file_name);
        } else {
            if (opt_d_set) {
                exec_path = "python /usr/sbin/yaml_to_json.py " + std::string(config_file_name);
            } else {
                exec_path = "python ../../scripts/yaml_to_json.py " + std::string(config_file_name);
            }
        }
        std::string json_string = exec(exec_path.c_str());
        config_json = json::parse(json_string.c_str());
        config.update_config(config_json, 0);
        if (start_new_kotekan_mode(config) == -1) {
            ERROR("Error with config file, exiting...");
            return -1;
        }
    }

    // Main REST callbacks.
    rest_server->register_json_callback("/start", [&] (connectionInstance &conn, json& json_config) {
        std::lock_guard<std::mutex> lock(kotekan_state_lock);
        if (running) {
            conn.send_error("Already running", STATUS_REQUEST_FAILED);
        }

        config.update_config(json_config, 0);

        try {
            if (!start_new_kotekan_mode(config)) {
                conn.send_error("Mode not supported", STATUS_BAD_REQUEST);
                return;
            }
        } catch (std::out_of_range ex) {
            DEBUG("Out of range exception %s", ex.what());
            delete kotekan_mode;
            kotekan_mode = nullptr;
            conn.send_error(ex.what(), STATUS_BAD_REQUEST);
            return;
        } catch (std::runtime_error ex) {
            DEBUG("Runtime error %s", ex.what());
            delete kotekan_mode;
            kotekan_mode = nullptr;
            conn.send_error(ex.what(), STATUS_BAD_REQUEST);
            return;
        } catch (std::exception ex) {
            DEBUG("Generic exception %s", ex.what());
            delete kotekan_mode;
            kotekan_mode = nullptr;
            conn.send_error(ex.what(), STATUS_BAD_REQUEST);
            return;
        }
        conn.send_empty_reply(STATUS_OK);
    });

    rest_server->register_json_callback("/stop", [&](connectionInstance &conn, json &json_request) {
        std::lock_guard<std::mutex> lock(kotekan_state_lock);
        if (!running) {
            conn.send_error("kotekan is already stopped", STATUS_REQUEST_FAILED);
            return;
        }
        assert(kotekan_mode != nullptr);
        kotekan_mode->stop_processes();
        // TODO should we have three states (running, shutting down, and stopped)?
        // This would prevent this function from blocking on join.
        kotekan_mode->join();
        delete kotekan_mode;
        kotekan_mode = nullptr;
        conn.send_empty_reply(STATUS_OK);
    });

    rest_server->register_json_callback("/status", [&](connectionInstance &conn, json &json_request){
        std::lock_guard<std::mutex> lock(kotekan_state_lock);
        json reply;
        reply["running"] = running;
        conn.send_json_reply(reply);
    });

    for(EVER){
        sleep(1);
        if (sig_value == SIGINT) {
            INFO("Got SIGINT, shutting down kotekan...");
            std::lock_guard<std::mutex> lock(kotekan_state_lock);
            if (kotekan_mode != nullptr) {
                INFO("Attempting to stop and join kotekan_processes...");
                kotekan_mode->stop_processes();
                kotekan_mode->join();
                delete kotekan_mode;
            }
            break;
        }
    }

    INFO("kotekan shutdown successfully.");

    closelog();

    return 0;
}
