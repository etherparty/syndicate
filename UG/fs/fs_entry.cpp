/*
   Copyright 2013 The Trustees of Princeton University
   All Rights Reserved
*/

#include "fs_entry.h"
#include "manifest.h"
#include "storage.h"
#include "url.h"
#include "ms-client.h"
#include "collator.h"
#include "consistency.h"


int _debug_locks = 0;

int fs_entry_set_config( struct md_syndicate_conf* conf ) {
   _debug_locks = conf->debug_lock;
   return 0;
}

// insert a child entry into an fs_entry_set
void fs_entry_set_insert( fs_entry_set* set, char const* name, struct fs_entry* child ) {
   long nh = fs_entry_name_hash( name );
   return fs_entry_set_insert_hash( set, nh, child );
}

// insert a child entry into an fs_entry_set
void fs_entry_set_insert_hash( fs_entry_set* set, long hash, struct fs_entry* child ) {
   for( unsigned int i = 0; i < set->size(); i++ ) {
      if( set->at(i).second == NULL ) {
         set->at(i).second = child;
         set->at(i).first = hash;
         return;
      }
   }

   fs_dirent dent( hash, child );
   set->push_back( dent );
}


// find a child entry in a fs_entry_set
struct fs_entry* fs_entry_set_find_name( fs_entry_set* set, char const* name ) {
   long nh = fs_entry_name_hash( name );
   return fs_entry_set_find_hash( set, nh );
}


// find a child entry in an fs_entry set
struct fs_entry* fs_entry_set_find_hash( fs_entry_set* set, long nh ) {
   for( unsigned int i = 0; i < set->size(); i++ ) {
      if( set->at(i).first == nh )
         return set->at(i).second;
   }
   return NULL;
}


// remove a child entry from an fs_entry_set
bool fs_entry_set_remove( fs_entry_set* set, char const* name ) {
   long nh = fs_entry_name_hash( name );
   bool removed = false;
   for( unsigned int i = 0; i < set->size(); i++ ) {
      if( set->at(i).first == nh ) {
         // invalidate this
         set->at(i).second = NULL;
         set->at(i).first = 0;
         removed = true;
         break;

         // TODO: recompress
      }
   }

   return removed;
}


// replace an entry
bool fs_entry_set_replace( fs_entry_set* set, char const* name, struct fs_entry* replacement ) {
   long nh = fs_entry_name_hash( name );
   for( unsigned int i = 0; i < set->size(); i++ ) {
      if( set->at(i).first == nh ) {
         (*set)[i].second = replacement;
         return true;
      }
   }
   return false;
}


// count the number of entries in an fs_entry_set
unsigned int fs_entry_set_count( fs_entry_set* set ) {
   unsigned int ret = 0;
   for( unsigned int i = 0; i < set->size(); i++ ) {
      if( set->at(i).second != NULL )
         ret++;
   }
   return ret;
}

// dereference an iterator to an fs_entry_set member
struct fs_entry* fs_entry_set_get( fs_entry_set::iterator* itr ) {
   return (*itr)->second;
}

// dereference an iterator to an fs_entry_set member
long fs_entry_set_get_name_hash( fs_entry_set::iterator* itr ) {
   return (*itr)->first;
}

// calculate the block ID from an offset
uint64_t fs_entry_block_id( off_t offset, struct md_syndicate_conf* conf ) {
   return ((uint64_t)offset) / conf->blocking_factor;
}

// set up the core of the FS
int fs_core_init( struct fs_core* core, struct md_syndicate_conf* conf ) {
   if( core == NULL ) {
      return -EINVAL;
   }

   memset( core, 0, sizeof(struct fs_core) );
   core->conf = conf;
   core->num_files = 0;

   pthread_rwlock_init( &core->lock, NULL );
   pthread_rwlock_init( &core->fs_lock, NULL );

   // initialize the root, but make it searchable and mark it as stale 
   core->root = CALLOC_LIST( struct fs_entry, 1 );

   int rc = fs_entry_init_dir( core, core->root, "/", conf->metadata_url, 1, SYS_USER, SYS_USER, conf->volume, 0755, 4096, 0, 0 );
   if( rc != 0 ) {
      errorf("fs_entry_init_dir rc = %d\n", rc );
      return rc;
   }

   core->root->link_count = 1;
   fs_entry_set_insert( core->root->children, ".", core->root );
   fs_entry_set_insert( core->root->children, "..", core->root );

   // we're stale; refresh on read
   fs_entry_mark_read_stale( core->root );
   
   return 0;
}

