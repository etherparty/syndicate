/*
   Copyright 2013 The Trustees of Princeton University
   All Rights Reserved
*/

/*
 * Utility functions (debugging, etc)
 */
 
#include "util.h"

int _DEBUG = 1;

/* Converts a hex character to its integer value */
char from_hex(char ch) {
  return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

/* Converts an integer value to its hex character*/
char to_hex(char code) {
  static char hex[] = "0123456789abcdef";
  return hex[code & 15];
}


// concatenate two paths
char* fullpath( char* root, const char* path ) {
   char delim = 0;
   
   int len = strlen(path) + strlen(root) + 1;
   if( root[strlen(root)-1] != '/' ) {
      len++;
      delim = '/';
   }
   
   char* ret = (char*)malloc( len );
   
   if( ret == NULL )
      return NULL;
   
   bzero(ret, len);
      
   strcpy( ret, root );
   if( delim != 0 ) {
      ret[strlen(ret)] = '/';
   }
   strcat( ret, path );
   return ret;
}

/* Allocate a path from the given path, with a '/' added to the end */
char* dir_path( const char* path ) {
   int len = strlen(path);
   char* ret = NULL;
   
   if( path[len-1] != '/' ) {
      ret = (char*)malloc( len + 2 );
      if( ret == NULL )
         return NULL;
         
      sprintf( ret, "%s/", path );
   }
   else {
      ret = (char*)malloc( len + 1 );
      if( ret == NULL )
         return NULL;
         
      strcpy( ret, path );
   }
   
   return ret;
}


/*
 * Get the current time in seconds since the epoch, local time
 */
int64_t currentTimeSeconds() {
   struct timeval tp;
   gettimeofday(&tp, NULL);
   return tp.tv_sec;
}


// get the current time in milliseconds
int64_t currentTimeMillis() {
   struct timespec ts;
   clock_gettime( CLOCK_REALTIME, &ts );
   int64_t ts_sec = (int64_t)ts.tv_sec;
   int64_t ts_nsec = (int64_t)ts.tv_nsec;
   return (ts_sec * 1000) + (ts_nsec / 1000000);
}

// get the current time in microseconds
int64_t currentTimeMicros() {
   struct timespec ts;
   clock_gettime( CLOCK_REALTIME, &ts );
   int64_t ts_sec = (int64_t)ts.tv_sec;
   int64_t ts_nsec = (int64_t)ts.tv_nsec;
   return (ts_sec * 1000000) + (ts_nsec / 1000);
}

double currentTimeMono() {
   struct timespec ts;
   clock_gettime( CLOCK_MONOTONIC, &ts );
   return (ts.tv_sec * 1e9) + (double)ts.tv_nsec;
}

/*
 * Get the user's umask
 */
mode_t get_umask() {
  mode_t mask = umask(0);
  umask(mask);
  return mask;
}


// calculate the sha-256 hash of something.
// caller must free the hash buffer.
unsigned char* sha256_hash_data( char const* input, size_t len ) {
   unsigned char* obuf = (unsigned char*)calloc( SHA256_DIGEST_LENGTH, 1 );
   NULLCHECK( obuf, 0 );
   SHA256( (unsigned char*)input, strlen(input), obuf );
   return obuf;
}

// calculate the sha-256 hash of a string
unsigned char* sha256_hash( char const* input ) {
   return sha256_hash_data( input, strlen(input) );
}

// duplicate a sha1
unsigned char* sha256_dup( unsigned char const* sha256 ) {
   unsigned char* ret = (unsigned char*)calloc( SHA256_DIGEST_LENGTH, 1 );
   NULLCHECK( ret, 0 );
   memcpy( ret, sha256, SHA_DIGEST_LENGTH );
   return ret;
}

// compare two SHA256 hashes
int sha256_cmp( unsigned char const* sha256_1, unsigned char const* sha256_2 ) {
   if( sha256_1 == NULL )
      return -1;
   if( sha256_2 == NULL )
      return 1;
   
   return strncasecmp( (char*)sha256_1, (char*)sha256_2, SHA_DIGEST_LENGTH );
}


// make a sha-256 hash printable
char* sha256_printable( unsigned char const* sha256 ) {
   char* ret = (char*)calloc( sizeof(char) * (2 * SHA256_DIGEST_LENGTH + 1), 1 );
   NULLCHECK( ret, 0 );

   char buf[3];
   for( int i = 0; i < SHA256_DIGEST_LENGTH; i++ ) {
      sprintf(buf, "%02x", sha256[i] );
      ret[2*i] = buf[0];
      ret[2*i + 1] = buf[1];
   }
   
   return ret;
}

// make a printable sha-1 hash into data
unsigned char* sha256_data( char const* sha256_printed ) {
   unsigned char* ret = (unsigned char*)calloc( SHA256_DIGEST_LENGTH, 1 );
   
   for( size_t i = 0; i < strlen( sha256_printed ); i+=2 ) {
      unsigned char tmp1 = (unsigned)from_hex( sha256_printed[i] );
      unsigned char tmp2 = (unsigned)from_hex( sha256_printed[i+1] );
      ret[i >> 1] = (tmp1 << 4) | tmp2;
   }
   
   return ret;
}


// hash a file
unsigned char* sha256_file( char const* path ) {
   FILE* f = fopen( path, "r" );
   if( !f ) {
      return NULL;
   }
   
   SHA256_CTX context;
   SHA256_Init( &context );
   unsigned char* new_checksum = (unsigned char*)calloc( SHA256_DIGEST_LENGTH, 1 );
   unsigned char buf[32768];
   
   ssize_t num_read;
   while( !feof( f ) ) {
      num_read = fread( buf, 1, 32768, f );
      if( ferror( f ) ) {
         errorf("sha256_file: I/O error reading %s\n", path );
         SHA256_Final( new_checksum, &context );
         free( new_checksum );
         fclose( f );
         return NULL;
      }
      
      SHA256_Update( &context, buf, num_read );
   }
   fclose(f);
   
   SHA256_Final( new_checksum, &context );
   
   return new_checksum;
}

// hash a file, given its descriptor 
unsigned char* sha256_fd( int fd ) {
   SHA256_CTX context;
   SHA256_Init( &context );
   unsigned char* new_checksum = (unsigned char*)calloc( SHA256_DIGEST_LENGTH, 1 );
   unsigned char buf[32768];
   
   ssize_t num_read = 1;
   while( num_read > 0 ) {
      num_read = read( fd, buf, 32768 );
      if( num_read < 0 ) {
         errorf("sha256_file: I/O error reading FD %d, errno=%d\n", fd, -errno );
         SHA256_Final( new_checksum, &context );
         free( new_checksum );
         return NULL;
      }
      
      SHA256_Update( &context, buf, num_read );
   }
   
   SHA256_Final( new_checksum, &context );
   
   return new_checksum;
}


// make a directory sanely
int mkdir_sane( char* dirpath ) {
   DIR* dir = opendir( dirpath );
   if( dir == NULL && errno == ENOENT ) {
      // the directory does not exist, so try making it
      int old = umask( ~(0700) );
      int rc = mkdir( dirpath, 0700 );
      umask( old );
      if( rc != 0 && errno != EEXIST ) {
         return -errno;     // couldn't make the directory, and it wasn't because it already existed.
      }
   }
   else if( dir == NULL ) {
      // some other error
      return -errno;
   }
   else {
      // close the directory; we can write to it
      closedir( dir );
      return 0;
   }
   return 0;
}


// remove a directory sanely.
// rf = true means that the function will recursively remove all files
int rmdir_sane( char* dirpath, bool rf ) {
   //logerr("ERR: rmdir_sane not implemented\n");
   return 0;
}

// check and see whether or not a directory exists
int dir_exists( char* dirpath ) {
   DIR* dir = opendir( dirpath );
   if( dir == NULL )
      // doesn't exist or we can't read it
      return -errno;
   
   closedir( dir );
   return 0;      // it existed when we checked
}


// allocate a string that contains the directory component of a path.
// if a well-formed path is given, then a string ending in a / is returned
char* dirname( char* path, char* dest ) {
   int delim_i = strlen(path) - 1;
   for( ; delim_i > 0; delim_i-- ) {
      if( path[delim_i] == '/' )
         break;
   }
   memset( dest, 0, MAX(delim_i, 1) + 1 );
   strncpy( dest, path, MAX(delim_i, 1) );
   
   return dest;
}


// load a file into RAM
// return a pointer to the bytes.
// set the size.
char* load_file( char* path, size_t* size ) {
   struct stat statbuf;
   int rc = stat( path, &statbuf );
   if( rc != 0 )
      return NULL;
   
   char* ret = (char*)calloc( statbuf.st_size, 1 );
   if( ret == NULL )
      return NULL;
   
   FILE* f = fopen( path, "r" );
   if( !f ) {
      free( ret );
      return NULL;
   }
   
   *size = fread( ret, 1, statbuf.st_size, f );
   fclose( f );
   return ret;
}

//////// courtesy of http://www.geekhideout.com/urlcode.shtml  //////////


/* Returns a url-encoded version of str */
/* IMPORTANT: be sure to free() the returned string after use */
char *url_encode(char const *str, size_t len) {
  char *pstr = (char*)str;
  char *buf = (char*)calloc(len * 3 + 1, 1);
  char *pbuf = buf;
  while (*pstr) {
    if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~') {
      *pbuf++ = *pstr;
    }
    else if (*pstr == ' ') {
      *pbuf++ = '+';
    }
    else {
      *pbuf++ = '%';
      *pbuf++ = to_hex(*pstr >> 4);
      *pbuf++ = to_hex(*pstr & 15);
    }
    pstr++;
  }
  *pbuf = '\0';
  return buf;
}

