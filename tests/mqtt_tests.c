#include "../src/mqtt.h"
#include "test_helpers.h"
#include "tests.h"
#include <stdio.h>
#include <stdlib.h>

static int test_variable_length_read(void)
{

    TEST_HEADER;

    // Test case 1: Valid single byte
    uint8_t test1[] = {0x7F}; // 127
    Buffer buf1     = {.data = test1, .size = 1, .read_pos = 0};
    usize len1;
    isize bytes1 = mqtt_variable_length_read(&buf1, &len1);
    ASSERT_EQ(len1, 127);
    ASSERT_EQ(bytes1, 1);

    // Test case 2: Valid multi-byte
    uint8_t test2[] = {0x80, 0x01}; // 128
    Buffer buf2     = {.data = test2, .size = 2, .read_pos = 0};
    usize len2;
    isize bytes2 = mqtt_variable_length_read(&buf2, &len2);
    ASSERT_EQ(len2, 128);
    ASSERT_EQ(bytes2, 2);

    // Test case 3: Maximum valid value
    uint8_t test3[] = {0xFF, 0xFF, 0xFF, 0x7F}; // 268435455
    Buffer buf3     = {.data = test3, .size = 4, .read_pos = 0};
    usize len3;
    isize bytes3 = mqtt_variable_length_read(&buf3, &len3);
    ASSERT_EQ(len3, 268435455);
    ASSERT_EQ(bytes3, 4);

    // Test case 4: Malformed - 5 bytes
    uint8_t test4[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x7F};
    Buffer buf4     = {.data = test4, .size = 5, .read_pos = 0};
    usize len4;
    isize bytes4 = mqtt_variable_length_read(&buf4, &len4);
    ASSERT_EQ(len4, 0);
    ASSERT_EQ(bytes4, 0);

    TEST_FOOTER;
    return 0;
}

static int test_variable_length_write(void)
{

    TEST_HEADER;

    // Test case 1: Valid single byte
    uint8_t test1[] = {0x7F}; // 127
    Buffer buf1     = {.data = (uint8[5]){0}, .write_pos = 0, .size = 5};
    isize bytes1    = mqtt_variable_length_write(&buf1, 127);
    ASSERT_EQ(1, bytes1);
    ASSERT_TRUE(memcmp(test1, buf1.data, 1) == 0, " FAIL: encoded buffer doesn't match expected\n");

    // Test case 2: Valid multi-byte
    uint8_t test2[] = {0x80, 0x01}; // 128
    Buffer buf2     = {.data = (uint8[5]){0}, .write_pos = 0, .size = 5};
    isize bytes2    = mqtt_variable_length_write(&buf2, 128);
    ASSERT_EQ(2, bytes2);
    ASSERT_TRUE(memcmp(test2, buf2.data, 2) == 0, " FAIL: encoded buffer doesn't match expected\n");

    // Test case 3: Maximum valid value
    uint8_t test3[] = {0xFF, 0xFF, 0xFF, 0x7F}; // 268435455
    Buffer buf3     = {.data = (uint8[5]){0}, .write_pos = 0, .size = 5};
    isize bytes3    = mqtt_variable_length_write(&buf3, 268435455);
    ASSERT_EQ(4, bytes3);
    ASSERT_TRUE(memcmp(test3, buf3.data, 4) == 0, " FAIL: encoded buffer doesn't match expected\n");

    TEST_FOOTER;
    return 0;
}

int mqtt_tests(void)
{
    printf("* %s\n\n", __FUNCTION__);

    int cases   = 2;
    int success = cases;

    success += test_variable_length_read();
    success += test_variable_length_write();

    printf("\n Test suite summary: %d passed, %d failed\n", success, cases - success);

    return success < cases ? -1 : 0;
}
