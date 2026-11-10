"""Read-only keyed SD evidence audit; --decode additionally needs ffprobe/ffmpeg.
No card file is unlocked, deleted or rewritten. Keys are read locally, never printed.
"""
import argparse,hashlib,hmac,json,re,struct,subprocess
from pathlib import Path

def tag(key,fields,body=b''):
    return hmac.new(key,(''.join(str(x)+'\n' for x in fields)).encode('utf8')+body,hashlib.sha256).hexdigest()

def clip_tag(c,key):
    fields=['SMARTVEST CLIP 2',c['role'],c['stem'],c.get('videoStem',c['stem']),c['clockSource']]
    fields += [c['session'],c.get('utcStartMs') or 0,c['start'],c['end'],c['first'],c['last'],c['bytes'],c['frames'],c['maxGapMs'],int(c['good']),int(c['recovered']),int(c['legacy']),c['payloadSha256']]
    return tag(key,fields)

def canonical_header(frames,duration,peak,movi_end):
    out=bytearray()
    def four(s):out.extend(s.encode('ascii'))
    def u32(n):out.extend(struct.pack('<I',n))
    def u16(n):out.extend(struct.pack('<H',n))
    four('RIFF');u32(0);four('AVI ');four('LIST');u32(192);four('hdrl');four('avih');u32(56);u32(125000);u32(0);u32(0);u32(16)
    for n in [0,0,1,0,640,480,0,0,0,0]:u32(n)
    four('LIST');u32(116);four('strl');four('strh');u32(56);four('vids');four('MJPG')
    for n in [0,0,0,1,8,0,0,0,0xffffffff,0]:u32(n)
    for n in [0,0,640,480]:u16(n)
    four('strf');u32(40);u32(40);u32(640);u32(480);u16(1);u16(24);four('MJPG');u32(640*480*3)
    for n in [0,0,0,0]:u32(n)
    four('LIST');u32(0);four('movi');assert len(out)==224
    for at,n in {4:movi_end+frames*16,32:duration*1000//frames,48:frames,60:peak,128:duration,132:frames*1000,140:frames,144:peak,216:movi_end-220}.items():struct.pack_into('<I',out,at,n)
    return bytes(out)

def verify_avi(path,c):
    size=path.stat().st_size;frames=c['frames'];duration=c['end']-c['start']
    assert 0<frames<=1024 and 0<duration<=120000 and size==c['bytes']<=128*1024*1024,'Invalid frame/duration/size bounds'
    payload=hashlib.sha256();whole=hashlib.sha256()
    with path.open('rb') as f:
        header=f.read(224);assert len(header)==224,'Short AVI header'
        movi_end=220+struct.unpack_from('<I',header,216)[0]
        assert movi_end+8+frames*16==size,'Index/file size mismatch'
        peak=struct.unpack_from('<I',header,60)[0]
        assert header==canonical_header(frames,duration,peak,movi_end),'Noncanonical AVI header'
        whole.update(header);entries=[];largest=0
        for _ in range(frames):
            at=f.tell();chunk=f.read(8);assert len(chunk)==8 and chunk[:4]==b'00dc','Invalid frame chunk'
            n=struct.unpack_from('<I',chunk,4)[0]
            assert 4<=n<=1024*1024 and f.tell()+n+(n&1)<=movi_end,'Invalid frame size'
            payload.update(chunk);whole.update(chunk);first=b'';last=b'';left=n
            while left:
                block=f.read(min(left,8192));assert block,'Short frame'
                first=(first+block)[:2];last=(last+block)[-2:];payload.update(block);whole.update(block);left-=len(block)
            assert first==b'\xff\xd8' and last==b'\xff\xd9','Invalid JPEG boundaries'
            if n&1:
                pad=f.read(1);assert pad==b'\0','Invalid frame padding';payload.update(pad);whole.update(pad)
            entries.append(struct.pack('<4sIII',b'00dc',16,at-220,n));largest=max(largest,n)
        assert f.tell()==movi_end and largest==peak,'Frame boundary/peak mismatch'
        index=f.read();assert index==b'idx1'+struct.pack('<I',frames*16)+b''.join(entries),'Invalid AVI index'
        whole.update(index)
    assert hmac.compare_digest(payload.hexdigest(),c['payloadSha256']),'Payload integrity mismatch'
    return whole.hexdigest()

def audit(root,keys,decode=False):
    failures=0;manifests=sorted(list((root/'events').glob('*.json'))+list((root/'system/events').glob('*.json')))
    for path in manifests:
        try:
            assert path.resolve().is_relative_to(root),'Manifest escapes card root'
            d=json.loads(path.read_text(encoding='utf8'));print(f"Event {d['boot']}/{d['sequence']} role={d['role']} stage={d['stage']}")
            assert d['stage']==2,'Incomplete: retained footage is not a successful full window'
            key=keys[d['role']];saved=d.pop('eventSeal')
            body=json.dumps(d,separators=(',',':'),ensure_ascii=False).encode('utf8')
            assert hmac.compare_digest(saved,tag(key,['SMARTVEST EVENT 2',d['role']],body)),'Event seal mismatch or legacy event'
            clips=d['clips'];assert clips and len(clips)<=16,'Invalid clip count'
            assert d['triggerTimeKnown'] and d['prePossible'],'Unknown/incomplete timing'
            assert clips[0]['first']<=d['start'] and clips[-1]['last']>=d['end'],'Missing window coverage'
            previous=0
            for c in clips:
                assert c['schema']==2 and c['good'] and not c['legacy'] and not c['recovered'],'Unverified legacy/recovered/unfinished clip'
                assert c['role']==d['role'] and hmac.compare_digest(c['seal'],clip_tag(c,key)),'Clip seal mismatch'
                assert c['session']==d['session'] and c['maxGapMs']<=2000,'Session/gap mismatch'
                assert not previous or c['first']<=previous+2000,'Gap between clips'
                previous=c['last'];stem=c['stem'];video=c.get('videoStem',stem)
                for value in (stem,video):assert re.fullmatch(r'/clips/(?:c_|front_|rear_)[a-z0-9_-]+',value) and len(value)<=80,'Invalid clip path'
                candidates=[(root/(video.lstrip('/')+suffix)).resolve() for suffix in ('_PROTECTED_INTERRUPTION.avi','_PROTECTED_INCIDENT.avi','_LOCKED.avi','.avi')]
                assert all(p.is_relative_to(root) for p in candidates),'Video escapes card root'
                found=[p for p in candidates if p.exists()]
                assert len(found)==1,'Missing or ambiguous recording copies'
                file=found[0]
                assert not file.name.endswith('_PROTECTED_INTERRUPTION.avi'),'Recovered footage is not a complete incident window'
                pins=[root/(stem.lstrip('/')+'.lock'),root/'system/clips'/(stem[7:]+'.lock')]
                assert file!=candidates[-1] or any(p.resolve().is_relative_to(root) and p.exists() and p.read_bytes()==b'L' for p in pins),'Missing protection'
                digest=verify_avi(file,c);print(' ',file.name,'frames=',c['frames'],'verified sha256=',digest)
                if decode:
                    info=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-show_streams','-of','json',str(file)]))['streams'][0]
                    assert info['codec_name']=='mjpeg' and int(info['nb_read_frames'])==c['frames']
                    subprocess.run(['ffmpeg','-v','error','-xerror','-i',str(file),'-f','null','-'],check=True)
        except (KeyError,ValueError,TypeError,OSError,AssertionError,subprocess.CalledProcessError) as error:
            print('  FAIL:',error);failures+=1
    if not manifests:print('FAIL: no event manifests');failures+=1
    return failures

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('card_root',type=Path);parser.add_argument('--decode',action='store_true')
    parser.add_argument('--pairing-header',type=Path,default=Path(__file__).resolve().parents[2]/'common/CameraPairingKeys.h');args=parser.parse_args()
    try:
        text=args.pairing_header.read_text(encoding='utf8');keys={role.lower():bytes.fromhex(key) for role,key in re.findall(r'constexpr char (FRONT|REAR)_KEY\[\] = "([0-9a-f]{64})";',text)}
        assert set(keys)=={'front','rear'},'Matching private pairing header is required'
        return bool(audit(args.card_root.resolve(),keys,args.decode))
    except (OSError,AssertionError):print('FAIL: matching private pairing header unavailable');return 1
if __name__=='__main__':raise SystemExit(main())
