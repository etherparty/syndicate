/*
   Copyright 2013 The Trustees of Princeton University
   All Rights Reserved
*/

#include "syndicate-httpd.h"

bool g_running = false;
struct md_HTTP g_http;

// HTTP authentication callback
uint64_t httpd_HTTP_authenticate( struct md_HTTP_connection_data* md_con_data, char* username, char* password ) {

   struct syndicate_connection* syncon = (struct syndicate_connection*)md_con_data->cls;
   struct syndicate_state* state = syncon->state;
   struct ms_client* client = state->ms;

   uint64_t ug = ms_client_authenticate( client, md_con_data, username, password );
   if( ug == MD_GUEST_UID ) {
      // someone we don't know
      return -EACCES;
   }
   return 0;
}

// GET streaming handler (note: never return 0)
ssize_t httpd_GET_stream(void* cls, uint64_t pos, char* buf, size_t max) {
   struct httpd_GET_data* data = (struct httpd_GET_data*)(cls);

   ssize_t nr = fs_entry_read( data->state->core, data->fh, buf, max, data->offset );
   if( nr < 0 ) {
      errorf( "fs_entry_read rc = %zd\n", nr );
      return -1;
   }
   if( nr == 0 ) {
      // end-of-file
      return -1;
   }

   data->offset += nr;
   
   return nr;
}

// GET free stream handler
void http_GET_cleanup(void* cls) {
   struct httpd_GET_data* data = (struct httpd_GET_data*)(cls);

   dbprintf( "close %s\n", data->fh->path );
   fs_entry_close( data->state->core, data->fh );
   free( data->fh );
   free( data );
}


// parse a byterange header
bool parse_byterange( char* header, uint64_t* start, uint64_t* end ) {
   // extract the byte range
   if( strstr( header, "bytes=" ) == NULL )
      return false;
   
   char range_value[1024];
   strncpy( range_value, header, 1024 );
   
   char* tmp = NULL;
   bool rc = false;

   char* start_tok = strtok_r( range_value, " bytes=", &tmp );
   char* end_tok = (char*)strstr( start_tok, "-" );
   if( end_tok == NULL )
      return rc;

   *end_tok = 0;
   end_tok++;

   if( start_tok != NULL && end_tok != NULL ) {
      char *tmp1 = NULL, *tmp2 = NULL;

      uint64_t start_range = strtol( start_tok, &tmp1, 10 );
      uint64_t end_range = strtol( end_tok, &tmp2, 10 );

      if( *tmp1 == '\0' && *tmp2 == '\0' ) {
         *start = start_range;
         *end = end_range;
         rc = true;
      }
   }

   return rc;
}


// HTTP connect callback
void* httpd_HTTP_connect( struct md_HTTP_connection_data* md_con_data ) {
   struct httpd_connection_data* dat = CALLOC_LIST( struct httpd_connection_data, 1 );
   dat->fd = -1;
   dat->err = 0;
   return dat;
}


// HTTP head handler
struct md_HTTP_response* httpd_HTTP_HEAD_handler( struct md_HTTP_connection_data* md_con_data ) {

   char* url = md_con_data->url_path;
   struct syndicate_state* state = syndicate_get_state();

   uint64_t owner = 0;
   if( md_con_data->user )
      owner = md_con_data->user->uid;
   else
      owner = state->conf.owner;

   dbprintf( "client_HTTP_HEAD_handler on %s\n", url);

   struct md_HTTP_response* resp = CALLOC_LIST( struct md_HTTP_response, 1 );

   // parse the url_path into its constituent components
   struct http_request_data reqdat;
   
   int rc = http_parse_request( md_con_data->http, resp, &reqdat, url );
   if( rc < 0 ) {
      // error!
      return resp;
   }

   // may need to redirect this request...
   // status of requested object
   struct stat sb;
   memset(&sb, 0, sizeof(sb));

   char* redirect_url = NULL;
   rc = http_process_redirect( state, &redirect_url, &sb, &reqdat );

   // error?
   if( rc < 0 ) {
      char buf[100];
      snprintf(buf, 100, "HEAD http_process_redirect rc = %d\n", rc );
      http_io_error_resp( resp, rc, buf );
      return resp;
   }