// make use of a collator
int fs_core_use_collator( struct fs_core* core, Collator* col ) {
   core->col = col;
   return 0;
}

// make use of an MS context
int fs_core_use_ms( struct fs_core* core, struct ms_client* ms ) {
   core->ms = ms;
   return 0;
}

// destroy the core of the FS
int fs_core_destroy( struct fs_core* core ) {
   //delete core->dtp;

   pthread_rwlock_destroy( &core->lock );
   pthread_rwlock_destroy( &core->fs_lock );

   fs_entry_destroy( core->root, true );

   free( core->root );

   return 0;
}

// rlock the fs
int fs_core_fs_rlock( struct fs_core* core ) {
   return pthread_rwlock_rdlock( &core->fs_lock );
}

// wlock the fs
int fs_core_fs_wlock( struct fs_core* core ) {
   return pthread_rwlock_wrlock( &core->fs_lock );
}

// unlock the fs
int fs_core_fs_unlock( struct fs_core* core ) {
   return pthread_rwlock_unlock( &core->fs_lock );
}


// unlink a directory's immediate children and subsequent descendants
int fs_unlink_children( struct fs_core* core, fs_entry_set* dir_children, bool remove_data ) {
   
   queue<struct fs_entry*> destroy_queue;
   for( fs_entry_set::iterator itr = dir_children->begin(); itr != dir_children->end(); ) {
      struct fs_entry* child = fs_entry_set_get( &itr );

      if( child == NULL ) {
         itr = dir_children->erase( itr );
         continue;
      }

      long fent_name_hash = fs_entry_set_get_name_hash( &itr );

      if( fent_name_hash == fs_entry_name_hash( "." ) || fent_name_hash == fs_entry_name_hash( ".." ) ) {
         itr++;
         continue;
      }

      if( fs_entry_wlock( child ) != 0 ) {
         itr = dir_children->erase( itr );
         continue;
      }

      destroy_queue.push( child );
      
      itr = dir_children->erase( itr );
   }
   
   while( destroy_queue.size() > 0 ) {
      struct fs_entry* fent = destroy_queue.front();
      destroy_queue.pop();

      int old_type = fent->ftype;
      
      fent->ftype = FTYPE_DEAD;
      fent->link_count = 0;

      if( old_type == FTYPE_FILE ) {
         if( fent->open_count == 0 ) {
            if( URL_LOCAL( fent->url ) && remove_data ) {
               fs_entry_remove_local_data( core, GET_FS_PATH( core->conf->data_root, fent->url ), fent->version );
            }
            
            fs_entry_destroy( fent, false );
            free( fent );
         }
      }

      else {
         fs_entry_set* children = fent->children;
         fent->children = NULL;

         fent->link_count = 0;

         if( fent->open_count == 0 ) {
            fs_entry_destroy( fent, false );
            free( fent );
         }

         for( fs_entry_set::iterator itr = children->begin(); itr != children->end(); itr++ ) {
            struct fs_entry* child = fs_entry_set_get( &itr );

            if( child == NULL )
               continue;

            long fent_name_hash = fs_entry_set_get_name_hash( &itr );

            if( fent_name_hash == fs_entry_name_hash( "." ) || fent_name_hash == fs_entry_name_hash( ".." ) )
               continue;

            if( fs_entry_wlock( child ) != 0 )
               continue;

            destroy_queue.push( child );
         }

         delete children;
      }
   }

   return 0;
}

// destroy a filesystem.
// this is NOT thread-safe!
int fs_destroy( struct fs_core* core ) {
   fs_entry_wlock( core->root );
   int rc = fs_unlink_children( core, core->root->children, false );

   pthread_rwlock_destroy( &core->lock );
   pthread_rwlock_destroy( &core->fs_lock );

   fs_entry_destroy( core->root, false );

   free( core->root );
   return rc;
}



