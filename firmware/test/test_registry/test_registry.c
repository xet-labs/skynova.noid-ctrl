/*
 * test_registry.c - tests for the device manifest, status rules and the `devices` table text.
 * Run on the PC:  pio test -e native -f test_registry
 *
 * The expected table lines are copied from console-commands.md ("The devices table"), so a change
 * to either side shows up here.
 */
#include <string.h>
#include <unity.h>
#include "skynoid_registry.h"

void setUp(void) {}
void tearDown(void) {}

static skr_probe_t P;
static skr_report_t R;

static int idx(const char *name)
{
    for (size_t i = 0; i < SKR_MANIFEST_LEN; i++) { if (strcmp(SKR_MANIFEST[i].name, name) == 0) { return (int)i; } }
    TEST_FAIL_MESSAGE("no such manifest entry");
    return -1;
}

static const skr_row_t *row(const char *name)
{
    for (size_t i = 0; i < R.n; i++) { if (strcmp(R.rows[i].name, name) == 0) { return &R.rows[i]; } }
    return NULL;
}

static skl_dev_status_t st(const char *name)
{
    const skr_row_t *r = row(name);
    TEST_ASSERT_NOT_NULL_MESSAGE(r, name);
    return r->status;
}

/* A healthy single robot: required I2C devices answer, identity checks pass, ADC plausible. */
static void healthy(void)
{
    memset(&P, 0, sizeof P);
    for (size_t i = 0; i < SKR_MANIFEST_LEN; i++) { P.in[i].check = 1; P.in[i].value = 3000; }
    P.present[0x29] = true; P.present[0x41] = true; P.present[0x3C] = true;
    P.in[idx("console_uart2")].flags = SKR_F_DISABLED;
}

static void test_manifest_is_sane(void)
{
    TEST_ASSERT_TRUE(SKR_MANIFEST_LEN <= SKR_MAX_ENTRIES);
    for (size_t i = 0; i < SKR_MANIFEST_LEN; i++) {
        for (size_t j = i + 1; j < SKR_MANIFEST_LEN; j++) {
            TEST_ASSERT_TRUE(strcmp(SKR_MANIFEST[i].name, SKR_MANIFEST[j].name) != 0);
        }
        if (SKR_MANIFEST[i].kind == SKR_I2C) {
            for (int role = 0; role < 3; role++) {
                if (SKR_MANIFEST[i].need[role] != 0) { TEST_ASSERT_TRUE(SKR_MANIFEST[i].addr[role] >= 0x08 && SKR_MANIFEST[i].addr[role] <= 0x77); }
            }
        } else {
            TEST_ASSERT_NOT_NULL(SKR_MANIFEST[i].where);
        }
    }
}

static void test_healthy_single_has_no_failure(void)
{
    healthy();
    skr_evaluate(SKR_ROLE_SINGLE, &P, &R);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_OK, st("vl53l0x"));
    TEST_ASSERT_EQUAL_INT(SKL_DEV_OK, st("ina219"));
    TEST_ASSERT_EQUAL_INT(SKL_DEV_OK, st("oled_left"));
    TEST_ASSERT_EQUAL_INT(SKL_DEV_ABSENT, st("pca9685"));
    TEST_ASSERT_EQUAL_INT(SKL_DEV_UNVERIFIED, st("buzzer"));
    TEST_ASSERT_EQUAL_INT(SKL_DEV_DISABLED, st("console_uart2"));
    TEST_ASSERT_EQUAL_INT(SKL_DEV_OK, st("ir_front_left"));
    TEST_ASSERT_EQUAL_UINT8(0, skr_fail_count(&R));
    TEST_ASSERT_EQUAL_INT(0, R.counts[SKL_DEV_UNEXPECTED]);
}

static void test_ina219_moved_to_0x40_when_nothing_else_owns_it(void)
{
    healthy();
    P.present[0x41] = false; P.present[0x40] = true;
    P.in[idx("pca9685")].check = -1;                      /* the ACK at 0x40 is not a PCA9685 */
    skr_evaluate(SKR_ROLE_SINGLE, &P, &R);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_MOVED, st("ina219"));
    TEST_ASSERT_EQUAL_UINT8(0x40, row("ina219")->found);
    TEST_ASSERT_NOT_NULL(strstr(row("ina219")->detail, "i2c remap ina219 0x40 save"));
    TEST_ASSERT_EQUAL_INT(SKL_DEV_ABSENT, st("pca9685"));
    TEST_ASSERT_EQUAL_INT(0, R.counts[SKL_DEV_UNEXPECTED]);   /* 0x40 is accounted for by the moved ina219 */
    TEST_ASSERT_EQUAL_UINT8(1, skr_fail_count(&R));           /* required and moved */
}

