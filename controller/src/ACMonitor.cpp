#include "ACMonitor.h"
#include "WsServer.h"
#include "esp_log.h"
#include <cmath>
#include <cstring>
#include "String.h"
#include <cstdlib>
#include <string>

static const char* TAG = "ACMonitor";

ACMonitor::ACMonitor() {
    adcMutex_ = xSemaphoreCreateRecursiveMutex();
    needToComputeVoltageDcOffset = true;
    Voltage_DC_Offset = 0.0;
    needToComputeCurrentDcOffset = true;
    needTocomputeCurrentDcOffsetCounter = 0;
    Current_DC_Offset = 0.0;
    vacPresent = false;
    vacPresentCount = 0;
}

ACMonitor::~ACMonitor() {
    if (dma_handle_) {
        adc_continuous_stop(dma_handle_);
        adc_continuous_deinit(dma_handle_);
        dma_handle_ = nullptr;
    }
    if (adc_handle_) {
        adc_oneshot_del_unit(adc_handle_);
        adc_handle_ = nullptr;
    }
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (voltage_handle_) {
        adc_cali_delete_scheme_line_fitting(voltage_handle_);
        voltage_handle_ = nullptr;
    }
    if (current_handle_) {
        adc_cali_delete_scheme_line_fitting(current_handle_);
        current_handle_ = nullptr;
    }
#else
    if (voltage_handle_) {
        adc_cali_delete_scheme_curve_fitting(voltage_handle_);
        voltage_handle_ = nullptr;
    }
    if (current_handle_) {
        adc_cali_delete_scheme_curve_fitting(current_handle_);
        current_handle_ = nullptr;
    }
#endif
}

void ACMonitor::init() {

    // One-shot unit
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = unit_;
    unit_cfg.clk_src = ADC_RTC_CLK_SRC_RC_FAST;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle_));

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = atten_;
    chan_cfg.bitwidth = width_;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle_, voltage_channel_, &chan_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle_, current_channel_, &chan_cfg));

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = unit_;
    cali_cfg.atten = atten_;
    cali_cfg.bitwidth = width_;
#if CONFIG_IDF_TARGET_ESP32
    cali_cfg.default_vref = 1100;
#endif
    ESP_ERROR_CHECK(adc_cali_create_scheme_line_fitting(&cali_cfg, &voltage_handle_));
    ESP_ERROR_CHECK(adc_cali_create_scheme_line_fitting(&cali_cfg, &current_handle_));
#else
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = unit_;
    cali_cfg.atten = atten_;
    cali_cfg.bitwidth = width_;

    cali_cfg.chan = voltage_channel_;
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &voltage_handle_));

    cali_cfg.chan = current_channel_;
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &current_handle_));
#endif
}

ACMonitor::ACResults ACMonitor::readVoltage() {
    ACResults voltageResults = readSamples(VOLTAGE, voltage_channel_);
    float voltage = voltageResults.rms_voltage;
    if (voltage < 10.0f) {
        voltage = 0.0f;
    } else if (voltage > 150.0f) {
        ESP_LOGW(TAG, "Voltage reading out of range: %.2f V", voltage);
        voltage = 0.0f;
    }
    return voltageResults;
}

ACMonitor::ACResults ACMonitor::readCurrent() {
     ACResults currentResults = readSamples(CURRENT, current_channel_);
    return currentResults;
}