static int fs_entry_init_data( struct fs_core* core, struct fs_entry* fent, int type, char const* name, char const* url, int64_t version, uid_t owner, uid_t acting_owner, gid_t volume, mode_t mode, off_t size, int64_t mtime_sec, int32_t mtime_nsec ) {
   struct timespec ts;
   clock_gettime( CLOCK_REALTIME, &ts );
   
   if( mtime_sec <= 0 ) {
      mtime_sec = ts.tv_sec;
      mtime_nsec = ts.tv_nsec;
   }

   fent->name = strdup( name );
   md_sanitize_path( fent->name );
   
   fent->url = strdup( url );
   fent->version = version;
   fent->owner = owner;
   fent->acting_owner = acting_owner;
   fent->acting_owner = owner;
   fent->volume = volume;
   fent->mode = mode & 0777;
   fent->size = size;
   fent->ctime_sec = ts.tv_sec;
   fent->ctime_nsec = ts.tv_nsec;
   fent->atime = fent->ctime_sec;
   fent->mtime_sec = mtime_sec;
   fent->mtime_nsec = mtime_nsec;
   fent->link_count = 0;
   fent->manifest = new file_manifest( core, fent->version );
   fent->max_read_freshness = core->conf->default_read_freshness;
   fent->max_write_freshness = core->conf->default_write_freshness;
   fent->read_stale = false;

   clock_gettime( CLOCK_REALTIME, &fent->refresh_time );
   
   ts.tv_sec = mtime_sec;
   ts.tv_nsec = mtime_nsec;
   fent->manifest->set_lastmod( &ts );

   return 0;
}

// common fs_entry initializion code
// a version of <= 0 will cause the FS to look at the underlying data to deduce the correct version
static int fs_entry_init_common( struct fs_core* core, struct fs_entry* fent, int type, char const* name, char const* url, int64_t version, uid_t owner, uid_t acting_owner, gid_t volume, mode_t mode, off_t size, int64_t mtime_sec, int32_t mtime_nsec) {

   memset( fent, 0, sizeof(struct fs_entry) );
   fs_entry_init_data( core, fent, type, name, url, version, owner, acting_owner, volume, mode, size, mtime_sec, mtime_nsec );
   pthread_rwlock_init( &fent->lock, NULL );
   
   return 0;
}

// create an FS entry that is a file
int fs_entry_init_file( struct fs_core* core, struct fs_entry* fent, char const* name, char const* url, int64_t version, uid_t owner, uid_t acting_owner, gid_t volume, mode_t mode, off_t size, int64_t mtime_sec, int32_t mtime_nsec ) {
   fs_entry_init_common( core, fent, FTYPE_FILE, name, url, version, owner, acting_owner, volume, mode, size, mtime_sec, mtime_nsec );
   fent->ftype = FTYPE_FILE;

   if( URL_LOCAL( url ) )
      return fs_entry_publish_file( core, GET_FS_PATH( core->conf->data_root, fent->url ), version, mode );
   else
      return 0;
}

// create an FS entry that is a directory
int fs_entry_init_dir( struct fs_core* core, struct fs_entry* fent, char const* name, char const* url, int64_t version, uid_t owner, uid_t acting_owner, gid_t volume, mode_t mode, off_t size, int64_t mtime_sec, int32_t mtime_nsec ) {
   fs_entry_init_common( core, fent, FTYPE_DIR, name, url, version, owner, acting_owner, volume, mode, size, mtime_sec, mtime_nsec );
   fent->ftype = FTYPE_DIR;
   fent->children = new fs_entry_set();
   return 0;
}

// get the next file version number
// TODO: make sure this never goes backwards in time
int64_t fs_entry_next_file_version(void) {
   return abs((int64_t)currentTimeMillis());
}

// get the next block version number (unique with high probability)
int64_t fs_entry_next_block_version(void) {
   int64_t upper = CMWC4096() & 0x7fffffff;
   int64_t lower = CMWC4096();

   int64_t ret = (upper << 32) | lower;
   return ret;
}

