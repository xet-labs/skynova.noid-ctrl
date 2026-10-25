/* led.c - status LED pattern timing. Pure logic, see skynoid_core.h. Test: test_core.c, test_led_*. */
#include "skynoid_core.h"

bool skl_led_on(int mode, uint32_t t_ms)
{
    switch (mode) {
    case 1: return (t_ms % 1000u) < 500u;                  /* slow: 500 ms on, 500 ms off */
    case 2: return true;                                   /* solid */
    case 3: return (t_ms % 200u) < 100u;                   /* fast: 100 ms on, 100 ms off */
    case 4: {                                              /* double: on 100, off 100, on 100, off 1700 */
        const uint32_t p = t_ms % 2000u;
        return p < 100u || (p >= 200u && p < 300u);
    }
    default: return false;                                 /* off and unknown modes */
    }
}