ACMonitor::ACResults ACMonitor::readSamples(MeasurementType type, adc_channel_t channel) {
    ACResults r = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    if (xSemaphoreTakeRecursive(adcMutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "ADC busy, skipping read");
        return r;
    }

    // Create continuous ADC handle
    adc_continuous_handle_cfg_t handle_cfg = {
        .max_store_buf_size = FRAME_SIZE,
        .conv_frame_size = FRAME_SIZE,
        .flags = {
            .flush_pool = 0
        },
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&handle_cfg, &dma_handle_));

    // Pattern
    adc_digi_pattern_config_t pattern = {};
    pattern.atten = atten_;
    pattern.channel = channel;
    pattern.unit = unit_;
    pattern.bit_width = width_;

    adc_continuous_config_t dig_cfg = {};
    dig_cfg.pattern_num = 1;
    dig_cfg.adc_pattern = &pattern;
    dig_cfg.sample_freq_hz = SAMPLE_RATE;
    dig_cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    dig_cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE1;

    ESP_ERROR_CHECK(adc_continuous_config(dma_handle_, &dig_cfg));
    ESP_ERROR_CHECK(adc_continuous_start(dma_handle_));

    uint8_t* buffer = (uint8_t*)malloc(FRAME_SIZE);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate ADC DMA buffer");
        adc_continuous_stop(dma_handle_);
        adc_continuous_deinit(dma_handle_);
        dma_handle_ = nullptr;
        xSemaphoreGiveRecursive(adcMutex_);
        return r;
    }

    uint32_t length = 0;
    esp_err_t ret = adc_continuous_read(dma_handle_, buffer, FRAME_SIZE, &length, 1000);

    if (ret != ESP_OK) {
        if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW("ADC_DMA", "ADC read timeout");
        } else {
            ESP_LOGE("ADC_DMA", "ADC read error: %s", esp_err_to_name(ret));
        }
        free(buffer);
        adc_continuous_stop(dma_handle_);
        adc_continuous_deinit(dma_handle_);
        dma_handle_ = nullptr;
        xSemaphoreGiveRecursive(adcMutex_);
        return r;
    }

    adc_digi_output_data_t* data = (adc_digi_output_data_t*)buffer;
    int count = length / sizeof(adc_digi_output_data_t);
    if (count > (int)SAMPLE_COUNT) count = SAMPLE_COUNT;

    for (int i = 0; i < count; i++) {
        samples_[i] = data[i].type2.data;
    }
//    applySGFilter(samples_, count);
    free(buffer);
    adc_continuous_stop(dma_handle_);
    adc_continuous_deinit(dma_handle_);
    dma_handle_ = nullptr;
    
    // do a coarse check for vac present;
    if(type == VOLTAGE)
    {
        vacPresent = false;
        if(isAcPresent(samples_, count))
        {
            vacPresent = true;
            vacPresentCount++;
        }
        else
        {
            vacPresent = false;
            vacPresentCount = 0;
            needToComputeVoltageDcOffset = true;
            needToComputeCurrentDcOffset = true;
            needTocomputeCurrentDcOffsetCounter = 0;
        }
    }
    // vac is presnt and we have had 2 cycles of it being present
    if(vacPresent && (vacPresentCount > 1))
    {
        vacPresentCount = 2;  // keep counter from overflowing
        r = analyze_ac(type, samples_, count, SAMPLE_RATE);
        if(r.rms_adc > 1150)
        {
//            ESP_LOGI(TAG,"type: %d, rms_adc is > 1150 dc_offset:%0.1f", type, r.dc_offset);
    //        printSamples(samples_, count, "VOLTAGE SAMPLES large");
        }
    }
    else
    {
//        ESP_LOGI(TAG,"No VAC present");
        // r was initialized to 0
//        WsServer::instance().postDebug("No Voltage Present, skip");
    }
    xSemaphoreGiveRecursive(adcMutex_);

    return r;
}

