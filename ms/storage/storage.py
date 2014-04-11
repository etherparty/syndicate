#!/usr/bin/env python

"""
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
"""

"""
This is the high-level storage API.
"""

import storagetypes
from storagetypes import *

import logging

import MS
import MS.entry
from MS.entry import MSEntry, MSEntryShard, MSENTRY_TYPE_DIR
from MS.volume import Volume, VolumeAccessRequest
from MS.user import SyndicateUser
from MS.gateway import Gateway

import types
import errno
import time
import datetime
import random

from common.msconfig import *

# ----------------------------------
def _read_user_and_volume( email, volume_name_or_id ):
   user_fut = SyndicateUser.Read( email, async=True )
   volume_fut = Volume.Read( volume_name_or_id, async=True )
   
   storagetypes.wait_futures( [user_fut, volume_fut] )
   
   user = user_fut.get_result()
   volume = volume_fut.get_result()
   
   if user == None:
      raise Exception("No such User '%s'" % email)
   
   if volume == None:
      raise Exception("No such Volume '%s'" % volume_name_or_id )
   
   return user, volume

# ----------------------------------
def _get_volume_id( volume_name_or_id ):
   try:
      volume_id = int(volume_name_or_id)
      return volume_id
   except:
      volume = Volume.Read( volume_name_or_id )
      if volume == None:
         raise Exception("No such Volume '%s'" % volume_name_or_id )
      else:
         return volume.volume_id

# ----------------------------------
def _check_authenticated( kw ):
   # check a method's kw dict for caller_user, and verify it's there 
   caller_user = kw.get("caller_user", None)
   if caller_user is None:
      raise Exception("Anonymous user is insufficiently privileged")
   
   del kw['caller_user']
   return caller_user

# ----------------------------------
def create_user( email, openid_url, **fields ):
   user_key = SyndicateUser.Create( email, openid_url=openid_url, **fields )
   return user_key.get()

# ----------------------------------
def read_user( email ):
   return SyndicateUser.Read( email )

# ----------------------------------
def update_user( email, **fields ):
   SyndicateUser.Update( email, **fields )
   return True

# ----------------------------------
def delete_user( email ):
   return SyndicateUser.Delete( email )

# ----------------------------------
def list_users( attrs=None, **q_opts ):
   return SyndicateUser.ListAll( attrs, **q_opts )
   
# ----------------------------------
def register_account( email, password, signing_public_key ):
   return SyndicateUser.Register( email, password, signing_public_key=signing_public_key )

# ----------------------------------
def reset_account_credentials( email, password_salt, password_hash ):
   return SyndicateUser.Reset( email, password_salt, password_hash )

# ----------------------------------
def list_user_access_requests( email, **q_opts ):
   user = read_user( email )
   if user == None:
      raise Exception("No such user '%s'" % email)
   return VolumeAccessRequest.ListUserAccessRequests( user.owner_id, **q_opts )

# ----------------------------------
def request_volume_access( email, volume_name_or_id, caps, message ):
   user, volume = _read_user_and_volume( email, volume_name_or_id )
   
   # defensive checks...
   if user == None:
      raise Exception("No such user '%s'" % email)
   if volume == None or volume.deleted:
      raise Exception("No such volume '%s'" % volume_name_or_id )
   
   return VolumeAccessRequest.RequestAccess( user.owner_id, volume.volume_id, volume.name, caps, message )

# ----------------------------------
def remove_volume_access( email, volume_name_or_id ):
   user, volume = _read_user_and_volume( email, volume_name_or_id )
   
   # defensive checks...
   if user == None:
      raise Exception("No such user '%s'" % email)
   if volume == None or volume.deleted:
      raise Exception("No such volume '%s'" % volume_name_or_id )
   
   return VolumeAccessRequest.RemoveAccessRequest( user.owner_id, volume.volume_id )

