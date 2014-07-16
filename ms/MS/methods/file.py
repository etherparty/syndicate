#!/usr/bin/env python

"""
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
"""

import MS
from MS.volume import Volume
from MS.entry import *
from storage import storage
import storage.storagetypes as storagetypes

from common import *
import common.msconfig as msconfig

import random
import os
import errno

import logging

# ----------------------------------
def make_ms_reply( volume, error ):
   """
   Generate a pre-populated ms_reply protobuf.
   Include the volume's certificate versioning information.
   """
   
   reply = ms_pb2.ms_reply()

   reply.volume_version = volume.version
   reply.cert_version = volume.cert_version
   reply.error = error

   return reply

# ----------------------------------
def file_update_init_response( volume ):
   """
   Create a protobuf ms_reply structure, and populate it with preliminary details.
   """
   reply = make_ms_reply( volume, 0 )
   reply.listing.status = 0
   reply.listing.ftype = 0
   reply.error = 0
   reply.signature = ""

   return reply
   
   
# ----------------------------------
def file_update_complete_response( volume, reply ):
   """
   Sign a protobuf ms_reply structure, using the Volume's private key.
   """
   reply.signature = ""
   reply_str = reply.SerializeToString()
   sig = volume.sign_message( reply_str )
   reply.signature = sig
   reply_str = reply.SerializeToString()
   
   return reply_str


# ----------------------------------
def file_update_get_attrs( entry_dict, attr_list ):
   """
   Get a set of required attriutes.
   Return None if at least one is missing.
   """
   ret = {}
   for attr_name in attr_list:
      if not entry_dict.has_key(attr_name):
         return None 
      
      ret[attr_name] = entry_dict[attr_name]
   
   return ret

# ----------------------------------
def _resolve( owner_id, volume, file_id, file_version, write_nonce ):
   """
   Read file and listing of the given file_id.
   """

   file_memcache = MSEntry.Read( volume, file_id, memcache_keys_only=True )
   file_data = storagetypes.memcache.get( file_memcache )
   listing = MSEntry.ListDir( volume, file_id )

   all_ents = None
   file_fut = None
   error = 0
   need_refresh = True
   file_data_fut = None
   
   # do we need to consult the datastore?
   if file_data is None:
      logging.info( "file %s not cached" % file_id )
      
      file_data_fut = MSEntry.Read( volume, file_id, futs_only=True )
      all_futs = MSEntry.FlattenFuture( file_data_fut )
      
      storagetypes.wait_futures( all_futs )
      
      file_data = MSEntry.FromFuture( file_data_fut )
      
      if file_data != None:
         
         cacheable = {
            file_memcache: file_data
         }
         
         logging.info( "cache file %s (%s)" % (file_id, file_data) )
         storagetypes.memcache.set_multi( cacheable )
 
   if file_data is not None:
      # got data...
      logging.info("%s has type %s" % (file_data.name, file_data.ftype))
      
      # do we need to actually send this?
      if file_data.version == file_version and file_data.write_nonce == write_nonce:
         need_refresh = False

      else:
         if file_data.ftype == MSENTRY_TYPE_DIR:
            if listing != None:
               all_ents = [file_data] + listing
            else:
               all_ents = [file_data]
         
            logging.info("listing of %s: %s" % (file_data.file_id, listing))
            
         else:
            all_ents = [file_data]

   # check security
   error = -errno.EACCES
   
   if file_data is None:
      # not found
      error = -errno.ENOENT 
      
   elif file_data.ftype == MSENTRY_TYPE_DIR:
      # directory. check permissions
      if file_data.owner_id == owner_id or (file_data.mode & 0055) != 0:
         # readable
         error = 0

   elif file_data.ftype == MSENTRY_TYPE_FILE:
      # file.  check permissions
      if file_data.owner_id == owner_id or (file_data.mode & 0044) != 0:
         # readable
         error = 0

   reply = make_ms_reply( volume, error )
   
   if error == 0:
      # all is well.
      
      reply.listing.ftype = file_data.ftype
      
      # modified?
      if not need_refresh:
         reply.listing.status = ms_pb2.ms_listing.NOT_MODIFIED
         
      else:
         reply.listing.status = ms_pb2.ms_listing.NEW

         for ent in all_ents:
            ent_pb = reply.listing.entries.add()
            ent.protobuf( ent_pb )
         
         logging.info("Resolve %s: Serve back: %s" % (file_id, all_ents))
   
   else:
      reply.listing.ftype = 0
      reply.listing.status = ms_pb2.ms_listing.NONE
      
   # sign and deliver
   return (error, file_update_complete_response( volume, reply ))
   

