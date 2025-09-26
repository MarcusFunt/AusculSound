#if defined(ARDUINO_XIAO_MG24)
#include "mg24_adc.h"

static bool dmaCompleteCallback(unsigned int channel, unsigned int sequenceNo, void *userParam);

void MG24_ADC_Class::initADC() {
    IADC_Init_t adcInit = IADC_INIT_DEFAULT;
    IADC_InitScan_t initScan = IADC_INITSCAN_DEFAULT;
    IADC_AllConfigs_t initAllConfigs = IADC_ALLCONFIGS_DEFAULT;
    IADC_ScanTable_t scanTable = IADC_SCANTABLE_DEFAULT;

    CMU_ClockEnable(cmuClock_IADC0, true);
    CMU_ClockEnable(cmuClock_GPIO, true);
    CMU_ClockSelectSet(cmuClock_IADCCLK, cmuSelect_FSRCO);

    const uint32_t iadcClkSourceHz = CMU_ClockFreqGet(cmuClock_IADCCLK);


    adcInit.srcClkPrescale = IADC_calcSrcClkPrescale(IADC0, CLK_SRC_ADC_FREQ, 0);
    adcInit.warmup = iadcWarmupNormal;
    adcInit.iadcClkSuspend1 = true;

    const uint32_t iadcClkHz = iadcClkSourceHz / (static_cast<uint32_t>(adcInit.srcClkPrescale) + 1u);

    initAllConfigs.configs[0].reference = iadcCfgReferenceInt1V2;
    initAllConfigs.configs[0].vRef = 1200;
    initAllConfigs.configs[0].osrHighSpeed = iadcCfgOsrHighSpeed2x;
    initAllConfigs.configs[0].analogGain = iadcCfgAnalogGain1x;
    initAllConfigs.configs[0].adcClkPrescale = IADC_calcAdcClkPrescale(IADC0, CLK_ADC_FREQ, 0, iadcCfgModeNormal, adcInit.srcClkPrescale);

    const uint32_t adcClkPrescale = static_cast<uint32_t>(initAllConfigs.configs[0].adcClkPrescale) + 1u;
    const uint32_t adcClkHz = adcClkPrescale == 0u ? 0u : (iadcClkHz / adcClkPrescale);

    uint32_t timerCycles = 0u;
    if (_sampling_rate > 0u && adcClkHz > 0u) {
        timerCycles = adcClkHz / _sampling_rate;
    }

    if (timerCycles == 0u) {
        timerCycles = 1u;
    }

    if (timerCycles > 0xFFFFu) {
        timerCycles = 0xFFFFu;
    }

    adcInit.timerCycles = static_cast<uint16_t>(timerCycles);

    initScan.triggerSelect = iadcTriggerSelTimer;
    initScan.triggerAction = iadcTriggerActionOnce;
    initScan.dataValidLevel = iadcFifoCfgDvl2;
    initScan.fifoDmaWakeup = true;
    initScan.start = false;
    initScan.alignment = iadcAlignRight12;

    scanTable.entries[0].posInput = IADC_INPUT_0_PORT_PIN;
    scanTable.entries[0].negInput = iadcNegInputGnd;
    scanTable.entries[0].includeInScan = true;

    IADC_reset(IADC0);
    IADC_init(IADC0, &adcInit, &initAllConfigs);
    IADC_initScan(IADC0, &initScan, &scanTable);
    GPIO->CDBUSALLOC |= GPIO_CDBUSALLOC_CDODD0_ADC0;
}

uint8_t MG24_ADC_Class::initLDMA() {
    sl_status_t status;
    // Initialize DMA with default parameters
    DMADRV_Init();
    // Allocate DMA channel
    status = DMADRV_AllocateChannel(&this->dma_channel, NULL);
    if (status != ECODE_EMDRV_DMADRV_OK) {
        return 0;
    }
    DMADRV_PeripheralMemoryPingPong(
        this->dma_channel,
        dmadrvPeripheralSignal_IADC0_IADC_SCAN,
        buf_0,
        buf_1,
        (void *)&(IADC0->SCANFIFODATA),
        false,
        _buf_size ,
        dmadrvDataSize2,
        dmaCompleteCallback,
        NULL);

    IADC_command(IADC0, iadcCmdEnableTimer);
    IADC_command(IADC0, iadcCmdStartScan);
    return 1;
}

uint8_t MG24_ADC_Class::begin() {
    pinMode(MIC_INPUTPIN, INPUT);
    pinMode(MIC_ENPIN, OUTPUT);
    digitalWrite(MIC_ENPIN, HIGH);
    initADC();
    return initLDMA();
}

void MG24_ADC_Class::end() {
    // PRS_SourceAsyncSignalSet(PRS_CHANNEL, 0, 0);
    // Stop sampling
    DMADRV_StopTransfer(this->dma_channel);
    IADC_command(IADC0, iadcCmdDisableTimer);

    // Free resources
    DMADRV_FreeChannel(this->dma_channel);

    // Reset the ADC
    IADC_reset(IADC0);
    digitalWrite(MIC_ENPIN, LOW);
}
void MG24_ADC_Class::pause(){
    DMADRV_PauseTransfer(this->dma_channel);
}
void MG24_ADC_Class::resume(){
    DMADRV_ResumeTransfer(this->dma_channel);
}

static bool dmaCompleteCallback(unsigned int channel, unsigned int sequenceNo, void *userParam){
    if (MG24_ADC_Class::_onReceive) {
        MG24_ADC_Class::_onReceive((sequenceNo % 2) ? MG24_ADC_Class::buf_0_ptr : MG24_ADC_Class::buf_1_ptr, *MG24_ADC_Class::_buf_size_ptr);
    }
    *MG24_ADC_Class::_buf_count_ptr = sequenceNo % 2 ? 0 : 1;
    return true;
}

#endif