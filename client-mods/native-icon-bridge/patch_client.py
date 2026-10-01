"""Load the bridge before Lua queues creation of a browser view (4.8 NA x64)."""
import struct

SITE = 0x604AE0
ORIGINAL = bytes.fromhex('488bc44881ec98000000')
TICK_SITE = 0x131ef0
TICK_ORIGINAL = bytes.fromhex('48894c2408b888460000')
WARDROBE_SITES = (
    (0x4ae1c0, bytes.fromhex('488bc44881ecf8020000'), False),
    (0x4ad360, bytes.fromhex('4883ec28488d056d337900'), True),
    (0x803680, bytes.fromhex('4883ec28488d054dd04300'), True),
)
AWESOMIUM_SHA256 = '618b62df03032a94cdef5800f6de92a1f0b297234efc54ccb6a82fe5f086ceff'

def align(value, boundary):
    return (value + boundary - 1) // boundary * boundary

def patch_dll(source):
    data = bytearray(source)
    pe = struct.unpack_from('<I', data, 60)[0]
    if data[pe:pe+4] != b'PE\0\0' or struct.unpack_from('<H', data, pe+4)[0] != 0x8664:
        raise ValueError('Expected x64 PE client')
    optional = pe + 24
    if struct.unpack_from('<H', data, optional)[0] != 0x20b:
        raise ValueError('Expected PE32+')
    count = struct.unpack_from('<H', data, pe+6)[0]
    table = optional + struct.unpack_from('<H', data, pe+20)[0]
    sections = [struct.unpack_from('<8sIIIIIIHHI', data, table+i*40) for i in range(count)]
    def offset(rva):
        for name, size, address, raw_size, raw, *_ in sections:
            if address <= rva < address + raw_size:
                return raw + rva - address
        raise ValueError(f'Unmapped RVA {rva:x}')
    site = offset(SITE)
    if data[site:site+len(ORIGINAL)] != ORIGINAL:
        raise ValueError('Unsupported or already patched CreateWebView prologue')
    if data[offset(TICK_SITE):offset(TICK_SITE)+len(TICK_ORIGINAL)] != TICK_ORIGINAL:
        raise ValueError('Unsupported native browser event pump')
    for address, original, _ in WARDROBE_SITES:
        if data[offset(address):offset(address)+len(original)] != original:
            raise ValueError(f'Unsupported Wardrobe lifecycle prologue {address:x}')
    for address, original in ((0x8053f0,bytes.fromhex('4c8bdc4881ec78040000')),
                              (0x804bd0,bytes.fromhex('4883ec2848895c2430'))):
        if data[offset(address):offset(address)+len(original)] != original:
            raise ValueError(f'Unsupported native character preview {address:x}')
    new_header = table + count*40
    headers_size = struct.unpack_from('<I', data, optional+60)[0]
    if new_header+40 > headers_size or any(data[new_header:new_header+40]):
        raise ValueError('No spare section header')
    section_align, file_align = struct.unpack_from('<II', data, optional+32)
    rva = align(max(s[2]+max(s[1], s[3]) for s in sections), section_align)
    raw = align(len(data), file_align)
    old_imports, _ = struct.unpack_from('<II', data, optional+120)
    imports = bytearray()
    cursor = offset(old_imports)
    while any(data[cursor:cursor+20]):
        imports.extend(data[cursor:cursor+20]); cursor += 20
        if len(imports)>4096:
            raise ValueError('Unexpected import directory')
    payload = bytearray(imports + bytes(40))
    descriptor = len(imports)
    def append(value, alignment=1):
        payload.extend(bytes(align(len(payload), alignment)-len(payload)))
        result = rva+len(payload); payload.extend(value); return result
    module = append(b'AionIconBridge.dll\0')
    name = append(b'\0\0AionIconBridgeInitialize\0', 2)
    visibility_name = append(b'\0\0AionWardrobeVisibility\0', 2)
    tick_name = append(b'\0\0AionWardrobeTick\0', 2)
    lookup = append(struct.pack('<QQQQ', name, visibility_name, tick_name, 0), 8)
    iat = append(struct.pack('<QQQQ', name, visibility_name, tick_name, 0), 8)
    struct.pack_into('<IIIII', payload, descriptor, lookup, 0, 0, module, iat)
    # Preserve the Lua state and argument registers across the initialization call.
    # Entry RSP is 8 mod 16; 0x48 reserves aligned stack + Windows shadow space.
    hook = append(b'', 16)
    code = bytearray.fromhex('4883ec4848894c242048895424284c894424304c894c2438')
    call_rva = hook+len(code)
    code += b'\xff\x15'+struct.pack('<i', iat-(call_rva+6))
    code += bytes.fromhex('488b4c2420488b5424284c8b4424304c8b4c24384883c448')
    code += ORIGINAL
    code += b'\xe9'+struct.pack('<i', SITE+len(ORIGINAL)-(hook+len(code)+5))
    payload.extend(code)
    hooks = []
    tick_entry = append(b'',16)
    code = bytearray.fromhex('4883ec4848894c242048895424284c894424304c894c2438')
    call = tick_entry+len(code)
    code += b'\xff\x15'+struct.pack('<i',iat+16-(call+6))
    code += bytes.fromhex('488b4c2420488b5424284c8b4424304c8b4c24384883c448')
    code += TICK_ORIGINAL
    code += b'\xe9'+struct.pack('<i',TICK_SITE+len(TICK_ORIGINAL)-(tick_entry+len(code)+5))
    payload.extend(code);hooks.append((TICK_SITE,TICK_ORIGINAL,tick_entry))
    for address, original, destroy in WARDROBE_SITES:
        entry = append(b'',16)
        code = bytearray.fromhex('4883ec4848894c242048895424284c894424304c894c2438')
        if destroy: code += bytes.fromhex('baffffffff')
        call = entry+len(code)
        code += b'\xff\x15'+struct.pack('<i',iat+8-(call+6))
        code += bytes.fromhex('488b4c2420488b5424284c8b4424304c8b4c24384883c448')
        relocated = bytearray(original)
        if destroy:
            # The destructor's LEA points at its original native vtable.
            target = address+11+struct.unpack_from('<i',original,7)[0]
            struct.pack_into('<i',relocated,7,target-(entry+len(code)+11))
        code += relocated
        code += b'\xe9'+struct.pack('<i',address+len(original)-(entry+len(code)+5))
        payload.extend(code);hooks.append((address,original,entry))
    virtual_size = len(payload)
    raw_size = align(virtual_size, file_align)
    payload.extend(bytes(raw_size-len(payload)))
    data.extend(bytes(raw-len(data))); data.extend(payload)
    struct.pack_into('<8sIIIIIIHHI', data, new_header, b'.aicons\0', virtual_size, rva, raw_size, raw, 0, 0, 0, 0, 0xE0000060)
    struct.pack_into('<H', data, pe+6, count+1)
    struct.pack_into('<I', data, optional+56, align(rva+virtual_size,section_align))
    struct.pack_into('<I', data, optional+4, struct.unpack_from('<I',data,optional+4)[0]+raw_size)
    struct.pack_into('<I', data, optional+8, struct.unpack_from('<I',data,optional+8)[0]+raw_size)
    struct.pack_into('<I', data, optional+64, 0)  # checksum regenerated by loader when needed
    struct.pack_into('<II', data, optional+120, rva, len(imports)+40)
    data[site:site+len(ORIGINAL)] = b'\xe9'+struct.pack('<i',hook-(SITE+5))+b'\x90'*(len(ORIGINAL)-5)
    for address, original, entry in hooks:
        pos=offset(address)
        data[pos:pos+len(original)] = b'\xe9'+struct.pack('<i',entry-(address+5))+b'\x90'*(len(original)-5)
    return bytes(data)
