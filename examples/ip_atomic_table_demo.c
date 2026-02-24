#include "ip_atomic_table.h"

#include <inttypes.h>
#include <stdio.h>

int main(void)
{
    const uint32_t ips[] = {
        UINT32_C(0xc0000201), UINT32_C(0xc0000201),
        UINT32_C(0), UINT32_MAX, UINT32_C(0xc6336407)
    };
    ip_atomic_table_t table = {0};
    size_t i;

    if (!ip_atomic_table_init(&table, 8U)) {
        fputs("atomic table initialization failed\n", stderr);
        return 1;
    }
    for (i = 0U; i < sizeof(ips) / sizeof(ips[0]); ++i) {
        ip_atomic_insert_result_t result;
        if (!ip_atomic_table_get_or_insert(&table, ips[i], &result)) {
            ip_atomic_table_destroy(&table);
            return 1;
        }
        printf("ip=0x%08" PRIx32 " slot=%zu outcome=%s probes=%zu\n",
               ips[i], result.index,
               result.kind == IP_ATOMIC_INSERTED ? "NEW" :
               result.kind == IP_ATOMIC_EXISTS ? "EXISTS" : "FULL",
               result.probes);
    }
    printf("unique IPs=%zu\n", ip_atomic_table_count_quiescent(&table));
    ip_atomic_table_destroy(&table);
    return 0;
}
