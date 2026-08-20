/*
 * rdm.h — RDM (ANSI E1.20) scaffolding
 * v1.0.0
 *
 * IMPLEMENTED:
 *   - Correct BREAK/MAB timing for RDM start code (0xCC)
 *   - Half-duplex turnaround (direction switching is automatic in hardware
 *     on this transceiver — no DE pin to drive)
 *   - Packet framing & checksum
 *   - GET/SET: DEVICE_INFO, DMX_START_ADDRESS, IDENTIFY_DEVICE,
 *              SUPPORTED_PARAMETERS, DEVICE_LABEL, SOFTWARE_VERSION_LABEL
 *
 * STUBBED (not implemented):
 *   - DISC_UNIQUE_BRANCH (DUB) — discovery collision tree
 *   - DISC_MUTE / DISC_UN_MUTE
 *   - Full discovery polling loop
 *
 * To enable RDM, call rdmPoll() from loop() periodically (e.g. every 20ms).
 * The UART RX must be wired (GPIO16 ← module RXD).
 */
#pragma once
#include <Arduino.h>
#include "dmx.h"
#include "config.h"

// ─── Constants ───────────────────────────────────────────────────────────────
#define RDM_START_CODE      0xCC
#define RDM_SUB_START       0x01
#define RDM_BREAK_US        176   // RDM BREAK: 176µs min
#define RDM_MAB_US          12

// Command classes
#define CC_DISC_COMMAND             0x10
#define CC_DISC_COMMAND_RESPONSE    0x11
#define CC_GET_COMMAND              0x20
#define CC_GET_COMMAND_RESPONSE     0x21
#define CC_SET_COMMAND              0x30
#define CC_SET_COMMAND_RESPONSE     0x31

// Response types
#define RT_ACK                      0x00
#define RT_ACK_TIMER                0x01
#define RT_NACK_REASON              0x02

// NACK reasons
#define NR_UNKNOWN_PID              0x0000
#define NR_FORMAT_ERROR             0x0001
#define NR_HARDWARE_FAULT           0x0002
#define NR_PROXY_REJECT             0x0003
#define NR_WRITE_PROTECT            0x0004
#define NR_UNSUPPORTED_COMMAND_CLASS 0x0005
#define NR_DATA_OUT_OF_RANGE        0x0006

// PIDs
#define PID_DISC_UNIQUE_BRANCH      0x0001
#define PID_DISC_MUTE               0x0002
#define PID_DISC_UN_MUTE            0x0003
#define PID_SUPPORTED_PARAMETERS    0x0050
#define PID_DEVICE_INFO             0x0060
#define PID_DEVICE_LABEL            0x0082
#define PID_SOFTWARE_VERSION_LABEL  0x00C0
#define PID_DMX_START_ADDRESS       0x00F0
#define PID_IDENTIFY_DEVICE         0x1000

// ─── UID ─────────────────────────────────────────────────────────────────────
// Manufacturer ID: 0x7FF0 (prototype/development range)
// Device ID: lower 4 bytes of ESP32 MAC
static uint8_t rdmUID[6];

inline void rdmInitUID() {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  rdmUID[0] = 0x7F;
  rdmUID[1] = 0xF0;
  rdmUID[2] = mac[2];
  rdmUID[3] = mac[3];
  rdmUID[4] = mac[4];
  rdmUID[5] = mac[5];
  Serial.printf("[rdm] UID: %02X:%02X:%02X:%02X:%02X:%02X\n",
    rdmUID[0],rdmUID[1],rdmUID[2],rdmUID[3],rdmUID[4],rdmUID[5]);
}

// ─── Packet helpers ──────────────────────────────────────────────────────────
inline uint16_t rdmChecksum(const uint8_t* buf, size_t len) {
  uint16_t sum = 0;
  for (size_t i = 0; i < len; i++) sum += buf[i];
  return sum;
}

// Build a response packet into outBuf, return total length
inline size_t rdmBuildResponse(
    uint8_t* outBuf,
    const uint8_t* destUID,      // 6 bytes — controller UID
    uint8_t  transactionNum,
    uint8_t  responseType,       // RT_ACK etc.
    uint8_t  msgCount,
    uint16_t subDevice,
    uint8_t  cmdClass,
    uint16_t pid,
    const uint8_t* pdData,
    uint8_t  pdLen)
{
  uint8_t msgLen = 24 + pdLen; // header (24) + PD
  outBuf[0]  = RDM_START_CODE;
  outBuf[1]  = RDM_SUB_START;
  outBuf[2]  = msgLen;
  memcpy(outBuf + 3,  destUID,  6);  // destination = controller
  memcpy(outBuf + 9,  rdmUID,   6);  // source      = us
  outBuf[15] = transactionNum;
  outBuf[16] = responseType;
  outBuf[17] = msgCount;
  outBuf[18] = (subDevice >> 8) & 0xFF;
  outBuf[19] =  subDevice & 0xFF;
  outBuf[20] = cmdClass;
  outBuf[21] = (pid >> 8) & 0xFF;
  outBuf[22] =  pid & 0xFF;
  outBuf[23] = pdLen;
  if (pdLen && pdData) memcpy(outBuf + 24, pdData, pdLen);
  uint16_t cs = rdmChecksum(outBuf, msgLen);
  outBuf[msgLen]     = (cs >> 8) & 0xFF;
  outBuf[msgLen + 1] =  cs & 0xFF;
  return msgLen + 2;
}

