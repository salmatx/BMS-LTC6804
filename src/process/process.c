/// This module provides functions to compute aggregated statistics from raw BMS samples. Computed statistics
/// are used for and publishing via MQTT and monitoring battery status.

/*==============================================================================================================*/
/*                                                Includes                                                      */
/*==============================================================================================================*/
#include "process.h"
#include "configuration.h"
#include <string.h>

/*==============================================================================================================*/
/*                                             Private Macros                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                              Private Types                                                   */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Private Function Prototypes                                            */
/*==============================================================================================================*/
static void check_limits_sample(const bms_sample_t *s, bms_stats_t *flags);
static void init_stats_from_first(const bms_sample_t *raw_sample, bms_stats_t *out);
static void accumulate_sample(const bms_sample_t *raw_sample, bms_stats_t *out);
static void calculate_average(bms_stats_t *accumulated_samples);
static void remove_processed_samples(bms_sample_buffer_t *buf, size_t sample_count);

/*==============================================================================================================*/
/*                                            Private Constants                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                            Private Variables                                                 */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                      Public Variables and Constants                                          */
/*==============================================================================================================*/

/*==============================================================================================================*/
/*                                       Public Function Definitions                                            */
/*==============================================================================================================*/
/// This function computes aggregated statistics from raw BMS samples stored in a ring buffer. It processes up to
/// 1 second worth of samples (20 samples). If no limit violations are detected in these samples, a single 1 s statistics
/// window is computed. If any limit violations are detected, the samples are split into 0.2 s windows (4 samples each) and
/// multiple statistics windows are computed.
///
/// \param[in,out] buf Pointer to ring buffer containing raw BMS samples. Head and count are updated after processing.
/// \param[out] out_stats Pointer to output statistics buffer
/// \return Number of processed samples
size_t bms_compute_stats(bms_sample_buffer_t *buf, bms_stats_buffer_t *out_stats)
{
    if (!buf || !out_stats || !buf->samples || buf->count == 0) {
        return 0;
    }

    out_stats->stats_count = 0;

    // Definition of count of samples per 1s time window
    const size_t samples_per_1s   = 20;
    // Definition of count of samples per 0.2s time window
    const size_t samples_per_0_2s = 4;

    // Require a full 1 s window; if fewer samples, skip processing
    if (buf->count < samples_per_1s) {
        return 0;
    }

    // If more than 20 samples available fix the count to 20 to process only 1 s worth of data
    size_t available_samples_count = buf->count;
    if (available_samples_count > samples_per_1s) {
        available_samples_count = samples_per_1s;
    }

    // First pass: check for any limit violations in the available samples
    bms_stats_t flags = {0};
    for (size_t i = 0; i < available_samples_count; ++i) {
        const bms_sample_t *s = &buf->samples[bms_buf_index(buf, i)];
        check_limits_sample(s, &flags);
    }

    // Determine processing mode based on presence of violations
    bool any_violation = (flags.cell_errors != 0);
    bms_stats_t st = {0};

    // Case 1: no violations - single 1 s stats over all available samples.
    if (!any_violation) {

        // Initialize stats for accumulation and set timestamp from first sample
        const bms_sample_t *first = &buf->samples[bms_buf_index(buf, 0)];
        init_stats_from_first(first, &st);

        // Second pass: accumulate all samples
        for (size_t i = 0; i < available_samples_count; ++i) {
            const bms_sample_t *s = &buf->samples[bms_buf_index(buf, i)];
            accumulate_sample(s, &st);
            // Set no violations as none were detected during first pass
            st.cell_errors = 0;
        }

        // Calculate averages
        calculate_average(&st);
        // Set inspection bit to indicate valid data
        st.cell_errors |= 0x0001u;
        // Store single stats window in output buffer
        out_stats->stats_array[0] = st;
        out_stats->stats_count    = 1;

        // Consume processed samples from ring buffer (advance head and reduce count)
        remove_processed_samples(buf, available_samples_count);

        return available_samples_count;

    // Case 2: violation present - split into 0.2 s subwindows.
    } else {
        // Number of stats windows created
        size_t windows_created = 0;
        // Offset into available samples
        size_t offset = 0;
    
        // Calculate statistics for 5 windows
        while (offset < available_samples_count && windows_created < BMS_MAX_STATS_WINDOWS) {
            // Initialize stats for accumulation and set timestamp from first sample
            const bms_sample_t *first = &buf->samples[bms_buf_index(buf, offset)];
            init_stats_from_first(first, &st);
    
            // Second pass: accumulate all samples
            for (size_t i = 0; i < samples_per_0_2s; ++i) {
                const bms_sample_t *s = &buf->samples[bms_buf_index(buf, offset + i)];
                accumulate_sample(s, &st);
                check_limits_sample(s, &st);
            }
    
            // Calculate averages
            calculate_average(&st);
            // Set inspection bit to indicate valid data
            st.cell_errors |= 0x0001u;
            // Store single stats window in output buffer
            out_stats->stats_array[windows_created] = st;
            windows_created++;
    
            // Calculate offset for storing samples in correct index
            offset += samples_per_0_2s;
        }
    
        out_stats->stats_count = windows_created;

        // Consume processed samples from ring buffer (advance head and reduce count)
        remove_processed_samples(buf, available_samples_count);
    
        return available_samples_count;
    }
}

