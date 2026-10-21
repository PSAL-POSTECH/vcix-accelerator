// FSA's ExecutionPlan.scala (VCA-EPFL/FSA 3d2c70b) as data: per instruction, what each PE control signal, the
// scratchpad, the comparators and the accumulator do at each cycle of its timer, for an array of N rows and columns.
// The instruction's head (conflict-free cycle + 1) and busy cycles are read off the plan, as the RTL's controller does.
#ifndef FSA_PLAN_HPP
#define FSA_PLAN_HPP

#include <algorithm>
#include <cstdint>
#include <vector>

namespace fsa {

enum Kind { LOAD_STATIONARY, ATTN_SCORE, ATTN_VALUE, RECIPROCAL, ATTN_LSE_NORM };
enum Signal { MAC, ACC_UI, LOAD_REG_LI, LOAD_REG_UI, FLOW_LR, FLOW_UD, FLOW_DU, UPDATE_REG, EXP2, SIGNALS };
enum Direction { PARALLEL, UPWARD, DOWNWARD };
enum SpadConst { NOT_CONST, ONE, ATTENTION_SCALE, EXP2_SLOPES };
enum CmpCmd { UPDATE, PROP_MAX, PROP_MAX_DIFF, PROP_ZERO, RESET, PROP_EXP2_INTERCEPTS };
enum AccCmd { EXP_S1, EXP_S2, ACC_SA, ACC, SET_SCALE, ACC_RECIPROCAL };
constexpr int PWL_PIECES = 8;

struct Flow { Signal signal; Direction direction; int start, repeat; };
struct SpRead { int cycle, repeat; SpadConst constant; };
struct CmpCtrl { int cycle, repeat; CmpCmd cmd; };
struct AccRead { int cycle, repeat; bool rmw; };
struct AccCtrl { int cycle, repeat; AccCmd cmd; };

struct Plan {
  Kind kind;
  int n = 0;
  std::vector<Flow> flows;
  std::vector<SpRead> sp_read;
  std::vector<CmpCtrl> cmp;
  std::vector<AccRead> acc_read;
  std::vector<AccCtrl> acc;
  int conflict_free = 0;
  bool wait_prev_acc = false;