// ─── Process incoming RDM packet ─────────────────────────────────────────────
inline void rdmHandlePacket(const uint8_t* pkt, size_t len, Config& cfg) {
  if (len < 26) return;
  if (pkt[0] != RDM_START_CODE || pkt[1] != RDM_SUB_START) return;

  uint8_t  msgLen  = pkt[2];
  if ((size_t)msgLen + 2 > len) return;

  // Verify checksum
  uint16_t csCalc = rdmChecksum(pkt, msgLen);
  uint16_t csRecv = (pkt[msgLen] << 8) | pkt[msgLen + 1];
  if (csCalc != csRecv) { Serial.println("[rdm] bad checksum"); return; }

  // Check destination UID (us or broadcast FF:FF:FF:FF:FF:FF)
  const uint8_t* destUID = pkt + 3;
  bool broadcast = true;
  for (int i = 0; i < 6; i++) if (destUID[i] != 0xFF) { broadcast = false; break; }
  bool forUs = broadcast || (memcmp(destUID, rdmUID, 6) == 0);
  if (!forUs) return;

  const uint8_t* srcUID   = pkt + 9;
  uint8_t  tn      = pkt[15];
  uint8_t  cc      = pkt[20];
  uint16_t pid     = (pkt[21] << 8) | pkt[22];
  uint8_t  pdLen   = pkt[23];
  const uint8_t* pd = pkt + 24;

  // Discovery branch — stub
  if (cc == CC_DISC_COMMAND && pid == PID_DISC_UNIQUE_BRANCH) {
    // TODO: implement DUB collision tree
    Serial.println("[rdm] DUB stub — not implemented");
    return;
  }
  if (cc == CC_DISC_COMMAND && (pid == PID_DISC_MUTE || pid == PID_DISC_UN_MUTE)) {
    Serial.printf("[rdm] DISC_%s stub\n", pid == PID_DISC_MUTE ? "MUTE" : "UN_MUTE");
    return;
  }

  // Build response buffer
  uint8_t resp[128];
  size_t  respLen = 0;

  if (cc == CC_GET_COMMAND) {
    switch (pid) {
      case PID_DEVICE_INFO: {
        uint8_t pd_out[19] = {
          0x01, 0x00,                     // RDM Protocol Version 1.0
          0x00, 0x01,                     // Device Model ID
          0xE1, 0x31,                     // Product Category: fixture (DMX decoder)
          0x00, 0x00, 0x01, 0x00,         // Software version
          0x02, 0x00,                     // DMX Footprint (512)
          (uint8_t)(cfg.dmxStartAddress >> 8),
          (uint8_t)(cfg.dmxStartAddress & 0xFF),
          0x00,                           // Sub-device count
          0x00,                           // Sensor count
        };
        pd_out[12] = (cfg.dmxStartAddress >> 8) & 0xFF;
        pd_out[13] =  cfg.dmxStartAddress & 0xFF;
        respLen = rdmBuildResponse(resp, srcUID, tn, RT_ACK, 0, 0,
                                   CC_GET_COMMAND_RESPONSE, pid, pd_out, 19);
        break;
      }
      case PID_DMX_START_ADDRESS: {
        uint8_t pd_out[2] = {
          (uint8_t)(cfg.dmxStartAddress >> 8),
          (uint8_t)(cfg.dmxStartAddress & 0xFF)
        };
        respLen = rdmBuildResponse(resp, srcUID, tn, RT_ACK, 0, 0,
                                   CC_GET_COMMAND_RESPONSE, pid, pd_out, 2);
        break;
      }
      case PID_IDENTIFY_DEVICE: {
        uint8_t pd_out[1] = { cfg.rdmIdentify ? 1 : 0 };
        respLen = rdmBuildResponse(resp, srcUID, tn, RT_ACK, 0, 0,
                                   CC_GET_COMMAND_RESPONSE, pid, pd_out, 1);
        break;
      }
      case PID_DEVICE_LABEL: {
        uint8_t pd_out[32];
        uint8_t llen = (uint8_t)strnlen(cfg.rdmLabel, 32);
        memcpy(pd_out, cfg.rdmLabel, llen);
        respLen = rdmBuildResponse(resp, srcUID, tn, RT_ACK, 0, 0,
                                   CC_GET_COMMAND_RESPONSE, pid, pd_out, llen);
        break;
      }
      case PID_SOFTWARE_VERSION_LABEL: {
        const char* ver = "v1.0.0";
        respLen = rdmBuildResponse(resp, srcUID, tn, RT_ACK, 0, 0,
                                   CC_GET_COMMAND_RESPONSE, pid,
                                   (const uint8_t*)ver, (uint8_t)strlen(ver));
        break;
      }
      case PID_SUPPORTED_PARAMETERS: {
        uint8_t pd_out[] = {
          0x00, 0x82,  // DEVICE_LABEL
          0x00, 0xC0,  // SOFTWARE_VERSION_LABEL
          0x00, 0xF0,  // DMX_START_ADDRESS
          0x10, 0x00,  // IDENTIFY_DEVICE
        };
        respLen = rdmBuildResponse(resp, srcUID, tn, RT_ACK, 0, 0,
                                   CC_GET_COMMAND_RESPONSE, pid, pd_out, sizeof(pd_out));
        break;
      }
      default: {
        // NACK: Unknown PID
        uint8_t nack[2] = { 0x00, 0x00 };
        respLen = rdmBuildResponse(resp, srcUID, tn, RT_NACK_REASON, 0, 0,
                                   CC_GET_COMMAND_RESPONSE, pid, nack, 2);
        break;
      }
    }
  } else if (cc == CC_SET_COMMAND) {
    switch (pid) {
      case PID_DMX_START_ADDRESS: {
        if (pdLen >= 2) {
          cfg.dmxStartAddress = (pd[0] << 8) | pd[1];
          Serial.printf("[rdm] set DMX start addr: %d\n", cfg.dmxStartAddress);
          respLen = rdmBuildResponse(resp, srcUID, tn, RT_ACK, 0, 0,
                                     CC_SET_COMMAND_RESPONSE, pid, nullptr, 0);
        }
        break;
      }
      case PID_IDENTIFY_DEVICE: {
        if (pdLen >= 1) {
          cfg.rdmIdentify = pd[0] != 0;
          Serial.printf("[rdm] identify: %s\n", cfg.rdmIdentify ? "ON" : "OFF");
          respLen = rdmBuildResponse(resp, srcUID, tn, RT_ACK, 0, 0,
                                     CC_SET_COMMAND_RESPONSE, pid, nullptr, 0);
        }
        break;
      }
      case PID_DEVICE_LABEL: {
        uint8_t llen = min((int)pdLen, 32);
        memcpy(cfg.rdmLabel, pd, llen);
        cfg.rdmLabel[llen] = '\0';
        respLen = rdmBuildResponse(resp, srcUID, tn, RT_ACK, 0, 0,
                                   CC_SET_COMMAND_RESPONSE, pid, nullptr, 0);
        break;
      }
      default: {
        uint8_t nack[2] = { 0x00, 0x00 };
        respLen = rdmBuildResponse(resp, srcUID, tn, RT_NACK_REASON, 0, 0,
                                   CC_SET_COMMAND_RESPONSE, pid, nack, 2);
        break;
      }
    }
  }

  if (respLen && !broadcast) {
    // BREAK + response
    dmxSetTransmit();
    uart_set_line_inverse(DMX_UART, UART_SIGNAL_TXD_INV);
    delayMicroseconds(RDM_BREAK_US);
    uart_set_line_inverse(DMX_UART, 0);
    delayMicroseconds(RDM_MAB_US);
    dmxSendRaw(resp, respLen);
    dmxSetReceive();
  }
}

// ─── Poll for incoming RDM ────────────────────────────────────────────────────
// Call this from loop() every ~20ms when not actively sending DMX
static uint8_t rdmRxBuf[256];
static size_t  rdmRxLen = 0;
static uint32_t rdmRxLastByte = 0;

inline void rdmPoll(Config& cfg) {
  // Briefly switch to receive mode and read any waiting bytes
  dmxSetReceive();
  delayMicroseconds(50);

  int b;
  while ((b = dmxReadByte(1)) >= 0) {
    if (rdmRxLen == 0 && b != RDM_START_CODE) continue; // wait for start
    if (rdmRxLen < sizeof(rdmRxBuf)) {
      rdmRxBuf[rdmRxLen++] = (uint8_t)b;
      rdmRxLastByte = millis();
    }
  }

  // Process complete packet (timeout = 5ms after last byte)
  if (rdmRxLen > 0 && (millis() - rdmRxLastByte) > 5) {
    rdmHandlePacket(rdmRxBuf, rdmRxLen, cfg);
    rdmRxLen = 0;
  }

  dmxSetTransmit();
}
