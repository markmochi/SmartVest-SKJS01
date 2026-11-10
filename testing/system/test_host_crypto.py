from pathlib import Path
import hashlib,subprocess,tempfile

with tempfile.TemporaryDirectory() as folder:
    root=Path(folder);source=root/'crypto.cpp';binary=root/'crypto'
    source.write_text('''#include <openssl/evp.h>
#include <cstdio>
int main(){unsigned char out[32];if(!PKCS5_PBKDF2_HMAC("password",8,(const unsigned char*)"salt",4,10000,EVP_sha256(),32,out))return 1;for(unsigned char b:out)std::printf("%02x",b);}
''',encoding='utf8')
    subprocess.run(['g++','-std=c++17',str(source),'-lcrypto','-o',str(binary)],check=True)
    actual=subprocess.run([str(binary)],check=True,capture_output=True,text=True).stdout
    expected=hashlib.pbkdf2_hmac('sha256',b'password',b'salt',10000).hex()
    assert actual==expected
print('PASS host PBKDF2 implementation matches Python hashlib known-input result')
