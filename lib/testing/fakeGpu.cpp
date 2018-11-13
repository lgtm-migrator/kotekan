#include <time.h>
#include <sys/time.h>
#include <csignal>
#include <unistd.h>
#include <iterator>
#include <csignal>
#include "gsl-lite.hpp"

#include "fakeGpu.hpp"
#include "errors.h"
#include "fpga_header_functions.h"
#include "chimeMetadata.h"
#include "visUtil.hpp"


REGISTER_KOTEKAN_PROCESS(fakeGpu);

fakeGpu::fakeGpu(Config& config, const string& unique_name,
                 bufferContainer &buffer_container) :
    KotekanProcess(config, unique_name, buffer_container,
                   std::bind(&fakeGpu::main_thread, this))
{

    apply_config(0);

    out_buf = get_buffer("out_buf");
    register_producer(out_buf, unique_name.c_str());

    // Check that the buffer is large enough
    auto expected_frame_size =
        num_freq_in_frame * gpu_N2_size(num_elements, block_size);
    if (out_buf->frame_size < expected_frame_size) {
        ERROR("Buffer size too small (%i bytes, minimum required %i bytes)",
              out_buf->frame_size, expected_frame_size);
        std::raise(SIGINT);
    }
}

fakeGpu::~fakeGpu() {
}

void fakeGpu::apply_config(uint64_t fpga_seq) {

    freq = config.get<int>(unique_name, "freq");
    cadence = config.get_default<float>(unique_name, "cadence", 5.0);

    pre_accumulate = config.get_default<bool>(
        unique_name, "pre_accumulate", true);

    if(pre_accumulate) {
        samples_per_data_set = config.get<int32_t>(
            unique_name, "samples_per_data_set");
    }

    block_size = config.get<int32_t>(unique_name, "block_size");
    num_elements = config.get<int32_t>(unique_name, "num_elements");
    num_frames = config.get_default<int32_t>(unique_name, "num_frames", -1);
    num_freq_in_frame = config.get_default<int32_t>(
        unique_name, "num_freq_in_frame", 1);
    wait = config.get_default<bool>(unique_name, "wait", true);

    // Fetch the correct fill function
    std::string pattern_name = config.get<std::string>(unique_name, "pattern");

    // Validate and creeate test pattern
    if (!FACTORY(fakeGpuPattern)::exists(pattern_name)) {
        ERROR("Test pattern \"%s\" does not exist.", pattern_name.c_str());
        std::raise(SIGINT);
    }
    pattern = FACTORY(fakeGpuPattern)::create_unique(
        pattern_name, config, unique_name);
}

void fakeGpu::main_thread() {

    int frame_count = 0;
    frameID frame_id(out_buf);
    timeval tv;
    timespec ts;

    uint64_t delta_seq, delta_ns;
    uint64_t fpga_seq = 0;
    const auto nprod_gpu = gpu_N2_size(num_elements, block_size);

    // This encoding of the stream id should ensure that bin_number_chime gives
    // back the original frequency ID when it is called later.
    // NOTE: all elements must have values < 16 for this encoding to work out.
    stream_id_t s = {0, (uint8_t)(freq % 16), (uint8_t)(freq / 16), (uint8_t)(freq / 256)};

    // Set the start time
    clock_gettime(CLOCK_REALTIME, &ts);

    // Calculate the increment in time between samples
    if(pre_accumulate) {
        delta_seq = samples_per_data_set;
        delta_ns = samples_per_data_set * 2560;
    } else {
        delta_seq = 1;
        delta_ns = (uint64_t)(cadence * 1000000000);
    }

    // Get the amount of time we need to sleep for.
    timespec delta_ts;
    delta_ts.tv_sec = delta_ns / 1000000000;
    delta_ts.tv_nsec = delta_ns % 1000000000;

    while (!stop_thread) {
        int32_t * output = (int *)wait_for_empty_frame(
            out_buf, unique_name.c_str(), frame_id
        );
        if (output == NULL) break;

        DEBUG("Simulating GPU buffer in %s[%d]",
              out_buf->buffer_name, frame_id);

        allocate_new_metadata_object(out_buf, frame_id);
        set_fpga_seq_num(out_buf, frame_id, fpga_seq);
        set_stream_id_t(out_buf, frame_id, s);

        // Set the two times
        TIMESPEC_TO_TIMEVAL(&tv, &ts);
        set_first_packet_recv_time(out_buf, frame_id, tv);
        set_gps_time(out_buf, frame_id, ts);

        // Fill the buffer with the specified mode
        gsl::span<int32_t> data(output, output + 2 * nprod_gpu);
        chimeMetadata* metadata =
            (chimeMetadata *)out_buf->metadata[frame_id]->metadata;
        pattern->fill(data, metadata, frame_count, freq);

        // Mark full and move onto next frame...
        mark_frame_full(out_buf, unique_name.c_str(), frame_id++);

        // Increase total frame count
        frame_count++;

        // Increment time
        fpga_seq += delta_seq;

        // Increment the timespec
        ts = ts + delta_ts;

        // Cause kotekan to exit if we've hit the maximum number of frames
        if (num_frames > 0 && frame_count > num_frames) {
            INFO("Reached frame limit [%i frames]. Exiting kotekan...", num_frames);
            std::raise(SIGINT);
            return;
        }

        // TODO: only sleep for the extra time required, i.e. account for the
        // elapsed time each loop
        if (this->wait) nanosleep(&delta_ts, nullptr);
    }
}