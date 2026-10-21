# golden.py N TILES: attention.c's inputs through FSA's golden model (VCA-EPFL/FSA 3d2c70b python/fa_ref.py
# FlashAttentionTile, as main.py's ref_pyeasyfloat runs it, on pyeasyfloat of VCA-EPFL/easyfloat 973af49); prints O
# as attention.c does. attention-n<N>.expected is its output. Needs both repositories on PYTHONPATH, and numpy.
import sys
import numpy as np
from fa_ref import FlashAttentionTile
from pyeasyfloat.backend import PyEasyFloatBackend

n, tiles = int(sys.argv[1]), int(sys.argv[2])
state = 0x2545F491
def nxt():
    global state
    state ^= (state << 13) & 0xffffffff; state ^= state >> 17; state ^= (state << 5) & 0xffffffff
    return state
def half():
    r = nxt()
    return ((r >> 31) << 15) | ((11 + (r >> 10) % 6) << 10) | (r & 0x3ff)
rows = np.array([[half() for _ in range(n)] for _ in range(n * (1 + 2 * tiles))], dtype=np.uint16).view(np.float16)
Q, K, V = rows[:n], rows[n:n * (1 + tiles)], rows[n * (1 + tiles):]
m = np.full((n, 1), np.float32(-np.inf)); l = np.full((n, 1), np.float32(0)); o = np.full((n, n), np.float32(0))
for j in range(tiles):
    t = FlashAttentionTile(Q, K[j * n:(j + 1) * n], V[j * n:(j + 1) * n], m, l, o, mul_ew=5, mul_mw=10, acc_ew=8,
                           acc_mw=23, backend=PyEasyFloatBackend())
    m, l, o = t.AccRowMaxS, t.AccRowSum, t.AccO
for row in t.NormO:
    print(''.join(f"{x.to_bits():08x} " for x in row))