  // ControlGen.FlowRange: row `row` sees signal of a flow at timer start + re + its delay.
  int delay(Direction d, int row) const { return d == PARALLEL ? 0 : d == UPWARD ? n - 1 - row : row; }
  int flow_end(const Flow &f) const { return f.start + f.repeat + (f.direction == PARALLEL ? 0 : n - 1); }
  // ExecutionPlan.computeMaxCycle, accStartCycle and accumulateMaxCycle (exclusive ends, inclusive start).
  int compute_max() const {
    int m = 0;
    for (const Flow &f : flows) m = std::max(m, flow_end(f));
    for (const SpRead &r : sp_read) m = std::max(m, r.cycle + r.repeat);
    for (const CmpCtrl &c : cmp) m = std::max(m, c.cycle + c.repeat);
    return m;
  }
  int acc_max() const {
    int m = 0;
    for (const AccRead &r : acc_read) m = std::max(m, r.cycle + r.repeat);
    for (const AccCtrl &c : acc) m = std::max(m, c.cycle + c.repeat);
    return m;
  }
  int acc_start() const {
    int m = -1;
    for (const AccRead &r : acc_read) m = m < 0 ? r.cycle : std::min(m, r.cycle);
    for (const AccCtrl &c : acc) m = m < 0 ? c.cycle : std::min(m, c.cycle);
    return m < 0 ? -1 : std::min(m, compute_max());
  }
  // Cycles after it fires that the next instruction may fire (MatrixEngineController's conflictFree), and that its
  // FSM is busy (its compute and accumulate flags), counting the fire cycle.
  uint64_t head() const { return static_cast<uint64_t>(conflict_free) + 1; }
  uint64_t busy() const { return static_cast<uint64_t>(std::max(compute_max(), acc_max())) + 1; }
};

inline Plan plan(Kind k, int n, uint32_t reciprocal_cycles) {
  Plan p;
  p.kind = k, p.n = n;
  auto flow = [&](Signal s, Direction d, int start, int repeat) { p.flows.push_back({s, d, start, repeat}); };
  auto sp = [&](int cycle, int repeat, SpadConst c) { p.sp_read.push_back({cycle, repeat, c}); };
  auto cmp = [&](int cycle, int repeat, CmpCmd c) { p.cmp.push_back({cycle, repeat, c}); };
  auto acc = [&](int cycle, int repeat, AccCmd c) { p.acc.push_back({cycle, repeat, c}); };
  switch (k) {
    case LOAD_STATIONARY:
      sp(0, n, NOT_CONST);
      flow(LOAD_REG_LI, PARALLEL, 1, n);
      p.conflict_free = n - 1;
      break;
    case ATTN_SCORE: {
      sp(0, n, NOT_CONST);
      flow(MAC, UPWARD, 1, n), flow(FLOW_LR, UPWARD, 1, n);
      flow(FLOW_UD, DOWNWARD, n + 1, n), cmp(n + 1, n, UPDATE);
      flow(FLOW_DU, UPWARD, n + 4, n);
      flow(LOAD_REG_UI, PARALLEL, 2 * n + 1, 1), cmp(2 * n + 1, 1, PROP_MAX), sp(2 * n + 1, 1, ONE);
      for (Signal s : {UPDATE_REG, ACC_UI, FLOW_UD, FLOW_LR}) flow(s, DOWNWARD, 2 * n + 2, 1);
      cmp(2 * n + 2, 1, PROP_MAX_DIFF), sp(2 * n + 2, 1, ATTENTION_SCALE);
      for (Signal s : {FLOW_UD, UPDATE_REG, FLOW_LR}) flow(s, DOWNWARD, 2 * n + 3, 1);
      const int exp2_start = 2 * n + 4, exp2_end = exp2_start + PWL_PIECES - 1;
      cmp(exp2_start - 1, PWL_PIECES, PROP_EXP2_INTERCEPTS), sp(exp2_start - 1, PWL_PIECES, EXP2_SLOPES);
      for (Signal s : {FLOW_UD, FLOW_LR, ACC_UI, EXP2}) flow(s, DOWNWARD, exp2_start, PWL_PIECES);
      cmp(exp2_end, 1, PROP_ZERO), sp(exp2_end, 1, ONE);
      for (Signal s : {MAC, ACC_UI, FLOW_LR}) flow(s, DOWNWARD, exp2_end + 1, 1);
      p.conflict_free = exp2_end;
      acc(3 * n + n + 2, 1, EXP_S1), acc(3 * n + n + 3, 1, EXP_S2);
      p.acc_read.push_back({exp2_end + 2 * n - 1, 1, true});
      acc(exp2_end + 2 * n, 1, ACC_SA);
      break;
    }
    case ATTN_VALUE:
      sp(0, n, NOT_CONST);
      for (Signal s : {MAC, ACC_UI, FLOW_LR}) flow(s, DOWNWARD, 1, n);
      p.conflict_free = 2 * n - 2;
      p.acc_read.push_back({2 * n - 1, n, true});
      acc(2 * n, n, ACC_SA);
      break;
    case RECIPROCAL:
      p.acc_read.push_back({0, 1, false});
      acc(1, 1, SET_SCALE), acc(2, static_cast<int>(reciprocal_cycles), ACC_RECIPROCAL);
      p.conflict_free = 2 + static_cast<int>(reciprocal_cycles) - 1;
      p.wait_prev_acc = true;
      break;
    case ATTN_LSE_NORM:
      cmp(0, 1, RESET);
      p.acc_read.push_back({0, n, true});
      acc(1, n, ACC);
      p.conflict_free = n;
      p.wait_prev_acc = true;
      break;
  }
  return p;
}

}  // namespace fsa

#endif
