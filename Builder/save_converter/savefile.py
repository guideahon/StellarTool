import logging
import struct
import pickle
import json
import zlib
import shutil
import sys
import os
import os.path as osp
from io import BytesIO
from datetime import datetime
from abc import ABC

class MyBytesIO(BytesIO):
  def read_fstring(self):
    length = self.read_i32()
    code = 'ascii' if length > 0 else 'utf-16-le'
    length = length if length > 0 else -2 * length
    return '' if length == 0 else self.read(length).decode(code)[:-1]

  def read_bool(self):
    return self.read(1) != b'\0'

  def write_fstring(self, s):
    if not len(s):
      return self.write_i32(0)
    if type(s) is str:
      s += '\0'
      length = len(s)
      try:
        data = s.encode("ascii")
      except UnicodeEncodeError:
        data = s.encode("utf-16-le")
        length *= -1
    self.write_i32(length)
    self.write(data)

  def write_bool(self, x):
    self.write(b'\1' if x else b'\0')

  def begin_size(self):
    pos = self.tell()
    self.write_u64(0)
    return pos

  def end_size(self, pos, start):
    end = self.tell()
    self.seek(pos)
    self.write_u64(end - start)
    self.seek(end)
for t, fmt in zip(['u8', 'i32', 'u32', 'i64', 'u64', 'float'], ['<B', '<i', '<I', '<q', '<Q', '<f']):
  setattr(MyBytesIO, 'read_' + t, (lambda fmt: lambda self: struct.unpack(fmt, self.read(struct.calcsize(fmt)))[0])(fmt))
  setattr(MyBytesIO, 'write_' + t, (lambda fmt: lambda self, x: self.write(struct.pack(fmt, x)))(fmt))

STRUCT, ARRAY, MAP = "Struct", "Array", "Map"
class Context:
  def __init__(self, property_name="", container=""):
    self.property_name=property_name
    self.container=container

  def child(self,**kwargs):
    d = dict(property_name=self.property_name, container=self.container)
    d.update(kwargs)
    return Context(**d)
class Property(ABC):
  @classmethod
  def parseHeader(cls, reader, ctx): return cls()
  def parsePayload(self, reader, ctx): pass
  @classmethod
  def parse(cls, reader, ctx):
    obj = cls.parseHeader(reader, ctx)
    obj.parsePayload(reader, ctx)
    return obj
  def serializeHeader(self, writer, ctx): pass
  def serializePayload(self, writer, ctx): pass
  def serialize(self, writer, ctx):
    self.serializeHeader(writer, ctx)
    self.serializePayload(writer, ctx)

  def toJson(self):
    return self.value
  @classmethod
  def fromJson(cls, data, ctx):
    obj = cls()
    obj.value = data
    return obj
  @staticmethod
  def detectType(data):
    if type(data) is dict:
      return ''
    elif type(data) is list and len(data) > 2 and type(data[0]) is float:
      return 'Quat' if len(data) == 4 else 'Vector'
    else: return TYPE_MAPPING.get(type(data), 'None')
class SizedProperty(Property):
  @classmethod
  def parseHeader(cls, reader, ctx):
    if ctx.container == STRUCT:
      reader.seek(9, 1) # skip size + 1B zero
    return cls()

  def serializeHeader(self, writer, ctx):
    if ctx.container in {MAP, ARRAY}:
      return -1, 0
    pos = writer.tell()
    writer.write(b'\0' * 9) # 8B size + 1B zero
    return pos, writer.tell()
  def serialize(self, writer, ctx):
    pos, start = self.serializeHeader(writer, ctx)
    self.serializePayload(writer, ctx)
    if pos >= 0:
      writer.end_size(pos, start)
