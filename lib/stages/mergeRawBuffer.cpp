#include "mergeRawBuffer.hpp"

#include "BeamMetadata.hpp"   // for BeamMetadata
#include "StageFactory.hpp"  // for REGISTER_KOTEKAN_STAGE, StageMakerTemplate
#include "buffer.h"          // for allocate_new_metadata_object, copy_metadata, mark_frame_empty
#include "chimeMetadata.hpp" // for atomic_add_lost_timesamples, zero_lost_samples
#include "kotekanLogging.hpp" // for INFO

#include <atomic>      // for atomic_bool
#include <exception>   // for exception
#include <functional>  // for _Bind_helper<>::type, bind, function
#include <regex>       // for match_results<>::_Base_type
#include <stdexcept>   // for runtime_error
#include <string>      // for string
#include <visUtil.hpp> // for frameID, modulo
#include <iostream>    // for cout

using kotekan::bufferContainer;
using kotekan::Config;
using kotekan::Stage;
using std::string;

REGISTER_KOTEKAN_STAGE(mergeRawBuffer);

mergeRawBuffer::mergeRawBuffer(Config& config_, const std::string& unique_name,
                               bufferContainer& buffer_container) :
    Stage(config_, unique_name, buffer_container,
          std::bind(&mergeRawBuffer::main_thread, this)) {

    // Apply config
    _samples_per_data_set = config.get<uint32_t>(unique_name, "samples_per_data_set");
    _num_pol = config.get<uint32_t>(unique_name, "num_pol");
    _raw_frames_per_merged_frame = config.get<uint32_t>(unique_name, "raw_frames_per_merged_frame"); 
    in_buf = get_buffer("in_buf");
    register_consumer(in_buf, unique_name.c_str());

    out_buf = get_buffer("out_buf");

    register_producer(out_buf, unique_name.c_str());
    
}

mergeRawBuffer::~mergeRawBuffer() {}


void mergeRawBuffer::main_thread() {
    frameID in_buffer_ID(in_buf);
    uint8_t* in_frame;
    frameID out_buffer_ID(out_buf);
    uint8_t* out_frame;
    // Since the input buffer is a ring buffer and the in_buf_ID goes back to zero
    // when the ID reaches the num_frame, we need something something to keep the 
    // turns.
    uint32_t in_buf_turns = 0;
    
    while (!stop_thread) {
        // Get an input buffer, This call is blocking!
        in_frame = wait_for_full_frame(in_buf, unique_name.c_str(), in_buffer_ID);
        if (in_frame == nullptr)
            break;

	BeamMetadata* in_metadata = (BeamMetadata*)get_metadata(in_buf, in_buffer_ID);

        out_frame = (uint8_t*)wait_for_empty_frame(out_buf, unique_name.c_str(), out_buffer_ID);
        if (out_frame == nullptr)
             break;
        
        // compute the frame index for the out_buf
	uint32_t true_frame_ID = in_buf_turns * in_buf -> num_frames + in_buffer_ID;
	uint32_t sub_frame_index = true_frame_ID % _raw_frames_per_merged_frame;	
	// compute the size of the sub_frame of the out_frame
	uint32_t sub_frame_size = sizeof(FreqIDBeamMetadata) + in_buf -> frame_size;
	// Get the start position of a sub_out_frame's meatadata
	uint32_t out_frame_metadata_pos = sub_frame_size * sub_frame_index;
	// Get the memory block for the metadata of sub_out_frame.
	FreqIDBeamMetadata* sub_frame_metadata = (FreqIDBeamMetadata *)&out_frame[out_frame_metadata_pos];
	// Copy the in_frame metadata the the out frame's subframe metadata
	// Using memory copy here will give a warning.
	sub_frame_metadata -> fpga_seq_start = in_metadata -> fpga_seq_start;
	sub_frame_metadata -> ctime = in_metadata -> ctime;
	sub_frame_metadata -> stream_id = in_metadata -> stream_id;
	sub_frame_metadata -> dataset_id = in_metadata -> dataset_id;
	sub_frame_metadata -> beam_number = in_metadata -> beam_number;
	sub_frame_metadata -> ra = in_metadata -> ra;
	sub_frame_metadata -> dec = in_metadata -> dec;
	sub_frame_metadata -> scaling = in_metadata -> scaling;

	// Add the frequency id into the metadata, for chime one stream has on frequency.
	// TODO Have the output buffer handle the stream with mulitple frequency
	const uint32_t num_freq_per_stream = Telescope::instance().num_freq_per_stream();

        for (uint32_t f = 0; f < num_freq_per_stream; ++f) {
	    // TODO this does not handling the stream with mulitple frequencies. since at right now, 
	    // num_freq_per_stream is 1
            sub_frame_metadata -> frequency_bin = Telescope::instance().to_freq_id(in_metadata->stream_id, f);
        }

	// Get the start position of a sub_out_frame's data
	uint32_t out_frame_data_pos = out_frame_metadata_pos + sizeof(FreqIDBeamMetadata);
	// Get the memory block for the data of of a subframe.
	uint8_t * sub_frame_data = &out_frame[out_frame_data_pos];
	// copy data from in frame to sub_out_frame
	memcpy(sub_frame_data, in_frame, in_buf -> frame_size);

	FreqIDBeamMetadata* debud_frame_metadata = (FreqIDBeamMetadata *)&out_frame[sub_frame_index * sub_frame_size];
	DEBUG2("Sub frame Beam RA: {:f}, Dec: {:f}, scaling: {:d}, , beam_num: {:d}, freq_id {:d}\n", debud_frame_metadata -> ra, debud_frame_metadata -> dec, debud_frame_metadata -> scaling, debud_frame_metadata-> beam_number,  debud_frame_metadata-> frequency_bin);
        
	mark_frame_empty(in_buf, unique_name.c_str(), in_buffer_ID);
	in_buffer_ID++;
	
	if (sub_frame_index == _raw_frames_per_merged_frame - 1){
            mark_frame_full(out_buf, unique_name.c_str(), out_buffer_ID);
	    out_buffer_ID++;
	}
	// If in_buffer_ID goes back to zero, it has read the whole ring buffer once.
	if ((uint32_t)in_buffer_ID == 0){
	    in_buf_turns++;
	}
    }
}
