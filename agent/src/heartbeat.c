#include "heartbeat.h"
#include <time.h>

void heartbeat_loop(const NodeIdentity *id, int interval_seconds) {
    printf("\n🔋 Lithium Node Agent v%s\n", LITHIUM_VERSION);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  Node     : %s\n", id->node_id);
    printf("  Status   : Active\n");
    printf("  Heartbeat: every %ds\n", interval_seconds);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");

    int tick = 0;
    while (1) {
        tick++;
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char timebuf[32];
        strftime(timebuf, sizeof(timebuf), "%H:%M:%S", t);

        printf("[%s] ♥ heartbeat #%d — %s online\n", timebuf, tick, id->node_id);
        fflush(stdout);
        sleep(interval_seconds);
    }
}
