#ifndef LIBCONFIG_H
#define LIBCONFIG_H

#include <string>

/*
If you want to add new fields to the configuration,
you need to modify the following files:
1. pybind
2. construct function in lib.py
3. args.parse in server.py
*/
typedef struct ServerConfig {
    int service_port;
    std::string log_level;
    std::string dev_name;
    size_t prealloc_size;  // unit: GB
    int ib_port;
    std::string link_type;
    int minimal_allocate_size;  // unit: KB
    int num_stream;             // can only be 1,2,4, number of stream for each client
    bool autoincrease;
} server_config_t;

typedef struct ClientConfig {
    int service_port;
    std::string log_level;
    std::string dev_name;
    std::string host_addr;
    int ib_port;
    std::string link_type;
} client_config_t;

#endif
