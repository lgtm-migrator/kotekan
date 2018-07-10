#ifndef VISTRUNCATE
#define VISTRUNCATE

#include "KotekanProcess.hpp"
#include "buffer.h"
#include <xmmintrin.h>
#include <immintrin.h>

/**
 * @class visTruncate
 * @brief Truncates visibility, eigenvalue and weight values.
 *
 * eigenvalues and weights are truncated with a fixed precision that is set in
 * the config. visibility values are truncated to a precision based on their
 * weight.
 *
 * @par Buffers
 * @buffer in_buf The input stream.
 *         @buffer_format visBuffer.
 *         @buffer_metadata visMetadata
 * @buffer out_buf The output stream with truncated values.
 *         @buffer_format visBuffer.
 *         @buffer_metadata visMetadata
 *
 * @conf   err_sq_lim               Limit for the error of visibility truncation.
 * @conf   weight_fixed_precision   Fixed precision for weight truncation.
 * @conf   data_fixed_precision     Fixed precision for eigenvector and visibility truncation (if weights are zero).
 *
 * @author Tristan Pinsonneault-Marotte, Rick Nitsche
 */
class visTruncate : public KotekanProcess {
public:
    /// Constructor; loads parameters from config
    visTruncate(Config &config, const string& unique_name, bufferContainer &buffer_container);
    ~visTruncate();

    /// Main loop over buffer frames
    void main_thread() override;

    void apply_config(uint64_t fpga_seq) override;
private:
    // Buffers
    Buffer * in_buf;
    Buffer * out_buf;

    // Truncation parameters
    float err_sq_lim;
    float w_prec;
    float vis_prec;

    // Timing
    double start_time;;
    double wait_time = 0.;
    double truncate_time = 0.;
    double copy_time = 0.;
    double last_time;

};

#endif