# ----------------------------------
def list_volume_access_requests( volume_name_or_id, **q_opts ):
   caller_user = _check_authenticated( q_opts )
   
   # volume must exist (we need its ID)
   volume = read_volume( volume_name_or_id )
   if volume == None or volume.deleted:
      raise Exception("No such volume '%s'" % volume_name_or_id )
   
   # verify volume ownership
   if volume.owner_id != caller_user.owner_id and not caller_user.is_admin:
      raise Exception("User '%s' cannot read Volume '%s'" % (caller_user.email, volume.name))
   
   return VolumeAccessRequest.ListVolumeAccessRequests( volume.volume_id, **q_opts )

# ----------------------------------
def list_volume_access( volume_name_or_id, **q_opts ):
   caller_user = _check_authenticated( q_opts )
   
   # volume must exist (we need its ID)
   volume = read_volume( volume_name_or_id )
   if volume == None or volume.deleted:
      raise Exception("No such volume '%s'" % volume_name_or_id )
   
   if volume.owner_id != caller_user.owner_id and not caller_user.is_admin:
      raise Exception("User '%s' cannot read Volume '%s'" % (caller_user.email, volume.name))

   return VolumeAccessRequest.ListVolumeAccess( volume.volume_id, **q_opts )

# ----------------------------------
def set_volume_access( email, volume_name_or_id, caps, **caller_user_dict ):
   caller_user = _check_authenticated( caller_user_dict )
   
   user, volume = _read_user_and_volume( email, volume_name_or_id )
   if user is None:
      # defensive check
      raise Exception("No such user '%s'" % email)
   
   if volume is None or volume.deleted:
      # defensive check 
      raise Exception("No such Volume '%s'" % volume_name_or_id)
   
   return VolumeAccessRequest.GrantAccess( user.owner_id, volume.volume_id, volume.name, caps )

# ----------------------------------
def list_volume_user_ids( volume_name_or_id, **q_opts ):
   caller_user = _check_authenticated( q_opts )
   
   volume_id = _get_volume_id( volume_name_or_id )
   
   def __user_from_access_request( req ):
      return SyndicateUser.Read_ByOwnerID( req.requester_owner_id, async=True )
   
   q_opts["map_func"] = __user_from_access_request
   user_futs = VolumeAccessRequest.ListAll( {"VolumeAccessRequest.volume_id ==": volume_id}, **q_opts )
   
   storagetypes.wait_futures( user_futs )
   
   ret = [u.email for u in filter( lambda x: x != None, [uf.get_result() for uf in user_futs] )]
   return ret

# ----------------------------------
def create_volume( email, name, description, blocksize, **attrs ):
   caller_user = _check_authenticated( attrs )
   
   # user must exist
   user = read_user( email )
   if user is None:
      raise Exception("No such user '%s'" % email)
   
   # a user can only create volumes for herself (admin can for anyone)
   if caller_user.owner_id != user.owner_id and not caller_user.is_admin:
      raise Exception("Caller cannot create Volumes for other users")
   
   # check quota for this user
   user_volume_ids = list_accessible_volumes( email, caller_user=caller_user, projection=['volume_id'] )
   if len(user_volume_ids) > user.get_volume_quota():
      raise Exception("User '%s' has exceeded Volume quota" % email )
   
   new_volume_key = Volume.Create( user, blocksize=blocksize, name=name, description=description, **attrs )
   if new_volume_key != None:
      # create succeed.  Make a root directory
      volume = new_volume_key.get()
      MSEntry.MakeRoot( user.owner_id, volume )
      return new_volume_key.get()
      
   else:
      raise Exception("Failed to create Volume")
   

# ----------------------------------
def read_volume( volume_name_or_id ):
   return Volume.Read( volume_name_or_id )

# ----------------------------------
def volume_update_shard_count( volume_id, num_shards ):
   return Volume.update_shard_count( volume_id, num_shards )
   
# ----------------------------------
def update_volume( volume_name_or_id, **fields ):
   Volume.Update( volume_name_or_id, **fields )
   return True

