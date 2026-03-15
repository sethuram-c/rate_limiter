#include "../src/ipv4_parse_internal.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_valid_equivalence(void)
{
    static const struct {
        const char *text;
        uint32_t expected;
    } cases[] = {
        {"0.0.0.0", UINT32_C(0x00000000)},
        {"127.0.0.1", UINT32_C(0x7f000001)},
        {"192.0.2.1", UINT32_C(0xc0000201)},
        {"255.255.255.255", UINT32_C(0xffffffff)}
    };
    size_t i;
    for (i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        uint32_t span = 5U;
        uint32_t cstr = 6U;
        size_t length = strlen(cases[i].text);
        assert(rl_ipv4_parse_span(cases[i].text, length, &span));
        assert(rl_ipv4_parse_cstr(cases[i].text, &cstr));
        assert(span == cstr && span == cases[i].expected);
    }
}

static void test_bad_input_does_not_change_result(void)
{
    const char raw[] = {'1','9','2','.','0','.','2','.','1'};
    const char embedded_nul[] = {'1','9','2','\0','.','0','.','2','.','1'};
    const char garbage[] = {'1','9','2','.','0','.','2','.','1','x'};
    const char too_long[] = "111111111111111111111.1.1.1";
    uint32_t out = UINT32_C(0x12345678);
    assert(rl_ipv4_parse_span(raw, sizeof raw, &out));
    assert(out == UINT32_C(0xc0000201));
    out = UINT32_C(0x12345678);
    assert(!rl_ipv4_parse_span(NULL, 1U, &out));
    assert(!rl_ipv4_parse_span(raw, 0U, &out));
    assert(!rl_ipv4_parse_span(raw, SIZE_MAX, &out));
    assert(!rl_ipv4_parse_span(raw, sizeof raw, NULL));
    assert(!rl_ipv4_parse_span(embedded_nul, sizeof embedded_nul, &out));
    assert(!rl_ipv4_parse_span(garbage, sizeof garbage, &out));
    assert(!rl_ipv4_parse_span("192.0.2.999", 11U, &out));
    assert(!rl_ipv4_parse_cstr(too_long, &out));
    assert(!rl_ipv4_parse_cstr(NULL, &out));
    assert(!rl_ipv4_parse_cstr("", &out));
    assert(!rl_ipv4_parse_cstr("192.0.2.1 ", &out));
    assert(!rl_ipv4_parse_cstr("1.2.3.4", NULL));
    assert(out == UINT32_C(0x12345678));
}

int main(void)
{
    test_valid_equivalence();
    test_bad_input_does_not_change_result();
    puts("internal IPv4 parsing tests passed: bounded/C-string parity and rejection");
    return 0;
}