ACMonitor::ACResults ACMonitor::analyze_ac(MeasurementType type,
                                           const uint16_t* samples,
                                           int count,
                                           float sample_rate) {
    ACResults r = {};
    // compute dc offset once, always use the same offset after that
    if (type == VOLTAGE)
    {
        if(needToComputeVoltageDcOffset)
        {
            ESP_LOGI(TAG,"Voltage - Computing DC_Offset ");
            Voltage_DC_Offset = compute_initial_dc_offset(type, samples, count);
            if((Voltage_DC_Offset > 3000) || (Voltage_DC_Offset < 2000))
            {
                ESP_LOGI(TAG,"Voltage - Invalid coarse DC_Offset %0.1f", Voltage_DC_Offset);
                printSamples(samples, count, "Voltage - Invalid coarse DC_OFFSET computed" );
            }
            else
            {
                needToComputeVoltageDcOffset = false;
            
                // now lets do a fine dc offset calculation just using 2 cycles of the sine wave
                centerSamples(samples_, centeredSamples_, count, Voltage_DC_Offset);
                int zc1 = 0;
                int zc2 = 0;
                int zc3 = 0;
                if (determineZeroCrossings(type, centeredSamples_, count, zc1, zc2, zc3))
                {
                    float fineDcOffset = compute_fine_dc_offset(type, centeredSamples_, count, zc1, zc3);
                    ESP_LOGI(TAG,"fine dc_offset calculation coarse: %0.1f   fine: %0.1f", Voltage_DC_Offset, fineDcOffset);
                    Voltage_DC_Offset = Voltage_DC_Offset - fineDcOffset;
                    if((Voltage_DC_Offset > 3000) || (Voltage_DC_Offset < 2000))
                    {
                        needToComputeVoltageDcOffset = true;
                        ESP_LOGI(TAG,"Voltage - Invalid fine DC_Offset %0.1f fine %0.1f", Voltage_DC_Offset, fineDcOffset);
                        printSamples(samples, count, "Voltage - Invalid fine DC_OFFSET computed" );
                    }                    

                }
                else
                {
                    needToComputeVoltageDcOffset = true;
                    ESP_LOGI(TAG,"Did not compute fine dc offset due to zero crossing error");
                }
            }
        }
        r.dc_offset = Voltage_DC_Offset;
    }
    else
    {
        if(needToComputeCurrentDcOffset)
        {
            Current_DC_Offset = compute_initial_dc_offset(type, samples, count);
            needToComputeCurrentDcOffset = false;
            ESP_LOGI(TAG,"Current - DC_Offset %0.1f ", Current_DC_Offset);
            
            WsServer::instance().postDebug("Current - DC_Offset %0.1f", Current_DC_Offset);
            if(Current_DC_Offset > 2500.0 || Current_DC_Offset < 1500.0)
            {
                ESP_LOGI(TAG,"Current - Invalid DC_Offset %0.1f set to 1950.0", Current_DC_Offset);
                WsServer::instance().postDebug("Current - Invalid DC_Offset %0.1f set to 1950.0", Current_DC_Offset);
                printSamples(samples, count, "Current - Invalid DC_OFFSET computed" );
                Current_DC_Offset = 1950.0;
            }
        }
        r.dc_offset = Current_DC_Offset;        
    }
//    r.dc_offset = compute_dc_offset(samples, count);
    //print0SamplesInterval(type, samples, count, 20, 25, "Raw Samples");
    float theOffset = Current_DC_Offset;
    if(type == VOLTAGE)
    {
        theOffset = Voltage_DC_Offset;
    }
    centerSamples(samples_, centeredSamples_, count, theOffset);
    //print0FloatSamplesInterval(type, centeredSamples_, count, 20, 25, "Centered Samples");
    // 1. Initialize the filter (do this once)
    initLowPass1k(&lpf);
    filterSamples(centeredSamples_, filteredSamples_, count, &lpf);

    //print1FloatSamplesInterval(type, filteredSamples_, count, 20, 25, "Filtered Samples");

    r.peak_adc  = compute_peak(filteredSamples_, count, 0);
    r.rms_adc   = compute_rms(type, filteredSamples_, count, 0);

    if (type == VOLTAGE) {
//        ESP_LOGI(TAG,"Voltage - dcOffset: %0.2f vrms: %0.2f vpeak: %0.2f ",r.dc_offset, r.rms_adc, r.peak_adc);
        r.frequency = compute_frequency(filteredSamples_, count, 0, sample_rate);
        r.rms_voltage   = r.rms_adc * voltage_scale_;
        r.peak_voltage  = r.peak_adc * voltage_scale_;
        r.rms_current   = 0.0f;
        r.peak_current  = 0.0f;
    } else {
        r.frequency = 0.0;
        r.rms_current   = r.rms_adc * current_scale_;
        r.peak_current  = r.peak_adc * current_scale_;
        r.rms_voltage   = 0.0f;
        r.peak_voltage  = 0.0f;
        if(r.rms_current > 30.0f) 
        {
            if(needTocomputeCurrentDcOffsetCounter < 5)
            {
                ESP_LOGI(TAG,"Current - dcOffset: %0.2f Irms: %0.2f Ipeak: %0.2f ",r.dc_offset, r.rms_current, r.peak_current);
                WsServer::instance().postDebug("Current - dcOffset: %0.2f Irms: %0.2f Ipeak: %0.2f ",r.dc_offset, r.rms_current, r.peak_current);
                needToComputeCurrentDcOffset = true;  // force recalculation of dc offset on next read if current is above 30A, this is a safety measure to prevent runaway current readings due to an invalid dc offset calculation
                needTocomputeCurrentDcOffsetCounter++;
            }
            else
            {
                ESP_LOGI(TAG,"Current - tried 5 times, give up trying to recalculate dc offset ");
                WsServer::instance().postDebug("Current - tried 5 times,give up trying to recalculate dc offset ");
            }
        }
    }

    return r;
}

