/*
   Copyright 2013 The Trustees of Princeton University
   All Rights Reserved
*/

#include "read.h"
#include "manifest.h"
#include "storage.h"
#include "network.h"
#include "url.h"

// Given an offset, get the corresponding block's data
// fent must be at least read-locked first
ssize_t fs_entry_do_read_block( struct fs_core* core, char const* fs_path, struct fs_entry* fent, off_t offset, char* block_bits ) {

   if( offset >= fent->size ) {
      return 0;      // EOF
   }
   // get the URL
   char* block_url = fs_entry_get_block_url( core, fs_path, fent, offset );

   if( block_url == NULL ) {
      // something's wrong
      errorf( "no URL for data at %" PRId64 "\n", offset );
      return -ENODATA;
   }

   if( URL_LOCAL( block_url ) ) {
      // this is a locally-hosted block--get its bits
      int fd = open( GET_PATH( block_url ), O_RDONLY );
      if( fd < 0 ) {
         fd = -errno;
         errorf( "could not open %s, errno = %d\n", GET_PATH( block_url ), fd );
         free( block_url );
         return fd;
      }

      ssize_t nr = fs_entry_get_block_local( core, fd, block_bits );
      if( nr < 0 ) {
         errorf("fs_entry_get_block_local(%d) rc = %zd\n", fd, nr );
         free( block_url );
         close( fd );
         return nr;
      }
      
      close( fd );
      free( block_url );

      dbprintf("read %zd bytes locally\n", nr );
      return nr;
   }

   else {
      // this is a remotely-hosted block--get its bits
      ssize_t nr = fs_entry_download_block( core, block_url, block_bits );
      if( nr <= 0 ) {
         // try a replica
         if( core->conf->replica_urls != NULL ) {
            for( int i = 0; core->conf->replica_urls[i] != NULL; i++ ) {
               uint64_t block_id = fs_entry_block_id( offset, core->conf );
               char* replica_block_url = fs_entry_replica_block_url( core->conf->replica_urls[i], fent->version, block_id, fent->manifest->get_block_version( block_id ) );
               nr = fs_entry_download_block( core, replica_block_url, block_bits );
               free( replica_block_url );

               if( nr > 0 )
                  break;
            }
         }
      }
      if( nr <= 0 ) {
         nr = -ENODATA;
      }
      else {
         dbprintf("read %ld bytes remotely\n", (long)nr);
      }

      free( block_url );
      return nr;
   }
}


// get a block's data, be it local or remote
// NEED TO LOCK THE FILE HANDLE FIRST!
// fh->fent must be at least read-locked
ssize_t fs_entry_read_block( struct fs_core* core, struct fs_file_handle* fh, off_t offset, char* block_bits ) {
   return fs_entry_do_read_block( core, fh->path, fh->fent, offset, block_bits );
}

// read a block, given a path and block ID
ssize_t fs_entry_read_block( struct fs_core* core, char* fs_path, uint64_t block_id, char* block_bits ) {
   int err = 0;
   struct fs_entry* fent = fs_entry_resolve_path( core, fs_path, SYS_USER, 0, false, &err );
   if( !fent || err ) {
      return err;
   }

   off_t offset = block_id * core->conf->blocking_factor;

   ssize_t ret = fs_entry_do_read_block( core, fs_path, fent, offset, block_bits );

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
   struct timespec latency_ts;

   BEGIN_TIMING_DATA( ts );
   
   // refresh the path metadata
   int rc = fs_entry_revalidate_path( core, fh->path );
   if( rc != 0 ) {
      errorf("fs_entry_revalidate_path(%s) rc = %d\n", fh->path, rc );
      fs_file_handle_unlock( fh );
      return rc;
   }
   
   fs_entry_wlock( fh->fent );
   
   if( fh->fent->size < offset ) {
      // eof
      fs_entry_unlock( fh->fent );
      fs_file_handle_unlock( fh );
      return 0;
   }

   rc = fs_entry_revalidate_manifest( core, fh->path, fh->fent );
   if( rc != 0 ) {
      errorf("fs_entry_revalidate_manifest(%s) rc = %d\n", fh->path, rc );
      fs_entry_unlock( fh->fent );
      fs_file_handle_unlock( fh );
      return -EREMOTEIO;
   }

   off_t file_size = fh->fent->size;
   
   fs_entry_unlock( fh->fent );

   END_TIMING_DATA( ts, latency_ts, "metadata latency" );

   BEGIN_TIMING_DATA( read_ts );
   
   ssize_t total_read = 0;
   ssize_t block_offset = offset % core->conf->blocking_factor;
   ssize_t ret = 0;
   

   char* block = CALLOC_LIST( char, core->conf->blocking_factor );

   bool done = false;
   while( (size_t)total_read < count && ret >= 0 && !done ) {
      // read the next block
      fs_entry_rlock( fh->fent );
      bool eof = (unsigned)(offset + total_read) > fh->fent->size;

      if( eof ) {
         ret = 0;
         fs_entry_unlock( fh->fent );
         break;
      }

      ssize_t tmp = fs_entry_read_block( core, fh, offset + total_read, block );
      
      if( tmp > 0 ) {
         ssize_t total_copy = MIN( file_size - (total_read + offset), (unsigned)MIN( (size_t)(tmp - block_offset), count - total_read ) );
         dbprintf("file_size = %zd, total_read = %zd, tmp = %zd, block_offset = %zd, count = %lu, total_copy = %zd\n", file_size, total_read, tmp, block_offset, count, total_copy);
         
         memcpy( buf + total_read, block + block_offset, total_copy );         

         // did we re-integrate this block?
         // if so, store it
         int64_t block_id = fs_entry_block_id( offset + total_read, core->conf );
         if( URL_LOCAL( fh->fent->url ) && !fh->fent->manifest->is_block_local( block_id ) ) {
            int rc = fs_entry_collate( core, fh->path, fh->fent, block_id, fh->fent->manifest->get_block_version( block_id ), block );
            if( rc != 0 ) {
               errorf("WARN: fs_entry_collate_block(%s, %" PRId64 ") rc = %d\n", fh->path, block_id, rc );
            }
         }
         
         total_read += total_copy;
      }
      else if( tmp == 0 ) {
         // EOF
         ret = tmp;
         done = true;
      }
      
      else {
         errorf( "could not read %s, rc = %zd\n", fh->path, tmp );
         ret = tmp;
         done = true;
      }

      fs_entry_unlock( fh->fent );
      block_offset = 0;
   }

   free( block );

   if( ret >= 0 )
      ret = total_read;
   
   fs_file_handle_unlock( fh );

   END_TIMING_DATA( read_ts, ts2, "read data" );
   
   END_TIMING_DATA( ts, ts2, "read" );

   return ret;
}