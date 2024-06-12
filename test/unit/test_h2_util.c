/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <apr.h>
#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_buckets.h>

#include "test_common.h"
#include "h2_util.h"

/*
 * Helpers
 */

/*
 * Test Fixture -- runs once per test
 */

static apr_pool_t *g_pool;

static void h2_util_setup(void)
{
    if (apr_pool_create(&g_pool, NULL) != APR_SUCCESS) {
        exit(1);
    }
}

static void h2_util_teardown(void)
{
    apr_pool_destroy(g_pool);
}

static void base64_roundtrip(const char *buf_in, size_t buf_len)
{
    const char *buf_out, *buf64;
    apr_size_t out_len;
    
    buf64 = h2_util_base64url_encode(buf_in, buf_len, g_pool);
    ck_assert(buf64);
    
    out_len = h2_util_base64url_decode(&buf_out, buf64, g_pool);
    
    ck_assert_int_eq(buf_len, out_len);
    ck_assert_mem_eq(buf_in, buf_out, buf_len);
}

/*
 * Tests
 */
START_TEST(base64_h2_util_roundtrip)
{
    base64_roundtrip("1", 1);
    base64_roundtrip("12", 2);
    base64_roundtrip("123", 3);
    base64_roundtrip("1234", 4);
    base64_roundtrip("12345", 5);
    base64_roundtrip("123456", 6);
    base64_roundtrip("1234567", 7);
    base64_roundtrip("12345678", 8);
    base64_roundtrip("123456789", 9);
}
END_TEST

static void largetrip(int step)
{
    char buffer[256];
    int i, start;

    for (start = 0; start < 256; ++start) {
        for (i = 0; i < 256; ++i) {
            buffer[(start+(i*step)) % 256] = (char)i;
        }
        base64_roundtrip(buffer, 256);
    }
}

START_TEST(base64_h2_util_largetrip)
{
    largetrip(1);
    largetrip(3);
    largetrip(5);
    largetrip(17);
    largetrip(31);
    largetrip(53);
    largetrip(101);
    largetrip(167);
    largetrip(223);
}
END_TEST

TCase *h2_util_test_case(void)
{
    TCase *testcase = tcase_create("h2_util");

    tcase_add_checked_fixture(testcase, h2_util_setup, h2_util_teardown);

    tcase_add_test(testcase, base64_h2_util_roundtrip);
    tcase_add_test(testcase, base64_h2_util_largetrip);

    return testcase;
}