float ACMonitor::compute_initial_dc_offset(MeasurementType type, const uint16_t* samples, int count) {
    uint32_t sum = 0;
    for (int i = 0; i < count; i++) {
        sum += samples[i];
    }
    return (float)sum / count;
}
float ACMonitor::compute_fine_dc_offset(MeasurementType type, const float* samples, int count, int startIndex, int stopIndex)
 {
    float sum = 0;
    int max = 0;
    int min = 0;
    for (int i = startIndex; i <= stopIndex; i++)
    {
        if(samples[i] > max) max = samples[i];
        if(samples[i] < min) min = samples[i];
        sum += samples[i];
    }
    int span = stopIndex - startIndex;
    float result = sum / (float)span;
//    if(fabs(result) > 1000)
    {
        ESP_LOGI(TAG,"Invalid fine dc offset start %d stop %d max %d min %d sum %0.1f span %d", startIndex, stopIndex, max, min, sum, span);
        ESP_LOGI(TAG,"Invalid fine dc offset result: %0.1f", result);
    }
    return result;
}

// must use a centered sample set
bool ACMonitor::determineZeroCrossings(MeasurementType type, const float* samples, int count, int& zc1, int& zc2, int& zc3)
{
    bool result = true;
    // --- 1. Find zero-crossings (negative → positive) ---
    int zc[10];
    int zc_count = 0;

    for (int i = 1; i < count; i++) {
        float prev = samples[i - 1];
        float curr = samples[i];

        if (prev < 0 && curr >= 0) {
            if (zc_count < 10) {
                if(zc_count == 0)
                {
                    ESP_LOGI(TAG, "zeroCrossing Voltage - zc[%d] = %d", zc_count, i);
                    zc[zc_count++] = i;
                }
                else
                {
                    // make sure this is not too close to the last one
                    if(i > (zc[zc_count-1] + 10))
                    {
                        ESP_LOGI(TAG, "zeroCrossing Voltage - zc[%d] = %d", zc_count, i);
                        zc[zc_count++] = i;
                    }
                    else
                    {
                        ESP_LOGI(TAG, "Voltage - false alarm index i: %d, last i: %d", i, zc[zc_count-1]);
                    }
                }
            }
        }
    }

    // Need at least 3 zero-crossings to get 2 full cycles
    if (zc_count < 3) {
        if(type == MeasurementType::VOLTAGE)
        {
            ESP_LOGI(TAG, "Voltage - dzc: Did not find 3 zero crossings");
        }
        else{
            ESP_LOGI(TAG, "Current - dzc: Did not find 3 zero crossings");            
        }

        zc1 = 0;
        zc2 = 0;
        zc3 = count;
        return false;
    }

    // --- 2. Define the window for exactly 2 cycles ---
    int start = zc[0];
    int end   = zc[2];   // two full cycles later

    if (end > count) end = count;

    int n = end - start;

    if (n <= 0)
    {
        if(type == MeasurementType::VOLTAGE)
        {
            ESP_LOGI(TAG, "Voltage - Zero crossings error, end index < start index");
        }
        else
        {
            ESP_LOGI(TAG, "Current - Zero crossings error, end index < start index");
        }
        zc1 = 0;
        zc2 = 0;
        zc3 = count;
        return false;
    }
    zc1 = zc[0];
    zc2 = zc[1];
    zc3 = zc[2];
    return result;    
}
bool ACMonitor::isAcPresent(const uint16_t* samples, int count)
{
    bool result = false;
    int max = 0;
    int min = 4095;
    for(int i = 0; i < count; i++)
    {
        if(samples[i] > max) max = samples[i];
        if(samples[i] < min) min = samples[i];
    }
    if((max - min) > 1000 )
    {
        result = true;
    }
//    ESP_LOGI(TAG,"isAcPresent result: %d max: %d  min: %d delta: %d", result, max, min, max-min);
    return result;
}
float ACMonitor::compute_peak(const float* samples, int count, float offset) {
    float peak = 0.0f;
    for (int i = 0; i < count; i++) {
        float v = std::fabs(samples[i] - offset);
        if (v > peak) peak = v;
    }
    return peak;
}

