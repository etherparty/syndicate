/*
   Copyright 2013 The Trustees of Princeton University
   All Rights Reserved
*/


#ifndef _READDIR_H_
#define _READDIR_H_

#include "fs_entry.h"

struct fs_dir_entry** fs_entry_readdir( struct fs_core* core, struct fs_dir_handle* dirh, int* err );
struct fs_dir_entry** fs_entry_readdir_lowlevel( struct fs_core* core, char const* fs_path, struct fs_entry* dent );

#endif