register_property = lambda Type: lambda cls: PROPERTY_REGISTRY.setdefault(Type, cls) or cls
register_struct = lambda Type: lambda cls: STRUCT_REGISTRY.setdefault(Type, cls) or cls
parse_property = lambda t: lambda self, reader, ctx: setattr(self, 'value', getattr(reader, 'read_' + t)())
newProperty = lambda k, t: (k, type(k, (SizedProperty,), {
  'value': None,
  'parsePayload': parse_property(t),
  'serializePayload': lambda self, writer, ctx: getattr(writer, 'write_' + t)(self.value),
}))
PROPERTY_REGISTRY = dict(newProperty(k + 'Property', t) for k, t in zip(['Int', 'UInt32', 'Float', 'Bool', 'Str', 'Int64'], ['i32', 'u32', 'float', 'bool', 'fstring', 'i64']))
def parseBool(cls, reader, ctx):
  if ctx.container == STRUCT:
    reader.seek(8, 1) # skip 8B zero
  obj = cls()
  obj.value = reader.read_bool()
  if ctx.container == STRUCT:
    reader.seek(1, 1) # skip 1B zero
  return obj
def serializeBool(self, writer, ctx):
  pos, _ = self.serializeHeader(writer, ctx)
  if pos >= 0:
    writer.seek(-1, 1)
  self.serializePayload(writer, ctx)
  if pos >= 0:
    writer.write_u8(0)
PROPERTY_REGISTRY['BoolProperty'].parse = classmethod(parseBool)
PROPERTY_REGISTRY['BoolProperty'].serialize = serializeBool
PROPERTY_REGISTRY['None'] = type('NoneProperty', (Property,), {'serializeHeader': lambda self, writer, ctx: writer.write_fstring('None')})
GlobalNone = PROPERTY_REGISTRY['None']()
PropertyFullName = {}
JsonPatches = {
  'Header': 0,
  'Type': '',
  'PropertyMeta': {},
}
updatePatches = lambda src, dst: [dst[k].update(v) if type(v) is dict else dst.update([(k, v)]) for k, v in src.items() if k in dst]
def setMeta(name, **args):
  PropertyMeta.setdefault(name, args)
  if args != PropertyMeta[name]:
    JsonPatches['PropertyMeta'][name] = {k: v.hex() if isinstance(v, bytes) else v for k, v in args.items()}
def checkPropertyName(name):
  if len(name) < 35:
    return name
  if all(c in '0123456789ABCDEF' for c in name[-32:]) and name[-33] == '_':
    shortName = name.split('_')[0]
    PropertyFullName.setdefault(shortName, name)
    return shortName
  return name
def rememberType(base, __type__):
  def parseHeader(cls, reader, ctx):
    obj = super(cls, cls).parseHeader(reader, ctx)
    if ctx.container == STRUCT:
      setMeta(ctx.property_name, __type__=__type__)
    return obj
  return type(__type__, (base,), {'parseHeader': classmethod(parseHeader)})
PROPERTY_REGISTRY['NameProperty'] = rememberType(PROPERTY_REGISTRY['StrProperty'], "NameProperty")
PROPERTY_REGISTRY['UInt32Property'] = rememberType(PROPERTY_REGISTRY['UInt32Property'], "UInt32Property")
PROPERTY_REGISTRY['Int64Property'] = rememberType(PROPERTY_REGISTRY['Int64Property'], "Int64Property")
class NamedProperty(Property):
  def __init__(self, name="", value=None, type_name=""):
    self.name = name
    self.value = value
    self.type_name = type_name

  @classmethod
  def parse(cls, reader, ctx):
    name = reader.read_fstring()
    if name == "None":
      return GlobalNone
    type_name = reader.read_fstring()
    prop = PROPERTY_REGISTRY[type_name].parse(reader, ctx.child(property_name=checkPropertyName(name), container=STRUCT))
    return cls(name, prop, type_name)

  def serialize(self, writer, ctx):
    writer.write_fstring(self.name)
    writer.write_fstring(self.type_name)
    self.value.serialize(writer, ctx.child(property_name=checkPropertyName(self.name), container=STRUCT))

class FloatVector(Property):
  LENGTH=0
  def parsePayload(self, reader, ctx):
    self.value = [reader.read_float() for _ in range(self.LENGTH)]
  def serializePayload(self, writer, ctx):
    for v in self.value:
      writer.write_float(v)
TYPE_MAPPING = {
  bool: 'BoolProperty',
  int: 'IntProperty',
  float: 'FloatProperty',
  str: 'StrProperty',
  list: 'ArrayProperty'
}
STRUCT_REGISTRY = {k: type(k, (FloatVector,), {'LENGTH': l}) for k, l in zip(['Vector', 'Quat'], [3, 4])}
def fromJsonGuid(cls, data, ctx):
  obj = cls()
  obj.value = bytes.fromhex(data)
  return obj