/* Returns a url-decoded version of str */
/* IMPORTANT: be sure to free() the returned string after use */
char *url_decode(char const *str, size_t* len) {
  char *pstr = (char*)str, *buf = (char*)calloc(strlen(str) + 1, 1), *pbuf = buf;
  size_t l = 0;
  while (*pstr) {
    if (*pstr == '%') {
      if (pstr[1] && pstr[2]) {
        *pbuf++ = from_hex(pstr[1]) << 4 | from_hex(pstr[2]);
        pstr += 2;
        l++;
      }
    } 
    else if (*pstr == '+') { 
      *pbuf++ = ' ';
      l++;
    } 
    else {
      *pbuf++ = *pstr;
      l++;
    }
    pstr++;
  }
  *pbuf = '\0';
  l++;
  if( len != NULL ) {
     *len = l;
  }
  
  return buf;
}

//////////////////////////////////////////////////////////////////////////


// does a string match a pattern?
int reg_match(const char *string, char const *pattern) {
   int status;
   regex_t re;

   if( regcomp(&re, pattern, REG_EXTENDED|REG_NOSUB) != 0) {
      return 0;
   }
   status = regexec(&re, string, (size_t)0, NULL, 0);
   regfree(&re);
   if (status != 0) {
      return 0;
   }
   return 1;
}


