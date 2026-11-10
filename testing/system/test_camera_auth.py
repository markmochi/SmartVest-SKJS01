from pathlib import Path
import hashlib,hmac,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
    binary=Path(tmp)/'auth.exe'
    subprocess.run(['g++','-std=c++17','-I'+str(root/'testing/system/platform'),'-Wall','-Wextra','-Werror',str(root/'testing/system/test_camera_auth.cpp'),'-lcrypto','-o',str(binary)],check=True)
    text=subprocess.check_output([str(binary)],text=True)
    expected=hmac.new(bytes(range(32)),b'abc',hashlib.sha256).hexdigest()
    assert 'HMAC '+expected in text
    print(text,end='')