STRUCT_REGISTRY.update(
  VectorControls=STRUCT_REGISTRY['Quat'],
  Rotator=STRUCT_REGISTRY['Vector'],
  Guid=type('Guid', (Property,), {
    'parsePayload': lambda self, reader, ctx: setattr(self, 'value', reader.read(16)),
    'serializePayload': lambda self, writer, ctx: writer.write(self.value),
    'toJson': lambda self: self.value.hex(),
    'fromJson': classmethod(fromJsonGuid)
  })
)
getCodec = lambda name: STRUCT_REGISTRY.get(name, TaggedStruct)

@register_property("EnumProperty")
class EnumProperty(PROPERTY_REGISTRY['StrProperty']):
  __type__ = "EnumProperty"
  @classmethod
  def parseHeader(cls, reader, ctx):
    obj = cls()
    if ctx.container in {MAP, ARRAY}:
      return obj
    reader.seek(8, 1) # skip size
    obj.enum_name = reader.read_fstring()
    obj.flag = reader.read_u8()
    setMeta(ctx.property_name, __type__=cls.__type__, flag=obj.flag)
    return obj
  def parsePayload(self, reader, ctx):
    if getattr(self, 'enum_name', '') == 'None':
      self.value = reader.read_u8()
    else:
      super().parsePayload(reader, ctx)

  def serializeHeader(self, writer, ctx):
    if ctx.container in {MAP, ARRAY}:
      return -1, 0
    pos = writer.begin_size()
    writer.write_fstring(self.enum_name)
    writer.write_u8(self.flag)
    start = writer.tell()
    return pos, start
  def serializePayload(self, writer, ctx):
    if getattr(self, 'enum_name', '') == 'None':
      writer.write_u8(self.value)
    else:
      super().serializePayload(writer, ctx)

  def toJson(self):
    enum_name = getattr(self, 'enum_name', '')
    return f'{enum_name}::{self.value}' if enum_name else self.value
  @classmethod
  def fromJson(cls, data, ctx):
    obj = super().fromJson(data, ctx)
    if not ctx.container in {MAP, ARRAY}:
      meta = PropertyMeta.get(ctx.property_name, {})
      obj.__dict__.update(meta)
      obj.enum_name, _, obj.value = obj.value.partition('::')
      if obj.enum_name == 'None':
        obj.value = 0
    return obj
PROPERTY_REGISTRY['ByteProperty'] = type('ByteProperty', (EnumProperty,), {'__type__': "ByteProperty"})
@register_property("MapProperty")
class MapProperty(SizedProperty):
  @classmethod
  def parseHeader(cls, reader, ctx):
    obj = cls()
    reader.seek(8, 1) # skip size
    obj.key_type = reader.read_fstring()
    obj.value_type = reader.read_fstring()
    reader.seek(5, 1) # skip unknown zeros
    obj.count = reader.read_i32()
    setMeta(ctx.property_name, __type__="MapProperty", key_type=obj.key_type, value_type=obj.value_type)
    return obj
  def parsePayload(self, reader, ctx):
    key_parser = PROPERTY_REGISTRY[self.key_type]
    value_parser = PROPERTY_REGISTRY[self.value_type]
    ctxChild = ctx.child(container=MAP)
    self.items = [(key_parser.parse(reader, ctxChild), value_parser.parse(reader, ctxChild)) for _ in range(self.count)]

  def serializeHeader(self, writer, ctx):
    pos = writer.begin_size()
    writer.write_fstring(self.key_type)
    writer.write_fstring(self.value_type)
    writer.write(b"\0"*5)
    start = writer.tell() - 4
    writer.write_i32(len(self.items))
    return pos, start
  def serializePayload(self, writer, ctx):
    ctxChild = ctx.child(container=MAP)
    for k, v in self.items:
      k.serialize(writer, ctxChild)
      v.serialize(writer, ctxChild)

  def toJson(self):
    return {k.toJson(): v.toJson() for k, v in self.items}

  @classmethod
  def fromJson(cls, data, ctx):
    obj = cls()
    meta = PropertyMeta.get(ctx.property_name, {})
    obj.key_type = meta.get("key_type", 'StrProperty')
    obj.value_type = meta.get("value_type", 'StrProperty')
    key_cls = PROPERTY_REGISTRY[obj.key_type]
    val_cls = PROPERTY_REGISTRY[obj.value_type]
    ctxChild = ctx.child(container=MAP)
    obj.items = [(key_cls.fromJson(int(k) if obj.key_type == 'IntProperty' else k, ctxChild), val_cls.fromJson(v, ctxChild)) for k, v in data.items()]
    return obj