float ACMonitor::compute_rms(MeasurementType type, const float* samples, int count, float offset)
{
    // --- 1. Find zero-crossings (negative → positive) ---
    int zc[10];
    int zc_count = 0;

    for (int i = 1; i < count; i++) {
        float prev = samples[i - 1] - offset;
        float curr = samples[i] - offset;

        if (prev < 0 && curr >= 0) {
            if (zc_count < 10) {
                zc[zc_count++] = i;
            }
        }
    }

    // Need at least 3 zero-crossings to get 2 full cycles
    if (zc_count < 3) {
        if(type == MeasurementType::VOLTAGE)
        {
            ESP_LOGI(TAG, "Voltage - compRms: Did not find 3 zero crossings");
        }
        else{
            ESP_LOGI(TAG, "Current - compRms: Did not find 3 zero crossings");            
        }
        // fallback to full-window RMS
        double sum_sq = 0;
        for (int i = 0; i < count; i++) {
            float v = samples[i] - offset;
            sum_sq += v * v;
        }
        return sqrt(sum_sq / count);
    }

    // --- 2. Define the window for exactly 2 cycles ---
    int start = zc[0];
    int end   = zc[2];   // two full cycles later

    if (end > count) end = count;

    int n = end - start;

    if (n <= 0)
    {
        if(type == MeasurementType::VOLTAGE)
        {
            ESP_LOGI(TAG, "Voltage - Zero crossings error, end index < start index");
        }
        else
        {
            ESP_LOGI(TAG, "Current - Zero crossings error, end index < start index");
        }
         return 0.0f;
    }

    // --- 3. Compute RMS over the 2-cycle window ---
    double sum_sq = 0.0;
    for (int i = start; i < end; i++) {
        float v = samples[i] - offset;
        sum_sq += v * v;
    }

    float mean_sq = (float)(sum_sq / n);
    return sqrtf(mean_sq);
}

float ACMonitor::compute_frequency(const float* samples,
                                   int count,
                                   float offset,
                                   float sample_rate) {
    int last = -1;
    for (int i = 1; i < count; i++) {
        float prev = samples[i - 1] - offset;
        float curr = samples[i] - offset;

        if (prev < 0 && curr >= 0) {
            if (last >= 0 && (i - last) > 10) {
                float period_samples = (float)(i - last);
                float freq = sample_rate / period_samples;
//                ESP_LOGI(TAG,"frequency is %0.1f", freq);
                return freq;
            }
            last = i;
        }
    }
    return 0.0f;
}
void ACMonitor::print0SamplesInterval(MeasurementType type, const uint16_t* samples, int count, int start, int stop, const char* header)
{
    static int loopCounter1 = 0;
  
    if((loopCounter1 > start) && (loopCounter1 < stop))
    {
        if (type == VOLTAGE)
        {
            loopCounter1++;
            std::string fullHeader = std::string(header) + " VOLTAGE SAMPLES";
            printSamples(samples, count, fullHeader.c_str());
        }
        else
        {
    //        loopCounter1++;
    //        std::string fullHeader = std::string(header) + " CURRENT SAMPLES";
    //        printSamples(samples, count, fullHeader.c_str());
        }
    }
    else
    {
        if(loopCounter1 >stop+1) 
        {
            loopCounter1 = stop+1;
        }
        else{
            loopCounter1++;
        }
    }
}
void ACMonitor::print0FloatSamplesInterval(MeasurementType type, const float* samples, int count, int start, int stop, const char* header)
{
    static int loopCounter2 = 0;
  
    if((loopCounter2 > start) && (loopCounter2 < stop))
    {
        if (type == VOLTAGE)
        {
            loopCounter2++;
            std::string fullHeader = std::string(header) + " VOLTAGE SAMPLES";
            printFloatSamples(samples, count, fullHeader.c_str());
        }
        else
        {
    //        loopCounter2++;
    //        std::string fullHeader = std::string(header) + " CURRENT SAMPLES";
    //        printFloatSamples(samples, count, fullHeader.c_str());
        }
    }
    else
    {
        if(loopCounter2 >stop+1) 
        {
            loopCounter2 = stop+1;
        }
        else{
            loopCounter2++;
        }
    }
}
void ACMonitor::print1FloatSamplesInterval(MeasurementType type, const float* samples, int count, int start, int stop, const char* header)
{
    static int loopCounter3 = 0;
  
    if((loopCounter3 > start) && (loopCounter3 < stop))
    {
        if (type == VOLTAGE)
        {
            loopCounter3++;
            std::string fullHeader = std::string(header) + " VOLTAGE SAMPLES";
            printFloatSamples(samples, count, fullHeader.c_str());
        }
        else
        {
//            loopCounter3++;
    //        std::string fullHeader = std::string(header) + " CURRENT SAMPLES";
    //        printFloatSamples(samples, count, fullHeader.c_str());
        }
    }
    else
    {
        if(loopCounter3 >stop+1) 
        {
            loopCounter3 = stop+1;
        }
        else{
            loopCounter3++;
        }
    }
}
void ACMonitor::printFloatSamples(const float* samples, int count, const char* header)
{
        ESP_LOGI(TAG, "----- %s (%d samples) -----", header, count);

    for (int i = 0; i < count; i++) {
        printf("%7.1f", samples[i]);

        // Print comma except at end of line
        if ((i % 15) != 14 && i != count - 1) {
            printf(", ");
        }

        // New line every 15 values
        if ((i % 15) == 14) {
            printf("\n");
        }
    }

    // Final newline if last line wasn't complete
    if (count % 15 != 0) {
        printf("\n");
    }

    ESP_LOGI(TAG, "----- END %s -----", header);
}
void ACMonitor::printSamples(const uint16_t* samples, int count, const char* header)
{
    ESP_LOGI(TAG, "----- %s (%d samples) -----", header, count);

    for (int i = 0; i < count; i++) {
        printf("%5u", samples[i]);

        // Print comma except at end of line
        if ((i % 15) != 14 && i != count - 1) {
            printf(", ");
        }

        // New line every 15 values
        if ((i % 15) == 14) {
            printf("\n");
        }
    }

    // Final newline if last line wasn't complete
    if (count % 15 != 0) {
        printf("\n");
    }

    ESP_LOGI(TAG, "----- END %s -----", header);
}

