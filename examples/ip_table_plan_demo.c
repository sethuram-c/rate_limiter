#include "ip_table_plan.h"

#include <stdint.h>
#include <stdio.h>

int main(void)
{
    ip_model_slot_t slots[8] = {{0}};
    const uint32_t ips[] = {UINT32_C(0xC0000201), UINT32_C(0xC0000202),
                            UINT32_C(0), UINT32_MAX, UINT32_C(0xC0000201)};
    size_t i;
    for (i = 0U; i < sizeof ips / sizeof ips[0]; ++i) {
        ip_insert_result_t r;
        if (!ip_table_model_insert(slots, 8U, ips[i], &r)) {
            fputs("invalid table model\n", stderr);
            return 1;
        }
        printf("ip=0x%08x action=%s slot=%zu probes=%zu\n",
               (unsigned int)ips[i],
               r.kind == IP_INSERT_NEW ? "insert" :
               r.kind == IP_INSERT_EXISTING ? "found" : "full",
               r.index, r.probes);
    }
    puts("phase 9 demo: sequential hash-table design only");
    return 0;
}