# ----------------------------------
def file_resolve( gateway, volume, file_id, file_version_str, write_nonce_str ):
   """
   Resolve a (volume_id, file_id, file_vesion, write_nonce) to file metadata.
   This is part of the File Metadata API, so it takes strings for file_version_str and write_nonce_str.
   If these do not parse to Integers, then this method fails (returns None).
   """
   file_version = -1
   write_nonce = -1
   try:
      file_version = int(file_version_str)
      write_nonce = int(write_nonce_str)
   except:
      return None 
   
   logging.info("resolve /%s/%s/%s/%s" % (volume.volume_id, file_id, file_version, write_nonce) )
   
   owner_id = msconfig.GATEWAY_ID_ANON
   if gateway != None:
      owner_id = gateway.owner_id
      
   rc, reply = _resolve( owner_id, volume, file_id, file_version, write_nonce )
   
   logging.info("resolve /%s/%s/%s/%s rc = %d" % (volume.volume_id, file_id, file_version, write_nonce, rc) )
   
   return reply


# ----------------------------------
def file_create( reply, gateway, volume, update ):
   """
   Create a file or directory, using the given ms_update structure.
   Add the new entry to the given ms_reply protobuf, containing data from the created MSEntry.
   This is part of the File Metadata API.
   """
   attrs = MSEntry.unprotobuf_dict( update.entry )
   
   logging.info("create /%s/%s (%s)" % (attrs['volume_id'], attrs['file_id'], attrs['name'] ) )
   
   rc, ent = MSEntry.Create( gateway.owner_id, volume, **attrs )
   
   logging.info("create /%s/%s (%s) rc = %s" % (attrs['volume_id'], attrs['file_id'], attrs['name'], rc ) )
   
   # have an entry?
   if ent is not None:
      ent_pb = reply.listing.entries.add()
      ent.protobuf( ent_pb )
   
   return rc


# ----------------------------------
def file_update( reply, gateway, volume, update ):
   """
   Update the metadata records of a file or directory, using the given ms_update structure.
   Add the new entry to the given ms_reply protobuf, containing data from the updated MSEntry.
   This is part of the File Metadata API.
   """
   attrs = MSEntry.unprotobuf_dict( update.entry )
   
   logging.info("update /%s/%s (%s)" % (attrs['volume_id'], attrs['file_id'], attrs['name'] ) )
   
   rc, ent = MSEntry.Update( gateway.owner_id, volume, **attrs )
   
   logging.info("update /%s/%s (%s) rc = %s" % (attrs['volume_id'], attrs['file_id'], attrs['name'], rc ) )
   
   # have an entry 
   if ent is not None:
      ent_pb = reply.listing.entries.add()
      ent.protobuf( ent_pb )
   
   return rc


# ----------------------------------
def file_delete( reply, gateway, volume, update ):
   """
   Delete a file or directory, using the fields of the given ms_update structure.
   This is part of the File Metadata API.
   """
   attrs = MSEntry.unprotobuf_dict( update.entry )
   
   logging.info("delete /%s/%s (%s)" % (attrs['volume_id'], attrs['file_id'], attrs['name'] ) )
   
   rc = MSEntry.Delete( gateway.owner_id, volume, **attrs )
   
   logging.info("delete /%s/%s (%s) rc = %s" % (attrs['volume_id'], attrs['file_id'], attrs['name'], rc ) )
   
   return rc