// duplicate an FS entry
int fs_entry_dup( struct fs_core* core, struct fs_entry* fent, struct fs_entry* src ) {
   fs_entry_init_common( core, fent, src->ftype, src->name, src->url, src->version, src->owner, src->acting_owner, src->volume, src->mode, src->size, src->mtime_sec, src->mtime_nsec );
   fent->ftype = src->ftype;

   if( src->children ) {
      fent->children = new fs_entry_set();
      for( fs_entry_set::iterator itr = src->children->begin(); itr != src->children->end(); itr++ ) {
         fs_dirent d( itr->first, itr->second );
         fent->children->push_back( d );
      }
   }

   fent->manifest = new file_manifest( src->manifest );

   return 0;
}


// create an FS entry from an md_entry.
int fs_entry_init_md( struct fs_core* core, struct fs_entry* fent, struct md_entry* ent ) {
   char* basename = md_basename( ent->path, NULL );
   if( ent->type == MD_ENTRY_DIR ) {
      // this is a directory
      fs_entry_init_dir( core, fent, basename, ent->url, ent->version, ent->owner, ent->acting_owner, ent->volume, ent->mode, ent->size, ent->mtime_sec, ent->mtime_nsec );
   }
   else {
      // this is a file
      fs_entry_init_file( core, fent, basename, ent->url, ent->version, ent->owner, ent->acting_owner, ent->volume, ent->mode, ent->size, ent->mtime_sec, ent->mtime_nsec );
   }

   free( basename );
   return 0;
}


// destroy an FS entry
int fs_entry_destroy( struct fs_entry* fent, bool needlock ) {

   // free common fields
   if( needlock )
      fs_entry_wlock( fent );

   if( fent->name ) {
      free( fent->name );
      fent->name = NULL;
   }

   if( fent->url ) {
      free( fent->url );
      fent->url = NULL;
   }

   if( fent->manifest ) {
      delete fent->manifest;
      fent->manifest = NULL;
   }

   if( fent->children ) {
      delete fent->children;
      fent->children = NULL;
   }

   fent->ftype = FTYPE_DEAD;      // next thread to hold this lock knows this is a dead entry
   fs_entry_unlock( fent );
   pthread_rwlock_destroy( &fent->lock );
   return 0;
}

// free an fs_dir_entry
int fs_dir_entry_destroy( struct fs_dir_entry* dent ) {
   md_entry_free( &dent->data );
   return 0;
}


// free a list of fs_dir_entrys
int fs_dir_entry_destroy_all( struct fs_dir_entry** dents ) {
   for( unsigned int i = 0; dents[i] != NULL; i++ ) {
      fs_dir_entry_destroy( dents[i] );
      free( dents[i] );
   }
   return 0;
}

// calculate the hash of a name
long fs_entry_name_hash( char const* name ) {
   return md_hash( name );
}

// lock a file for reading
int fs_entry_rlock( struct fs_entry* fent ) {
   if( _debug_locks ) {
      dbprintf( "%s\n", fent->name );
   }

   int rc = pthread_rwlock_rdlock( &fent->lock );
   return rc;
}

// lock a file for writing
int fs_entry_wlock( struct fs_entry* fent ) {
   if( _debug_locks ) {
      dbprintf( "%s\n", fent->name);
   }

   int rc = pthread_rwlock_wrlock( &fent->lock );
   if( fent->ftype == FTYPE_DEAD )
      return -ENOENT;
   
   if( rc == 0 )
      fent->write_locked = true;
   
   return rc;
}

// unlock a file
int fs_entry_unlock( struct fs_entry* fent ) {
   if( _debug_locks ) {
      dbprintf( "%s\n", fent->name );
   }

   fent->write_locked = false;
   return pthread_rwlock_unlock( &fent->lock );
}

// lock a file handle for reading
int fs_file_handle_rlock( struct fs_file_handle* fh ) {
   return pthread_rwlock_rdlock( &fh->lock );
}

// lock a file handle for writing
int fs_file_handle_wlock( struct fs_file_handle* fh ) {
   return pthread_rwlock_wrlock( &fh->lock );
}

// unlock a file handle
int fs_file_handle_unlock( struct fs_file_handle* fh ) {
   return pthread_rwlock_unlock( &fh->lock );
}

// lock a directory handle for reading
int fs_dir_handle_rlock( struct fs_dir_handle* dh ) {
   return pthread_rwlock_rdlock( &dh->lock );
}

// lock a directory handle for writing
int fs_dir_handle_wlock( struct fs_dir_handle* dh ) {
   return pthread_rwlock_wrlock( &dh->lock );
}