// curl transfer constructor
CURLTransfer::CURLTransfer( int num_threads ) {
   this->curlm_handles = (CURLM**)malloc( sizeof(CURLM*) * num_threads );
   this->curlm_running = (int*)calloc( sizeof(int) * num_threads, 1 );
   
   for( int i = 0; i < num_threads; i++ ) {
      this->curlm_handles[i] = curl_multi_init();
   }
   
   this->num_handles = num_threads;
}

// curl transfer destructor
CURLTransfer::~CURLTransfer() {
   for( int i = 0; i < this->num_handles; i++ ) {
      for( list<CURL*>::iterator itr = this->added.begin(); itr != this->added.end(); itr++ ) {
         curl_multi_remove_handle( this->curlm_handles[i], *itr );
      }
      curl_multi_cleanup( this->curlm_handles[i] );
   }  
   
   free( this->curlm_handles );
   free( this->curlm_running );
}

// add a curl handle
int CURLTransfer::add_curl_easy_handle( int thread_no, CURL* handle ) {
   int rc = curl_multi_add_handle( this->curlm_handles[thread_no], handle );
   if( rc == CURLE_OK ) {
      this->added.push_back( handle );
   }
   return rc;
}


// remove a curl handle
int CURLTransfer::remove_curl_easy_handle( int thread_no, CURL* handle ) {
   int rc = curl_multi_remove_handle( this->curlm_handles[thread_no], handle );
   if( rc == CURLE_OK ) {
      bool erased = false;
      
      do {
         erased = false;
         
         for( list<CURL*>::iterator itr = this->added.begin(); itr != this->added.end(); itr++ ) {
            if( *itr == handle ) {
               this->added.erase( itr );
               erased = true;
               break;
            }
         }
      } while( erased );
   }
   
   return rc;
}

