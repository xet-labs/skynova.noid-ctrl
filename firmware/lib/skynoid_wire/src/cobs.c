/* cobs.c - COBS (Consistent Overhead Byte Stuffing).
 *
 * Idea: take the data, remove every 0x00 byte, and keep the frame edges as
 * the only 0x00 on the wire. Each block of non-zero bytes is stored behind a
 * "code" byte that says how long the block is (code = block length + 1).
 * A receiver that starts in the middle of a stream just waits for the next
 * 0x00 and is back in step. Overhead: at most 1 byte per 254 bytes.
 *
 * Example:  11 22 00 33   ->   03 11 22 02 33
 */
#include "skynoid_wire.h"

size_t skw_cobs_encode(const uint8_t *in, size_t len, uint8_t *out)
{
    size_t read = 0;
    size_t write = 1;          /* out[0] is reserved for the first code byte */
    size_t code_pos = 0;       /* where the current block's code byte lives */
    uint8_t code = 1;          /* current block length + 1 */

    while (read < len) {
        if (in[read] == 0) {                 /* a zero ends the block */
            out[code_pos] = code;
            code_pos = write++;
            code = 1;
        } else {
            out[write++] = in[read];
            code++;
            if (code == 0xFF) {              /* block is full (254 bytes) */
                out[code_pos] = code;
                code_pos = write++;
                code = 1;
            }
        }
        read++;
    }
    out[code_pos] = code;                    /* close the last block */
    return write;
}

int skw_cobs_decode(const uint8_t *in, size_t len, uint8_t *out, size_t cap)
{
    size_t read = 0;
    size_t write = 0;

    while (read < len) {
        uint8_t code = in[read++];
        if (code == 0) {
            return -1;                       /* a zero can never appear inside */
        }
        for (uint8_t i = 1; i < code; i++) { /* copy the block's data bytes */
            if (read >= len || write >= cap) {
                return -1;                   /* block runs past the end */
            }
            out[write++] = in[read++];
        }
        /* Every block except a full 0xFF one stood for a zero byte, except
         * the very last block which has no zero after it. */
        if (code != 0xFF && read < len) {
            if (write >= cap) {
                return -1;
            }
            out[write++] = 0;
        }
    }
    return (int)write;
}