@register_struct("")
class TaggedStruct(Property):
  @classmethod
  def parse(cls, reader, ctx):
    obj = cls()
    obj.items = [v for v in iter((lambda: NamedProperty.parse(reader, ctx)), GlobalNone)]
    return obj
  def serialize(self, writer, ctx):
    [item.serialize(writer, ctx) for item in self.items + [GlobalNone]]

  def toJson(self):
    return {checkPropertyName(v.name): v.value.toJson() for v in self.items}
  @staticmethod
  def constructProperty(v, ctx):
    # Try to use stored metadata first (PropertyMeta keyed by short names)
    meta = PropertyMeta.get(ctx.property_name, {})
    t = meta.get('__type__') or Property.detectType(v)
    if t in STRUCT_REGISTRY:
      cls = STRUCT_REGISTRY[t]
      return cls.fromJson(v, ctx), t
    if t in PROPERTY_REGISTRY:
      return PROPERTY_REGISTRY[t].fromJson(v, ctx), t
    return Property.fromJson(v, ctx), t
  @classmethod
  def fromJson(cls, data, ctx):
    obj = cls()
    obj.items = [NamedProperty(PropertyFullName.get(k, k), *TaggedStruct.constructProperty(v, ctx.child(property_name=k, container=STRUCT))) for k, v in data.items()]
    return obj
@register_property(Type="ArrayProperty")
class ArrayProperty(SizedProperty):
  __type__ = "ArrayProperty"
  Skip4 = False
  @classmethod
  def parse(cls, reader, ctx):
    obj = cls()
    reader.seek(8, 1) # skip size
    obj.item_type = reader.read_fstring()
    reader.seek(1, 1)
    if cls.Skip4:
      reader.seek(4, 1)
    count = reader.read_i32()
    obj.array_name = None
    child = ctx.child(container=ARRAY)
    if obj.item_type == "StructProperty":
      obj.array_name = reader.read_fstring()
      reader.read_fstring() # skip item type, should be "StructProperty"
      obj.prototype = StructProperty.parseHeader(reader, child)
      parser = obj.prototype.codec
      extra = {
        'array_name': obj.array_name,
        'prototype': obj.prototype.getMeta()
      }
    else:
      parser = PROPERTY_REGISTRY[obj.item_type]
      extra = {}
    setMeta(ctx.property_name, __type__=cls.__type__, item_type=obj.item_type, **extra)
    obj.items = [parser.parse(reader, child) for _ in range(count)]
    return obj

  def serializeHeader(self, writer, ctx):
    pos = writer.begin_size()
    writer.write_fstring(self.item_type)
    writer.write_u8(0)
    start = writer.tell()
    if self.Skip4:
      writer.write_i32(0)
    writer.write_i32(len(self.items))
    return pos, start
  def serializePayload(self, writer, ctx):
    if self.item_type == "StructProperty":
      writer.write_fstring(self.array_name)
      writer.write_fstring("StructProperty")
      pos, start = self.prototype.serializeHeader(writer, ctx)
    ctxChild = ctx.child(container=ARRAY)
    for x in self.items:
      x.serialize(writer, ctxChild)
    if self.item_type == "StructProperty":
      writer.end_size(pos, start)

  def toJson(self):
    return [item.toJson() for item in self.items]
  @classmethod
  def fromJson(cls, data, ctx):
    obj = cls()
    obj.item_type = None
    meta = PropertyMeta.get(ctx.property_name, {})
    obj.__dict__.update(meta)
    if obj.item_type == "StructProperty":
      prototype = StructProperty()
      prototype.__dict__.update(obj.prototype)
      prototype.codec = getCodec(prototype.struct_name)
      obj.prototype = prototype
    if not data:
      obj.items = []
      return obj
    if not obj.item_type:
      obj.item_type = Property.detectType(data[0])
    ctxChild = ctx.child(container=ARRAY)
    item_cls = obj.prototype.codec if obj.item_type == "StructProperty" else PROPERTY_REGISTRY.get(obj.item_type) or getCodec(obj.item_type)
    obj.items = [item_cls.fromJson(item, ctxChild) for item in data]
    return obj
