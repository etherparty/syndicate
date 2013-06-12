/*
   Copyright 2013 The Trustees of Princeton University
   All Rights Reserved
*/

#ifndef _XATTR_H_
#define _XATTR_H_

#include "fs_entry.h"

// extended attributes
ssize_t fs_entry_getxattr( struct fs_core* core, char const* path, char const *name, char *value, size_t size, uid_t user, gid_t volume );
int fs_entry_setxattr( struct fs_core* core, char const* path, char const *name, char const *value, size_t size, int flags, uid_t user, gid_t volume );
ssize_t fs_entry_listxattr( struct fs_core* core, char const* path, char *list, size_t size, uid_t user, gid_t volume );
int fs_entry_removexattr( struct fs_core* core, char const* path, char const *name, uid_t user, gid_t volume );

#endif
