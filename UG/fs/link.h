/*
   Copyright 2013 The Trustees of Princeton University
   All Rights Reserved
*/


#ifndef _LINK_H_
#define _LINK_H_

#include "fs_entry.h"

int fs_entry_attach_lowlevel( struct fs_core* core, struct fs_entry* parent, struct fs_entry* fent );
int fs_entry_attach( struct fs_core* core, struct fs_entry* fent, char const* path, uid_t user, gid_t vol );

#endif