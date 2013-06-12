#!/usr/bin/python

import socket
import time
import sys

s = socket.socket( socket.AF_INET, socket.SOCK_STREAM )
s.connect( ("localhost", 8889) )

http_header = "GET %s HTTP/1.0\r\nHost: localhost\r\n\r\n" % (sys.argv[1])

http_m = http_header

print "<<<<<<<<<<<<<<<<<<<<<<<<<"
print http_m
print "<<<<<<<<<<<<<<<<<<<<<<<<<\n"

s.send( http_m )
   
time.sleep(1.0)

print ">>>>>>>>>>>>>>>>>>>>>>>>>"
ret = s.recv(16384)
while ret != None and len(ret) > 0:
   print ret
   ret = s.recv(16384)
print ">>>>>>>>>>>>>>>>>>>>>>>>>\n"

s.close()