   // got a new URL? re-extract the information
   if( rc == 0 ) {
      // we would need to redirect; re-extract the information

      char* url_path = md_path_from_url( redirect_url );

      free( redirect_url );
      http_request_data_free( &reqdat );

      rc = http_parse_request( md_con_data->http, resp, &reqdat, url_path );

      free( url_path );

      if( rc < 0 ) {
         return resp;
      }
   }

   struct md_entry ent;
   memset( &ent, 0, sizeof(ent) );

   rc = fs_entry_to_md_entry( state->core, md_con_data->url_path, owner, state->conf.volume, &ent );
   if( rc < 0 ) {
      errorf( "fs_entry_to_md_entry rc = %d\n", rc );
      char buf[100];
      snprintf(buf, 100, "HEAD fs_entry_to_md_entry rc = %d\n", rc );
      http_io_error_resp( resp, rc, buf );
   }
   else {
      // got data; serialize it
      // TODO: protobufs
      /*
      char* tmp = md_to_string( &ent, NULL );
      char* md_str = md_prepend( tmp, "\n", NULL );
      free( tmp );
      */
      char const* md_str = "NOT YET IMPLEMENTED\n";
      md_create_HTTP_response_ram_nocopy( resp, "text/plain", 200, md_str, strlen(md_str) + 1 );
   }
   
   http_request_data_free( &reqdat );

   return resp;
}


// GET a directory
static int httpd_GET_dir( struct md_HTTP_response* resp, struct md_HTTP_connection_data* md_con_data, struct http_request_data* reqdat ) {

   struct syndicate_state* state = syndicate_get_state();

   uint64_t owner = 0;
   if( md_con_data->user )
      owner = md_con_data->user->uid;
   else
      owner = state->conf.owner;

   int rc = 0;
   struct fs_dir_handle* fdh = fs_entry_opendir( state->core, reqdat->fs_path, owner, state->conf.volume, &rc );
   if( fdh != NULL && rc == 0 ) {
      struct fs_dir_entry** dirents = fs_entry_readdir( state->core, fdh, &rc );

      fs_entry_closedir( state->core, fdh );
      free( fdh );

      if( rc == 0 && dirents ) {
         stringstream sts;

         for( int i = 0; dirents[i] != NULL; i++ ) {
            // convert to public URL
            if( dirents[i]->ftype == FTYPE_FILE && URL_LOCAL( dirents[i]->data.url ) ) {
               // convert to public file URL
               free( dirents[i]->data.url );
               dirents[i]->data.url = fs_entry_public_file_url( state->core, dirents[i]->data.path, dirents[i]->data.version );
            }
            else if( URL_LOCAL( dirents[i]->data.url ) ) {
               // convert to public data URL...on the metadata server
               free( dirents[i]->data.url );
               dirents[i]->data.url = fs_entry_public_dir_url( state->core, dirents[i]->data.path );
            }

            //char* tmp = md_serialize_entry( &dirents[i]->data, NULL );
            char const* tmp = "USE PROTOBUFS";
            sts << tmp << "\n";
            //free( tmp );
         }

         string ret = sts.str();
         md_create_HTTP_response_ram( resp, "text/plain", 200, ret.c_str(), ret.size() + 1 );
      }
      else {
         char buf[100];
         snprintf( buf, 100, "GET fs_entry_readdir rc = %d\n", rc );
         http_io_error_resp( resp, rc, buf );
      }

      if( dirents ) {
         fs_dir_entry_destroy_all( dirents );
         free( dirents );
      }
   }
   else {
      char buf[100];
      snprintf(buf, 100, "GET fs_entry_opendir rc = %d\n", rc );
      http_io_error_resp( resp, rc, buf );
   }

   // handled!
   return 0;
}


