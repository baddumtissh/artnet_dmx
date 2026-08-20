/*
 * artnet.h — Art-Net DMX packet parser
 * v1.0.0
 *
 * Supports: ArtDmx (OpCode 0x5000)
 * Ref: Art-Net 4 specification
 */
#pragma once
#include <Arduino.h>

#define ARTNET_PORT       6454
#define ARTNET_ID         "Art-Net\0"   // 8 bytes, null-terminated
#define ARTNET_OP_DMX     0x5000

// Minimum packet size: 18 bytes header
#define ARTNET_HDR_LEN    18

/*
 * ArtDmx packet layout:
 *  0-7   ID "Art-Net\0"
 *  8-9   OpCode (LE) 0x5000
 *  10-11 ProtVer (BE) 14
 *  12    Sequence
 *  13    Physical
 *  14    SubUni  (low byte of 15-bit universe)
 *  15    Net     (high 7 bits)
 *  16-17 Length (BE, even, 2-512)
 *  18+   Data
 */
inline void artnetProcess(const uint8_t* buf, size_t len,
                          uint8_t* dmxBuffer, uint16_t localUniverse,
                          volatile bool* dirty) {
  if (len < ARTNET_HDR_LEN + 2) return;
  if (memcmp(buf, "Art-Net\0", 8) != 0) return;

  uint16_t opcode = buf[8] | (buf[9] << 8); // LE
  if (opcode != ARTNET_OP_DMX) return;

  // Reconstruct 15-bit universe
  uint16_t subuni = buf[14];
  uint16_t net    = buf[15] & 0x7F;
  uint16_t pktUniverse = (net << 8) | subuni;
  if (pktUniverse != localUniverse) return;

  uint16_t dmxLen = (buf[16] << 8) | buf[17];
  if (dmxLen < 2 || dmxLen > 512) return;
  if (len < (size_t)(ARTNET_HDR_LEN + dmxLen)) return;

  // slots 1..dmxLen
  memcpy(dmxBuffer + 1, buf + ARTNET_HDR_LEN, dmxLen);
  *dirty = true;
}
