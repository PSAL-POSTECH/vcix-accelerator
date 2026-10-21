# plan_sim.py N TILES: FSA's matrix controller (MatrixEngineController.scala, MatrixControlFSM) cycle by cycle, on the
# ExecutionPlan.scala schedules of VCA-EPFL/FSA 3d2c70b: when each instruction of TILES tiles fires and is done. run.sh
# expects its firing cycles.
import sys

PWL, RECIP = 8, (23 + 3 + 2 - 1) // 2 + 2   # exp2PwlPieces; Div.nCycles(23, 2) of easyfloat


class Plan:
    def __init__(self, n):
        self.rows = n
        self.sp, self.cmp, self.pe, self.acc = [], [], [], []
        self.sem = None
        self.cf = None

    def flow(self, kind, start, repeat):     # ControlGen effEnd: parallel start+repeat, up/down + rows-1
        self.pe.append(start + repeat + (0 if kind == 'par' else self.rows - 1))

    def compute_max(self):
        return max([c + r for c, r in self.sp + self.cmp] + self.pe + [0])

    def acc_start(self):
        if not self.acc:
            return -1
        return min(min(c for c, _ in self.acc), self.compute_max())

    def acc_max(self):
        return max([c + r for c, r in self.acc] + [0])


def load_stationary(n):
    p = Plan(n)
    p.sp.append((0, n)); p.flow('par', 1, n); p.cf = n - 1
    return p


def attn_score(n):
    p = Plan(n)
    p.sp.append((0, n)); p.flow('up', 1, n); p.flow('up', 1, n)
    p.flow('down', n + 1, n); p.cmp.append((n + 1, n))
    p.flow('up', n + 4, n)
    p.flow('par', 2 * n + 1, 1); p.cmp.append((2 * n + 1, 1)); p.sp.append((2 * n + 1, 1))
    for _ in range(4): p.flow('down', 2 * n + 2, 1)
    p.cmp.append((2 * n + 2, 1)); p.sp.append((2 * n + 2, 1))
    for _ in range(3): p.flow('down', 2 * n + 3, 1)
    s = 2 * n + 4; e = s + PWL - 1
    p.cmp.append((s - 1, PWL)); p.sp.append((s - 1, PWL))
    for _ in range(4): p.flow('down', s, PWL)
    p.cmp.append((e, 1)); p.sp.append((e, 1))
    for _ in range(3): p.flow('down', e + 1, 1)
    p.cf = e
    p.acc += [(3 * n + n + 2, 1), (3 * n + n + 3, 1), (e + n + n - 1, 1), (e + n + n, 1)]
    return p


def attn_value(n):
    p = Plan(n)
    p.sp.append((0, n)); p.flow('down', 1, n); p.flow('down', 1, n); p.flow('down', 1, n)
    p.cf = 2 * n - 2
    p.acc += [(n + n - 1, n), (n + n, n)]
    return p


def reciprocal(n):
    p = Plan(n)
    p.acc += [(0, 1), (1, 1), (2, RECIP)]
    p.cf = 2 + RECIP - 1
    return p


def lse_norm(n):
    p = Plan(n)
    p.cmp.append((0, 1)); p.acc += [(0, n), (1, n)]
    p.cf = n
    return p


class Fsm:
    def __init__(self):
        self.plan = None; self.cf = self.af = False; self.ct = self.at = 0; self.free = False

    def busy(self):
        return self.cf or self.af


def run(program, n):
    plans = {'ls': load_stationary, 'score': attn_score, 'value': attn_value, 'recip': reciprocal, 'norm': lse_norm}
    wait = {'recip', 'norm'}                       # waitPrevAcc in kernel.py
    fsms = [Fsm(), Fsm()]
    enq, pc, t, log = 0, 0, 0, []
    while pc < len(program) or any(f.busy() for f in fsms):
        # conflictFree of this cycle (Ehr port 0) is seen by the enqueue below (port 1)
        for f in fsms:
            p = f.plan
            if p is None: continue
            if p.acc_max() > 0 and p.cf >= p.acc_start():
                hit = f.af and f.at == p.cf - p.acc_start()
            else:
                hit = f.cf and f.ct == p.cf
            if hit: f.free = True
        fire = None
        if pc < len(program):
            name = program[pc]
            prev = fsms[1 - enq]
            ok = not any(f.busy() for f in fsms) if name in wait else (not prev.busy() or prev.free)
            if ok and not fsms[enq].busy():
                fire = name
        # timers and flags (MatrixControlFSM "Update flags / timers")
        nxt = []
        for f in fsms:
            p = f.plan
            cf, af, ct, at = f.cf, f.af, f.ct, f.at
            if p is not None:
                c_done = cf and ct == p.compute_max() - 1
                a_done = af and p.acc_max() > 0 and at == p.acc_max() - 1 - p.acc_start()
                a_start = cf and p.acc_start() > 0 and ct == p.acc_start() - 1
                ct = 0 if c_done else (ct + 1 if cf else ct)
                at = 0 if a_done else (at + 1 if af else at)
                if c_done: cf = False
                if a_start: af = True
                if a_done: af = False
                if a_done or (c_done and not f.af and not a_start):
                    log.append((f.name, 'done', t))
            nxt.append((cf, af, ct, at))
        for f, (cf, af, ct, at) in zip(fsms, nxt):
            f.cf, f.af, f.ct, f.at = cf, af, ct, at
        if fire:
            f = fsms[enq]
            p = plans[fire](n)
            f.plan, f.name = p, f'{fire}#{pc}'
            f.cf = p.compute_max() > 0
            f.af = p.acc_max() > 0 and p.acc_start() == 0
            f.ct = f.at = 0
            f.free = p.cf == 0
            log.append((f.name, 'fire', t))
            enq, pc = 1 - enq, pc + 1
        t += 1
    return log


if __name__ == '__main__':
    n, tiles = int(sys.argv[1]), int(sys.argv[2])
    prog = ['ls', 'score', 'value'] * tiles + ['recip', 'norm']
    for name, what, t in run(prog, n):
        print(f'{t:5d} {what:4s} {name}')