// GET a file block
static int httpd_GET_file_blocks( struct md_HTTP_response* resp, struct md_HTTP_connection_data* md_con_data, struct http_request_data* reqdat, struct stat* sb ) {

   struct syndicate_state* state = syndicate_get_state();
   struct md_HTTP_header** client_headers = md_con_data->headers;

   uint64_t owner = 0;
   if( md_con_data->user )
      owner = md_con_data->user->uid;
   else
      owner = state->conf.owner;

   // request for a file
   // begin streaming the data back
   int err = 0;
   struct fs_file_handle* fh = fs_entry_open( state->core, reqdat->fs_path, NULL, owner, state->conf.volume, O_RDONLY, ~state->conf.usermask, &err );
   if( fh == NULL ) {
      errorf( "could not open %s, rc = %d\n", reqdat->fs_path, err );

      char buf[100];
      snprintf(buf, 100, "GET fs_entry_open rc = %d\n", err );
      http_io_error_resp( resp, err, buf );

      // handled!
      return 0;
   }

   // stream it back
   struct httpd_GET_data* get_data = CALLOC_LIST( struct httpd_GET_data, 1 );
   get_data->state = state;
   get_data->fh = fh;
   get_data->offset = 0;

   uint64_t start_range = 0;
   uint64_t end_range = 0;
   int status = 200;
   off_t size = sb->st_size;

   // was this a byte-range request?
   for( int i = 0; client_headers[i] != NULL; i++ ) {
      if( strcasecmp( client_headers[i]->header, "Content-Range" ) == 0 ) {
         if( parse_byterange( client_headers[i]->value, &start_range, &end_range ) ) {
            if( start_range < (unsigned)sb->st_size ) {
               end_range = MIN( (unsigned)sb->st_size, end_range );
               status = 206;
               get_data->offset = start_range;
               size = end_range - start_range + 1;
            }
            else {
               char buf[200];
               snprintf(buf, 200, "GET out of range (%" PRIu64 " >= %" PRId64 ")\n", start_range, sb->st_size );
               md_create_HTTP_response_ram_static( resp, "text/plain", 416, buf, strlen(buf) + 1 );
               status = 416;
            }
            break;
         }
      }
   }

   if( status < 400 ) {
      // success! (otherwise, already handled)
      dbprintf( "opened %s, will read\n", fh->path );
      md_create_HTTP_response_stream( resp, "application/octet-stream", status, size, state->conf.blocking_factor, httpd_GET_stream, get_data, http_GET_cleanup );
   }

   // handled!
   return 0;
}


// HTTP GET handler
struct md_HTTP_response* httpd_HTTP_GET_handler( struct md_HTTP_connection_data* md_con_data ) {

   char* url = md_con_data->url_path;
   struct syndicate_state* state = syndicate_get_state();

   dbprintf( "client_HTTP_GET_handler on %s\n", url);

   struct md_HTTP_response* resp = CALLOC_LIST( struct md_HTTP_response, 1 );

   // parse the url_path into its constituent components
   struct http_request_data reqdat;
   
   int rc = http_parse_request( md_con_data->http, resp, &reqdat, url );
   if( rc < 0 ) {
      // error!
      return resp;
   }

   // status of requested object
   struct stat sb;
   memset(&sb, 0, sizeof(sb));

   char* redirect_url = NULL;

   int redirect_rc = http_process_redirect( state, &redirect_url, &sb, &reqdat );

   // error?
   if( redirect_rc < 0 ) {
      errorf( "http_process_redirect rc = %d\n", redirect_rc );

      http_request_data_free( &reqdat );

      char buf[100];
      snprintf(buf, 100, "GET http_process_redirect rc = %d\n", redirect_rc );
      http_io_error_resp( resp, redirect_rc, buf );
      return resp;
   }

   if( S_ISDIR( sb.st_mode ) ) {
      // request for directory listing
      httpd_GET_dir( resp, md_con_data, &reqdat );

      http_request_data_free( &reqdat );
      return resp;
   }
   
   // got a new URL? re-extract the information
   if( redirect_rc == 0 ) {
      // we would need to redirect; re-extract the information
      
      char* url_path = md_path_from_url( redirect_url );

      free( redirect_url );
      http_request_data_free( &reqdat );
      
      rc = http_parse_request( md_con_data->http, resp, &reqdat, url_path );
      
      free( url_path );
      
      if( rc < 0 ) {
         http_request_data_free( &reqdat );
         return resp;
      }
   }

   // handle a file
   httpd_GET_file_blocks( resp, md_con_data, &reqdat, &sb );

   http_request_data_free( &reqdat );
   
   return resp;
}


