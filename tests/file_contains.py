import sys
import argparse

parser = argparse.ArgumentParser(description='Check that the first input file contains all lines from the second input files. Each expectation must be unique.')
parser.add_argument('input', help='the input file to check')
parser.add_argument('expectation', help='the expectation file, one per line')
args = parser.parse_args()

expectations = set(l for l in open(args.expectation))
detected = set(l for l in open(args.input) if l.startswith('PARCOACH'))

missed = expectations - detected
extra = detected - expectations

n_missed = len(missed)
n_extra = len(extra)

if n_missed > 0:
  print("Some expectations ({}) were missed:".format(n_missed))
  for m in missed:
    print(m)
if n_extra > 0:
  print("Some diagnostics ({}) were detected and unexpected:".format(n_extra))
  for m in extra:
    print(m)

sys.exit(n_missed + n_extra)
