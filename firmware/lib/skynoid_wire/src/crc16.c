/* crc16.c - CRC-16/CCITT-FALSE for the Skynoid link.
 *
 * Why this CRC: it is small, standard, and detects every 1-3 bit error and
 * every burst up to 16 bits in frames of our size. Its main job here is to
 * reject half-sent frames and boot text, not to fight line noise.
 * Do NOT use the ESP32 ROM CRC: it reflects bits and gives different values.
 */
#include "skynoid_wire.h"

uint16_t skw_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;                    /* start value */
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int bit = 0; bit < 8; bit++) {    /* one bit at a time: small and clear */
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}
