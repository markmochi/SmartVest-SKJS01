from pathlib import Path
import io,struct,subprocess,tempfile,importlib.util,hashlib,json
from PIL import Image
root=Path(__file__).resolve().parents[2]/'Main_and_Cameras'
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);Image.new('RGB',(640,480),(40,100,140)).save(p/'frame.jpg')
    binary=p/'integrity.exe'
    subprocess.run(['g++','-std=c++17','-I'+str(root.parent/'testing/system/platform'),'-Wall','-Wextra','-Werror','-I'+str(root/'../testing/main/camera'),str(root/'../testing/main/camera/integrity_test.cpp'),'-lcrypto','-o',str(binary)],check=True)
    subprocess.run([str(binary),str(p/'card'),str(p/'frame.jpg')],check=True)
    spec=importlib.util.spec_from_file_location('camera_evidence',root/'tools/check_evidence.py');auditor=importlib.util.module_from_spec(spec);spec.loader.exec_module(auditor)
    file=p/'card/clips/front_test.avi';raw=file.read_bytes();movi_end=220+struct.unpack_from('<I',raw,216)[0]
    clip={'frames':24,'start':0,'end':3000,'bytes':len(raw),'payloadSha256':hashlib.sha256(raw[224:movi_end]).hexdigest()}
    assert auditor.verify_avi(file,clip)==hashlib.sha256(raw).hexdigest()
    at=224;count=0
    while raw[at:at+4]==b'00dc':
        n=struct.unpack_from('<I',raw,at+4)[0]
        with Image.open(io.BytesIO(raw[at+8:at+8+n])) as image:image.load();assert image.size==(640,480)
        count+=1;at+=8+n+(n&1)
    assert count==24 and raw[at:at+4]==b'idx1'
    print('PASS independently decoded all 24 VGA JPEG frames; no hardware or player playback claimed')
    key=bytes(range(32));clip.update(schema=2,role='front',stem='/clips/front_test',videoStem='/clips/front_test',session=1,first=0,last=2999,maxGapMs=125,good=True,recovered=False,legacy=False,clockSource='unsynced',utcStartMs=None)
    clip['seal']=auditor.clip_tag(clip,key)
    event={'schema':1,'role':'front','boot':1,'sequence':1,'session':1,'test':True,'stage':2,'triggerTimeKnown':True,'prePossible':True,'start':0,'end':2999,'trigger':1500,'transportMarginMs':2000,'clips':[clip]}
    body=json.dumps(event,separators=(',',':'),ensure_ascii=False).encode('utf8');event['eventSeal']=auditor.tag(key,['SMARTVEST EVENT 2','front'],body)
    events=p/'card/system/events';events.mkdir(parents=True);manifest=events/'e_1_1.json';manifest.write_text(json.dumps(event,separators=(',',':')),encoding='utf8')
    locked=file.with_name('front_test_LOCKED.avi');file.rename(locked)
    assert auditor.audit(p/'card',{'front':key,'rear':key})==0
    incident=file.with_name('front_test_PROTECTED_INCIDENT.avi');locked.rename(incident);locked=incident
    assert auditor.audit(p/'card',{'front':key,'rear':key})==0
    duplicate=file.with_name('front_test_LOCKED.avi');duplicate.write_bytes(raw)
    assert auditor.audit(p/'card',{'front':key,'rear':key})==1;duplicate.unlink()
    interruption=file.with_name('front_test_PROTECTED_INTERRUPTION.avi');locked.rename(interruption)
    assert auditor.audit(p/'card',{'front':key,'rear':key})==1;interruption.rename(locked)
    damaged=bytearray(raw);damaged[400]^=1;locked.write_bytes(damaged);assert auditor.audit(p/'card',{'front':key,'rear':key})==1;locked.write_bytes(raw)
    event['end']=2998;manifest.write_text(json.dumps(event,separators=(',',':')),encoding='utf8');assert auditor.audit(p/'card',{'front':key,'rear':key})==1
    print('PASS read-only evidence auditor: valid keyed window, same-size payload tamper and event-window tamper')
