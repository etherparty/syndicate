# Generated by the protocol buffer compiler.  DO NOT EDIT!

from googlepb.protobuf import descriptor
from googlepb.protobuf import message
from googlepb.protobuf import reflection
from googlepb.protobuf import descriptor_pb2
# @@protoc_insertion_point(imports)


import googlepb.protobuf.descriptor_pb2

DESCRIPTOR = descriptor.FileDescriptor(
  name='google/protobuf/compiler/plugin.proto',
  package='googlepb.protobuf.compiler',
  serialized_pb='\n%google/protobuf/compiler/plugin.proto\x12\x18googlepb.protobuf.compiler\x1a google/protobuf/descriptor.proto\"}\n\x14\x43odeGeneratorRequest\x12\x18\n\x10\x66ile_to_generate\x18\x01 \x03(\t\x12\x11\n\tparameter\x18\x02 \x01(\t\x12\x38\n\nproto_file\x18\x0f \x03(\x0b\x32$.googlepb.protobuf.FileDescriptorProto\"\xaa\x01\n\x15\x43odeGeneratorResponse\x12\r\n\x05\x65rror\x18\x01 \x01(\t\x12\x42\n\x04\x66ile\x18\x0f \x03(\x0b\x32\x34.googlepb.protobuf.compiler.CodeGeneratorResponse.File\x1a>\n\x04\x46ile\x12\x0c\n\x04name\x18\x01 \x01(\t\x12\x17\n\x0finsertion_point\x18\x02 \x01(\t\x12\x0f\n\x07\x63ontent\x18\x0f \x01(\t')




_CODEGENERATORREQUEST = descriptor.Descriptor(
  name='CodeGeneratorRequest',
  full_name='googlepb.protobuf.compiler.CodeGeneratorRequest',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  fields=[
    descriptor.FieldDescriptor(
      name='file_to_generate', full_name='googlepb.protobuf.compiler.CodeGeneratorRequest.file_to_generate', index=0,
      number=1, type=9, cpp_type=9, label=3,
      has_default_value=False, default_value=[],
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None),
    descriptor.FieldDescriptor(
      name='parameter', full_name='googlepb.protobuf.compiler.CodeGeneratorRequest.parameter', index=1,
      number=2, type=9, cpp_type=9, label=1,
      has_default_value=False, default_value=unicode("", "utf-8"),
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None),
    descriptor.FieldDescriptor(
      name='proto_file', full_name='googlepb.protobuf.compiler.CodeGeneratorRequest.proto_file', index=2,
      number=15, type=11, cpp_type=10, label=3,
      has_default_value=False, default_value=[],
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  options=None,
  is_extendable=False,
  extension_ranges=[],
  serialized_start=101,
  serialized_end=226,
)


_CODEGENERATORRESPONSE_FILE = descriptor.Descriptor(
  name='File',
  full_name='googlepb.protobuf.compiler.CodeGeneratorResponse.File',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  fields=[
    descriptor.FieldDescriptor(
      name='name', full_name='googlepb.protobuf.compiler.CodeGeneratorResponse.File.name', index=0,
      number=1, type=9, cpp_type=9, label=1,
      has_default_value=False, default_value=unicode("", "utf-8"),
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None),
    descriptor.FieldDescriptor(
      name='insertion_point', full_name='googlepb.protobuf.compiler.CodeGeneratorResponse.File.insertion_point', index=1,
      number=2, type=9, cpp_type=9, label=1,
      has_default_value=False, default_value=unicode("", "utf-8"),
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None),
    descriptor.FieldDescriptor(
      name='content', full_name='googlepb.protobuf.compiler.CodeGeneratorResponse.File.content', index=2,
      number=15, type=9, cpp_type=9, label=1,
      has_default_value=False, default_value=unicode("", "utf-8"),
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None),
  ],
  extensions=[
  ],
  nested_types=[],
  enum_types=[
  ],
  options=None,
  is_extendable=False,
  extension_ranges=[],
  serialized_start=337,
  serialized_end=399,
)

_CODEGENERATORRESPONSE = descriptor.Descriptor(
  name='CodeGeneratorResponse',
  full_name='googlepb.protobuf.compiler.CodeGeneratorResponse',
  filename=None,
  file=DESCRIPTOR,
  containing_type=None,
  fields=[
    descriptor.FieldDescriptor(
      name='error', full_name='googlepb.protobuf.compiler.CodeGeneratorResponse.error', index=0,
      number=1, type=9, cpp_type=9, label=1,
      has_default_value=False, default_value=unicode("", "utf-8"),
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None),
    descriptor.FieldDescriptor(
      name='file', full_name='googlepb.protobuf.compiler.CodeGeneratorResponse.file', index=1,
      number=15, type=11, cpp_type=10, label=3,
      has_default_value=False, default_value=[],
      message_type=None, enum_type=None, containing_type=None,
      is_extension=False, extension_scope=None,
      options=None),
  ],
  extensions=[
  ],
  nested_types=[_CODEGENERATORRESPONSE_FILE, ],
  enum_types=[
  ],
  options=None,
  is_extendable=False,
  extension_ranges=[],
  serialized_start=229,
  serialized_end=399,
)

_CODEGENERATORREQUEST.fields_by_name['proto_file'].message_type = googlepb.protobuf.descriptor_pb2._FILEDESCRIPTORPROTO
_CODEGENERATORRESPONSE_FILE.containing_type = _CODEGENERATORRESPONSE;
_CODEGENERATORRESPONSE.fields_by_name['file'].message_type = _CODEGENERATORRESPONSE_FILE
DESCRIPTOR.message_types_by_name['CodeGeneratorRequest'] = _CODEGENERATORREQUEST
DESCRIPTOR.message_types_by_name['CodeGeneratorResponse'] = _CODEGENERATORRESPONSE

class CodeGeneratorRequest(message.Message):
  __metaclass__ = reflection.GeneratedProtocolMessageType
  DESCRIPTOR = _CODEGENERATORREQUEST
  
  # @@protoc_insertion_point(class_scope:googlepb.protobuf.compiler.CodeGeneratorRequest)

class CodeGeneratorResponse(message.Message):
  __metaclass__ = reflection.GeneratedProtocolMessageType
  
  class File(message.Message):
    __metaclass__ = reflection.GeneratedProtocolMessageType
    DESCRIPTOR = _CODEGENERATORRESPONSE_FILE
    
    # @@protoc_insertion_point(class_scope:googlepb.protobuf.compiler.CodeGeneratorResponse.File)
  DESCRIPTOR = _CODEGENERATORRESPONSE
  
  # @@protoc_insertion_point(class_scope:googlepb.protobuf.compiler.CodeGeneratorResponse)

# @@protoc_insertion_point(module_scope)
