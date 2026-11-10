"""Compile real configuration headers, deliberately introducing collisions.

Checks GPIO reservations even for disabled peripherals, invalid IO/ADC pins,
unsupported board/PSRAM profiles, matching protocol/baud and shipped switches.
Does not establish physical wiring or sensor circuit safety.
"""
from pathlib import Path
import itertools
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
main = root/'Main_and_Cameras/SmartVest_SJKS01_Main'
sensing = root/'Sensing/SmartVest_SJKS01_Sensing'
checks = 0

def mutate(text, name, value):
    text, count = re.subn(r'(\b'+name+r'\s*=(?!=)\s*)[^,;\n]+', lambda m: m[1]+str(value), text)
    assert count == 1, (name, count)
    return text

def compile_config(source, host, changes=(), defines=(), expected=True):
    global checks
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        text = (source/'Config.h').read_text()
        for name, value in changes:
            text = mutate(text, name, value)
        (tmp/'Config.h').write_text(text)
        validation = source/'src/network/ConfigValidation.h'
        if validation.exists():
            target = tmp/'src/network/ConfigValidation.h'
            target.parent.mkdir(parents=True)
            shutil.copyfile(validation, target)
        (tmp/'check.cpp').write_text('#include "Config.h"\nint main() {}\n')
        result = subprocess.run(['g++', '-std=c++17', '-I'+str(host), '-I'+str(tmp),
                                 *['-D'+d for d in defines], '-fsyntax-only',
                                 str(tmp/'check.cpp')], capture_output=True, text=True)
        assert (result.returncode == 0) == expected, (changes, defines, result.stderr)
        if not expected:
            assert 'static assertion failed' in result.stderr or '#error' in result.stderr, result.stderr
        checks += 1

main_host = root/'testing/main/host'
sense_host = root/'testing/sensing/host'
classic = ['ARDUINO_ARCH_ESP32=1', 'CONFIG_IDF_TARGET_ESP32=1', 'CORE_DEBUG_LEVEL=0']
compile_config(main, main_host, defines=classic)
compile_config(sensing, sense_host, defines=classic)

main_pins = [('SIM800L_RX_PIN',16), ('SIM800L_TX_PIN',17), ('NEO6M_RX_PIN',4),
             ('NEO6M_TX_PIN',5), ('I2C_SDA_PIN',21), ('I2C_SCL_PIN',22),
             ('SENSOR_RX_PIN',26), ('SENSOR_TX_PIN',27), ('LED_RED_PIN',23),
             ('LED_GREEN_PIN',18), ('LED_BLUE_PIN',19), ('DEBUG_TX_PIN',1)]
for (name,_), (_, pin) in itertools.combinations(main_pins,2):
    compile_config(main,main_host,[(name,pin)],expected=False)
for pin in [0,2,3,5,6,11,12,15,20,24,28,31,34,35,36,39,40,-1]:
    compile_config(main,main_host,[('SENSOR_TX_PIN',pin)],expected=False)
# Disabled features still reserve their pins.
for flag,pin in [('ENABLE_SIM800L',16),('ENABLE_NEO6M',4)]:
    compile_config(main,main_host,[(flag,'false'),('SENSOR_RX_PIN',pin)],expected=False)
compile_config(main,main_host,[('SENSOR_RX_PIN',23)],expected=False)
compile_config(main,main_host,[('SENSOR_BAUD',9600)],expected=False)
compile_config(main,main_host,defines=['CORE_DEBUG_LEVEL=1'],expected=False)

sense_pins=[('MAIN_RX_PIN',16),('MAIN_TX_PIN',17),('SPI_SCK_PIN',18),('SPI_MISO_PIN',19),('SPI_MOSI_PIN',23),('SPI_CS_PIN',21),('FRONT_PIEZO_PIN',34),('REAR_PIEZO_PIN',35),('BATTERY_RX_PIN',32),('BATTERY_TX_PIN',33)]
for (name,_),(_,pin) in itertools.combinations(sense_pins,2):
    compile_config(sensing,sense_host,[(name,pin)],expected=False)
for pin in [0,1,2,3,5,6,11,12,15,20,24,28,31,34,35,36,39,40,-1]:
    compile_config(sensing,sense_host,[('MAIN_TX_PIN',pin)],expected=False)
for pin in [4,16,17,21,22,25,26,27,37,38]:
    compile_config(sensing,sense_host,[('FRONT_PIEZO_PIN',pin)],expected=False)
compile_config(sensing,sense_host,[('PIEZO_IMPACT_DETECTION_ENABLED','true')])
compile_config(sensing,sense_host,[('PIEZO_IMPACT_DETECTION_ENABLED','false')])
compile_config(sensing,sense_host,[('LINK_BAUD',9600)],expected=False)
compile_config(sensing,sense_host,[('BATTERY_BAUD',9600)],expected=False)
compile_config(sensing,sense_host,[('BATTERY_TX_PIN',10)],expected=False)
compile_config(sensing,sense_host,[('ARM_CRASH_DETECTION','false')],expected=True)
compile_config(sensing,sense_host,defines=['SV_PIEZO_MAPPING_CONFIRMED=1','SV_PIEZO_CIRCUIT_VERIFIED=0'],expected=False)
compile_config(sensing,sense_host,defines=['SV_PIEZO_MAPPING_CONFIRMED=1','SV_PIEZO_CIRCUIT_VERIFIED=1'])
for src,host in [(main,main_host),(sensing,sense_host)]:
    compile_config(src,host,defines=['ARDUINO_ARCH_ESP32=1','CONFIG_IDF_TARGET_ESP32S3=1'],expected=False)
    compile_config(src,host,defines=[*classic,'BOARD_HAS_PSRAM=1'],expected=False)

assert (main/'src/link/CrashProtocol.h').read_bytes() == (sensing/'src/link/CrashProtocol.h').read_bytes()
assert 'HardwareSerial uart{0}' in (main/'src/link/SensorLink.h').read_text()
assert 'HardwareSerial uart{1}' in (main/'src/gps/NEO6MService.h').read_text()
assert 'uart(2)' in (main/'src/gsm/SIM800LService.h').read_text()
assert 'HardwareSerial uart{2}' in (sensing/'src/link/EventLink.h').read_text()
print(f'PASS: {checks} real-header compile checks; complete GPIO reservation map, invalid pins, board gates, baud, protocol and UART ownership')
