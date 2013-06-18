#!/usr/bin/python

"""
   Copyright 2013 The Trustees of Princeton University
   All Rights Reserved
"""

import storage.storagetypes as storagetypes
from Crypto.Hash import SHA256

import os
import base64
import uuid

import protobufs.ms_pb2 as ms_pb2

import types
import errno
import time
import datetime
import random


USERNAME_LENGTH = 256
PASSWORD_LENGTH = 256

class Gateway( storagetypes.Object ):

   base_url = storagetypes.Text()
   volume_id = storagetypes.Integer()           # which volume are we attached to?

   required_attrs = [
      "base_url",
      "volume_id"
   ]
   

class UserGateway( Gateway ):

   # owner ID of all files created by this gateway
   owner_id = storagetypes.Integer()
   
   # credentials that allow this UserGateway to communicate with other UserGateways
   ms_username = storagetypes.String()
   ms_password_hash = storagetypes.String()


   required_attrs = Gateway.required_attrs + [
      "owner_id",
      "ms_username",
      "ms_password_hash"
   ]

   validators = Gateway.merge_dict( Gateway.validators, {
      "ms_password_hash": (lambda cls, value: len( value.translate(None, "0123456789abcdef") ) == 0)
   })

   key_attrs = Gateway.key_attrs + [
      "ms_username"
   ]
   
   def get_credential_entry(self):
      """
      Generate a serialized user record
      """
      return "%s:%s:%s" % (self.owner_id, self.ms_username, self.ms_password_hash)


   @classmethod
   def generate_password_hash( cls, pw ):

      h = SHA256.new()
      h.update( pw )

      pw_hash = h.hexdigest()

      return pw_hash
      

   @classmethod
   def generate_credentials( cls ):
      """
      Generate (usernaem, password, SHA256(password)) for this gateway
      """
      password = os.urandom( PASSWORD_LENGTH )

      username = base64.encodestring( uuid.uuid4().urn )
      pw_hash = UserGateway.generate_password_hash( password )
      
      return (username, password, pw_hash)
      
   def new_credentials( self ):
      """
      Generate new credentials for this UG and save them.
      Return the (username, password) combo
      """
      username, password, pw_hash = UserGateway.generate_credentials()

      self.ms_username = username
      self.ms_password_hash = pw_hash
      self.put()

      return (username, password)
      

   def protobuf_cred( self, cred_pb ):
      """
      Populate an ms_volume_gateway_cred structure
      """
      cred_pb.owner_id = self.owner_id
      cred_pb.username = self.ms_username
      cred_pb.password_hash = self.ms_password_hash


   def authenticate( self, password ):
      """
      Authenticate this UG
      """
      h = SHA256.new()
      h.update( password )
      pw_hash = h.hexdigest()

      return pw_hash == self.ms_password_hash

   @classmethod
   def cache_listing_key( cls, **kwargs ):
      assert 'volume_id' in kwargs, "Required attributes: volume_id"
      return "UGs: volume=%s" % kwargs['volume_id']


   @classmethod
   def Create( cls, user, volume, **kwargs ):
      """
      Given a user and volume, create a user gateway.
      Extra kwargs:
         ms_username          str
         ms_password          str
         ms_password_hash     str
      """

      kwargs['volume_id'] = volume.volume_id
      kwargs['owner_id'] = user.owner_id

      UserGateway.fill_defaults( kwargs )

      if kwargs.get("ms_password") != None:
         kwargs[ "ms_password_hash" ] = UserGateway.generate_password_hash( kwargs.get("ms_password") )

      if kwargs.get("ms_username") == None or kwargs.get("ms_password_hash") == None:
         # generate new credentials
         username, password, password_hash = UserGateway.generate_credentials()
         kwargs["ms_username"] = username
         kwargs["ms_password"] = password
         kwargs["ms_password_hash"] = password_hash

      missing = UserGateway.find_missing_attrs( kwargs )
      if len(missing) != 0:
         raise Exception( "Missing attributes: %s" % (", ".join( missing )))

      invalid = UserGateway.validate_fields( kwargs )
      if len(invalid) != 0:
         raise Exception( "Invalid values for fields: %s" % (", ".join( invalid )) )

      # TODO: transaction

      ug_key = storagetypes.make_key( UserGateway, UserGateway.make_key_name( ms_username=kwargs["ms_username"] ) )
      ug = ug_key.get()

      # if this is not new, then we're in error
      if ug != None and ug.ms_username != None and len( ug.ms_username ) > 0:
         raise Exception( "User Gateway '%s' already exists!" % kwargs["ms_username"] )

      else:
         del kwargs['ms_password']
         ug = UserGateway( key=ug_key, **kwargs )

         # clear cached UG listings
         storagetypes.memcache.delete( UserGateway.cache_listing_key( volume_id=volume.volume_id ) )

         return ug.put()


   @classmethod
   def Read( cls, ms_username ):
      """
      Given a UG username, find the UG record
      """
      ug_key_name = UserGateway.make_key_name( ms_username=ms_username )

      ug = storagetypes.memcache.get( ug_key_name )
      if ug == None:
         ug_key = storagetypes.make_key( UserGateway, UserGateway.make_key_name( ms_username=ms_username ) )
         ug = ug_key.get( use_memcache=False )
         storagetypes.memcache.set( ug_key_name, ug )

      return ug

      
   @classmethod
   def Delete( cls, ms_username ):
      """
      Given a UG username, delete it
      """
      ug_key_name = UserGateway.make_key_name( ms_username=ms_username )

      ug_key = storagetypes.make_key( UserGateway, ug_key_name )

      ug_key.delete()

      return True
      

   @classmethod
   def ListAll( cls, volume_id ):
      """
      Given a volume id, find all UserGateway records bound to it.  Cache the results
      """
      cache_key = UserGateway.cache_listing_key( volume_id=volume_id )

      results = storagetypes.memcache.get( cache_key )
      if results == None:
         qry = UserGateway.query( UserGateway.volume_id == volume_id )
         results_qry = qry.fetch(None, batch_size=1000 )

         results = []
         for rr in results_qry:
            results.append( rr )

         storagetypes.memcache.add( cache_key, results )

      return results



   
class AcquisitionGateway( Gateway ):
   pass
  
class ReplicaGateway( Gateway ):
   pass
   