# ----------------------------------
def file_rename( reply, gateway, volume, update ):
   """
   Rename a file or directory, using the fields of the given ms_update structure.
   This is part of the File Metadata API.
   """
   src_attrs = MSEntry.unprotobuf_dict( update.entry )
   dest_attrs = MSEntry.unprotobuf_dict( update.dest )
   
   logging.info("rename /%s/%s (name=%s, parent=%s) to (name=%s, parent=%s)" % 
                  (src_attrs['volume_id'], src_attrs['file_id'], src_attrs['name'], src_attrs['parent_id'], dest_attrs['name'], dest_attrs['parent_id']) )
   
   rc = MSEntry.Rename( gateway.owner_id, volume, src_attrs, dest_attrs )
   
   logging.info("rename /%s/%s (name=%s, parent=%s) to (name=%s, parent=%s) rc = %s" % 
                  (src_attrs['volume_id'], src_attrs['file_id'], src_attrs['name'], src_attrs['parent_id'], dest_attrs['name'], dest_attrs['parent_id'], rc) )
   
   return rc


# ----------------------------------
def file_chcoord( reply, gateway, volume, update ):
   """
   Change the coordinator of a file, using the fields of the given ms_update structure.
   Add a new entry to the given ms_reply protobuf, containing data from the updated MSEntry.
   This is part of the File Metadata API.
   """
   attrs = MSEntry.unprotobuf_dict( update.entry )
   
   logging.info("chcoord /%s/%s (%s) to %s" % (attrs['volume_id'], attrs['file_id'], attrs['name'], gateway.g_id) )
   
   rc, ent = MSEntry.Chcoord( gateway.owner_id, gateway, volume, **attrs )
   
   logging.info("chcoord /%s/%s (%s) rc = %d" % (attrs['volume_id'], attrs['file_id'], attrs['name'], rc ) )
   
   # have an entry 
   if ent is not None:
      ent_pb = reply.listing.entries.add()
      ent.protobuf( ent_pb )
   
   return rc


# ----------------------------------
def file_update_parse( request_handler ):
   """
   Parse the raw data from the 'ms-metadata-updates' POST field into an ms_updates protobuf.
   Return None on failure.
   """
   
   # will have gotten metadata updates
   updates_field = request_handler.request.POST.get( 'ms-metadata-updates' )

   if updates_field == None:
      # no valid data given (malformed)
      return None

   # extract the data
   data = updates_field.file.read()
   
   # parse it 
   updates_set = ms_pb2.ms_updates()

   try:
      updates_set.ParseFromString( data )
   except:
      return None
   
   return updates_set


# ----------------------------------
def file_update_auth( gateway, volume ):
   """
   Verify whether or not the given gateway (which can be None) is allowed to update (POST) metadata
   in the given volume.
   """
   
   # gateway must be known
   if gateway == None:
      logging.error("Unknown gateway")
      return False
   
   # NOTE: gateways run on behalf of a user, so gateway.owner_id is equivalent to the user's ID.
   
   # this can only be a User Gateway or an Acquisition Gateway
   if gateway.gateway_type != GATEWAY_TYPE_UG and gateway.gateway_type != GATEWAY_TYPE_AG:
      logging.error("Not a UG or RG")
      return False
   
   # if this is an archive, on an AG owned by the same person as the Volume can write to it
   if volume.archive:
      if gateway.gateway_type != GATEWAY_TYPE_AG or gateway.owner_id != volume.owner_id:
         logging.error("Not an AG, or not the Volume owner")
         return False
   
   # if this is not an archive, then the gateway must have CAP_WRITE_METADATA
   elif not gateway.check_caps( GATEWAY_CAP_WRITE_METADATA ):
      logging.error("Write metadata is forbidden to this Gateway")
      return False
   
   # allowed!
   return True


# ----------------------------------
def file_read_auth( gateway, volume ):
   """
   Verify whether or not the given gateway (which can be None) is allowed 
   to read (GET) file metadata from the given volume.
   """
   
   # gateway authentication required?
   if volume.need_gateway_auth() and gateway is None:
      logging.error( "no gateway" )
      return False

   # this must be a User Gateway, if there is a specific gateway
   if gateway is not None and gateway.gateway_type != GATEWAY_TYPE_UG:
      logging.error( "not UG" )
      return False
   
   # this gateway must be allowed to read metadata
   if gateway is not None and not gateway.check_caps( GATEWAY_CAP_READ_METADATA ):
      logging.error( "bad caps: %s" % gateway.caps )
      return False
   
   return True


