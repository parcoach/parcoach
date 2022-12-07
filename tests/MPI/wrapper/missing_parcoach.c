// RUN: %not %wrapper notacompiler -c %s -o /dev/null 2>&1 | %filecheck --check-prefix=CHECK-NOPROG %s
// CHECK-NOPROG: unable to find 'notacompiler' in PATH
// RUN: %not %wrapper --args 2>&1 | %filecheck --check-prefix=CHECK-WRONGARGS %s
// CHECK-WRONGARGS: --args must be followed by the program to run
int main() {
  return 0;
}
