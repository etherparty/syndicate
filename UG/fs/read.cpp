/*
   Copyright 2013 The Trustees of Princeton University

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "cache.h"
#include "read.h"
#include "manifest.h"
#include "network.h"
#include "url.h"
#include "fs_entry.h"

// verify the integrity of a block, given the fent (and its manifest).
// fent must be at least read-locked
int fs_entry_verify_block( struct fs_core* core, struct fs_entry* fent, uint64_t block_id, char* block_bits, size_t block_len ) {
   unsigned char* block_hash = BLOCK_HASH_DATA( block_bits, block_len );
   
   int rc = fent->manifest->hash_cmp( block_id, block_hash );
   
   free( block_hash );
   
   if( rc != 0 ) {
      errorf("Hash mismatch (rc = %d, len = %zu)\n", rc, block_len );
      return -EPROTO;
   }
   else {
      return 0;
   }
}


// read a block known to be remote
// fent must be read-locked
ssize_t fs_entry_read_remote_block( struct fs_core* core, char const* fs_path, struct fs_entry* fent, uint64_t block_id, uint64_t block_version, char* block_bits, size_t block_len ) {

   if( block_id * block_len >= (unsigned)fent->size ) {
      return 0;      // EOF
   }

   if( fs_path == NULL ) {
      return -EINVAL;
   }

   // this is a remotely-hosted block--get its bits
   char* block_url = NULL;
   
   int gateway_type = ms_client_get_gateway_type( core->ms, fent->coordinator );
   
   if( gateway_type < 0 ) {
      // unknown gateway---maybe try reloading the certs?
      errorf("Unknown gateway %" PRIu64 "\n", fent->coordinator );
      ms_client_sched_volume_reload( core->ms );
      return -EAGAIN;
   }
      
   char* block_buf = NULL;
   ssize_t nr = 0;
   
   // this file may be locally coordinated, so don't download from ourselves.
   if( !FS_ENTRY_LOCAL( core, fent ) ) {
      
      if( gateway_type == SYNDICATE_UG )
         block_url = fs_entry_remote_block_url( core, fent->coordinator, fs_path, fent->version, block_id, block_version );
      else if( gateway_type == SYNDICATE_RG )
         block_url = fs_entry_RG_block_url( core, fent->coordinator, fent->volume, fent->file_id, fent->version, block_id, block_version );
      else if( gateway_type == SYNDICATE_AG )
         block_url = fs_entry_AG_block_url( core, fent->coordinator, fs_path, fent->version, block_id, block_version );
      
      if( block_url == NULL ) {
         errorf("Failed to compute block URL for Gateway %" PRIu64 "\n", fent->coordinator);
         return -ENODATA;
      }

      nr = fs_entry_download_block( core, block_url, &block_buf, block_len );
      
      if( nr <= 0 ) {
         errorf("fs_entry_download_block(%s) rc = %zd\n", block_url, nr );
      }
   }
   
   if( FS_ENTRY_LOCAL( core, fent ) || (nr <= 0 && gateway_type != SYNDICATE_AG) ) {
      // try from an RG
      uint64_t rg_id = 0;
      
      nr = fs_entry_download_block_replica( core, fent->volume, fent->file_id, fent->version, block_id, block_version, &block_buf, block_len, &rg_id );
      
      if( nr < 0 ) {
         // error
         errorf("Failed to read /%" PRIu64 "/%" PRIX64 ".%" PRId64 "/%" PRIu64 ".%" PRId64 " from RGs, rc = %zd\n", fent->volume, fent->file_id, fent->version, block_id, block_version, nr );
      }
      else {
         // success!
         dbprintf("Fetched %zd bytes of /%" PRIu64 "/%" PRIX64 ".%" PRId64 "/%" PRIu64 ".%" PRId64 " from RG %" PRIu64 "\n", nr, fent->volume, fent->file_id, fent->version, block_id, block_version, rg_id );
      }
   }
   if( nr < 0 ) {
      nr = -ENODATA;
   }
   else {
      // verify the block
      // TODO: do this for AGs as well, once AGs hash blocks
      if( gateway_type != SYNDICATE_AG ) {
         int rc = fs_entry_verify_block( core, fent, block_id, block_buf, nr );
         if( rc != 0 ) {
            nr = rc;
         }
      }
      if( nr >= 0 ) {
         memcpy( block_bits, block_buf, nr );
         dbprintf("read %ld bytes remotely\n", (long)nr);
      }
      
      free( block_buf );
   }

   if( block_url )
      free( block_url );
   return nr;
}


// Given an offset, get the corresponding block's data
// fent must be at least read-locked first
ssize_t fs_entry_read_block( struct fs_core* core, char const* fs_path, struct fs_entry* fent, uint64_t block_id, char* block_bits, size_t block_len ) {

   if( block_id * block_len >= (unsigned)fent->size ) {
      return 0;      // EOF
   }
   
   bool hit_cache = false;
   ssize_t rc = 0;
   
   uint64_t block_version = fent->manifest->get_block_version( block_id );
   
   // local?
   int block_fd = fs_entry_cache_open_block( core, core->cache, fent->file_id, fent->version, block_id, block_version, O_RDONLY );
   if( block_fd < 0 ) {
      if( block_fd != -ENOENT ) {
         errorf("WARN: fs_entry_cache_open_block( %" PRIX64 ".%" PRId64 "[%" PRIu64 ".%" PRId64 "] (%s) ) rc = %d\n", fent->file_id, fent->version, block_id, block_version, fs_path, block_fd );
      }
   }
   else {
      ssize_t read_len = fs_entry_cache_read_block( core, core->cache, fent->file_id, fent->version, block_id, block_version, block_fd, block_bits, block_len );
      if( read_len < 0 ) {
         errorf("fs_entry_cache_read_block( %" PRIX64 ".%" PRId64 "[%" PRIu64 ".%" PRId64 "] (%s) ) rc = %d\n", fent->file_id, fent->version, block_id, block_version, fs_path, (int)read_len );
      }
      else {
         // done!
         hit_cache = true;
         rc = read_len;
         
         // promote!
         fs_entry_cache_promote_block( core, core->cache, fent->file_id, fent->version, block_id, block_version );
      }
      
      close( block_fd );
      
      dbprintf("Cache HIT on %" PRIX64 ".%" PRId64 "[%" PRIu64 ".%" PRId64 "]\n", fent->file_id, fent->version, block_id, block_version );
   }
   
   if( !hit_cache ) {
      // get it remotely
      ssize_t read_len = fs_entry_read_remote_block( core, fs_path, fent, block_id, block_version, block_bits, block_len );
      if( read_len < 0 ) {
         errorf("fs_entry_read_remote_block( %" PRIX64 ".%" PRId64 "[%" PRIu64 ".%" PRId64 "] (%s)) rc = %d\n", fent->file_id, fent->version, block_id, block_version, fs_path, (int)read_len );
         rc = (int)read_len;
      }
      else {
         // done!
         rc = read_len;
         
         // need to dup this, since the cache will free it separately
         char* block_bits_dup = CALLOC_LIST( char, read_len );
         memcpy( block_bits_dup, block_bits, read_len );
         
         // cache this, but don't wait for it to finish
         // do NOT free the future--it'll get freed by the cache.
         struct cache_block_future* fut = fs_entry_cache_write_block_async( core, core->cache, fent->file_id, fent->version, block_id, block_version, block_bits_dup, read_len, true );
         if( fut == NULL ) {
            errorf("WARN: failed to cache %" PRIX64 ".%" PRId64 "[%" PRIu64 ".%" PRId64 "]\n", fent->file_id, fent->version, block_id, block_version );
         }
         
         dbprintf("Cache MISS on %" PRIX64 ".%" PRId64 "[%" PRIu64 ".%" PRId64 "],\n", fent->file_id, fent->version, block_id, block_version );
      }
   }
   
   return rc;
}

// read a block, given a path and block ID
ssize_t fs_entry_read_block( struct fs_core* core, char const* fs_path, uint64_t block_id, char* block_bits, size_t block_len ) {
   int err = 0;
   struct fs_entry* fent = fs_entry_resolve_path( core, fs_path, SYS_USER, 0, false, &err );
   if( !fent || err ) {
      return err;
   }

   ssize_t ret = fs_entry_read_block( core, fs_path, fent, block_id, block_bits, block_len );

   fs_entry_unlock( fent );

   return ret;
}


// read data from a file
ssize_t fs_entry_read( struct fs_core* core, struct fs_file_handle* fh, char* buf, size_t count, off_t offset ) {
   fs_file_handle_rlock( fh );
   
   if( fh->open_count <= 0 ) {
      // invalid
      fs_file_handle_unlock( fh );
      return -EBADF;
   }

   struct timespec ts, ts2, read_ts;
   
   BEGIN_TIMING_DATA( ts );
   
   int rc = fs_entry_revalidate_metadata( core, fh->path, fh->fent, NULL );
   if( rc != 0 ) {
      errorf("fs_entry_revalidate_metadata(%s) rc = %d\n", fh->path, rc );
      fs_file_handle_unlock( fh );
      return -EREMOTEIO;
   }
   
   fs_entry_rlock( fh->fent );
   
   if( !IS_STREAM_FILE( *(fh->fent) ) && fh->fent->size < offset ) {
      // eof
      fs_entry_unlock( fh->fent );
      fs_file_handle_unlock( fh );
      return 0;
   }

   off_t file_size = fh->fent->size;
   
   fs_entry_unlock( fh->fent );
   
   BEGIN_TIMING_DATA( read_ts );
   
   ssize_t total_read = 0;

   // if we're reading from an AG, then the blocksize is different...
   size_t block_len = 0;
   if( fh->is_AG )
      block_len = fh->AG_blocksize;
   else
      block_len = core->blocking_factor;
   
   ssize_t block_offset = offset % block_len;
   ssize_t ret = 0;
   

   char* block = CALLOC_LIST( char, block_len );

   bool done = false;
   while( (size_t)total_read < count && ret >= 0 && !done ) {
      // read the next block
      fs_entry_rlock( fh->fent );
      bool eof = ((unsigned)(offset + total_read) >= fh->fent->size && !IS_STREAM_FILE( *(fh->fent) ));

      if( eof ) {
         dbprintf("EOF after reading %zd bytes\n", total_read);
         ret = 0;
         fs_entry_unlock( fh->fent );
         break;
      }

      // TODO: unlock fent somehow while we're reading/downloading
      uint64_t block_id = fs_entry_block_id( block_len, offset + total_read );
      ssize_t tmp = fs_entry_read_block( core, fh->path, fh->fent, block_id, block, block_len );
      
      if( tmp > 0 ) {
         size_t read_if_not_eof = (unsigned)MIN( (size_t)(tmp - block_offset), count - total_read );
         size_t read_if_eof = file_size - (total_read + offset);
         
         ssize_t total_copy = IS_STREAM_FILE( *(fh->fent) ) ? read_if_not_eof : MIN( read_if_eof, read_if_not_eof );
         
         if( total_copy == 0 ) {
            // EOF
            ret = total_read;
            done = true;
         }
         
         else {
            dbprintf("file_size = %ld, total_read = %zd, tmp = %zd, block_offset = %jd, count = %zu, total_copy = %zd\n", (long)file_size, total_read, tmp, (intmax_t)block_offset, count, total_copy);
            
            memcpy( buf + total_read, block + block_offset, total_copy );

            total_read += total_copy;
         }
      }
      else if( tmp >= 0 && tmp != (signed)block_len ) {
         // EOF
         dbprintf("EOF after %zd bytes read\n", total_read );
         ret = total_read;
         done = true;
      }
      
      else {
         // EOF on a stream file?
         if( IS_STREAM_FILE( *(fh->fent) ) && tmp == -ENOENT ) {
            // at the end of the file
            ret = 0;
         }
         else {
            errorf( "could not read %s, rc = %zd\n", fh->path, tmp );
            ret = tmp;
         }
         done = true;
      }

      fs_entry_unlock( fh->fent );
      block_offset = 0;
   }

   free( block );

   if( ret >= 0 ) {
      ret = total_read;
   }
   
   fs_file_handle_unlock( fh );

   END_TIMING_DATA( read_ts, ts2, "read data" );
   
   END_TIMING_DATA( ts, ts2, "read" );

   return ret;
}
