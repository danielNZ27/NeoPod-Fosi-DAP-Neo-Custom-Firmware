"""Stock V137 with a one-way diagnostic takeover after original DAC setup."""
from pathlib import Path
import hashlib,json,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'investigation/nanitecfw/tools'))
from rknano_fw import Image
out=ROOT/'build/neo-stock-dac-diagnostic'
stock=(ROOT/'DP3_V137.img').read_bytes()
assert hashlib.sha256(stock).hexdigest()=='fcc1cd3e35b46f1f8dbcb8da39fcc5eb3f1d469cdfade5e9c6ea3bd71ac6deef'
im=Image(stock); m=im.modules[0]; base=im.l2f(m['code_load'])
symbols=subprocess.check_output([str(ROOT/'toolchains/bin/arm-none-eabi-nm.exe'),str(out/'neo_stock_dac_diag.elf')],text=True)
entry=int(next(l.split()[0] for l in symbols.splitlines() if l.endswith(' stock_diag_entry')),16)
payload=(out/'neo_stock_dac_diag.bin').read_bytes(); payload_addr=0x03079d00
assert payload_addr+len(payload)<=0x0307c7b0
payload_off=base+payload_addr-m['code_exec']; end=payload_off+len(payload)
assert end<=im.l2f(m['data_load'])
# The extra loaded region must be genuine unused image padding.
assert set(stock[base+m['code_len']:end]) <= {0,255}
site=0x03051a84; site_off=base+site-m['code_exec']
assert stock[site_off:site_off+4]==bytes.fromhex('00220421')
delta=entry-(site+4); assert delta%2==0 and -(1<<24)<=delta<(1<<24)
u=delta&0x1ffffff; s=(u>>24)&1;i1=(u>>23)&1;i2=(u>>22)&1
branch=struct.pack('<HH',0xf000|(s<<10)|((u>>12)&1023),0x9000|((1^i1^s)<<13)|((1^i2^s)<<11)|((u>>1)&2047))
im.d[site_off:site_off+4]=branch
im.d[payload_off:end]=payload
struct.pack_into('<I',im.d,0x208+8,end-base)
im.set_date('20261030');struct.pack_into('<HH',im.d,4,0x100,46)
im.finalize()
assert len(im.d)==len(stock) and im.header_copy_ok() and im.crc_stored()==im.crc_calc()
# No stock executable/data/resource changes beyond the one branch and new payload.
assert im.d[base:site_off]==stock[base:site_off]
assert im.d[site_off+4:base+m['code_len']]==stock[site_off+4:base+m['code_len']]
assert im.d[end:im.fw_len]==stock[end:im.fw_len]
path=out/'DP3_V146.IMG';path.write_bytes(im.d)
report={'image':str(path),'sha256':hashlib.sha256(im.d).hexdigest(),'crc32':f'{im.crc_stored():08x}','hook':hex(site),'entry':hex(entry),'branch_hex':branch.hex(),'payload_bytes':len(payload),'stock_original_code_preserved_except_hook':True,'stock_resources_preserved':True}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
