#ifndef IDENTITY_H
#define IDENTITY_H

#include "common.h"

int  identity_init(NodeIdentity *id);
int  identity_init_dir(NodeIdentity *id, const char *dir);
int  identity_load(NodeIdentity *id);
int  identity_load_dir(NodeIdentity *id, const char *dir);
int  identity_save(const NodeIdentity *id);
int  identity_save_dir(const NodeIdentity *id, const char *dir);
void identity_print(const NodeIdentity *id);

#endif