PROPERTY_REGISTRY['SetProperty'] = type('SetProperty', (ArrayProperty,), dict(__type__="SetProperty", Skip4=True))

@register_property(Type="StructProperty")
class StructProperty(SizedProperty):
  @classmethod
  def parseHeader(cls, reader, ctx):
    obj = cls()
    if ctx.container == MAP:
      obj.codec = getCodec(ctx.property_name)
      return obj
    reader.seek(8, 1) # skip size
    obj.struct_name = reader.read_fstring()
    obj.guid = reader.read(16)
    obj.flag = reader.read_u8()
    if ctx.container != ARRAY: # prototype should not set Meta
      setMeta(ctx.property_name, __type__="StructProperty", struct_name=obj.struct_name, guid=obj.guid, flag=obj.flag)
    obj.codec = getCodec(obj.struct_name)
    return obj

  def parsePayload(self, reader, ctx):
    self.value = self.codec.parse(reader, ctx.child(container=STRUCT))

  def serializeHeader(self, writer, ctx):
    if ctx.container in {MAP, ARRAY}:
      return -1, 0
    pos = writer.begin_size()
    writer.write_fstring(self.struct_name)
    writer.write(self.guid)
    writer.write_u8(self.flag)
    start = writer.tell()
    return pos, start

  def serializePayload(self, writer, ctx):
    self.value.serialize(writer, ctx.child(container=STRUCT))

  def getMeta(self):
    extra = {
      "struct_name": self.struct_name,
      "guid": self.guid,
      "flag": self.flag,
    } if hasattr(self, 'guid') else {}
    return {
      "__type__": "StructProperty",
      **extra
    }

  def toJson(self):
    if hasattr(self, 'value'):
      return self.value.toJson()
    return {}

  @classmethod
  def fromJson(cls, data, ctx):
    obj = cls()
    meta = PropertyMeta.get(ctx.property_name, {})
    if meta.get("__type__") == "StructProperty":
      obj.__dict__.update(meta)
    t = Property.detectType(data)
    obj.codec = STRUCT_REGISTRY[t] if t != '' and t in STRUCT_REGISTRY else getCodec(obj.struct_name if hasattr(obj, 'struct_name') else '')
    if data is None: return obj
    obj.value = obj.codec.fromJson(data, ctx.child(container=STRUCT))
    return obj