# ----------------------------------
def file_xattr_get_and_check_xattr_readable( gateway, volume, file_id, xattr_name, caller_is_admin=False ):
   """
   Verify that an extended attribute is readable to the given gateway.
   Return (rc, xattr)
   """
   
   rc = 0
   
   rc, xattr = MSEntryXAttr.ReadXAttr( volume.volume_id, file_id, xattr_name )
   
   if rc != 0:
      return (rc, None)
   
   if xattr is None:
      return (-errno.ENOENT, None)
   
   # get gateway owner ID
   gateway_owner_id = GATEWAY_ID_ANON
   if gateway is not None:
      gateway_owner_id = gateway.owner_id

   # check permissions 
   if not MSEntryXAttr.XAttrReadable( gateway_owner_id, xattr, caller_is_admin ):
      logging.error("XAttr %s not readable by %s" % (xattr_name, gateway_owner_id))
      return (-errno.EACCES, None)
   
   return (0, xattr)


# ----------------------------------
def file_xattr_get_and_check_xattr_writable( gateway, volume, file_id, xattr_name, caller_is_admin=False ):
   """
   Verify that an extended attribute is writable to the given gateway.
   Return (rc, xattr)
   """
   
   rc = 0
   
   rc, xattr = MSEntryXAttr.ReadXAttr( volume.volume_id, file_id, xattr_name )
   
   if xattr is None and rc == -errno.ENODATA:
      # doesn't exist 
      return (0, None)
   
   if rc != 0:
      return (rc, None)
   
   if xattr is None:
      return (-errno.ENOENT, None)
   
   # get gateway owner ID
   gateway_owner_id = GATEWAY_ID_ANON
   if gateway is not None:
      gateway_owner_id = gateway.owner_id
   
   # check permissions 
   if not MSEntryXAttr.XAttrWritable( gateway_owner_id, xattr, caller_is_admin ):
      logging.error("XAttr %s not writable by %s" % (xattr_name, gateway_owner_id))
      return (-errno.EACCES, None)
   
   return (0, xattr)


# ----------------------------------
def file_xattr_get_and_check_msentry_readable( gateway, volume, file_id, caller_is_admin=False ):
   """
   Verify whether or not the given gateway (which can be None) is allowed 
   to read or list an MSEntry's extended attributes.
   """
   
   rc = 0
   
   # get the msentry
   msent = MSEntry.Read( volume, file_id )
   if msent is None:
      # does not exist
      rc = -errno.ENOENT
      
   else:
      # which gateway ID are we using?
      gateway_owner_id = GATEWAY_ID_ANON
      if gateway is not None:
         gateway_owner_id = gateway.owner_id
         
      if not caller_is_admin and msent.owner_id != gateway_owner_id and (msent.mode & 0044) == 0:
         # not readable.
         # don't tell the reader that this entry even exists.
         rc = -errno.ENOENT
         
   if rc != 0:
      msent = None
      
   return (rc, msent)


# ----------------------------------
def file_xattr_get_and_check_msentry_writeable( gateway, volume, file_id, caller_is_admin=False ):
   """
   Verify whether or not the given gateway (which can be None) is allowed 
   to update or delete an MSEntry's extended attributes.
   """
   
   rc = 0
   
   # get the msentry
   msent = MSEntry.Read( volume, file_id )
   if msent == None:
      # does not exist
      rc = -errno.ENOENT
      
   elif not caller_is_admin and msent.owner_id != gateway.owner_id and (msent.mode & 0022) == 0:
      logging.error("MSEntry %s not writable by %s" % (file_id, gateway.owner_id))
      
      # not writeable 
      # if not readable, then say ENOENT instead (don't reveal the existence of a metadata entry to someone who can't read it)
      if msent.owner_id != gateway.owner_id and (msent.mode & 0044) == 0:
         rc = -errno.ENOENT
      else:
         rc = -errno.EACCES

   return (rc, msent)


# ----------------------------------
def file_xattr_getxattr_response( volume, rc, xattr_value ):
   """
   Generate a serialized, signed ms_reply protobuf from
   a getxattr return code (rc) and xattr value.
   """
   
   # create and sign the response 
   reply = file_update_init_response( volume )
   reply.error = rc
   
   if rc == 0:
      reply.xattr_value = xattr_value
   
   return file_update_complete_response( volume, reply )


