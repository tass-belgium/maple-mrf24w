
#include "mrf24w.h"
#include "mrf24w_g2100.h"
#include "mrf24w_stack.h"
#include <string.h>
#include <stdio.h>

#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_dma.h"
#include "stm32f4xx_hal_spi.h"
#include "stm32f4xx_hal_gpio.h"

#include "pico_defines.h"
#include "pico_config.h"
#include "pico_device.h"

void *g_mrf24wInstance = NULL;
uint8_t stack_local_ip[4] = {192, 168, 0, 99};
uint8_t stack_gateway_ip[4] = {192, 168, 0, 1};
uint8_t stack_subnet_mask[4] = {255, 255, 255, 0};

// XXX: temp, better way?
int g_scanResultsReady = 0;

#define ASSERT(cond)    do{ if (!(cond)) { printf("assert in %s:%d\n", __FILE__, __LINE__, 0); while (1) {}; } } while (0)

/* SPI module */
SPI_TypeDef*    wf_spi;
/* CS pin */
GPIO_TypeDef*   wf_cs_port;
uint16_t        wf_cs_pin;
/* Reset pin */
GPIO_TypeDef*   wf_reset_port;
uint16_t        wf_reset_pin;
/* Interrupt pin */
GPIO_TypeDef*   wf_int_port;
uint16_t        wf_int_pin;


// XXX: Move?
SPI_HandleTypeDef hspi;


/**
* @brief This function handles SPI2 global interrupt.
*/
void SPI2_IRQHandler(void)
{
  HAL_NVIC_ClearPendingIRQ(SPI2_IRQn);
  HAL_SPI_IRQHandler(&hspi);
}

/**
* @brief This function handles EXTI Line 10->15 interrupts.
*/
void EXTI15_10_IRQHandler(void)
{
  HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
  wf_isr();
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_11);
} 

// XXX: supposed to be the wf_processEvent user callback
void wf_processEvent(uint8_t event, uint16_t eventInfo, uint8_t* extraInfo) {
    mrf24w_processEvent(event, eventInfo, extraInfo);
}

static int pico_mrf24_poll(struct pico_device * dev, int loop_score)
{
    wf_macProcess();
    if (g_scanResultsReady)
        wf_printScanResults();
}

static int pico_mrf24_send(struct pico_device * dev, void * buf, int len)
{
    printf(">tx %db\n", len);
}

struct pico_device * pico_eth_create(char *name, uint8_t *mac)
{
    struct pico_device * mrf24wg = PICO_ZALLOC(sizeof(struct pico_device));
    if (!mrf24wg)
        return NULL;

    /*          SPI             CS,               RESET            INTERRUPT   */
    printf("mrf24w> init\n");
    mrf24w_init(SPI2, GPIOB, GPIO_PIN_12, GPIOB, GPIO_PIN_10, GPIOB, GPIO_PIN_11);
    wf_init();

    mrf24wg->poll = pico_mrf24_poll;
    mrf24wg->send = pico_mrf24_send;

