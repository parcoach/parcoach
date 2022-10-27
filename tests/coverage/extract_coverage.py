#!/usr/bin/env python3
"""This extract the appropriate line coverage property from a json."""
import sys
import argparse
import json

def main() -> int:
  """The main."""
  parser = argparse.ArgumentParser(
      description = 'Extract the coverage percentage from a json file emitted by llvm-cov.')
  parser.add_argument('filename')
  args = parser.parse_args()
  with open(args.filename, encoding='utf-8') as json_file:
    data = json.load(json_file)
    coverage = data['data'][0]['totals']['lines']['percent']
    print(f'Coverage: {coverage:.2f}%')
    return 0
  return 1

if __name__ == '__main__':
  sys.exit(main())