# ----------------------------------
def file_xattr_listxattr_response( volume, rc, xattr_names ):
   """
   Generate a serialized, signed ms_reply protobuf from
   a listxattr return code (rc) and xattr names list.
   """

   # create and sign the response 
   reply = file_update_init_response( volume )
   reply.error = rc
   
   if rc == 0:
      
      for name in xattr_names:
         reply.xattr_names.append( name )
      
   return file_update_complete_response( volume, reply )


# ----------------------------------
def file_xattr_getxattr( gateway, volume, file_id, xattr_name, caller_is_admin=False ):
   """
   Get the value of the file's extended attributes, subject to access controls.
   This is part of the File Metadata API.
   """
   
   logging.info("getxattr /%s/%s/%s" % (volume.volume_id, file_id, xattr_name) )

   rc, msent = file_xattr_get_and_check_msentry_readable( gateway, volume, file_id, caller_is_admin )
   xattr_value = None
   
   if rc == 0 and msent != None:
      # check xattr readable 
      rc, xattr = file_xattr_get_and_check_xattr_readable( gateway, volume, file_id, xattr_name, caller_is_admin )
      
      if rc == 0:
         # success!
         xattr_value = xattr.xattr_value 
         
         
   logging.info("getxattr /%s/%s/%s rc = %d" % (volume.volume_id, file_id, xattr_name, rc) )

   return file_xattr_getxattr_response( volume, rc, xattr_value )


# ----------------------------------
def file_xattr_listxattr( gateway, volume, file_id, unused=None, caller_is_admin=False ):
   """
   Get the names of a file's extended attributes, subject to access controls.
   This is part of the File Metadata API.
   
   NOTE: unused=None is required for the File Metadata API dispatcher to work.
   """
   
   logging.info("listxattr /%s/%s" % (volume.volume_id, file_id) )

   rc, msent = file_xattr_get_and_check_msentry_readable( gateway, volume, file_id, caller_is_admin )
   xattr_names = []
   
   if rc == 0 and msent != None:
      
      # get gateway owner ID
      gateway_owner_id = GATEWAY_ID_ANON
      if gateway is not None:
         gateway_owner_id = gateway.owner_id
      
      # get the xattr names
      rc, xattr_names = MSEntryXAttr.ListXAttrs( volume, msent, gateway_owner_id, caller_is_admin )
   
   logging.info("listxattr /%s/%s rc = %d" % (volume.volume_id, file_id, rc) )

   return file_xattr_listxattr_response( volume, rc, xattr_names )


# ----------------------------------
def file_xattr_setxattr( reply, gateway, volume, update, caller_is_admin=False ):
   """
   Set the value of a file's extended attributes, subject to access controls.
   The affected file and attribute are determined from the given ms_update structure.
   Use the XATTR_CREATE and XATTR_REPLACE semantics from setxattr(2) (these 
   are fields in the given ms_update structure).
   This is part of the File Metadata API.
   """
   
   xattr_create = False 
   xattr_replace = False 
   xattr_mode = update.xattr_mode
   xattr_owner = update.xattr_owner
   
   if update.HasField( 'xattr_create' ):
      xattr_create = update.xattr_create
   
   if update.HasField( 'xattr_replace' ):
      xattr_replace = update.xattr_replace 
   
   attrs = MSEntry.unprotobuf_dict( update.entry )
   
   logging.info("setxattr /%s/%s (name=%s, parent=%s) %s = %s (create=%s, replace=%s, mode=0%o)" % 
                  (attrs['volume_id'], attrs['file_id'], attrs['name'], attrs['parent_id'], update.xattr_name, update.xattr_value, xattr_create, xattr_replace, xattr_mode))
      
   file_id = attrs['file_id']
   rc = 0

   # find gateway owner ID
   gateway_owner_id = GATEWAY_ID_ANON
   if gateway is not None:
      gateway_owner_id = gateway.owner_id
   
   # if we're creating, then the requested xattr owner ID must match the gateway owner ID 
   if xattr_create and gateway_owner_id != xattr_owner:
      logging.error("Request to create xattr '%s' owned by %s does not match Gateway owner %s" % (update.xattr_name, xattr_owner, gateway_owner_id))
      rc = -errno.EACCES
   
   if rc == 0:
      msent_rc, msent = file_xattr_get_and_check_msentry_writeable( gateway, volume, file_id, caller_is_admin )
      xattr_rc, xattr = file_xattr_get_and_check_xattr_writable( gateway, volume, file_id, update.xattr_name, caller_is_admin )
      
      # if the xattr doesn't exist and the msent isn't writable by the caller, then this is an error 
      if (xattr is None or xattr_rc == -errno.ENOENT) and msent_rc != 0:
         rc = msent_rc
      
      else:
         # set the xattr
         rc = MSEntryXAttr.SetXAttr( volume, msent, update.xattr_name, update.xattr_value, create=xattr_create, replace=xattr_replace, mode=xattr_mode, owner=gateway_owner_id, caller_is_admin=caller_is_admin )
      
   logging.info("setxattr /%s/%s (name=%s, parent=%s) %s = %s (create=%s, replace=%s, mode=0%o) rc = %s" % 
                  (attrs['volume_id'], attrs['file_id'], attrs['name'], attrs['parent_id'], update.xattr_name, update.xattr_value, xattr_create, xattr_replace, xattr_mode, rc) )
         
   return rc


