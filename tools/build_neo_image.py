"""Pack neo_diag.elf into a valid Fosi DAP Neo firmware update image.

Takes stock DP3_V137.img as template, substitutes Module 0 with neo_diag,
updates module 0 metadata, sets version/date, syncs header copy, and fixes CRC32.
"""
import argparse
import struct, sys, hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "investigation/nanitecfw/tools"))
from rknano_fw import Image, rkcrc, TABLE_OFF, ENTRY_FIELDS
STOCK_IMG = ROOT / "DP3_V137.img"
ELF_PATH = ROOT / "echopod/platform/neo_diag/neo_diag.elf"
OUT_IMG = ROOT / "echopod/build/neo_diag/DP3_V138.IMG"

sys.path.insert(0, str(ROOT / "echopod/tools"))
from make_image import firmware_from_elf, overlay_from_elf, crc32

def build():
    global ELF_PATH, OUT_IMG
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', type=int, default=138)
    parser.add_argument('--sd-output', type=Path)
    parser.add_argument('--elf', type=Path, default=ELF_PATH)
    parser.add_argument('--output', type=Path, default=OUT_IMG)
    parser.add_argument('--overlay', type=Path)
    parser.add_argument('--core1-mp3', type=Path)
    parser.add_argument('--core1-flac', type=Path)
    parser.add_argument('--core1-aac', type=Path)
    parser.add_argument('--core1-opus', type=Path)
    args = parser.parse_args()
    ELF_PATH, OUT_IMG = args.elf, args.output
    if not 138 <= args.version <= 599:
        raise ValueError('Neo update version must be 138..599')
    if args.sd_output:
        args.sd_output.mkdir(parents=True, exist_ok=True)
    print(f"Reading stock image {STOCK_IMG}...")
    stock_data = STOCK_IMG.read_bytes()
    if hashlib.sha256(stock_data).hexdigest() != 'fcc1cd3e35b46f1f8dbcb8da39fcc5eb3f1d469cdfade5e9c6ea3bd71ac6deef':
        raise ValueError('Unexpected stock template SHA-256')
    im = Image(stock_data)
    if not im.header_copy_ok() or im.crc_stored() != im.crc_calc():
        raise ValueError('Stock template integrity check failed')
    
    print(f"Reading ELF {ELF_PATH}...")
    elf_bytes = ELF_PATH.read_bytes()
    fw = firmware_from_elf(elf_bytes)
    
    code = fw.code
    data = fw.data
    bss_size = fw.bss_size
    
    print(f"Firmware code size: {len(code)} bytes, data: {len(data)} bytes, bss: {bss_size} bytes")
    
    mod0 = im.modules[0]
    code_file_off = im.l2f(mod0["code_load"])
    data_file_off = im.l2f(mod0["data_load"])
    
    print(f"Module 0 code file offset: 0x{code_file_off:x}, max len: 0x{mod0['code_len']:x}")
    print(f"Module 0 data file offset: 0x{data_file_off:x}, max len: 0x{mod0['data_len']:x}")
    
    if len(code) > mod0["code_len"]:
        raise ValueError(f"Code size {len(code)} exceeds mod0 capacity {mod0['code_len']}")
    if len(data) > mod0["data_len"]:
        raise ValueError(f"Data size {len(data)} exceeds mod0 capacity {mod0['data_len']}")
        
    # Write code into Module 0 slot (zero out the rest of the old code)
    im.d[code_file_off:code_file_off + len(code)] = code
    im.d[code_file_off + len(code):code_file_off + mod0["code_len"]] = b"\x00" * (mod0["code_len"] - len(code))
    
    # Write data into Module 0 slot
    im.d[data_file_off:data_file_off + len(data)] = data
    im.d[data_file_off + len(data):data_file_off + mod0["data_len"]] = b"\x00" * (mod0["data_len"] - len(data))
    
    # Describe the replacement module, including its larger BSS.
    values = (mod0['code_load'], 0x03050000, len(code),
              mod0['data_load'], 0x03004000, len(data),
              0x03004000 + len(data), bss_size)
    if values[6] + bss_size > 0x0304C000:
        raise ValueError('Firmware BSS exceeds data RAM')
    struct.pack_into('<8I', im.d, TABLE_OFF + 8, *values)
    # Auxiliary programs occupy the original trailer padding after the header copy.
    if args.overlay:
        overlay = overlay_from_elf(args.overlay.read_bytes())
        if not 16 <= len(overlay) <= 0x25000:
            raise ValueError('Rescan overlay exceeds Neo allocation')
        packed = b'EOVL' + struct.pack('<III', len(overlay), crc32(overlay[16:]), 0) + overlay[16:]
        if args.sd_output:
            (args.sd_output / 'neo_scan.bin').write_bytes(packed)
        im.d[0xC94400:0xC94400 + len(packed)] = packed
    programs = [
        (args.core1_mp3, 0xCB9400, 'neo_mp3.bin'),
        (args.core1_flac, 0xCD9600, 'neo_flac.bin'),
        (args.core1_aac, None, 'neo_aac.bin'),
        (args.core1_opus, None, 'neo_opus.bin')
    ]
    for source, offset, name in programs:
        if source:
            program = source.read_bytes()
            if not 0 < len(program) <= 0x20000:
                raise ValueError(f'Core 1 program {name} exceeds Neo allocation ({len(program)} bytes)')
            packed = b'EC1M' + struct.pack('<III', len(program), crc32(program), 0) + bytes(496) + program
            if args.sd_output:
                (args.sd_output / name).write_bytes(packed)
            if offset is not None:
                im.d[offset:offset + len(packed)] = packed

    # Update BCD build date: 2026-10-31
    im.set_date("20261031")
    
    # Update version field in header at 0x4 (version 1.38 = 0x01, 0x26 = 38)
    struct.pack_into("<HH", im.d, 0x4, 0x0100, args.version - 100)
    
    # Finalize: copy header to fw_size (0xc94000) and calculate CRC
    im.finalize()
    
    if len(im.d) != len(stock_data) or not im.header_copy_ok() or im.crc_stored() != im.crc_calc():
        raise ValueError('Generated image integrity check failed')
    OUT_IMG.parent.mkdir(parents=True, exist_ok=True)
    OUT_IMG.write_bytes(im.d)
    rknano_img = OUT_IMG.parent / "RKNANOFW.IMG"
    rknano_img.write_bytes(im.d)
    print(f"Successfully generated Neo firmware image: {OUT_IMG}")
    print(f"Also written: {rknano_img}")
    print(f"Size: {len(im.d)} bytes (0x{len(im.d):x})")
    print(f"CRC-32: 0x{im.crc_stored():08x} (calc: 0x{im.crc_calc():08x})")
    print(f"SHA-256: {hashlib.sha256(im.d).hexdigest()}")

if __name__ == "__main__":
    build()
