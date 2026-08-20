/*
 * sacn.h — sACN (E1.31) packet parser
 * v1.0.0
 *
 * Ref: ANSI E1.31-2018
 * Listens on multicast 239.255.X.Y where X.Y = universe (BE)
 */
#pragma once
#include <Arduino.h>
#include <IPAddress.h>

#define SACN_PORT         5568

// Static priority table per universe (simple single-source implementation)
// For multi-source merging, expand this to track per-CID sources
static uint8_t sacnLastPriority = 0;

// E1.31 framing offsets (after 16-byte Root PDU)
// Root PDU:     0-15
// Framing PDU: 16-76  (includes priority at offset 45, universe at 113)
// DMP PDU:     77+    (start addr at 125, count at 127, data at 129)

#define SACN_PREAMBLE_SIZE  0x0010
#define SACN_VECTOR_ROOT    0x00000004  // VECTOR_ROOT_E131_DATA
#define SACN_VECTOR_FRAME   0x00000002  // VECTOR_E131_DATA_PACKET
#define SACN_VECTOR_DMP     0x02        // VECTOR_DMP_SET_PROPERTY

#define SACN_MIN_LEN        126

inline IPAddress sacnMulticast(uint16_t universe) {
  // 239.255.(universe >> 8).(universe & 0xFF)
  return IPAddress(239, 255, (universe >> 8) & 0xFF, universe & 0xFF);
}

inline void sacnProcess(const uint8_t* buf, size_t len,
                        uint8_t* dmxBuffer, uint16_t localUniverse,
                        volatile bool* dirty) {
  if (len < SACN_MIN_LEN) return;

  // Preamble size check
  uint16_t preamble = (buf[0] << 8) | buf[1];
  if (preamble != SACN_PREAMBLE_SIZE) return;

  // Root vector (offset 18, 4 bytes BE)
  uint32_t rootVec = ((uint32_t)buf[18] << 24) | ((uint32_t)buf[19] << 16)
                   | ((uint32_t)buf[20] << 8)  |  buf[21];
  if (rootVec != SACN_VECTOR_ROOT) return;

  // Framing vector (offset 40, 4 bytes BE)
  uint32_t frameVec = ((uint32_t)buf[40] << 24) | ((uint32_t)buf[41] << 16)
                    | ((uint32_t)buf[42] << 8)  |  buf[43];
  if (frameVec != SACN_VECTOR_FRAME) return;

  // Priority (offset 45)
  uint8_t priority = buf[45];
  // Simple priority gate — last-wins within same priority
  if (priority < sacnLastPriority) return;
  sacnLastPriority = priority;

  // Universe (offset 113, 2 bytes BE)
  uint16_t universe = (buf[113] << 8) | buf[114];
  if (universe != localUniverse) return;

  // DMP vector (offset 117)
  if (buf[117] != SACN_VECTOR_DMP) return;

  // Address type (offset 118): should be 0xA1
  if (buf[118] != 0xA1) return;

  // Property count (offset 123, 2 bytes BE) — includes start code byte
  uint16_t propCount = (buf[123] << 8) | buf[124];
  if (propCount < 2 || propCount > 513) return;
  if (len < (size_t)(125 + propCount)) return;

  // buf[125] = DMX start code (should be 0x00)
  if (buf[125] != 0x00) return;

  // Copy channels (propCount - 1 = number of DMX slots)
  uint16_t slotCount = propCount - 1;
  memcpy(dmxBuffer + 1, buf + 126, slotCount);
  *dirty = true;
}