# ----------------------------------
def delete_volume( volume_name_or_id ):
   volume = Volume.Read( volume_name_or_id )
   if volume == None:
      return True
   
   ret = Volume.Delete( volume.volume_id )
   if ret:
      # delete succeeded.  Blow away the MSEntries
      MSEntry.DeleteAll( volume )
   
   return ret
   
# ----------------------------------
def list_volumes( attrs=None, **q_opts ):
   return Volume.ListAll( attrs, **q_opts )

# ----------------------------------
def list_public_volumes( **q_opts ):
   return list_volumes( {"Volume.private ==": False}, **q_opts )

# ----------------------------------
def list_archive_volumes( **q_opts ):
   return list_volumes( {"Volume.archive ==": True, "Volume.private ==": False}, **q_opts )

# ----------------------------------
def list_accessible_volumes( email, **q_opts ):
   caller_user = _check_authenticated( q_opts )
   
   # queried user must exist
   user = read_user( email )
   if user == None:
      raise Exception("No such user '%s'" % email)
   
   # privilege check...
   if user.owner_id != caller_user.owner_id and not caller_user.is_admin:
      raise Exception("User '%s' is not sufficiently privileged" % caller_user.email)
      
   def __volume_from_access_request( var ):
      return Volume.Read( var.volume_id, async=True )
   
   q_opts["map_func"] = __volume_from_access_request
   vols = VolumeAccessRequest.ListAll( {"VolumeAccessRequest.requester_owner_id ==": user.owner_id, "VolumeAccessRequest.status ==": VolumeAccessRequest.STATUS_GRANTED}, **q_opts )
   
   ret = filter( lambda x: x != None, vols )
   return ret

# ----------------------------------
def list_pending_volumes( email, **q_opts ):
   caller_user = _check_authenticated( q_opts )
   
   # user must exist
   user = read_user( email )
   if user == None:
      raise Exception("No such user '%s'" % email)
   
   # privilege check--only admin can list other users' pending volumes
   if user.owner_id != caller_user.owner_id and not caller_user.is_admin:
      raise Exception("User '%s' is not sufficiently privileged" % caller_user.email)
   
   def __volume_from_access_request( var ):
      return Volume.Read( var.volume_id, async=True )
   
   q_opts["map_func"] = __volume_from_access_request
   vols = VolumeAccessRequest.ListAll( {"VolumeAccessRequest.requester_owner_id ==": user.owner_id, "VolumeAccessRequest.status ==": VolumeAccessRequest.STATUS_PENDING}, **q_opts )
   
   ret = filter( lambda x: x != None, vols )
   return ret
   

# ----------------------------------
def create_gateway( volume_id, email, gateway_type, gateway_name, host, port, **kwargs ):
   if "encryption_password" in kwargs.keys():
      del kwargs['encryption_password']
      
   caller_user = _check_authenticated( kwargs )
   
   # user and volume must both exist
   user, volume = _read_user_and_volume( email, volume_id )
   
   if user == None:
      raise Exception("No such user '%s'" % email)
   
   if (volume == None or volume.deleted):
      raise Exception("No such volume '%s'" % volume_id)
      
   # if the caller user doesn't own this Volume (or isn't an admin), (s)he must have a valid access request
   if not caller_user.is_admin and caller_user.owner_id != volume.owner_id:
      
      # caller user must be the same as the user 
      if caller_user.owner_id != user.owner_id:
         raise Exception("Caller can only create gateways for itself.")
         
      # check access status
      access_request = VolumeAccessRequest.GetAccess( user.owner_id, volume.volume_id )
      
      if access_request == None:
         # user has not been given permission to access this volume
         raise Exception("User '%s' is not allowed to access Volume '%s'" % (email, volume.name))
      
      if access_request.status != VolumeAccessRequest.STATUS_GRANTED:
         # user has not been given permission to create a gateway here 
         raise Exception("User '%s' is not allowed to create a Gateway in Volume '%s'" % (email, volume.name))
      
      else:
         if not caller_user.is_admin:
            # admins can set caps to whatever they want, but not users 
            kwargs['caps'] = access_request.gateway_caps
         elif kwargs.has_hey('caps') and kwargs['caps'] != access_request.gateway_caps:
            raise Exception("User '%s' is not allowed to set Gateway capabilities")
   
   # verify quota 
   gateway_quota = user.get_gateway_quota( gateway_type )
   
   if gateway_quota == 0:
      raise Exception("User '%s' cannot create Gateways" % (email))
   
   if gateway_quota > 0:
      gateway_ids = list_gateways_by_user( user.email, caller_user=user, keys_only=True )
      if len(gateway_ids) >= gateway_quota:
         raise Exception("User '%s' has too many Gateways" % (email))
   
   gateway_key = Gateway.Create( user, volume, gateway_type=gateway_type, name=gateway_name, host=host, port=port, **kwargs )
   return gateway_key.get()

