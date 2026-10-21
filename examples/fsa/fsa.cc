// Example: FSA (Lin et al., arXiv 2507.11331; RTL VCA-EPFL/FSA 3d2c70b) as one vcix_uarch unit, libfsa.so: a systolic
// array of N x N, N = vpu_num_lanes, that runs FlashAttention's inner loop and its online softmax in place. Each
// instruction's head and busy cycles are its plan's (plan.hpp); its values come out of the array run cycle by cycle (array.hpp).
#include <cstdint>
#include <cstdio>
#include <deque>

#include "vcix_accel.hpp"
#include "vcix_uarch.hpp"

#include "fsa/array.hpp"
#include "tpu/machine.hpp"
#include "tpu/values.hpp"

namespace {

using easyfloat::Float;

class Fsa {
  using Host = vcix_uarch::Host;
  using Route = vcix_uarch::Route;
  using Row = std::vector<uint32_t>;
  using Kind = fsa::Kind;
  // Which of the controller's two FSMs the next instruction takes, and what the array holds for the next one.
  struct State {
    uint32_t next_fsm = 0;
    bool q = false, p = false, l = false, o = false, reciprocal = false;
  };
  struct Values {
    fsa::Array array;
    std::deque<fsa::Matrix> normalized;
  };

 public:
  explicit Fsa(const char *name)
      : issue_{name, "issue", "instructions", 1, vcix_uarch::PRIMARY}, array_{name, "array", "cycles", 1},
        fsm_{{name, "fsm0", "cycles", 1}, {name, "fsm1", "cycles", 1}}, wait_{name, "wait", "instructions", 1}, out_in_{name, "out", "rows", vcix_uarch::UNLIMITED},
        pop_{name, "pop", "rows", vcix_uarch::UNLIMITED} {}
  void configure(const vcix_uarch::Config &config) {
    n_ = static_cast<uint32_t>(config.uint(LANES_KEY, 128));
    if (n_ < 1 || n_ > 1024) throw vcix_accel::ConfigError(LANES_KEY, config.get(LANES_KEY), "is not an array size of 1 to 1024");
    const uint64_t entries = config.uint(QUEUE_KEY, 256);
    if (entries < 1 || entries > UINT32_MAX) throw vcix_accel::ConfigError(QUEUE_KEY, config.get(QUEUE_KEY), "is not a queue size");
    commands_.configure(static_cast<uint32_t>(entries));
    for (auto &b : busy_) b.configure(1, vcix_uarch::UNLIMITED, vcix_uarch::Stall::WHEN_OUTPUT_FULL);
    out_.configure(1, n_, vcix_uarch::Stall::WHEN_OUTPUT_FULL);
    state_.configure({});
    for (int k = fsa::LOAD_STATIONARY; k <= fsa::ATTN_LSE_NORM; k++) plans_[k] = fsa::plan(static_cast<Kind>(k), static_cast<int>(n_), RECIPROCAL_CYCLES);
    Values v;
    v.array.configure(n_, RECIPROCAL_CYCLES);
    values_.configure(std::move(v));
  }
  template <class H>
  void hardware(H &&h) {
    h.port(issue_), h.port(array_), h.port(fsm_[0]), h.port(fsm_[1]), h.port(wait_), h.port(out_in_), h.port(pop_);
    h.pipeline(busy_[0], fsm_[0]), h.pipeline(busy_[1], fsm_[1]), h.pipeline(out_, out_in_);
    h.queue(commands_);
    h.reg(state_), h.reg(values_);
  }