static void test_pca9685_at_0x40_is_not_a_moved_ina219(void)
{
    healthy();
    P.present[0x41] = false; P.present[0x40] = true;          /* a real PCA9685, identity passed */
    P.in[idx("ina219")].check = 0;
    skr_evaluate(SKR_ROLE_SINGLE, &P, &R);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_MISSING, st("ina219"));
    TEST_ASSERT_EQUAL_INT(SKL_DEV_OK, st("pca9685"));
    TEST_ASSERT_EQUAL_UINT8(1, skr_fail_count(&R));
}

static void test_missing_versus_absent(void)
{
    healthy();
    P.present[0x3C] = false;
    skr_evaluate(SKR_ROLE_SINGLE, &P, &R);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_MISSING, st("oled_left"));  /* required */
    TEST_ASSERT_EQUAL_INT(SKL_DEV_ABSENT, st("oled_right"));  /* optional */
    TEST_ASSERT_NOT_NULL(strstr(row("oled_left")->detail, "no ACK"));
    TEST_ASSERT_EQUAL_UINT8(1, skr_fail_count(&R));
}

static void test_identity_check_failure_is_fault(void)
{
    healthy();
    P.in[idx("vl53l0x")].check = 0;
    skr_evaluate(SKR_ROLE_SINGLE, &P, &R);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_FAULT, st("vl53l0x"));
    TEST_ASSERT_EQUAL_UINT8(0x29, row("vl53l0x")->found);
    TEST_ASSERT_EQUAL_UINT8(1, skr_fail_count(&R));
}

static void test_unexpected_addresses_get_rows_with_a_guess(void)
{
    healthy();
    P.present[0x57] = true; P.present[0x3D] = true; P.present[0x55] = true;
    skr_evaluate(SKR_ROLE_SINGLE, &P, &R);
    TEST_ASSERT_EQUAL_INT(3, R.counts[SKL_DEV_UNEXPECTED]);
    int seen57 = 0, seen3d = 0, seen55 = 0;
    for (size_t i = 0; i < R.n; i++) {
        if (R.rows[i].status != SKL_DEV_UNEXPECTED) { continue; }
        TEST_ASSERT_EQUAL_INT(-1, R.rows[i].index);
        if (R.rows[i].found == 0x57) { seen57 = 1; TEST_ASSERT_NOT_NULL(strstr(R.rows[i].detail, "at24c32")); }
        if (R.rows[i].found == 0x3D) { seen3d = 1; TEST_ASSERT_NOT_NULL(strstr(R.rows[i].detail, "alt address")); }
        if (R.rows[i].found == 0x55) { seen55 = 1; TEST_ASSERT_NOT_NULL(strstr(R.rows[i].detail, "no manifest entry")); }
    }
    TEST_ASSERT_TRUE(seen57 && seen3d && seen55);
    TEST_ASSERT_EQUAL_UINT8(0, skr_fail_count(&R));           /* unexpected is not a failure */
}

static void test_remapped_address_is_used(void)
{
    healthy();
    P.present[0x41] = false; P.present[0x44] = true;
    P.addr_override[idx("ina219")] = 0x44;
    skr_evaluate(SKR_ROLE_SINGLE, &P, &R);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_OK, st("ina219"));
    TEST_ASSERT_EQUAL_STRING("bus0 0x44", row("ina219")->where);
    TEST_ASSERT_EQUAL_INT(0, R.counts[SKL_DEV_UNEXPECTED]);
}

static void test_base_role_uses_its_own_addresses_and_list(void)
{
    healthy();
    memset(P.present, 0, sizeof P.present);
    P.present[0x29] = true; P.present[0x40] = true;
    skr_evaluate(SKR_ROLE_BASE, &P, &R);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_OK, st("ina219"));          /* base expects 0x40 */
    TEST_ASSERT_NULL(row("oled_left"));                       /* not part of the base role */
    TEST_ASSERT_NULL(row("touch_head"));
    TEST_ASSERT_NOT_NULL(row("vl53l0x"));
    TEST_ASSERT_NULL(row("pca9685"));                         /* devices.yaml: pca9685 is "no" for the base role */
}