# ----------------------------------
def file_xattr_removexattr( reply, gateway, volume, update, caller_is_admin=False ):
   """
   Remove a file's extended attribute, subject to access controls.
   The affected file and attribute are determined from the given ms_update structure.
   This is part of the File Metadata API.
   """

   attrs = MSEntry.unprotobuf_dict( update.entry )
   
   logging.info("removexattr /%s/%s (name=%s, parent=%s) %s" % 
                  (attrs['volume_id'], attrs['file_id'], attrs['name'], attrs['parent_id'], update.xattr_name))
      
   file_id = attrs['file_id']
   rc, msent = file_xattr_get_and_check_msentry_writeable( gateway, volume, file_id, caller_is_admin )
   
   if rc == 0:
      # check xattr writable 
      rc, xattr = file_xattr_get_and_check_xattr_writable( gateway, volume, file_id, update.xattr_name, caller_is_admin )
      
      if rc == 0:
         # get user id
         gateway_owner_id = GATEWAY_ID_ANON
         if gateway is not None:
            gateway_owner_id = gateway.owner_id
         
         # delete it 
         rc = MSEntryXAttr.RemoveXAttr( volume, msent, update.xattr_name, gateway_owner_id, caller_is_admin )
   
   logging.info("removexattr /%s/%s (name=%s, parent=%s) %s rc = %s" % 
                  (attrs['volume_id'], attrs['file_id'], attrs['name'], attrs['parent_id'], update.xattr_name, rc) )
   
   
   return rc


# ----------------------------------
def file_xattr_chmodxattr( reply, gateway, volume, update, caller_is_admin=False ):
   """
   Set the access mode for an extended attribute.
   """
   
   xattr_mode = None
   
   if update.HasField( 'xattr_mode' ):
      xattr_mode = update.xattr_mode 
   
   if xattr_mode is None:
      logging.error("chmodxattr: Missing xattr_mode field")
      rc = -errno.EINVAL
   
   else:
      attrs = MSEntry.unprotobuf_dict( update.entry )
      
      logging.info("chmodxattr /%s/%s (name=%s, parent=%s) %s = %s (mode=0%o)" % 
                     (attrs['volume_id'], attrs['file_id'], attrs['name'], attrs['parent_id'], update.xattr_name, update.xattr_value, xattr_mode))
         
      file_id = attrs['file_id']
      
      # is this xattr writable?
      rc, xattr = file_xattr_get_and_check_xattr_writable( gateway, volume, file_id, update.xattr_name, caller_is_admin )
      
      if rc == 0:
         # allowed!
         # get user id
         gateway_owner_id = GATEWAY_ID_ANON
         if gateway is not None:
            gateway_owner_id = gateway.owner_id
         
         rc = MSEntryXAttr.ChmodXAttr( volume, file_id, update.xattr_name, xattr_mode, gateway_owner_id, caller_is_admin )
         
      logging.info("chmodxattr /%s/%s (name=%s, parent=%s) %s = %s (mode=0%o) rc = %s" % 
                     (attrs['volume_id'], attrs['file_id'], attrs['name'], attrs['parent_id'], update.xattr_name, update.xattr_value, xattr_mode, rc) )
            
   return rc