  // load_stationary: vs2's N rows of halves, row i element i of every lane, are Q, held in the array.
  void load_stationary(Route &r) {
    if (!operand(r, "fsa load_stationary")) return;
    r.at_issue([&] { state_.write().q = true; });
    r.at_issue([&](const Host &host) { values_.write().array.issue(fsa::LOAD_STATIONARY, read(host, r), false); });
    matrix(r, fsa::LOAD_STATIONARY);
  }
  // attn_score: vs2 is K; S = QK^T replaces Q in the array, and its online softmax leaves P there and updates m and l.
  // [0] of the field at 19:15 accumulates onto the tiles before; 0 starts a row of tiles.
  void attn_score(Route &r) {
    if (!operand(r, "fsa attn_score")) return;
    r.require(state_.get().q, [] { tpu::fail("fsa attn_score: no load_stationary since the last attn_score"); });
    r.at_issue([&] {
      State &s = state_.write();
      s.q = false, s.p = true, s.l = true;
    });
    r.at_issue([&](const Host &host) { values_.write().array.issue(fsa::ATTN_SCORE, read(host, r), accumulate(r)); });
    matrix(r, fsa::ATTN_SCORE);
  }
  // attn_value: vs2 is V; O = scale O + PV with the scale of the score before.
  void attn_value(Route &r) {
    if (!operand(r, "fsa attn_value")) return;
    r.require(state_.get().p, [] { tpu::fail("fsa attn_value: no attn_score since the last attn_value"); });
    r.at_issue([&] {
      State &s = state_.write();
      s.p = false, s.o = true;
    });
    r.at_issue([&](const Host &host) { values_.write().array.issue(fsa::ATTN_VALUE, read(host, r), accumulate(r)); });
    matrix(r, fsa::ATTN_VALUE);
  }
  // reciprocal: 1 / l, row by row, into the accumulator.
  void reciprocal(Route &r) {
    r.require(state_.get().l, [] { tpu::fail("fsa reciprocal: no attn_score before it"); });
    r.at_issue([&] { state_.write().reciprocal = true; });
    r.at_issue([&](const Host &) { values_.write().array.issue(fsa::RECIPROCAL, {}, false); });
    matrix(r, fsa::RECIPROCAL);
  }
  // attn_lse_norm: O / l, whose N rows a pop then takes.
  void attn_lse_norm(Route &r) {
    r.require(state_.get().reciprocal && state_.get().o,
              [] { tpu::fail("fsa attn_lse_norm: no reciprocal and attn_value before it"); });
    r.at_issue([&] { state_.write().reciprocal = false; });
    r.at_issue([&](const Host &) {
      Values &v = values_.write();
      v.array.issue(fsa::ATTN_LSE_NORM, {}, false);
      v.array.run();
      v.normalized.push_back(v.array.output());
    });
    matrix(r, fsa::ATTN_LSE_NORM);
    r.send(out_, 1, n_, [&](const Host &, uint64_t i) {
      Values &v = values_.write();
      Row row;
      for (const Float &x : v.normalized.front()[i]) row.push_back(static_cast<uint32_t>(x.bits()));
      if (i + 1 == n_) v.normalized.pop_front();
      return row;
    });
  }
  // pop: vl rows of O / l, row i to element i of vd, a single per lane; one of more than were normalized never ends.
  void pop(Route &r) {
    const uint32_t sew = r.insn().sew_bits, vl = r.vl();
    r.require(sew == 32, [sew] { tpu::fail("fsa pop: a row is singles, and the elements are of %u bits", sew); });
    r.use(pop_, vl).receive(out_, vl, [&r](const Host &host, uint64_t i, Row &&row) {
      for (uint32_t lane = 0; lane < host.lanes() && lane < row.size(); lane++)
        tpu::put<uint32_t>(host, lane, vcix_accel::rd(r.insn()), i, row[lane]);
    });
    r.when_stuck([vl](uint64_t got) {
      tpu::fail("fsa pop: %u rows asked, %llu normalized", vl, static_cast<unsigned long long>(got));
    });
  }

  // What the [tpu] trace adds to an issue: the FSM the instruction takes and the commands ahead of it.
  void trace_queues(const vcix_accel::Insn &) const { std::printf(", fsm %u", state_.get().next_fsm); }

