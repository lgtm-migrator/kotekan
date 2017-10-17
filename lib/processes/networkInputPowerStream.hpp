#ifndef NETWORK_INPUT_POWER_STREAM_H
#define NETWORK_INPUT_POWER_STREAM_H

#include "powerStreamUtil.hpp"
#include <sys/socket.h>
#include "Config.hpp"
#include "buffer.h"
#include "KotekanProcess.hpp"
#include <atomic>

class networkInputPowerStream : public KotekanProcess {
public:
    networkInputPowerStream(Config& config,
                           const string& unique_name,
                           bufferContainer &buffer_container);
    virtual ~networkInputPowerStream();
    void main_thread();

    virtual void apply_config(uint64_t fpga_seq);
    void receive_packet(void *buffer, int length, int socket_fd);

private:
	void tcpConnect();

    struct Buffer *buf;

    uint32_t port;
    string server_ip;
    string protocol;

    bool tcp_connected=false;
    bool tcp_connecting=false;
	std::thread connect_thread;
    std::atomic_flag socket_lock;

    int freqs;
    int times;
    int elems;

    int id;

    uint64_t handshake_idx=-1;
    double   handshake_utc=-1;

	IntensityHeader header;
};

#endif