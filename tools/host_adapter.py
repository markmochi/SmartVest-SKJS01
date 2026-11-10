from pathlib import Path
import os,subprocess,tempfile,sys

def install(zig, root):
    original = subprocess.run
    outputs = {}
    zig = str(Path(zig).resolve())
    windows_crypto = Path(root)/'testing/system/platform/windows'

    def run(command, *args, **kwargs):
        if not isinstance(command, (list, tuple)) or not command:
            return original(command, *args, **kwargs)
        command = list(map(str, command))
        if not kwargs.get('capture_output'):
            if 'stdout' not in kwargs and sys.stdout is not sys.__stdout__:kwargs['stdout']=sys.stdout
            if 'stderr' not in kwargs and sys.stderr is not sys.__stderr__:kwargs['stderr']=sys.stderr
        if command[0] == 'g++':
            command = [zig, 'c++', '-UNDEBUG', *command[1:]]
            if os.name == 'nt':
                command = [x for x in command if not x.startswith('-fsanitize=')]
                if '-m32' in command:
                    command.remove('-m32')
                    command += ['-target','x86-windows-gnu']
                if '-lcrypto' in command:
                    command.remove('-lcrypto')
                    command += ['-I'+str(windows_crypto), '-lbcrypt']
            if '-o' in command:
                i = command.index('-o')+1
                p = Path(command[i])
                if os.name == 'nt' and not p.suffix:
                    outputs[str(p)] = str(p)+'.exe'
                    command[i] = outputs[str(p)]
            if '-fsyntax-only' in command:
                command.remove('-fsyntax-only')
                with tempfile.TemporaryDirectory() as tmp:
                    return original([*command, '-c', '-o', str(Path(tmp)/'check.o')], *args, **kwargs)
        elif command[0] in outputs:
            command[0] = outputs[command[0]]
        return original(command, *args, **kwargs)

    subprocess.run = run