// POST/PUT iterator--receive writes
int httpd_upload_iterator( void *coninfo_cls, enum MHD_ValueKind kind,
                           const char *key,
                           const char *filename, const char *content_type,
                           const char *transfer_encoding, const char *data,
                           uint64_t off, size_t size) {


   dbprintf( "POST/PUT %zu bytes\n", size );

   struct md_HTTP_connection_data *md_con_data = (struct md_HTTP_connection_data*)coninfo_cls;
   struct httpd_connection_data* dat = (struct httpd_connection_data*)md_con_data->cls;

   if( size > 0 ) {
      if( dat->fd < 0 ) {
         // new connection; make a temporary file for it (but unlink it after creation--no need to clutter the namespace)
         char* tmppath = strdup( SYNDICATE_HTTPD_TMP );
         dat->fd = mkstemp( tmppath );
         if( dat->fd < 0 ) {
            dat->err = -errno;
            errorf( "could not create temporary file, errno = %d\n", dat->err );
            return MHD_NO;
         }

         unlink( tmppath );
         free( tmppath );
      }

      ssize_t rc = write( dat->fd, data, size );
      if( rc < 0 ) {
         rc = -errno;
         errorf( "could not write, rc = %zd\n", rc );
         return MHD_NO;
      }
   }
   
   return MHD_YES;
}


// get the value of a mode header.
// return -ENOENT if no header
// return -EINVAL if invalid
mode_t httpd_get_mode_header( struct md_HTTP_header** headers ) {
   char const* mode_str = md_find_HTTP_header( headers, HTTP_MODE );
   if( mode_str ) {
      mode_t ret = (mode_t)strtol( mode_str, NULL, 8 );
      if( ret == 0 )
         return -EINVAL;
      if( ret > 0777 )
         return -EINVAL;
      return ret;
   }
   return -ENOENT;
}


// apply any uploaded headers
int httpd_upload_apply_headers( struct md_HTTP_connection_data* md_con_data, uint64_t owner, uint64_t volume, bool do_utime ) {
   struct syndicate_state *state = syndicate_get_state();
   
   mode_t mode = httpd_get_mode_header( md_con_data->headers );
   if( (signed)mode > 0 ) {
      int rc = fs_entry_chmod( state->core, md_con_data->url_path, owner, volume, mode );
      if( rc < 0 ) {
         errorf( "fs_entry_chmod(%s, %o) rc = %d\n", md_con_data->url_path, mode, rc );
         return rc;
      }
   }

   if( do_utime ) {
      struct utimbuf ub;
      ub.actime = currentTimeSeconds();
      ub.modtime = ub.actime;

      int rc = fs_entry_utime( state->core, md_con_data->url_path, &ub, owner, volume );
      if( rc < 0 ) {
         errorf( "fs_entry_utime(%s) rc = %d\n", md_con_data->url_path, rc );
         return rc;
      }
   }

   return 0;
}


static char const* MSG_200 = "OK\n";
static char const* MSG_201 = "CREATED\n";