  // sf.vc.iv 0-2 (vs2 an operand), sf.vc.i 0-1, sf.vc.v.i 0 (vd), as assembled; opcode, funct3, funct6 and vm.
  static constexpr uint32_t FORM = 0xFE00707F;
  static constexpr vcix_uarch::Instruction<Fsa> INSTRUCTIONS[] = {
      {0x2200305B, FORM, "fsa load_stationary", &Fsa::load_stationary},
      {0x2600305B, FORM, "fsa attn_score", &Fsa::attn_score},
      {0x2A00305B, FORM, "fsa attn_value", &Fsa::attn_value},
      {0x0200305B, FORM, "fsa reciprocal", &Fsa::reciprocal},
      {0x0600305B, FORM, "fsa attn_lse_norm", &Fsa::attn_lse_norm},
      {0x0000305B, FORM, "fsa pop", &Fsa::pop},
  };

 private:
  static constexpr const char *LANES_KEY = "vpu_num_lanes", *QUEUE_KEY = "fsa_queue_entries";
  // Div.nCycles(23, 2) of easyfloat: the accumulator's single-precision divider, 2 bits a cycle.
  static constexpr uint64_t RECIPROCAL_CYCLES = (23 + 3 + 2 - 1) / 2 + 2;

  uint64_t head(Kind k) const { return plans_[k].head(); }
  uint64_t busy(Kind k) const { return plans_[k].busy(); }
  // The controller: an instruction fires once the FSM it takes is no longer busy with the instruction two before and
  // the one before has passed its head (the command queue, whose head is the array); reciprocal and attn_lse_norm also
  // wait for the other FSM (waitPrevAcc). Firing, it sends busy - 2 rows into its FSM's busy line, of one stage and
  // one row a cycle: a row is seen the cycle after it is sent and leaves the stage the cycle after that, so the line
  // drains `busy` cycles after the firing. attn_lse_norm's last head cycle sends O (attn_lse_norm()).
  void matrix(Route &r, Kind k) {
    const uint32_t fsm = state_.get().next_fsm;
    const bool waits = plans_[k].wait_prev_acc;
    vcix_uarch::Pipeline<char> &mine = busy_[fsm], &other = busy_[1 - fsm];
    r.use(issue_);
    if (const uint64_t old = mine.output_rows()) r.receive(mine, old);
    r.at_issue([&] { state_.write().next_fsm = 1 - state_.get().next_fsm; });
    r.enqueue(commands_);
    if (waits) r.wait_until_empty(mine).transfer(1).through(wait_);
    r.wait_until_empty(waits ? other : mine).send(mine, busy(k) - 2);
    r.transfer(head(k) - 1 - (k == fsa::ATTN_LSE_NORM)).through(array_);
  }
  bool operand(Route &r, const char *what) {
    const uint32_t sew = r.insn().sew_bits, vl = r.vl(), n = n_;
    const bool ok = r.require(sew == 16, [what, sew] { tpu::fail("%s: its rows are halves, and the elements are of %u bits", what, sew); }) &&
                    r.require(vl == n, [what, vl, n] { tpu::fail("%s: %u rows, and the array has %u", what, vl, n); });
    return ok;
  }
  static bool accumulate(Route &r) { return vcix_accel::rs1(r.insn()) & 1; }
  // vs2's N x N halves: row i is element i of every lane, column j lane j.
  fsa::Matrix read(const Host &host, Route &r) const {
    fsa::Matrix m(n_, std::vector<Float>(n_));
    for (uint32_t row = 0; row < n_; row++)
      for (uint32_t col = 0; col < n_; col++)
        m[row][col] = Float::of_bits(tpu::get<uint16_t>(host, col, vcix_accel::rs2(r.insn()), row), fsa::MUL_EW, fsa::MUL_MW);
    return m;
  }

  uint32_t n_ = 128;
  fsa::Plan plans_[fsa::ATTN_LSE_NORM + 1];
  vcix_uarch::Port issue_, array_, fsm_[2], wait_, out_in_, pop_;
  vcix_uarch::Pipeline<char> busy_[2];
  vcix_uarch::Pipeline<Row> out_;
  vcix_uarch::CommandQueue commands_;
  vcix_uarch::Register<State> state_;
  vcix_uarch::Register<Values> values_;
};

class Model final : public tpu::Machine<Fsa> {
 public:
  Model() : Machine("FSA") {}
  const char *name() const override { return "fsa"; }
};

}  // namespace

VCIX_ACCEL_REGISTER(Model)
