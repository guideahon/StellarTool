import argparse
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from savefile import SaveFile

VERSION = '2.0.0'

def parse_args():
  parser = argparse.ArgumentParser(
    description='Stellar Blade save file JSON converter',
    formatter_class=argparse.ArgumentDefaultsHelpFormatter
  )
  parser.add_argument('command', choices=['tojson', 'fromjson', 'fix'], help='Command to execute')
  parser.add_argument('input', help='Input file path')
  parser.add_argument('output', nargs='?', help='Output file path (not required for fix)')
  parser.add_argument('-i', '--indent', type=int, default=2, help='JSON indent level (tojson only)')
  parser.add_argument('-v', '--version', action='version', version=VERSION)
  return parser.parse_args()

def main():
  args = parse_args()

  if not os.path.exists(args.input):
    print(f'Error: input file does not exist: {args.input}', file=sys.stderr)
    return 1

  if args.command != 'fix' and not args.output:
    print('Error: output file path is required for tojson and fromjson commands', file=sys.stderr)
    return 1

  try:
    if args.command == 'tojson':
      save = SaveFile.loadSave(args.input)
      save.dumpJson(args.output, indent=args.indent)
      print(f'Converted save file to JSON: {args.output}')
    elif args.command == 'fromjson':
      save = SaveFile.loadJson(args.input)
      save.dumpSave(args.output)
      print(f'Converted JSON to save file: {args.output}')
    elif args.command == 'fix':
      SaveFile.fix(args.input)
      print(f'Removed problematic properties in {args.input}')
    return 0
  except Exception as exc:
    print(f'Error: {exc}', file=sys.stderr)
    return 1

if __name__ == '__main__':
  sys.exit(main())