// 5‑point Savitzky–Golay smoothing filter (2nd order)
// Applies in-place smoothing to the samples array.
void ACMonitor::applySGFilter(uint16_t* samples, int count)
{
    if (count < 5) return;

    // Temporary buffer
    uint16_t* temp = (uint16_t*)malloc(count * sizeof(uint16_t));
    if (!temp) return;

    // Copy original
    for (int i = 0; i < count; i++) {
        temp[i] = samples[i];
    }

    // Apply SG filter to interior points
    for (int i = 2; i < count - 2; i++) {
        int32_t v =
            (-3 * temp[i - 2] +
             12 * temp[i - 1] +
             17 * temp[i] +
             12 * temp[i + 1] -
              3 * temp[i + 2]);

        samples[i] = (uint16_t)(v / 35);
    }

    // Leave first and last 2 samples unchanged
    free(temp);
}

void ACMonitor::filterSamples(const float *centered, float *filtered, int count, BiquadLPF *f)
{
    for (int i = 0; i < count; i++) {
        filtered[i] = processLPF(f, centered[i]);
    }
}

void ACMonitor::centerSamples(const uint16_t *samples, float *centered, int count, float dc_offset)
{
    // create centered samples by subtracting dc offset
    for (int i = 0; i < count; i++) {
        centered[i] = (float)samples[i] - dc_offset;
    }
}

void ACMonitor::initLowPass5k(BiquadLPF *f)
{
        // Precomputed Butterworth biquad coefficients
    f->b0 = 0.206572083826147;
    f->b1 = 0.413144167652294;
    f->b2 = 0.206572083826147;
    f->a1 = -0.369527377351241;
    f->a2 = 0.195815712655833;

    f->z1 = 0;
    f->z2 = 0;
}

void ACMonitor::initLowPass1k(BiquadLPF* f) {
    f->b0 = 0.020083365564211f;
    f->b1 = 0.040166731128422f;
    f->b2 = 0.020083365564211f;
    f->a1 = -1.561018075800718f;
    f->a2 = 0.641351538057563f;

    f->z1 = 0;
    f->z2 = 0;
}

float ACMonitor::processLPF(BiquadLPF * f, float x)
{
    float y = f->b0*x + f->z1;
    f->z1 = f->b1*x - f->a1*y + f->z2;
    f->z2 = f->b2*x - f->a2*y;
    return y;
}
