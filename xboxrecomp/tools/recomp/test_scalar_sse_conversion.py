"""Check scalar SSE conversion emission and execute the portable ARM helper."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from .disasm import Instruction, Operand
from .lifter import Lifter


class ScalarSseConversionTest(unittest.TestCase):
    def test_rounding_and_truncating_instructions(self):
        for mnemonic in ('cvtss2si', 'cvttss2si', 'cvtsd2si', 'cvttsd2si'):
            insn = Instruction(0, 4, mnemonic, 'eax, xmm0', '')
            insn.operands = [Operand(type='reg', reg='eax'), Operand(type='reg', reg='xmm0')]
            text = '\n'.join(Lifter().lift_instruction(insn))
            lane = "d" if "sd" in mnemonic else "f"
            self.assertIn(f"SSE_CVT_F2I(xmm0.{lane}[0]", text)
            self.assertIn(', %d)' % mnemonic.startswith('cvtt'), text)

    def test_portable_conversion_matches_x86(self):
        cc = shutil.which('cc')
        if not cc:
            self.skipTest('no C compiler')
        header = (Path(__file__).resolve().parents[2] / 'templates/runtime/recomp_types.h').read_text()
        helper = header.split('static inline int32_t SSE_CVT_F2I', 1)[1].split('\n}\n', 1)[0]
        # Compile the actual non-x86 branch even when the test host is x86.
        portable = 'static int32_t portable' + helper.split('#if', 1)[0] + helper.split('#else', 1)[1].split('#endif', 1)[0] + '\n}\n'
        source = '''
#include <assert.h>
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <emmintrin.h>
''' + portable + '''
int main(void) {
    double values[] = {0, -0.0, 2.5, 3.5, -2.5, 2.9, -2.9,
        2147483647.0, 2147483647.5, 2147483648.0, -2147483648.0,
        -2147483648.5, -2147483649.0, NAN, INFINITY, -INFINITY};
    int modes[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    for (unsigned m = 0; m < 4; m++) {
        assert(!fesetround(modes[m]));
        for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); i++) {
            volatile double value = values[i];
            assert(portable(value, 0) == _mm_cvtsd_si32(_mm_set_sd(value)));
            assert(portable(value, 1) == _mm_cvttsd_si32(_mm_set_sd(value)));
        }
    }
    return 0;
}
'''
        if 'x86' not in __import__('platform').machine().lower() and __import__('platform').machine() != 'AMD64':
            self.skipTest('comparison requires x86 SSE intrinsics')
        with tempfile.TemporaryDirectory() as directory:
            src = Path(directory) / 'convert.c'
            exe = Path(directory) / 'convert'
            src.write_text(source)
            subprocess.run([cc, '-O0', '-frounding-math', str(src), '-lm', '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