// process curl (do polling)
int CURLTransfer::process_curl( int thread_no ) {
   int rc = 0;
   
   // process downloads
   struct timeval timeout;
   memset( &timeout, 0, sizeof(timeout) );
   
   fd_set fdread;
   fd_set fdwrite;
   fd_set fdexcep;
   int maxfd = -1;

   long curl_timeout = -1;

   FD_ZERO(&fdread);
   FD_ZERO(&fdwrite);
   FD_ZERO(&fdexcep);
   
   // how long until we should call curl_multi_perform?
   rc = curl_multi_timeout(this->curlm_handles[ thread_no ], &curl_timeout);
   if( rc != 0 ) {
      errorf("CURLTransfer(%d)::process_curl: curl_multi_timeout rc = %d\n", thread_no, rc );
      return -1;
   }
   
   if( curl_timeout < 0 ) {
      // no timeout given; wait a default amount
      timeout.tv_sec = CURL_DEFAULT_SELECT_SEC;
      timeout.tv_usec = CURL_DEFAULT_SELECT_USEC;
   }
   else {
      timeout.tv_sec = 0;
      timeout.tv_usec = (curl_timeout % 1000) * 1000;
   }
   
   // get FDs
   rc = curl_multi_fdset(this->curlm_handles[ thread_no ], &fdread, &fdwrite, &fdexcep, &maxfd);
   
   if( rc != 0 ) {
      errorf("CURLTransfer(%d)::process_curl: curl_multi_fdset rc = %d\n", thread_no, rc );
      return -1;
   }

   // find out which FDs are ready
   rc = select(maxfd+1, &fdread, &fdwrite, &fdexcep, &timeout);
   if( rc < 0 ) {
      // we have a problem
      errorf("CURLTransfer(%d)::process_curl: select rc = %d, errno = %d\n", thread_no, rc, -errno );
      return -1;
   }
   
   // let CURL do its thing
   do {
      rc = curl_multi_perform( this->curlm_handles[ thread_no ], &this->curlm_running[ thread_no ] );
      if( rc == CURLM_OK )
         break;
   } while( rc != CURLM_CALL_MULTI_PERFORM );
   
   // process messages
   return 0;
}

// get the next curl message
CURLMsg* CURLTransfer::get_next_curl_msg( int thread_no ) {
   int num_msgs;
   return curl_multi_info_read( this->curlm_handles[thread_no], &num_msgs );
}


int timespec_cmp( struct timespec* t1, struct timespec* t2 ) {
   if( t1->tv_sec < t2->tv_sec )
      return -1;
   else if( t1->tv_sec > t2->tv_sec )
      return 1;
   else {
      if( t1->tv_nsec < t2->tv_nsec )
         return -1;
      else if( t1->tv_nsec > t2->tv_nsec )
         return 1;
      else
         return 0;
   }
}

// random number generator
static uint32_t Q[4096], c=362436; /* choose random initial c<809430660 and */
                                         /* 4096 random 32-bit integers for Q[]   */
uint32_t CMWC4096(void) {
   uint64_t t, a=18782LL;
   static uint32_t i=4095;
   uint32_t x,r=0xfffffffe;
   
   i=(i+1)&4095;
   t=a*Q[i]+c;
   c=(t>>32);
   x=t+c;
   
   if( x < c ) {
      x++;
      c++;
   }
   
   return(Q[i]=r-x);
}


int util_init(void) {
   // random number init
   int rfd = open("/dev/urandom", O_RDONLY );
   if( rfd < 0 ) {
      return -errno;
   }

   ssize_t nr = read( rfd, Q, 4096 * sizeof(uint32_t) );
   if( nr < 0 ) {
      return -errno;
   }
   if( nr != 4096 ) {
      return -ENODATA;
   }

   close( rfd );
   return 0;
}

