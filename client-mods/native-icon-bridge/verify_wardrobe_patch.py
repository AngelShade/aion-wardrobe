"""Execute the generated lifecycle trampolines in a disposable mapped image.

No running game process is accessed. Native function tails are replaced by
small ABI probes; the generated hook code and relocated vtable LEAs are exact.
"""
import argparse
import ctypes as c
import struct
from pathlib import Path
from patch_client import patch_dll, WARDROBE_SITES, TICK_SITE, TICK_ORIGINAL

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--game-dll', type=Path, required=True, help='Original supported 4.8 NA Game.dll')
source = parser.parse_args().game_dll.read_bytes()
data=patch_dll(source)
pe=struct.unpack_from('<I',data,60)[0];optional=pe+24
table=optional+struct.unpack_from('<H',data,pe+20)[0]
sections=[struct.unpack_from('<8sIIIIIIHHI',data,table+i*40) for i in range(struct.unpack_from('<H',data,pe+6)[0])]
offset=lambda r:next(s[4]+r-s[2] for s in sections if s[2]<=r<s[2]+s[3])
kernel=c.WinDLL('kernel32',use_last_error=True)
kernel.VirtualAlloc.argtypes=[c.c_void_p,c.c_size_t,c.c_uint32,c.c_uint32];kernel.VirtualAlloc.restype=c.c_void_p
kernel.VirtualProtect.argtypes=[c.c_void_p,c.c_size_t,c.c_uint32,c.POINTER(c.c_uint32)]
kernel.VirtualFree.argtypes=[c.c_void_p,c.c_size_t,c.c_uint32]
size=struct.unpack_from('<I',data,optional+56)[0]
base=kernel.VirtualAlloc(None,size,0x3000,4);assert base
calls=[]
@c.CFUNCTYPE(None,c.c_void_p,c.c_int)
def lifecycle(widget,event):calls.append((widget,event))
ticks=[]
@c.CFUNCTYPE(None)
def tick():ticks.append(True)
try:
 section=sections[-1];assert section[0].rstrip(b'\0')==b'.aicons'
 c.memmove(base+section[2],data[section[4]:section[4]+section[3]],section[3])
 imports=struct.unpack_from('<I',data,optional+120)[0]
 cursor=offset(imports)
 while any(data[cursor:cursor+20]):
  name_rva=struct.unpack_from('<I',data,cursor+12)[0]
  name_pos=offset(name_rva)
  if data[name_pos:data.index(0,name_pos)]==b'AionIconBridge.dll':break
  cursor+=20
 else:raise AssertionError('Bridge import missing')
 iat=struct.unpack_from('<I',data,cursor+16)[0]
 c.memmove(base+iat+8,struct.pack('<Q',c.cast(lifecycle,c.c_void_p).value),8)
 c.memmove(base+iat+16,struct.pack('<Q',c.cast(tick,c.c_void_p).value),8)
 c.memmove(base+TICK_SITE,data[offset(TICK_SITE):offset(TICK_SITE)+len(TICK_ORIGINAL)],len(TICK_ORIGINAL))
 # Stop before _chkstk; the restored prologue must still provide its exact
 # allocation size and original RCX/RDX/R8/R9 to the native function tail.
 tail=bytes.fromhex('488989000100008991080100004c8981100100004c898918010000c3')
 c.memmove(base+TICK_SITE+len(TICK_ORIGINAL),tail,len(tail))
 for address,original,destroy in WARDROBE_SITES:
  c.memmove(base+address,data[offset(address):offset(address)+len(original)],len(original))
  # Store argument registers after the original prologue to prove restoration.
  tail=bytes.fromhex('488989000100008991080100004c8981100100004c898918010000')
  tail+=b'\x48\x81\xc4'+struct.pack('<I',0x28 if destroy else 0x2f8)+b'\xc3'
  c.memmove(base+address+len(original),tail,len(tail))
 old=c.c_uint32();assert kernel.VirtualProtect(base,size,0x20,c.byref(old))
 fn=c.CFUNCTYPE(c.c_uint64,c.c_void_p,c.c_int,c.c_uint64,c.c_uint64)(base+TICK_SITE)
 widget=c.create_string_buffer(0x130);pointer=c.addressof(widget)
 result=fn(pointer,42,0x123456789,0x876543210)
 assert ticks==[True] and result==0x4688
 assert struct.unpack_from('<Qi4xQQ',widget,0x100)==(pointer,42,0x123456789,0x876543210)
 for address,original,destroy in WARDROBE_SITES:
  fn=c.CFUNCTYPE(c.c_uint64,c.c_void_p,c.c_int,c.c_uint64,c.c_uint64)(base+address)
  for event in (0,1,7):
   widget=c.create_string_buffer(0x130);pointer=c.addressof(widget)
   result=fn(pointer,event,0x123456789,0x876543210)
   assert calls[-1]==(pointer,-1 if destroy else event),calls[-1]
   assert struct.unpack_from('<Qi4xQQ',widget,0x100)==(pointer,event,0x123456789,0x876543210)
   if destroy:assert result==base+address+11+struct.unpack_from('<i',original,7)[0],'Relocated vtable LEA'
 unsupported=bytearray(source);unsupported[offset(WARDROBE_SITES[0][0])]^=1
 try:patch_dll(unsupported)
 except ValueError:pass
 else:raise AssertionError('Unsupported lifecycle accepted')
 print('PASS: native event-pump hook, 9 lifecycle calls, preserved argument registers, exact stack allocation/destructor vtable relocation, unsupported-client rejection')
finally:kernel.VirtualFree(base,0,0x8000)
