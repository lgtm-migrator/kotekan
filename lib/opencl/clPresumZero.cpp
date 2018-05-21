#include "clPresumZero.hpp"

REGISTER_CL_COMMAND(clPresumZero);

clPresumZero::clPresumZero(Config& config, const string &unique_name,
                            bufferContainer& host_buffers, clDeviceInterface& device) :
    clCommand("", "", config, unique_name, host_buffers, device)
{
     _num_elements = config.get_int(unique_name, "num_elements");
    _num_local_freq = config.get_int(unique_name, "num_local_freq");
    presum_len = _num_elements * _num_local_freq * 2 * sizeof (int32_t);
    presum_zeros = malloc(presum_len);
    memset(presum_zeros, 0, presum_len);
}

clPresumZero::~clPresumZero()
{
    free(presum_zeros);
}

cl_event clPresumZero::execute(int gpu_frame_id, const uint64_t& fpga_seq, cl_event pre_event)
{
    DEBUG2("CLPRESUMZERO::EXECUTE");

    clCommand::execute(gpu_frame_id, 0, pre_event);

    cl_mem gpu_memory_frame = device.get_gpu_memory_array("presum",
                                                gpu_frame_id, presum_len);

    // Data transfer to GPU
    CHECK_CL_ERROR( clEnqueueWriteBuffer(device.getQueue(0),
                                            gpu_memory_frame,
                                            CL_FALSE,
                                            0, //offset
                                            presum_len,
                                            presum_zeros,
                                            (pre_event==NULL)?0:1,
                                            (pre_event==NULL)?NULL:&pre_event,
                                            &post_event[gpu_frame_id]) );
    return post_event[gpu_frame_id];
}