# ----------------------------------
def file_xattr_chownxattr( reply, gateway, volume, update, caller_is_admin=False ):
   """
   Set the access mode for an extended attribute.
   """
   
   attrs = MSEntry.unprotobuf_dict( update.entry )
   
   logging.info("chownxattr /%s/%s (name=%s, parent=%s) %s = %s (owner=%s)" % 
                  (attrs['volume_id'], attrs['file_id'], attrs['name'], attrs['parent_id'], update.xattr_name, update.xattr_value, xattr_owner))
   
   
   xattr_owner = None
   
   if update.HasField( 'xattr_owner' ):
      xattr_owner = update.xattr_owner 
   
   if xattr_owner is None:
      logging.error("Missing xattr_owner field")
      rc = -errno.EINVAL
   
   else:
      file_id = attrs['file_id']
      
      # is this xattr writable?
      rc, xattr = file_xattr_get_and_check_xattr_writable( gateway, volume, file_id, update.xattr_name, caller_is_admin )
      
      if rc == 0:
         # allowed!
         # get user id
         gateway_owner_id = GATEWAY_ID_ANON
         if gateway is not None:
            gateway_owner_id = gateway.owner_id
         
         rc = MSEntryXAttr.ChownXAttr( volume, file_id, update.xattr_name, xattr_owner, gateway_owner_id, caller_is_admin )
      
   logging.info("chownxattr /%s/%s (name=%s, parent=%s) %s = %s (owner=%s) rc = %s" % 
                  (attrs['volume_id'], attrs['file_id'], attrs['name'], attrs['parent_id'], update.xattr_name, update.xattr_value, xattr_owner, rc) )
         
   return rc


# ----------------------------------
def file_manifest_log_check_access( gateway, msent ):
   """
   Verify that the gateway is allowed to manipulate the MSEntry's manifest log.
   """
   return msent.coordinator_id == gateway.g_id


# ----------------------------------
def file_manifest_log_response( volume, rc, log_record ):
   """
   Create a file response for the log record, if given.
   """
   
   reply = file_update_init_response( volume )
   reply.error = rc
   
   if rc == 0:
      pass 
   
   return file_update_complete_response( volume, reply )


# ----------------------------------
def file_manifest_log_peek( gateway, volume, file_id, caller_is_admin=False ):
   """
   Get the head of the manifest log for a particular file.
   Only serve data back if the requester is the coordinator of the file.
   """
   
   file_id_str = MSEntry.unserialize_id( file_id )
   
   logging.info("manifest log peek /%s/%s by %s" % (volume.volume_id, file_id_str, gateway.g_id))
   
   rc = 0
   log_head = None
   
   msent = MSEntry.Read( volume, file_id )
   if msent is None:
      logging.error("No entry for %s" % file_id)
      rc = -errno.ENOENT 
      
   else:
      
      # security check
      if not caller_is_admin and not file_manifest_log_check_access( gateway, msent ):
         logging.error("Gateway %s is not allowed to access the manifest log of %s" % (gateway.name, file_id_str))
         rc = -errno.EACCES
      
      else:
         # get the log head 
         log_head = MSEntryGCLog.Peek( volume.volume_id, file_id )
         
         if log_head is None:
            # no more data
            rc = -errno.ENODATA 
   
   logging.info("manifest log peek /%s/%s by %s rc = %s" % (volume.volume_id, file_id_str, gateway.g_id, rc))
   
   return file_manifest_log_response( volume, rc, log_record )


