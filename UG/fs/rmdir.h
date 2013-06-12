/*
   Copyright 2013 The Trustees of Princeton University
   All Rights Reserved
*/


#ifndef _RMDIR_H_
#define _RMDIR_H_

#include "fs_entry.h"
#include "consistency.h"

int fs_entry_rmdir( struct fs_core* core, char const* path, uid_t user, gid_t volume );

#endif