// HTTP POST/PUT handler--for handling creates and updates.
// only PUT can create
// only POST can update
void httpd_upload_finish( struct md_HTTP_connection_data* md_con_data ) {

   struct syndicate_state *state = syndicate_get_state();
   struct md_HTTP_header** client_headers = md_con_data->headers;
   struct httpd_connection_data* dat = (struct httpd_connection_data*)md_con_data->cls;

   uint64_t owner = 0;
   if( md_con_data->user != NULL )
      owner = md_con_data->user->uid;
   else
      owner = state->conf.owner;

   int fd = dat->fd;
   
   md_con_data->resp = CALLOC_LIST( struct md_HTTP_response, 1 );

   mode_t mode = httpd_get_mode_header( md_con_data->headers );
   if( mode <= 0 )
      mode = ~state->conf.usermask;

   if( fd < 0 ) {
      // no data was ever written.
      // can be mkdir() or truncate()
      if( md_con_data->mode == MD_HTTP_PUT) {
         // make a directory?
         if( strlen(md_con_data->url_path) > 0 && md_con_data->url_path[ strlen(md_con_data->url_path)-1 ] == '/' ) {
            // make a URL for this directory
            char* tmp = CALLOC_LIST( char, strlen(SYNDICATEFS_LOCAL_PROTO) + strlen(state->conf.data_root) + 1 );
            sprintf(tmp, "%s%s", SYNDICATEFS_LOCAL_PROTO, state->conf.data_root );
            char* dir_url = md_fullpath( tmp, md_con_data->url_path, NULL );
            free( tmp );

            int rc = fs_entry_mkdir( state->core, md_con_data->url_path, mode, owner, state->conf.volume );
            if( rc < 0 ) {
               // didn't work
               errorf( "fs_entry_mkdir rc = %d\n", rc );

               char buf[100];
               snprintf(buf, 100, "UPLOAD fs_entry_mkdir rc = %d\n", rc );
               http_io_error_resp( md_con_data->resp, rc, buf );
            }
            else {
               // success!
               md_create_HTTP_response_ram_static( md_con_data->resp, "text/plain", 201, MSG_201, strlen(MSG_201) + 1 );
            }
            free( dir_url );
            return;
         }
         else {
            int rc = fs_entry_truncate( state->core, md_con_data->url_path, 0, owner, state->conf.volume );
            if( rc < 0 ) {
               char buf[100];;
               snprintf(buf, 100, "UPLOAD fs_entry_truncate rc = %d\n", rc );
               http_io_error_resp( md_con_data->resp, rc, buf );
            }
            else {
               md_create_HTTP_response_ram_static( md_con_data->resp, "text/plain", 200, MSG_200, strlen(MSG_200) + 1 );
            }

            return;
         }
      }
      else if( md_con_data->mode == MD_HTTP_POST ) {
         // no data; just apply headers
         int rc = httpd_upload_apply_headers( md_con_data, owner, state->conf.volume, true );
         if( rc < 0 ) {
            char buf[100];
            snprintf(buf, 100, "UPLOAD httpd_upload_apply_headers rc = %d\n", rc );
            http_io_error_resp( md_con_data->resp, rc, buf );
         }
         else {
            md_create_HTTP_response_ram_static( md_con_data->resp, "text/plain", 200, MSG_200, strlen(MSG_200) );
         }
      }
      return;
   }
   else {
      lseek( fd, 0, SEEK_SET );
   }

   struct stat sb;
   int rc = fstat( fd, &sb );
   if( rc != 0 ) {
      rc = -errno;
      errorf( "fstat rc = %d\n", rc );

      char buf[100];
      snprintf( buf, 100, "UPLOAD fstat rc = %d\n", rc );
      http_io_error_resp( md_con_data->resp, rc, buf );
      return;
   }


   uint64_t start_range = 0;
   uint64_t end_range = 0;
   off_t size = sb.st_size;
   
   // was this a byte-range request?
   for( int i = 0; client_headers[i] != NULL; i++ ) {
      if( strcasecmp( client_headers[i]->header, "Content-Range" ) == 0 ) {
         if( parse_byterange( client_headers[i]->value, &start_range, &end_range ) ) {
            size = end_range - start_range + 1;
            break;
         }
      }
   }

   int err = 0;
   struct fs_file_handle* fh = NULL;

   // if this is a post, then open for writing
   if( md_con_data->mode == MD_HTTP_POST ) {
      fh = fs_entry_open( state->core, md_con_data->url_path, NULL, owner, state->conf.volume, O_WRONLY, mode, &err );
   }
   // if this is a put, then create
   else if( md_con_data->mode == MD_HTTP_PUT ) {
      char* proto_prefix = md_prepend( SYNDICATEFS_LOCAL_PROTO, state->conf.data_root, NULL );
      char* local_url = md_fullpath( proto_prefix, md_con_data->url_path, NULL );
      free( proto_prefix );

      fh = fs_entry_create( state->core, md_con_data->url_path, local_url, owner, state->conf.volume, mode, &err );

      free( local_url );
   }
   
   if( fh == NULL ) {
      // could not open
      errorf( "fs_entry_open rc = %d\n", err );

      char buf[100];
      snprintf( buf, 100, "UPLOAD fs_entry_open rc = %d\n", err );
      http_io_error_resp( md_con_data->resp, err, buf );

      close( fd );
      return;
   }

   // apply the mode (but not utime) headers
   rc = httpd_upload_apply_headers( md_con_data, owner, state->conf.volume, false );
   if( rc < 0 ) {
      errorf( "http_upload_apply_headers rc = %d\n", rc );

      char buf[100];
      snprintf( buf, 100, "UPLOAD http_upload_apply_headers rc = %d\n", rc );
      http_io_error_resp( md_con_data->resp, rc, buf );

      close( fd );
      fs_entry_close( state->core, fh );
      free( fh );
      return;
   }
   
   ssize_t nw = fs_entry_write( state->core, fh, fd, size, start_range );
   if( nw < 0 ) {
      // some error
      errorf( "fs_entry_write rc = %zd\n", nw );

      char buf[100];
      snprintf( buf, 100, "UPLOAD fs_entry_write rc = %zd\n", nw );
      http_io_error_resp( md_con_data->resp, nw, buf );
   }

   else if( nw != size ) {
      // not everything wrote
      errorf( "fs_entry_write: wrote %zd; expected %" PRId64 "\n", nw, size );

      char buf[200];
      snprintf( buf, 200, "UPLOAD fs_entry_write: wrote %zd; expected %" PRId64 "\n", nw, size );
      http_io_error_resp( md_con_data->resp, 413, buf );
   }

   else {
      if( md_con_data->mode == MD_HTTP_POST )
         md_create_HTTP_response_ram_static( md_con_data->resp, "text/plain", 200, MSG_200, strlen(MSG_200) + 1 );
      else if( md_con_data->mode == MD_HTTP_PUT )
         md_create_HTTP_response_ram_static( md_con_data->resp, "text/plain", 201, MSG_201, strlen(MSG_201) + 1 );
   }

   fs_entry_close( state->core, fh );
   free( fh );
   close( fd );      // this removes the data

   return;
}

