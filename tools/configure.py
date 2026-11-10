"""Create ignored configuration files for a new SmartVest deployment."""

from pathlib import Path
import os
import secrets


ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / 'Main_and_Cameras/SmartVest_SJKS01_Main/Config.h'
CAMERA = ROOT / 'Main_and_Cameras/SmartVest_SJKS01_Camera/CameraConfig.h'
KEYS = ROOT / 'common/CameraPairingKeys.h'
KEY_COPIES = (
    MAIN.parent / 'src/security/CameraPairingKeys.h',
    CAMERA.parent / 'src/CameraPairingKeys.h',
)
LAB = ROOT / 'IMU Lab Test/SmartVest_IMU_Finalization_Direction_Fixed/LabWiFi.h'
CREDENTIALS = ROOT / 'smartvest-credentials.txt'


def render(path, values):
    template = path.with_name(path.name + '.example').read_text(encoding='utf-8')
    for name, value in values.items():
        template = template.replace('@' + name + '@', value)
    if '@' in template:
        raise SystemExit(f'Unresolved template value in {path.name}')
    return template


def private_file(path, flags):
    return os.open(path, flags, 0o600)


def main():
    targets = (MAIN, CAMERA, KEYS, *KEY_COPIES, LAB, CREDENTIALS)
    existing = [str(path.relative_to(ROOT)) for path in targets if path.exists()]
    if existing:
        raise SystemExit('Existing configuration was preserved: ' + ', '.join(existing))

    ap = {'AP_NAME': 'SmartVest-' + secrets.token_hex(3),
          'AP_PASSWORD': secrets.token_urlsafe(18)}
    accounts = {'RIDER_PASSWORD': secrets.token_urlsafe(18),
                'DEVELOPER_PASSWORD': secrets.token_urlsafe(18)}
    pairing = render(KEYS, {'FRONT_KEY': secrets.token_hex(32), 'REAR_KEY': secrets.token_hex(32)})
    payloads = {
        MAIN: render(MAIN, {**ap, **accounts}),
        CAMERA: render(CAMERA, ap),
        KEYS: pairing,
        **dict.fromkeys(KEY_COPIES, pairing),
        LAB: render(LAB, ap),
        CREDENTIALS: (
            f"Vest Wi-Fi: {ap['AP_NAME']}\nWi-Fi password: {ap['AP_PASSWORD']}\n"
            f"Rider: admin\nInitial rider password: {accounts['RIDER_PASSWORD']}\n"
            f"Developer: developer\nInitial developer password: {accounts['DEVELOPER_PASSWORD']}\n"
        ),
    }
    created = []
    try:
        for path, content in payloads.items():
            with open(path, 'x', encoding='utf-8', newline='\n', opener=private_file) as file:
                created.append(path)
                file.write(content)
    except OSError:
        for path in reversed(created):
            path.unlink()
        raise
    print('Created local configuration. Keep smartvest-credentials.txt private.')


if __name__ == '__main__':
    main()