    /* Get MAC address */
    wf_getMacAddress(mac);
    printf("mrf24w> mac: %x:%x:%x:%x:%x:%x\n", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

    if(0 != pico_device_init(mrf24wg, name, mac)) {
        dbg("mrf24w> init failed.\r\n");
        return NULL;
    }

    /* start with a scan */
    {
        uint8_t connectionProfileId;
        uint8_t channelList[] = {};

        //connectionProfileId = wf_cpCreate();
        //ASSERT(connectionProfileId != 0xff);
        //wf_cpSetSsid(connectionProfileId, (uint8_t*) m_ssid, strlen(m_ssid));
        //wf_cpSetNetworkType(connectionProfileId, m_wirelessMode);
        //wf_caSetScanType(WF_ACTIVE_SCAN);
        //wf_caSetChannelList(channelList, sizeof (channelList));
        //wf_caSetListRetryCount(10);
        //wf_caSetBeaconTimeout(40);
        //wf_cpSetSecurity(connectionProfileId, m_securityType, 0, m_securityPassphrase, m_securityPassphraseLen);
        //wf_cmConnect(connectionProfileId);
        wf_scan(0xFF); // Scan using default filter
    }

    return mrf24wg;
}

void mrf24w_init(SPI_TypeDef *spi, GPIO_TypeDef *cs_gpio, uint16_t cs_pin, GPIO_TypeDef *reset_gpio, uint16_t reset_pin, GPIO_TypeDef *int_gpio, uint16_t int_pin)
{
    GPIO_InitTypeDef GPIO_InitStruct = {};

    m_securityType = WF_SECURITY_OPEN;
    m_wirelessMode = WF_INFRASTRUCTURE;

    wf_spi = spi;
    wf_cs_port = cs_gpio;           // CS:  YELLOW wire, floating  ,  PB12
    wf_cs_pin = cs_pin;
    wf_reset_port = reset_gpio;     // RES: BLUE wire,   floating  ,  PB10
    wf_reset_pin = reset_pin;
    wf_int_port = int_gpio;         // INT: ORANGE wire, pulled HIGH, PB11
    wf_int_pin = int_pin;

    /*
     * Enable all GPIO port clocks
     */
    __GPIOA_CLK_ENABLE();
    __GPIOB_CLK_ENABLE();
    __GPIOC_CLK_ENABLE();
    __GPIOD_CLK_ENABLE();
    __GPIOE_CLK_ENABLE();
    __GPIOH_CLK_ENABLE();

    // Interrupt pin -- PB11 ==> EXTI11
    GPIO_InitStruct.Pin = wf_int_pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING; // Still pulled up by a resistor for now.. (orange wire)
    //GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(wf_int_port, &GPIO_InitStruct);
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

    // Reset pin
    GPIO_InitStruct.Pin = wf_reset_pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(wf_reset_port, &GPIO_InitStruct);
    HAL_GPIO_WritePin(wf_reset_port, wf_reset_pin, GPIO_PIN_RESET); /* reset is active low */

    // CS pin
    GPIO_InitStruct.Pin = wf_cs_pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN; // Pulled low by break-out-board
    HAL_GPIO_Init(wf_cs_port, &GPIO_InitStruct);
    HAL_GPIO_WritePin(wf_cs_port, wf_cs_pin, GPIO_PIN_SET);

    // SPI pin config
    __SPI2_CLK_ENABLE();
    GPIO_InitStruct.Pin = (GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);    // PB13: SCL
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;                             // PB14: MISO
    GPIO_InitStruct.Speed = GPIO_SPEED_FAST;                            // PB15: MOSI
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    // SPI init
    // Maximum SPI clock for MRF24WG is 25 MHz
    hspi.Instance = wf_spi;
    hspi.Init.Mode = SPI_MODE_MASTER;
    hspi.Init.Direction = SPI_DIRECTION_2LINES;
    hspi.Init.DataSize = SPI_DATASIZE_8BIT;

    hspi.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi.Init.CLKPhase = SPI_PHASE_1EDGE;

    hspi.Init.NSS = SPI_NSS_SOFT;
    hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    hspi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi.Init.TIMode = SPI_TIMODE_DISABLED;
    hspi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLED;
    hspi.Init.CRCPolynomial = 0;
    HAL_SPI_Init(&hspi);
}

void mrf24w_setProcessEventFn(void * processEventFn)
{
    m_processEventFn = processEventFn;
}

void mrf24w_scan(uint8_t cpid) {
    wf_scan(cpid);
}

void mrf24w_connect() {
    uint8_t connectionProfileId;
    uint8_t channelList[] = {};

    connectionProfileId = wf_cpCreate();
    ASSERT(connectionProfileId != 0xff);
    wf_cpSetSsid(connectionProfileId, (uint8_t*) m_ssid, strlen(m_ssid));
    wf_cpSetNetworkType(connectionProfileId, m_wirelessMode);
    wf_caSetScanType(WF_ACTIVE_SCAN);
    wf_caSetChannelList(channelList, sizeof (channelList));
    wf_caSetListRetryCount(10);
    wf_caSetBeaconTimeout(40);
    wf_cpSetSecurity(connectionProfileId, m_securityType, 0, m_securityPassphrase, m_securityPassphraseLen);
    wf_cmConnect(connectionProfileId);
}

void mrf24w_setLocalIp(uint8_t localIpAddr[]) {
    memcpy(stack_local_ip, localIpAddr, 4);
}

void mrf24w_setGatewayIp(uint8_t gatewayIpAddr[]) {
    memcpy(stack_gateway_ip, gatewayIpAddr, 4);
}

void mrf24w_setSubnetMask(uint8_t subnetMask[]) {
    memcpy(stack_subnet_mask, subnetMask, 4);
}

void mrf24w_setSSID(const char* ssid) {
    strcpy(m_ssid, ssid);
}

void mrf24w_setSecurityPassphrase(const char* securityPassphrase) {
    strcpy((char*) m_securityPassphrase, securityPassphrase);
    m_securityPassphraseLen = strlen(securityPassphrase);
}

void mrf24w_setSecurityType(uint8_t securityType) {
    m_securityType = securityType;
}

void mrf24w_setWirelessMode(uint8_t wirelessMode) {
    m_wirelessMode = wirelessMode;
}

void mrf24w_processEvent(uint8_t event, uint16_t eventInfo, uint8_t* extraInfo) {
    wf_setFuncState(WF_PROCESS_EVENT_FUNC, WF_ENTERING_FUNCTION);

    printf("wf_processEvent: %d, %d\n", event, eventInfo);

    switch (event) {
        case WF_EVENT_CONNECTION_SUCCESSFUL:
            printf("WF_EVENT_CONNECTION_SUCCESSFUL\n");
            break;

        case WF_EVENT_CONNECTION_FAILED:
            printf("WF_EVENT_CONNECTION_FAILED: %d\n", eventInfo);
            break;

        case WF_EVENT_CONNECTION_TEMPORARILY_LOST:
            printf("WF_EVENT_CONNECTION_TEMPORARILY_LOST: %d\n", eventInfo);
            break;

        case WF_EVENT_CONNECTION_PERMANENTLY_LOST:
            printf("WF_EVENT_CONNECTION_PERMANENTLY_LOST: %d\n", eventInfo);
            break;

        case WF_EVENT_CONNECTION_REESTABLISHED:
            printf("WF_EVENT_CONNECTION_REESTABLISHED\n");
            break;

        case WF_EVENT_SCAN_RESULTS_READY:
            {
                g_scanResultsReady = eventInfo;
                printf("WF_EVENT_SCAN_RESULTS_READY: %d results\n", eventInfo);
                break;
            }

        case WF_EVENT_RX_PACKET_RECEIVED:
            printf("WF_EVENT_RX_PACKET_RECEIVED: %d\n", eventInfo);
            break;

        case WF_EVENT_INVALID_WPS_PIN:
            printf("WF_EVENT_INVALID_WPS_PIN\n");
            break;

        default:
            printf("UNKNOWN Event: %d\n", eventInfo);
            break;
    }

    if (m_processEventFn) {
        // TODO: make function pointer!
        //m_processEventFn(event, eventInfo, extraInfo);
    }

    wf_setFuncState(WF_PROCESS_EVENT_FUNC, WF_LEAVING_FUNCTION);
}

void wf_printScanResults(void)
{
    int i;
    int amount = g_scanResultsReady;
    tWFScanResult scanResult;

    g_scanResultsReady = 0;

    for (i = 0; i < amount; i++) {
        char ssid[20];
        wf_scanGetResult(i, &scanResult);
        strncpy(ssid, (const char*) scanResult.ssid, scanResult.ssidLen);
        ssid[scanResult.ssidLen] = '\0';
        printf("%s\n", ssid);
    }
}

