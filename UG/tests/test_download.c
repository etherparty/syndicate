/*
   Copyright 2011 Jude Nelson

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

#include "test_download.h"

pthread_t _threads[NUM_THREADS];
struct test_thread_args _args[NUM_THREADS];
uid_t _user = 12346;
gid_t _vol = 1;


void* thread_main( void* arg ) {
   struct test_thread_args* args = (struct test_thread_args*)arg;
   int thread_id = args->id;
   struct download* dh = args->dh;
   
   char download_dest_path[100];
   sprintf(download_dest_path, "/tmp/test_download.out.%d", thread_id );
   
   FILE* download_output = fopen( download_dest_path, "w" );
   char download_buf[4097];
   
   // open the download
   struct download_stream* ds = download_open( dh );
   
   ssize_t num_read = 0;
   off_t offset = 0;
   
   do {
      memset( download_buf, 0, 4097 );
      
      // read from the downlaod
      num_read = download_read( ds, download_buf, 4096, offset, true );
      
      if( num_read == 0 )
         break;
      
      if( num_read < 0 ) {
         printf("Thread %d: download_read rc = %ld\n", thread_id, num_read );
         fflush(stdout);
         break;
      }
      
      fwrite( download_buf, 1, num_read, download_output );
      
      offset += num_read;
   } while( num_read > 0 );
   
   fclose( download_output );
   
   // close the download
   int rc = download_close( ds );
   if( rc == DOWNLOAD_RC_DOWNLOAD_DESTROYED ) {
      printf("Thread %d freed download\n", thread_id);
      fflush(stdout);
      
      free( dh );
      dh = NULL;
   }
   
   return NULL;
}


int main( int argc, char** argv ) {
   
   struct md_syndicate_conf conf;
   md_read_conf((char*)"/etc/syndicate/syndicate-client.conf", &conf);
   
   md_init( &conf, NULL );
   downloader_init( RAM_CUTOFF, DISK_CUTOFF, false );
   
   conf.max_ram_filesize = RAM_CUTOFF;
   conf.max_disk_filesize = DISK_CUTOFF;
   
   // make a download
   struct download* dh = (struct download*)calloc( sizeof(struct download), 1 );
   
   int rc = init_download( dh, (char*)"http://www.cs.princeton.edu/~jcnelson/index.html", (char*)"/tmp/test_download.XXXXXX", NULL );
   
   // launch workers
   for( int i = 0; i < NUM_THREADS; i++ ) {
      _args[i].id = i+1;
      _args[i].dh = dh;
      pthread_create( &_threads[i], NULL, thread_main, &_args[i] );
   }
   
   printf("main() start download\n");
   fflush(stdout);
   
   rc = download_file( dh );
   
   printf("main() finish download, rc = %d\n", rc);
   fflush(stdout);
   
   // join workers
   for( int i = 0; i < NUM_THREADS; i++ ) {
      pthread_join( _threads[i], NULL );
   }
   
   rc = download_cancel( dh );
   if( rc == DOWNLOAD_RC_DOWNLOAD_DESTROYED ) {
      printf("main() freed download\n");
      fflush(stdout);
      
      free( dh );
      dh = NULL;
   }
   
   
   return 0;
}