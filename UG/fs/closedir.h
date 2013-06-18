/*
   Copyright 2013 The Trustees of Princeton University
   All Rights Reserved
*/

#ifndef _CLOSEDIR_H_
#define _CLOSEDIR_H_

#include "fs_entry.h"

int fs_entry_closedir( struct fs_core* core, struct fs_dir_handle* dirh );
int fs_dir_handle_close( struct fs_dir_handle* dh );

#endif