// unlock a directory handle
int fs_dir_handle_unlock( struct fs_dir_handle* dh ) {
   return pthread_rwlock_unlock( &dh->lock );
}

// read-lock a filesystem core
int fs_core_rlock( struct fs_core* core ) {
   return pthread_rwlock_rdlock( &core->lock );
}

// write-lock a filesystem core
int fs_core_wlock( struct fs_core* core ) {
   return pthread_rwlock_wrlock( &core->lock );
}

// unlock a filesystem core
int fs_core_unlock( struct fs_core* core ) {
   return pthread_rwlock_unlock( &core->lock );
}

// resolve an absolute path, running a given function on each entry as the path is walked
// returns the locked fs_entry at the end of the path on success
struct fs_entry* fs_entry_resolve_path_cls( struct fs_core* core, char const* path, uid_t user, gid_t vol, bool writelock, int* err, int (*ent_eval)( struct fs_entry*, void* ), void* cls ) {

   // if this path ends in '/', then append a '.'
   char* fpath = NULL;
   if( strlen(path) == 0 ) {
      *err = -EINVAL;
      return NULL;
   }

   if( path[strlen(path)-1] == '/' ) {
      fpath = md_fullpath( path, ".", NULL );
   }
   else {
      fpath = strdup( path );
   }

   char* tmp = NULL;

   char* name = strtok_r( fpath, "/", &tmp );
   while( name != NULL && strcmp(name, ".") == 0 ) {
      name = strtok_r( NULL, "/", &tmp );
   }

   if( name == NULL && writelock )
      fs_entry_wlock( core->root );
   else
      fs_entry_rlock( core->root );

   if( core->root->link_count == 0 ) {
      // filesystem was nuked
      free( fpath );
      fs_entry_unlock( core->root );
      *err = -ENOENT;
      return NULL;
   }

   struct fs_entry* cur_ent = core->root;
   struct fs_entry* prev_ent = NULL;

   while( name != NULL ) {

      // if this isn't a directory, then invalid path
      if( cur_ent->ftype != FTYPE_DIR ) {
         if( cur_ent->ftype == FTYPE_FILE )
            *err = -ENOTDIR;
         else
            *err = -ENOENT;

         free( fpath );
         fs_entry_unlock( cur_ent );
         if( prev_ent )
            fs_entry_unlock( prev_ent );

         return NULL;
      }

      // do we have permission to resolve this name?
      if( !IS_DIR_READABLE( cur_ent->mode, cur_ent->owner, cur_ent->volume, user, vol ) ) {

         // the appropriate read flag is not set
         *err = -EACCES;
         free( fpath );
         fs_entry_unlock( cur_ent );

         return NULL;
      }

      if( cur_ent == core->root && strcmp(name, "..") == 0 ) {
         // this is the root directory, and we tried to access /..
         name = strtok_r( NULL, "/", &tmp );
         continue;
      }

      // run our evaluator on this entry, if it exists
      if( ent_eval ) {
         int eval_rc = (*ent_eval)( cur_ent, cls );
         if( eval_rc != 0 ) {
            *err = eval_rc;
            free( fpath );
            fs_entry_unlock( cur_ent );

            return NULL;  
         }
      }

      // resolve next name
      prev_ent = cur_ent;
      cur_ent = fs_entry_set_find_name( prev_ent->children, name );

      if( cur_ent == NULL ) {
         // not found
         *err = -ENOENT;
         free( fpath );
         fs_entry_unlock( prev_ent );

         return NULL;
      }
      else {
         // next path name
         name = strtok_r( NULL, "/", &tmp );
         while( name != NULL && strcmp(name, ".") == 0 ) {
            name = strtok_r( NULL, "/", &tmp );
         }

         // attempt to lock.  If this is the last step of the path,
         // then write-lock it if needed
         if( name == NULL && writelock )
            fs_entry_wlock( cur_ent );
         else
            fs_entry_rlock( cur_ent );

         fs_entry_unlock( prev_ent );

         if( cur_ent->link_count == 0 || cur_ent->ftype == FTYPE_DEAD ) {
           // just got removed
           *err = -ENOENT;
           free( fpath );
           fs_entry_unlock( cur_ent );

           return NULL;
         }
      }
   }
   free( fpath );
   if( name == NULL ) {
      // ran out of path
      *err = 0;
      return cur_ent;
   }
   else {
      // not a directory
      *err = -ENOTDIR;
      fs_entry_unlock( cur_ent );
      return NULL;
   }
}