static void test_adc_rules(void)
{
    healthy();
    P.in[idx("ir_front_left")].value = 3120;
    P.in[idx("ir_front_right")].flags = SKR_F_SATURATED;
    P.in[idx("ir_rear_left")].flags = SKR_F_STUCK;
    P.in[idx("ir_rear_right")].flags = SKR_F_READ_ERROR;
    P.in[idx("motor_current_is")].flags = SKR_F_NOT_FITTED;
    skr_evaluate(SKR_ROLE_SINGLE, &P, &R);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_OK, st("ir_front_left"));
    TEST_ASSERT_EQUAL_STRING("raw 3120, plausible", row("ir_front_left")->detail);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_FAULT, st("ir_front_right"));
    TEST_ASSERT_EQUAL_INT(SKL_DEV_FAULT, st("ir_rear_left"));
    TEST_ASSERT_EQUAL_INT(SKL_DEV_FAULT, st("ir_rear_right"));
    TEST_ASSERT_EQUAL_INT(SKL_DEV_ABSENT, st("motor_current_is"));   /* optional and not wired */
    TEST_ASSERT_EQUAL_UINT8(3, skr_fail_count(&R));
}

static void test_gpio_outputs_and_system_rules(void)
{
    healthy();
    P.in[idx("touch_head")].value = 0;
    P.in[idx("touch_hand_l")].flags = SKR_F_STUCK;
    P.in[idx("servos_8x")].flags = SKR_F_NOT_FITTED;
    P.in[idx("buzzer")].flags = SKR_F_DISABLED;
    P.in[idx("nvs_flash")].flags = SKR_F_FAULT;
    P.in[idx("nvs_flash")].note = "nvs init failed";
    skr_evaluate(SKR_ROLE_SINGLE, &P, &R);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_OK, st("touch_head"));
    TEST_ASSERT_EQUAL_STRING("low", row("touch_head")->detail);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_FAULT, st("touch_hand_l"));
    TEST_ASSERT_EQUAL_INT(SKL_DEV_ABSENT, st("servos_8x"));
    TEST_ASSERT_EQUAL_INT(SKL_DEV_DISABLED, st("buzzer"));
    TEST_ASSERT_EQUAL_INT(SKL_DEV_FAULT, st("nvs_flash"));
    TEST_ASSERT_EQUAL_STRING("nvs init failed", row("nvs_flash")->detail);
    TEST_ASSERT_EQUAL_UINT8(2, skr_fail_count(&R));
}

static void test_required_not_fitted_is_missing(void)
{
    healthy();
    P.in[idx("ir_front_left")].flags = SKR_F_NOT_FITTED;
    skr_evaluate(SKR_ROLE_SINGLE, &P, &R);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_MISSING, st("ir_front_left"));
}

static void test_two_oleds_at_one_address_say_so(void)
{
    healthy();
    skr_evaluate(SKR_ROLE_SINGLE, &P, &R);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_OK, st("oled_left"));
    TEST_ASSERT_EQUAL_INT(SKL_DEV_OK, st("oled_right"));
    TEST_ASSERT_NOT_NULL(strstr(row("oled_right")->detail, "same address as oled_left"));
}

static void test_optional_moved_is_not_a_failure(void)
{
    healthy();
    P.present[0x42] = true;                                   /* a pca9685 strapped to 0x42 */
    P.in[idx("pca9685")].check = 1;
    skr_evaluate(SKR_ROLE_SINGLE, &P, &R);
    TEST_ASSERT_EQUAL_INT(SKL_DEV_MOVED, st("pca9685"));
    TEST_ASSERT_EQUAL_UINT8(0, skr_fail_count(&R));
}

