#ifndef IDENTITY_H
#define IDENTITY_H

#include "common.h"

int identity_init(NodeIdentity *id);
int identity_load(NodeIdentity *id);
int identity_save(const NodeIdentity *id);
void identity_print(const NodeIdentity *id);

#endif
