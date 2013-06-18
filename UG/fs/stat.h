/*
   Copyright 2013 The Trustees of Princeton University
   All Rights Reserved
*/


#ifndef _STAT_H_
#define _STAT_H_

#include "fs_entry.h"

// read metadata
int fs_entry_stat( struct fs_core* core, char const* path, struct stat* sb, uid_t user, gid_t volume );
int fs_entry_block_stat( struct fs_core* core, char const* path, uint64_t block_id, struct stat* sb );         // system use only
bool fs_entry_is_local( struct fs_core* core, char const* path, uid_t user, gid_t volume, int* err );
bool fs_entry_is_block_local( struct fs_core* core, char const* path, uid_t user, gid_t volume, uint64_t block_id );
char* fs_entry_get_url( struct fs_core* core, char const* path, uid_t user, gid_t volume, int* err );
char* fs_entry_get_host_url( struct fs_core* core, char const* path, char const* proto, uid_t user, gid_t volume, int* err );
int fs_entry_fstat( struct fs_core* core, struct fs_file_handle* fh, struct stat* sb );
int fs_entry_fstat_dir( struct fs_core* core, struct fs_dir_handle* dh, struct stat* sb );
int fs_entry_statfs( struct fs_core* core, char const* path, struct statvfs *statv, uid_t user, gid_t volume );
int fs_entry_access( struct fs_core* core, char const* path, int mode, uid_t user, gid_t volume );
int fs_entry_get_creation_time( struct fs_core* core, char const* fs_path, struct timespec* t );
int fs_entry_get_mod_time( struct fs_core* core, char const* fs_path, struct timespec* t );
int fs_entry_set_mod_time( struct fs_core* core, char const* fs_path, struct timespec* t );
int fs_entry_manifest_lastmod( struct fs_core* core, char const* fs_path, struct timespec* ts );
//int64_t fs_entry_read_version( struct fs_core* core, char const* fs_path );
int64_t fs_entry_get_version( struct fs_core* core, char const* fs_path );
int64_t fs_entry_get_block_version( struct fs_core* core, char* fs_path, uint64_t block_id );
char* fs_entry_get_manifest_str( struct fs_core* core, char* fs_path );
ssize_t fs_entry_serialize_manifest( struct fs_core* core, char* fs_path, char** manifest_bits );
ssize_t fs_entry_serialize_manifest( struct fs_core* core, struct fs_entry* fent, char** manifest_bits );

// write metadata
int fs_entry_chown( struct fs_core* core, char const* path, uid_t user, gid_t volume, uid_t new_user );
int fs_entry_chmod( struct fs_core* core, char const* path, uid_t user, gid_t volume, mode_t mode );
int fs_entry_utime( struct fs_core* core, char const* path, struct utimbuf* tb, uid_t user, gid_t volume );

#endif