static void test_header_and_row_match_the_spec_table(void)
{
    char b[160];
    skr_format_header(b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("NAME            KIND  WHERE           NEED  FOUND  STATUS      DETAIL", b);

    skr_row_t r;
    memset(&r, 0, sizeof r);
    r.name = "vl53l0x"; r.kind_text = "i2c"; r.need = 2; r.found = 0x29; r.status = SKL_DEV_OK;
    strcpy(r.where, "bus0 0x29"); strcpy(r.detail, "model 0xEE, range 342 mm");
    skr_format_row(&r, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("vl53l0x         i2c   bus0 0x29       req   0x29   ok          model 0xEE, range 342 mm", b);

    memset(&r, 0, sizeof r);
    r.name = "oled_left"; r.kind_text = "i2c"; r.need = 2; r.found = 0; r.status = SKL_DEV_MISSING;
    strcpy(r.where, "bus0 0x3C"); strcpy(r.detail, "no ACK: check wiring, power, address pad");
    skr_format_row(&r, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("oled_left       i2c   bus0 0x3C       req   -      MISSING     no ACK: check wiring, power, address pad", b);

    memset(&r, 0, sizeof r);
    r.name = "(unknown)"; r.kind_text = "i2c"; r.need = 0; r.found = 0x3D; r.status = SKL_DEV_UNEXPECTED;
    strcpy(r.where, "bus0 0x3D");
    skr_format_row(&r, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("(unknown)       i2c   bus0 0x3D       -     0x3D   unexpected", b);   /* empty detail: no trailing spaces */
}

static void test_summary_matches_the_spec_line(void)
{
    skr_report_t rep;
    memset(&rep, 0, sizeof rep);
    rep.counts[SKL_DEV_OK] = 5; rep.counts[SKL_DEV_MOVED] = 1; rep.counts[SKL_DEV_MISSING] = 1;
    rep.counts[SKL_DEV_FAULT] = 1; rep.counts[SKL_DEV_ABSENT] = 1; rep.counts[SKL_DEV_UNEXPECTED] = 1;
    rep.counts[SKL_DEV_UNVERIFIED] = 2;
    char b[120];
    skr_format_summary(&rep, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("summary: 5 ok, 1 moved, 1 MISSING, 1 FAULT, 1 absent, 1 unexpected, 2 unverified", b);
    memset(&rep, 0, sizeof rep);
    skr_format_summary(&rep, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("summary: no devices", b);
}

static void test_small_buffers_never_overflow(void)
{
    healthy();
    skr_evaluate(SKR_ROLE_SINGLE, &P, &R);
    for (size_t cap = 1; cap < 30; cap++) {
        char guard[64];
        memset(guard, 0x5A, sizeof guard);
        const size_t n = skr_format_row(&R.rows[0], guard, cap);
        TEST_ASSERT_TRUE(n < cap);
        TEST_ASSERT_EQUAL_UINT8(0, guard[n]);
        TEST_ASSERT_EQUAL_UINT8(0x5A, (uint8_t)guard[cap]);     /* byte after the buffer untouched */
        const size_t m = skr_format_summary(&R, guard, cap);
        TEST_ASSERT_TRUE(m < cap);
        TEST_ASSERT_EQUAL_UINT8(0x5A, (uint8_t)guard[cap]);
    }
    TEST_ASSERT_EQUAL_UINT(0, skr_format_row(&R.rows[0], NULL, 0));
}

static void test_report_never_overflows_with_every_address_answering(void)
{
    healthy();
    for (int a = 0; a < 128; a++) { P.present[a] = true; }
    skr_evaluate(SKR_ROLE_SINGLE, &P, &R);
    TEST_ASSERT_TRUE(R.n <= SKR_MAX_ROWS);
    TEST_ASSERT_TRUE(R.counts[SKL_DEV_UNEXPECTED] <= SKR_MAX_UNEXPECTED);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_manifest_is_sane);
    RUN_TEST(test_healthy_single_has_no_failure);
    RUN_TEST(test_ina219_moved_to_0x40_when_nothing_else_owns_it);
    RUN_TEST(test_pca9685_at_0x40_is_not_a_moved_ina219);
    RUN_TEST(test_missing_versus_absent);
    RUN_TEST(test_identity_check_failure_is_fault);
    RUN_TEST(test_unexpected_addresses_get_rows_with_a_guess);
    RUN_TEST(test_remapped_address_is_used);
    RUN_TEST(test_base_role_uses_its_own_addresses_and_list);
    RUN_TEST(test_adc_rules);
    RUN_TEST(test_gpio_outputs_and_system_rules);
    RUN_TEST(test_required_not_fitted_is_missing);
    RUN_TEST(test_two_oleds_at_one_address_say_so);
    RUN_TEST(test_optional_moved_is_not_a_failure);
    RUN_TEST(test_header_and_row_match_the_spec_table);
    RUN_TEST(test_summary_matches_the_spec_line);
    RUN_TEST(test_small_buffers_never_overflow);
    RUN_TEST(test_report_never_overflows_with_every_address_answering);
    return UNITY_END();
}