/*==============================================================================================================*/
/*                                       Private Function Definitions                                           */
/*==============================================================================================================*/
/// This function consumes samples from buffer by moving head and reducing count by value of sample_count.
///
/// \param[in, out] buf Pointer to ring buffer containing raw BMS samples
/// \param[in] sample_count Number of samples to be removed
/// \return None
static void remove_processed_samples(bms_sample_buffer_t *buf, size_t sample_count)
{
    // Consume all samples used for this calculation (the whole 1 s window). Moving head and reducing count.
    buf->head  = (buf->head + sample_count) % buf->capacity;
    buf->count -= sample_count;

    return;
}

/// This function checks a single BMS sample for limit violations and updates the error bitmask.
/// Function examines cell voltages and pack current of one raw sample against configured limits
/// and sets corresponding bits in the cell_errors field of the flags structure.
///
/// \param[in] s Pointer to the raw BMS sample to check.
/// \param[out] flags Pointer to ::bms_stats_t structure where violation bits will be set.
/// \return None
static void check_limits_sample(const bms_sample_t *s, bms_stats_t *flags)
{
    for (int i = 0; i < g_cfg.battery.num_cells; ++i) {
        float v = s->cell_v[i];
        if (v < g_cfg.battery.cell_v_min) {
            // Undervoltage bit
            flags->cell_errors |= (uint32_t)(1u << (i * 2 + 1u));
        }
        if (v > g_cfg.battery.cell_v_max) {
            // Overvoltage bit
            flags->cell_errors |= (uint32_t)(1u << (i * 2 + 2u));
        }
    }

    if (g_cfg.battery.current_enable) {
        if (s->pack_i < g_cfg.battery.series_pack_i_min) {
            // Undercurrent bit
            flags->cell_errors |= (1u << 25);
        }
        if (s->pack_i > g_cfg.battery.series_pack_i_max) {
            // Overcurrent bit
            flags->cell_errors |= (1u << 26);
        }
    }

    return;
}

/// This function initializes a ::bms_stats_t structure to zero and sets the timestamp from the first raw sample.
/// This function is used as the starting point for accumulating statistics over a time window.
///
/// \param[in] raw_sample Pointer to the first raw BMS sample in the window.
/// \param[out] out Pointer to the bms_stats_t structure to initialize.
/// \return None
static void init_stats_from_first(const bms_sample_t *raw_sample, bms_stats_t *out)
{
    out->timestamp     = raw_sample->timestamp;
    out->sample_count  = 0;
    out->cell_errors = 0;

    for (int c = 0; c < g_cfg.battery.num_cells; ++c) {
        out->cell_v_avg[c] = 0.0f;
    }

    out->pack_v_avg = 0.0f;

    out->pack_i_avg = 0.0f;

    out->temperature_avg = 0.0f;

    return;
}

/// This function accumulates a raw BMS sample into a ::bms_stats_t structure.
/// Updates running sums for averages, and updates values
/// pack voltage, pack current, and temperature. Also increments the sample count.
///
/// \param[in] raw_sample Pointer to the raw BMS sample to accumulate.
/// \param[out] out Pointer to the bms_stats_t structure where data will be accumulated.
/// \return None
static void accumulate_sample(const bms_sample_t *raw_sample, bms_stats_t *out)
{
    for (int c = 0; c < g_cfg.battery.num_cells; ++c) {
        out->cell_v_avg[c] += raw_sample->cell_v[c];
    }

    out->pack_v_avg += raw_sample->pack_v;

    if (g_cfg.battery.current_enable) {
        out->pack_i_avg += raw_sample->pack_i;
    }

    if (g_cfg.battery.temperature_enable) {
        out->temperature_avg += raw_sample->temperature;
    }

    out->sample_count++;

    return;
}

/// This function converts accumulated sums in a ::bms_stats_t structure into averages.
/// Divides the accumulated sums (cell_v_avg, pack_v_avg, pack_i_avg) by the sample count
/// to obtain the final average values. If sample_count is zero, the function returns without change.
///
/// \param[in, out] accumulated_samples Pointer to the bms_stats_t structure containing accumulated sums.
/// \return None
static void calculate_average(bms_stats_t *accumulated_samples)
{
    if (accumulated_samples->sample_count == 0) {
        return;
    }
    float inv_n = 1.0f / (float)accumulated_samples->sample_count;
    for (int c = 0; c < g_cfg.battery.num_cells; ++c) {
        accumulated_samples->cell_v_avg[c] *= inv_n;
    }
    accumulated_samples->pack_v_avg *= inv_n;
    if (g_cfg.battery.current_enable) {
        accumulated_samples->pack_i_avg *= inv_n;
    }
    if (g_cfg.battery.temperature_enable) {
        accumulated_samples->temperature_avg *= inv_n;
    }

    return;
}
