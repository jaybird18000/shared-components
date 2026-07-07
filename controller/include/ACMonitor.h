#pragma once

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>

class ACMonitor {
public:
    struct ACResults {
        float dc_offset;
        float peak_adc;
        float rms_adc;
        float frequency;
        float rms_voltage;
        float peak_voltage;
        float rms_current;
        float peak_current;
    };
    // 5 kHz low-pass biquad filter for 20 kHz sampling
    typedef struct {
        float b0, b1, b2;
        float a1, a2;
        float z1, z2;
    } BiquadLPF;

    enum MeasurementType {
        VOLTAGE,
        CURRENT
    };

    ACMonitor();
    ~ACMonitor();

    void init();
    ACResults readVoltage();
    ACResults readCurrent();

private:

    float Voltage_DC_Offset;
    bool needToComputeVoltageDcOffset;
    bool vacPresent;
    int vacPresentCount;
    int needTocomputeCurrentDcOffsetCounter;
    float Current_DC_Offset;
    bool needToComputeCurrentDcOffset;

    // One-shot + calibration
    adc_oneshot_unit_handle_t adc_handle_ = nullptr;
    adc_cali_handle_t voltage_handle_ = nullptr;
    adc_cali_handle_t current_handle_ = nullptr;

    // Continuous ADC DMA
    adc_continuous_handle_t dma_handle_ = nullptr;

    // Mutex to prevent re-entry
    SemaphoreHandle_t adcMutex_ = nullptr;

    // ADC config
    const adc_unit_t unit_ = ADC_UNIT_1;
    const adc_channel_t voltage_channel_ = ADC_CHANNEL_0;
    const adc_channel_t current_channel_ = ADC_CHANNEL_3;
    const adc_atten_t atten_ = ADC_ATTEN_DB_12;
    const adc_bitwidth_t width_ = ADC_BITWIDTH_12;

    // Sampling config
    static constexpr uint32_t SAMPLE_RATE = 20000;      // 20 kHz
    static constexpr uint32_t SAMPLE_COUNT = 1024;      // samples per frame
    static constexpr uint32_t FRAME_SIZE =
        SAMPLE_COUNT * sizeof(adc_digi_output_data_t);  // bytes per DMA frame

    // Scaling
    const float voltage_scale_ = 0.1713f;
    const float current_scale_ = (3.3f / 4095.0f) * 30.0f;

    // Sample buffer (member, not on stack)
    uint16_t samples_[SAMPLE_COUNT] = {0};
    float centeredSamples_[SAMPLE_COUNT] = {0.0};
    float filteredSamples_[SAMPLE_COUNT] = {0.0};    
    BiquadLPF lpf;

    // Internal helpers
    ACResults readSamples(MeasurementType type, adc_channel_t channel);
    void applySGFilter(uint16_t* samples, int count);
    void filterSamples(const float *centered, float *filtered, int count, BiquadLPF *f);
    void centerSamples(const uint16_t* samples, float *centered, int count, float dc_offset);
    void initLowPass5k(BiquadLPF* f);
    void initLowPass1k(BiquadLPF* f) ;
    float processLPF(BiquadLPF* f, float x);
    void print0SamplesInterval(MeasurementType type, const uint16_t* samples, int count, int start, int stop, const char* header);
    void print0FloatSamplesInterval(MeasurementType type, const float* samples, int count, int start, int stop, const char* header);
    void print1FloatSamplesInterval(MeasurementType type, const float* samples, int count, int start, int stop, const char* header);
    void printFloatSamples(const float* samples, int count, const char* header);
    void printSamples(const uint16_t* samples, int count, const char* header);
    ACResults analyze_ac(MeasurementType type,
                         const uint16_t* samples,
                         int count,
                         float sample_rate);

    bool isAcPresent(const uint16_t* samples, int count);
    float compute_initial_dc_offset(MeasurementType type, const uint16_t* samples, int count);
    float compute_fine_dc_offset(MeasurementType type, const float* samples, int count, int startIndex, int stopIndex);
    bool determineZeroCrossings(MeasurementType type, const float* samples, int count, int& zc1, int& zc2, int& zc3);
    float compute_peak(const float* samples, int count, float offset);
    float compute_rms(MeasurementType type, const float* samples, int count, float offset);
    float compute_frequency(const float* samples,
                            int count,
                            float offset,
                            float sample_rate);
};