# ----------------------------------
def read_gateway( g_name_or_id ):
   return Gateway.Read( g_name_or_id )

# ----------------------------------
def update_gateway( g_name_or_id, **fields):
   Gateway.Update( g_name_or_id, **fields )
   return True

# ----------------------------------
def set_gateway_caps( g_name_or_id, caps, **caller_user_dict ):
   caller_user = _check_authenticated( caller_user_dict )
   
   # gateway must exist...(defensive check)
   gateway = read_gateway( g_name_or_id )
   
   if gateway == None:
      raise Exception("No such Gateway '%s'" % g_name_or_id )
   
   # user and volume must exist...(defensive check)
   user, volume = _read_user_and_volume( gateway.owner_id, gateway.volume_id )
   
   if user == None:
      raise Exception("No user with ID '%s'" % gateway.owner_id )
   
   if volume == None or volume.deleted:
      raise Exception("No volume with ID '%s'" % gateway.volume_id )
   
   # caller must own the volume or be admin to do this
   if not caller_user.is_admin and caller_user.owner_id != volume.owner_id:
      raise Exception("User '%s' is not allowed to set the capabilities of Gateway '%s'" % user.email, gateway.name )
   
   Gateway.SetCaps( g_name_or_id, caps )
   return True

# ----------------------------------
def list_gateways( attrs=None, **q_opts):
   return Gateway.ListAll(attrs, **q_opts)


# ----------------------------------
def list_gateways_by_volume( volume_name_or_id, **q_opts ):
   caller_user = _check_authenticated( q_opts )
   
   # volume must exist
   volume = storage.read_volume( volume_name_or_id )
   if volume == None or volume.deleted:
      raise Exception("No such Volume '%s'" % volume_name_or_id )
   
   # only admin can list gateways of volumes she doesn't own
   if volume.owner_id != caller_user.owner_id and not caller_user.is_admin:
      raise Exception("User '%s' is not sufficiently privileged" % caller_user.email)
   
   return Gateway.ListAll( {"Gateway.volume_id ==" : volume_id}, **q_opts )


# ----------------------------------
def list_gateways_by_host( hostname, **q_opts ):
   return Gateway.ListAll( {"Gateway.host ==" : hostname}, **q_opts )

# ----------------------------------
def list_gateways_by_user( email, **q_opts ):
   caller_user = _check_authenticated( q_opts )
   
   # user must exist
   user = read_user( email )
   if user == None:
      raise Exception("No such user '%s'" % email )
   
   # only admin can list other users' gateways
   if caller_user.owner_id != user.owner_id and not caller_user.is_admin:
      raise Exception("User '%s' is not sufficiently privileged" % caller_user.email )
   
   return Gateway.ListAll( {"Gateway.owner_id ==": user.owner_id}, **q_opts )


# ----------------------------------
def delete_gateway( g_id ):
   # TODO: when deleting an AG, delete all of its files and directories as well
   return Gateway.Delete( g_id )


# ----------------------------------
def get_volume_root( volume ):
   return MSEntry.Read( volume, 0 )
