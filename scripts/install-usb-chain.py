#!/usr/bin/env python3
"""Install a separate RAM U-Boot payload; preserve original bootloader binaries."""
import argparse,hashlib,importlib.util,json,os
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);p.add_argument('action',choices=['stage','arm','restore']);args=p.parse_args()
r=args.directory.resolve();plan=json.loads((r/'plan.json').read_text());manifest=json.loads((r/'manifest.json').read_text())
helper=Path(__file__).with_name('install_helpers.py')
spec=importlib.util.spec_from_file_location('helpers',helper);h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)
def sha(data):return hashlib.sha256(data).hexdigest()
def backup(name,data):
 path=r/name
 if path.exists():
  if path.read_bytes()!=data:raise RuntimeError('preimage changed: '+name)
 else:h.save_file(path,data)
def bootcmd():
 s=plan['trial_slot'];n=(manifest['candidate_bytes']+511)//512
 commands=[f'mmc read 0x25000000 {s["start_sector"]:x} {n:x}',f'mmc read 0x24000000 {s["base_env_sector"]:x} 100','mmc read 0x01500000 28125 800','mmc read 0x26000000 30000 3000','mmc read 0x23000000 33f00 14','mmc read 0x01b00000 13781 19a9',f'mmc read 0x01f00000 {s["first_loader_sector"]:x} 1',f'mmc read 0x01f02000 {s["second_loader_sector"]:x} 1']
 commands += [c for c in plan['normal']['bootcmd'].split(';') if c.startswith('mw.l ')]
 commands += ['env set bootcmd ${qua_root_stage2}','go a','dcache off','icache off','go 0x01f00000','b2ndbc']
 result=';'.join(commands)
 assert len(result)<1024 and all(x not in result for x in ['run ','if ','&&','env import','env save'])
 return result.encode()
assert Path('/sys/class/block/mmcblk0/device/cid').read_text().strip()==plan['cid']
slot=plan['trial_slot'];start=slot['start_sector'];size=slot['sectors']*512
assert start==0x33800 and size==0x200*512
for protected in plan['protected_ranges']:assert not(start<protected['end'] and protected['start']<start+slot['sectors'])
fd=os.open('/dev/mmcblk0',os.O_RDWR)
try:
 assert sha(os.pread(fd,733664,0x20800))==plan['original_bootcode_sha256']
 assert sha(os.pread(fd,1048576,0x5024a00))==plan['buffers']['stage2']['sha256']
 payload=(r/'trial-slot.bin').read_bytes();assert len(payload)==size and sha(payload)==manifest['trial_slot_sha256']
 if args.action!='restore':
  for name in ['kernel','dtb','audio']:
   b=plan['buffers'][name];assert sha(os.pread(fd,b['bytes'],b['lba']*512))==b['sha256'],name
 if args.action=='stage':
  before=os.pread(fd,size,start*512);assert sha(before)==plan['buffers']['trial_slot_before']['sha256']
  backup('slot-before.bin',before);backup('factory-before.bin',os.pread(fd,0x400000,0x220000))
  h.pwrite_verified(fd,start*512,payload)
 else:
  assert sha(os.pread(fd,size,start*512))==manifest['trial_slot_sha256']
  for bank in [0x220000,0x420000]:
   h.FACTORY_OFFSET=bank;offset,raw,pairs,end=h.read_env(fd);env=dict(pairs)
   assert env[b'bootcmd'] in (plan['normal']['bootcmd'].encode(),bootcmd())
   assert env[b'bootargs']==plan['normal']['bootargs'].encode()
   if args.action=='arm':backup('environment-before-arm-'+hex(bank)+'.bin',raw)
   desired=bootcmd() if args.action=='arm' else plan['normal']['bootcmd'].encode()
   updated=[(key,desired if key==b'bootcmd' else b'2' if key==b'bootdelay' else value) for key,value in pairs]
   data=h.encode_env(raw,updated,end);h.pwrite_verified(fd,offset,data);assert h.read_env(fd)[1]==data
 result={'action':args.action,'nonce':plan['nonce'],'original_delay_seconds':2,'new_delay_seconds':3,'original_bootcode_unchanged':sha(os.pread(fd,733664,0x20800))==plan['original_bootcode_sha256']}
 h.save_file(r/(args.action+'-result.json'),(json.dumps(result,indent=2)+'\n').encode());print(json.dumps(result,indent=2))
finally:os.close(fd)