// DELETE handler
// NOTE: depth is ignored here (this isn't WebDAV)
struct md_HTTP_response* httpd_HTTP_DELETE_handler( struct md_HTTP_connection_data* md_con_data, int depth ) {
   struct syndicate_state *state = syndicate_get_state();

   uint64_t owner = 0;
   if( md_con_data->user != NULL )
      owner = md_con_data->user->uid;
   else
      owner = state->conf.owner;

   struct md_HTTP_response* resp = CALLOC_LIST( struct md_HTTP_response, 1 );
   
   struct stat sb;
   int rc = fs_entry_stat( state->core, md_con_data->url_path, &sb, owner, state->conf.volume );
   if( rc < 0 ) {
      // can't read
      char buf[100];
      snprintf(buf, 100, "DELETE fs_entry_stat rc = %d\n", rc );
      http_io_error_resp( resp, rc, buf );
      return resp;
   }

   // file? just unlink
   if( S_ISREG( sb.st_mode ) ) {
      rc = fs_entry_versioned_unlink( state->core, md_con_data->url_path, -1, owner, state->conf.volume );
      if( rc < 0 ) {
         // failed
         char buf[100];
         snprintf(buf, 100, "DELETE fs_entry_versioned_unlink rc = %d\n", rc );
         http_io_error_resp( resp, rc, buf );
      }
      else {
         md_create_HTTP_response_ram_static( resp, "text/plain", 200, MSG_200, strlen(MSG_200) );
      }
      
      return resp;
   }
   // directory? rmdir
   else {
      rc = fs_entry_rmdir( state->core, md_con_data->url_path, owner, state->conf.volume );
      if( rc < 0 ) {
         // failed
         char buf[100];
         snprintf(buf, 100, "DELETE fs_entry_rmdir rc = %d\n", rc );
         http_io_error_resp( resp, rc, buf );
      }
      else {
         md_create_HTTP_response_ram_static( resp, "text/plain", 200, MSG_200, strlen(MSG_200) );
      }
      
      return resp;
   }
}

// cleanup
void httpd_HTTP_cleanup(struct MHD_Connection *connection, void *con_cls, enum MHD_RequestTerminationCode term) {
   free( con_cls );
}


void usage( char const* name ) {
   errorf("Usage: %s [-c CONF_FILE] [-m MS_URL] [-u USERNAME] [-p PASSWORD] [-v VOLUME] [-g GATEWAY_NAME] [-P PORTNUM] [-G GATEWAY_PKEY] [-V VOLUME_PUBKEY] [-S TLS_PKEY] [-C TLS_CERT] [-f]\n", name );
   exit(1);
}


void quit_sigint( int param ) {
   g_running = 0;
   md_stop_HTTP( &g_http );
}

void quit_sigquit( int param ) {
   g_running = 0;
   md_stop_HTTP( &g_http );
}

void quit_sigterm( int param ) {
   g_running = 0;
   md_stop_HTTP( &g_http );
}