fromhex = lambda v: v.update(guid=bytes.fromhex(v['guid'])) or v if 'guid' in v else v
class SaveFile(TaggedStruct):
  HeadMagic = {b'GVAS': 0, b'EVAS': 1}
  @staticmethod
  def init():
    global GlobalPatches
    if getattr(sys, 'frozen', False):
      me = osp.dirname(osp.abspath(sys.executable))
    else:
      try:
        me = osp.dirname(osp.abspath(__file__))
      except:
        me = os.getcwd()
    SaveFile.me = me
    patch_path = osp.join(SaveFile.me, 'patches.bin')
    if osp.exists(patch_path):
      with open(patch_path, 'rb') as fp:
        GlobalPatches = pickle.load(fp)
    else:
      GlobalPatches = {'Header': b'GVAS', 'Header1': b'', 'Type': '', 'PropertyMeta': {}}
  @staticmethod
  def setMe(reader=None, patches=None):
    assert (reader is None) ^ (patches is None)
    global PropertyMeta
    PropertyMeta = GlobalPatches.get('PropertyMeta', {})
    if reader is not None:
      JsonPatches['Header'] = SaveFile.probeHeader(reader)
    else:
      updatePatches(patches, JsonPatches)
      PropertyFullName.update(patches['PropertyFullName'])
      PropertyMeta.update({k: fromhex(v) for k, v in JsonPatches['PropertyMeta'].items()})
    SaveFile.Header = GlobalPatches['Header']
    if JsonPatches['Header']:
      # The bundled patch cache normally supplies the EVAS header.  When the
      # cache is absent, preserve the magic detected in the input instead of
      # silently rewriting EVAS saves as GVAS.
      SaveFile.Header = (b'EVAS' if JsonPatches['Header'] == 1 else b'GVAS')
    SaveFile.Type = JsonPatches['Type'] or GlobalPatches['Type']
  @staticmethod
  def probeHeader(reader):
    magic = reader.read(4)
    reader.seek(0)
    return SaveFile.HeadMagic[magic]
  @classmethod
  def parse(cls, reader):
    try:
      cls.Header = reader.read(len(cls.Header))
      cls.Type = reader.read_fstring()
      return super().parse(reader, Context(container=STRUCT))
    except Exception as e:
      err_msg = str(e) if e.args else ''
      if err_msg:
        logging.exception(f"Error parsing save file: {err_msg}")
      else:
        logging.exception(f"Error parsing save file at position {hex(reader.tell())}")
      raise
  def serialize(self, writer):
    writer.write(SaveFile.Header)
    writer.write_fstring(SaveFile.Type)
    super().serialize(writer, Context(container=STRUCT))
    writer.write_i32(0) # write 4B zeros
    if JsonPatches['Header']:
      data = writer.getvalue()
      crc = zlib.crc32(data[8:]) & 0xffffffff
      writer.write_u32(crc)

  def dumpSave(self, filePath):
    data = MyBytesIO()
    self.serialize(data)
    if osp.exists(filePath):
      backupFile(filePath)
    with open(filePath, 'wb') as fp:
      fp.write(data.getvalue())
  @staticmethod
  def loadSave(filePath):
    SaveFile.init()
    with open(filePath, 'rb') as fp:
      data = fp.read()
    reader = MyBytesIO(data)
    SaveFile.setMe(reader=reader)
    result = SaveFile.parse(reader)
    file_version = int(osp.getmtime(filePath))
    savePatches(file_version)
    return result

  def dumpJson(self, filePath, **jsonArgs):
    args = {'ensure_ascii': False}
    args.update(jsonArgs)
    data = self.toJson()
    keys = ('Header', 'PropertyMeta')
    patches = {key: JsonPatches[key] for key in keys if JsonPatches[key]}
    patches['PropertyFullName'] = PropertyFullName
    if SaveFile.Type != GlobalPatches.get('Type', ''):
      patches['Type'] = SaveFile.Type
    if patches:
      data['$patches'] = patches
    with open(filePath, 'w', encoding='utf-8') as fp:
      json.dump(data, fp, **args)

  @staticmethod
  def loadJson(filePath):
    SaveFile.init()
    with open(filePath, 'r', encoding='utf-8') as fp:
      data = json.load(fp)
    patches = data.pop('$patches', {})
    SaveFile.setMe(patches=patches)
    return SaveFile.fromJson(data, Context(container=STRUCT))

  @staticmethod
  def fix(filePath):
    obj = SaveFile.loadSave(filePath)
    for item in obj.items:
      if item.name in {'AutoLoadCNS', 'CamPosition'}:
        obj.items.remove(item)
    obj.dumpSave(filePath)

def savePatches(version=0):
  newPatches = {
    'Header': SaveFile.Header[len(GlobalPatches['Header1']):] if JsonPatches['Header'] else SaveFile.Header,
    'Type': SaveFile.Type,
    'PropertyMeta': PropertyMeta,
    'version': version
  }
  if version >= GlobalPatches.get('version', 0):
    GlobalPatches.update(newPatches)
    patch_path = osp.join(getattr(SaveFile, 'me', osp.dirname(osp.abspath(__file__))), 'patches.bin')
    with open(patch_path, 'wb') as fp:
      pickle.dump(GlobalPatches, fp, protocol=5)

def backupFile(filePath):
  backFolder = osp.join(osp.dirname(filePath), 'Backup')
  os.makedirs(backFolder, exist_ok=True)
  name, ext = osp.splitext(osp.basename(filePath))
  timestamp = datetime.now().strftime("%Y%m%d%H%M%S")
  destPath = osp.join(backFolder, f"{name}_{timestamp}{ext}")
  shutil.copy2(filePath, destPath)
  return destPath
