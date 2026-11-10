"""Check Main against both the IDE limit and its preserved physical app slots.

Usage: python tools/check_flash_budget.py PATH_TO_BUILD_DIRECTORY
The existing physical partition table is deliberately not changed by this tool.
"""
from pathlib import Path
import argparse
import csv

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build', type=Path)
args = parser.parse_args()
image = args.build / 'SmartVest_SJKS01_Main.ino.bin'
size = image.stat().st_size
limit = 1310720
free = limit - size
assert free >= 8 * 1024, f'Main image {size:,} bytes leaves {free:,}; requires 8 KiB below the IDE limit'
source = Path(__file__).resolve().parents[1] / 'SmartVest_SJKS01_Main/partitions.csv'
assert (args.build / 'partitions.csv').read_bytes() == source.read_bytes(), 'Partition table mismatch'
slots = [int(row[4].strip(), 0) for row in csv.reader(source.read_text().splitlines())
         if len(row) >= 5 and row[1].strip() == 'app']
assert len(slots) == 2 and min(slots) == 0x1C0000, 'Unexpected physical application slots'
physical_free = min(slots) - size
assert physical_free >= 24 * 1024, 'Physical application reserve below 24 KiB'
print(f'PASS Main binary {size:,} bytes; {free:,} bytes below IDE limit (8 KiB minimum)')
print(f'PASS preserved physical app slots leave {physical_free:,} bytes (24 KiB minimum)')
print('PASS compiled partition table matches the preserved project partition table')