// resolve an absolute path.
// returns the locked fs_entry at the end of the path on success
struct fs_entry* fs_entry_resolve_path( struct fs_core* core, char const* path, uid_t user, gid_t vol, bool writelock, int* err ) {
   return fs_entry_resolve_path_cls( core, path, user, vol, writelock, err, NULL, NULL );
}


// convert an fs_entry to an md_entry.
// the URLs will all be public.
int fs_entry_to_md_entry( struct fs_core* core, char const* fs_path, uid_t owner, gid_t volume, struct md_entry* dest ) {
   int err = 0;
   struct fs_entry* fent = fs_entry_resolve_path( core, fs_path, owner, volume, false, &err );
   if( !fent || err ) {
      if( !err )
         err = -ENOMEM;

      return err;
   }

   err = fs_entry_to_md_entry( core, fs_path, fent, dest );

   fs_entry_unlock( fent );
   return err;
}

// convert an fs_entry to an md_entry.
// the URLs will all be public.
int fs_entry_to_md_entry( struct fs_core* core, char const* fs_path, struct fs_entry* fent, struct md_entry* dest ) {

   memset( dest, 0, sizeof(struct md_entry) );
   
   dest->type = fent->ftype == FTYPE_FILE ? MD_ENTRY_FILE : MD_ENTRY_DIR;
   dest->path = strdup( fs_path );

   if( URL_LOCAL( fent->url ) ) {
      dest->url = fs_entry_local_to_public( core, fent->url, fent->version );
      dbprintf("local to public: %s to %s\n", fent->url, dest->url );
   }
   else
      dest->url = strdup( fent->url );

   dest->ctime_sec = fent->ctime_sec;
   dest->ctime_nsec = fent->ctime_nsec;
   dest->mtime_sec = fent->mtime_sec;
   dest->mtime_nsec = fent->mtime_nsec;
   dest->owner = fent->owner;
   dest->acting_owner = fent->acting_owner;
   dest->volume = fent->volume;
   dest->mode = fent->mode;
   dest->size = fent->size;
   dest->version = fent->version;
   dest->max_read_freshness = fent->max_read_freshness;
   dest->max_write_freshness = fent->max_write_freshness;
      
   return 0;
}



// destroy a directory handle
void fs_dir_handle_destroy( struct fs_dir_handle* dh ) {
   dh->dent = NULL;
   if( dh->path ) {
      free( dh->path );
      dh->path = NULL;
   }
   pthread_rwlock_destroy( &dh->lock );
}



// destroy a file handle
// NOTE: it must be wlocked first
int fs_file_handle_destroy( struct fs_file_handle* fh ) {
   fh->fent = NULL;
   if( fh->path ) {
      free( fh->path );
      fh->path = NULL;
   }
   pthread_rwlock_unlock( &fh->lock );
   pthread_rwlock_destroy( &fh->lock );

   return 0;
}


// reversion a file.  Only valid for local files 
// FENT MUST BE WRITE-LOCKED!
int fs_entry_reversion_file( struct fs_core* core, char const* fs_path, struct fs_entry* fent, int64_t new_version ) {
   if( !URL_LOCAL( fent->url ) ) {
      return -EINVAL;
   }

   // reversion the data locally
   int rc = fs_entry_reversion_local_file( core, fs_path, fent, new_version );
   if( rc != 0 ) {
      return rc;
   }

   // set the version on local data, since the local reversioning succeeded
   fent->version = new_version;
   fent->manifest->set_file_version( core, new_version );


   struct md_entry ent;
   fs_entry_to_md_entry( core, fs_path, fent, &ent );

   // synchronously update
   rc = ms_client_update( core->ms, &ent );

   md_entry_free( &ent );

   if( rc != 0 ) {
      // failed to reversion remotely
      errorf("ms_client_update(%s.%" PRId64 " --> %" PRId64 ") rc = %d\n", fs_path, fent->version, new_version, rc );
   }
   
   return rc;
}