// daemon execution starts here!
int main( int argc, char** argv ) {

   int c;
   char* config_file = (char*)CLIENT_DEFAULT_CONFIG;
   int portnum = 0;
   bool foreground = false;
   
   char* logfile = NULL;
   char* pidfile = NULL;

   struct md_HTTP syndicate_http;
   char* username = NULL;
   char* password = NULL;
   char* volume_name = NULL;
   char* ms_url = NULL;
   char* gateway_name = NULL;
   char* volume_pubkey_path = NULL;
   char* gateway_pkey_path = NULL;
   char* tls_pkey_path = NULL;
   char* tls_cert_path = NULL;
  
   static struct option syndicate_options[] = {
      {"config-file",     required_argument,   0, 'c'},
      {"volume-name",     required_argument,   0, 'v'},
      {"gateway",         required_argument,   0, 'g'},
      {"username",        required_argument,   0, 'u'},
      {"password",        required_argument,   0, 'p'},
      {"port",            required_argument,   0, 'P'},
      {"foreground",      no_argument,         0, 'f'},
      {"MS",              required_argument,   0, 'm'},
      {"volume-pubkey",   required_argument,   0, 'V'},
      {"gateway-pkey",    required_argument,   0, 'G'},
      {"tls-pkey",        required_argument,   0, 'S'},
      {"tls-cert",        required_argument,   0, 'C'},
      {0, 0, 0, 0}
   };

   int opt_index = 0;
   
   while((c = getopt_long(argc, argv, "c:v:u:p:P:fm:V:G:S:C:", syndicate_options, &opt_index)) != -1) {
      switch( c ) {
         case 'v': {
            volume_name = optarg;
            break;
         }
         case 'c': {
            config_file = optarg;
            break;
         }
         case 'g': {
            gateway_name = optarg;
            break;
         }
         case 'u': {
            username = optarg;
            break;
         }
         case 'p': {
            password = optarg;
            break;
         }
         case 'P': {
            portnum = strtol(optarg, NULL, 10);
            break;
         }
         case 'm': {
            ms_url = optarg;
            break;
         }
         case 'f': {
            foreground = true;
            break;
         }
         case 'V': {
            volume_pubkey_path = optarg;
            break;
         }
         case 'G': {
            gateway_pkey_path = optarg;
            break;
         }
         case 'S': {
            tls_pkey_path = optarg;
            break;
         }
         case 'C': {
            tls_cert_path = optarg;
            break;
         }
         default: {
            break;
         }
      }
   }

   int rc = syndicate_init( config_file, &syndicate_http, portnum, ms_url, volume_name, gateway_name, username, password, volume_pubkey_path, gateway_pkey_path, tls_pkey_path, tls_cert_path );
   if( rc != 0 )
      exit(1);
   
   struct md_syndicate_conf* conf = syndicate_get_conf();
   if( portnum == 0 )
      portnum = conf->httpd_portnum;

   // create our HTTP server
   memset( &g_http, 0, sizeof(g_http) );
   
   md_HTTP_init( &g_http, MD_HTTP_TYPE_STATEMACHINE | MHD_USE_DEBUG | MHD_USE_POLL, conf );
   md_HTTP_authenticate( g_http, httpd_HTTP_authenticate );
   md_HTTP_connect( g_http, httpd_HTTP_connect );
   md_HTTP_GET( g_http, httpd_HTTP_GET_handler );
   md_HTTP_HEAD( g_http, httpd_HTTP_HEAD_handler );
   md_HTTP_DELETE( g_http, httpd_HTTP_DELETE_handler );
   md_HTTP_POST_iterator( g_http, httpd_upload_iterator );
   md_HTTP_PUT_iterator( g_http, httpd_upload_iterator );
   md_HTTP_POST_finish( g_http, httpd_upload_finish );
   md_HTTP_PUT_finish( g_http, httpd_upload_finish );
   md_HTTP_close( g_http, httpd_HTTP_cleanup );

   rc = md_start_HTTP( &g_http, portnum );
   if( rc < 0 ) {
      errorf( "md_HTTP_start on %d rc = %d\n", portnum, rc );
      exit(1);
   }

   signal( SIGINT, quit_sigint );
   signal( SIGTERM, quit_sigterm );
   signal( SIGQUIT, quit_sigquit );
   
   g_running = true;

   if( !foreground ) {
      // daemonize
      rc = md_daemonize( logfile, pidfile, NULL );
      if( rc < 0 ) {
         errorf( "md_daemonize rc = %d\n", rc );
         exit(1);
      }
   }
   else {
      // idle
      while( g_running ) {
         sleep(1);
      }
   }


   md_stop_HTTP( &syndicate_http );
   md_free_HTTP( &syndicate_http );
   
   syndicate_destroy();
   
   return 0;
}
