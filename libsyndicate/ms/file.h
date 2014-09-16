/*
   Copyright 2014 The Trustees of Princeton University

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

#ifndef _MS_CLIENT_FILE_H_
#define _MS_CLIENT_FILE_H_

#include "libsyndicate/ms/core.h"

// status responses from the MS on a locally cached metadata record
#define MS_LISTING_NEW          ms::ms_listing::NEW             // new entry
#define MS_LISTING_NOCHANGE     ms::ms_listing::NOT_MODIFIED    // entry/listing not modified
#define MS_LISTING_NONE         ms::ms_listing::NONE            // entry doesn't exist


// directory listing
struct ms_listing {
   int status;       
   int type;         // file or directory?
   vector<struct md_entry>* entries;
   
   bool have_more;
   char* serialized_cursor;
};

// path entry metadata for getting metadata listings
struct ms_path_ent {
   uint64_t volume_id;
   uint64_t file_id;
   int64_t version;
   int64_t write_nonce;
   char* name;

   void* cls;
};

// download context for performing a path listing 
struct ms_path_download_context {
   
   struct md_download_context* dlctx;
   
   int page_id;
   char* serialized_cursor;
   
   struct ms_path_ent* path_ent;
   
   bool have_listing;
   struct ms_listing listing_buf;
};

// request to manipulate metadata on the MS
struct ms_client_request {
   struct md_entry* ent;
   
   // optional; only if performing an ms::ms_update::UPDATE due to a write
   uint64_t* affected_blocks;
   size_t num_affected_blocks;
   
   // optional; only if performing an ms::ms_update::RENAME 
   struct md_entry* dest;
};

// multi-operation result 
struct ms_client_multi_result {
   
   int reply_error;             // result of the multi-RPC 
   int last_item;               // index of the last item to be processed in the list of requests
   
   uint64_t* file_ids;          // list of resulting file IDs
   size_t num_file_ids;
   
   int64_t* write_nonces;       // list of resulting write nonces
   size_t num_write_nonces;
   
   uint64_t* coordinator_ids;   // list of resulting coordinator IDs
   size_t num_coordinator_ids;
};


typedef map<long, struct md_update> ms_client_update_set;

typedef vector< struct ms_path_ent > ms_path_t;

// map file IDs to their child listings 
typedef map< uint64_t, struct ms_listing > ms_response_t;

extern "C" {
   
// file metadata API
int ms_client_create( struct ms_client* client, uint64_t* file_id, int64_t* write_nonce, struct md_entry* ent );
int ms_client_mkdir( struct ms_client* client, uint64_t* file_id, int64_t* write_nonce, struct md_entry* ent );
int ms_client_delete( struct ms_client* client, struct md_entry* ent );
int ms_client_update_write( struct ms_client* client, int64_t* write_nonce, struct md_entry* ent, uint64_t* affected_blocks, size_t num_affected_blocks );
int ms_client_update( struct ms_client* client, int64_t* write_nonce, struct md_entry* ent );
int ms_client_coordinate( struct ms_client* client, uint64_t* new_coordinator, int64_t* write_nonce, struct md_entry* ent );
int ms_client_rename( struct ms_client* client, int64_t* write_nonce, struct md_entry* src, struct md_entry* dest );

// multi-RPC API
int ms_client_multi_begin( struct ms_client* client, int ms_op, int ms_op_flags, struct ms_client_request* reqs, size_t num_reqs, struct ms_client_network_context* nctx );
int ms_client_multi_end( struct ms_client* client, struct ms_client_multi_result* results, struct ms_client_network_context* nctx );

// path resolution 
int ms_client_get_listings( struct ms_client* client, ms_path_t* path, ms_response_t* ms_response );
int ms_client_make_path_ent( struct ms_path_ent* path_ent, uint64_t volume_id, uint64_t file_id, int64_t version, int64_t write_nonce, char const* name, void* cls );

// memory management
void ms_client_free_path_ent( struct ms_path_ent* path_ent, void (*free_cls)(void*) );
void ms_client_free_path( ms_path_t* path, void (*free_cls)(void*) );
void ms_client_free_response( ms_response_t* ms_response );
void ms_client_free_listing( struct ms_listing* listing );
int ms_client_multi_result_free( struct ms_client_multi_result* result );

// low-level file API 
int ms_client_populate_update( struct md_update* up, int op, int flags, struct md_entry* ent );

int ms_client_single_rpc( struct ms_client* client, int ms_op, int ms_op_flags, struct ms_client_request* request, struct ms_client_multi_result* result );
int ms_client_update_rpc( struct ms_client* client, struct md_update* up );

int ms_client_send_updates( struct ms_client* client, ms_client_update_set* all_updates, ms::ms_reply* reply, bool verify_response );
int ms_client_send_updates_begin( struct ms_client* client, ms_client_update_set* all_updates, char** serialized_updates, struct ms_client_network_context* nctx );
int ms_client_send_updates_end( struct ms_client* client, ms::ms_reply* reply, bool verify_response, struct ms_client_network_context* nctx );

int ms_client_parse_reply( struct ms_client* client, ms::ms_reply* src, char const* buf, size_t buf_len, bool verify );

uint64_t ms_client_make_file_id();

}

#endif