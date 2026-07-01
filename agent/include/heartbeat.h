#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#include "common.h"

void heartbeat_loop(const NodeIdentity *id, int interval_seconds);

#endif