# ----------------------------------
def file_manifest_log_verify_deletion_receipts( volume_id, file_id, file_version, manifest_mtime_sec, manifest_mtime_nsec, receipt_list ):
   """
   Verify the legitimacy of the set of manifest deletion receipts.
   There must be one for every RG in the Volume, and 
   they must all be signed by the respective RGs.
   """
   
   for receipt in receipt_list:
      # basic sanity check 
      if receipt.volume_id != volume_id:
         return -errno.EINVAL 
      
      if receipt.file_id != file_id:
         return -errno.EINVAL
      
      if receipt.version != file_version:
         return -errno.EINVAL
      
      if receipt.manifest_mtime_sec != manifest_mtime_sec:
         return -errno.EINVAL
      
      if receipt.manifest_mtime_nsec != manifest_mtime_nsec:
         return -errno.EINVAL
      
   # get all volume RGs
   RGs = Gateway.ListAll( {"Gateway.volume_id ==" : volume_id, "Gateway.gateway_type ==": GATEWAY_TYPE_RG} )
   
   # make sure all RGs are listed in the receipts.
   # build up a ID <--> RG map in the process 
   RG_table = {}
   RG_present = {}
   for RG in RGs:
      RG_table[ RG.g_id ] = RG 
      RG_present[ RG.g_id ] = False
   
   # receipts must all refer to existing RGs
   for receipt in receipt_list:
      if not RG_table.has_key( receipt.RG_id ):
         # no receipt from this RG 
         logging.error("No such RG %s" % receipt.RG_id )
         return -errno.EAGAIN
      
      else:
         RG_present[ receipt.RG_id ] = True 
         
         
   # any unaccounted-for receipts?
   for (RG_id, present) in RG_present.items():
      if not present:
         logging.error("Missing receipt for RG %s" % RG_id )
         return -errno.EAGAIN
   
   # got receipts for all RGs
   # make sure all RG signatures match 
   for receipt in receipt_list:
      RG = RG_table[ receipt.RG_id ]
      valid = RG.verify_message( receipt )
      
      if not valid:
         logging.error("Receipt for RG %s has signature mismatch" % RG.g_id )
         return -errno.EINVAL 
      
   # success!
   return 0
      

# ----------------------------------
def file_manifest_log_remove( reply, gateway, volume, update, caller_is_admin=False ):
   """
   Remove a record of the manifest log for a particular file.
   This method will check to see if the requester:
     * is the coordinator of the file 
     * has the signed proofs from all the Volume's RGs that they don't have the manifest anymore.
   """
   
   # get receipts
   deletion_receipts = None 
   
   if update.HasField("deletion_receipts"):
      deletion_receipts = update.deletion_receipts 
   else:
      logging.error("manifest log remove: Missing deletion_receipts")
      return -errno.EINVAL 
   
   # check entry attrs
   attrs = MSEntry.unprotobuf_dict( update.entry )
   
   rc = 0
   required_attrs =  ['file_id', 'version', 'manifest_mtime_sec', 'manifest_mtime_nsec']
   
   attrs = file_update_get_attrs( attrs, required_attrs )
   
   if attrs is None:
      logging.error("manifest log remove: Missing one of %s" % required_attrs )
      rc = -errno.EINVAL
   
   else:
      
      # extract attrs
      file_id = attrs['file_id']
      file_id_str = MSEntry.unserialize_id( file_id )
      
      logging.info("manifest log remove /%s/%s by %s" % (volume.volume_id, file_id_str, gateway.g_id))
   
      version = attrs['version']
      manifest_mtime_sec = attrs['manifest_mtime_sec']
      manifest_mtime_nsec = attrs['manifest_mtime_nsec']
      
      # get msent
      msent = MSEntry.Read( volume, file_id )
      if msent is None:
         logging.error("No entry for %s" % file_id_str )
         rc = -errno.ENOENT
      
      else:
         # security check 
         if not caller_is_admin and not file_manifest_log_check_access( gateway, msent ):
            logging.error("Gateway %s is not allowed to access the manifest log of %s" % (gateway.name, file_id_str))
            
         else:
            # do we have proof-of-absence from all the RGs?
            rc = file_manifest_log_verify_deletion_receipts( volume.volume_id, file_id, version, manifest_mtime_sec, manifest_mtime_nsec, deletion_receipts )
            
            if rc != 0:
               logging.error("Failed to verify deletion receipts, rc = %s" % rc)
            
            else:
               # validated. Delete!
               MSEntryGCLog.Remove( volume.volume_id, file_id, version, manifest_mtime_sec, manifest_mtime_nsec )
   
      logging.info("manifest log remove /%s/%s by %s rc = %s" % (volume.volume_id, file_id_str, gateway.g_id, rc))
   
   return rc
