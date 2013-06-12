#!/usr/bin/env python

"""
   Copyright 2013 The Trustees of Princeton University
   All Rights Reserved
"""

import webapp2
import urlparse

import tests

import types
import random
import os
import time

class MSDebugHandler( webapp2.RequestHandler ):

   def get( self, testname, path ):
      start_time = time.time()
      
      if len(path) == 0:
         path = "/"

      if path[0] != '/':
         path = "/" + path
      
      args = self.request.GET.dict_of_lists()
      
      for (k,v) in args.items():
         if type(v) == types.ListType and len(v) == 1:
            args[k] = v[0]
      
      # debug request
      test = getattr( tests, testname )
      status = None
      msg = None
      if test == None:
         status = 404
         msg = "No such test '%s'" % testname
      else:
         status, msg = test.test( path, args )

      self.response.status = status
      self.response.headers['X-Total-Time'] = str( int( (time.time() - start_time) * 1e9) )
      self.response.write( msg )
      return

   def put( self, _path ):
      pass