// FSA's matrix engine cycle by cycle (VCA-EPFL/FSA 3d2c70b): two FSMs run plan.hpp's plans on the scratchpad, PEs,
// comparators and accumulator as FSA.scala and sa/*.scala wire them, a register wherever the RTL has one; arithmetic is
// easyfloat.hpp's. PROP_MAX keeps the comparator's max, as fa_ref.py does; CMP.scala writes max(0, max) there.
#ifndef FSA_ARRAY_HPP
#define FSA_ARRAY_HPP

#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <stdexcept>
#include <vector>

#include "formats.hpp"
#include "plan.hpp"

namespace fsa {

class Array {
  // A word on a wire: its bits and, for an exp2 intercept or slope, the PWL piece it belongs to.
  struct Word {
    Float f;
    int piece = -1;
  };
  // A Pipe: valid and the bits, the bits held while invalid.
  struct Pipe {
    bool valid = false;
    Word w;
    void set(bool v, const Word &x) { valid = v; if (v) w = x; }
  };
  struct Ctrl {
    bool valid = false;
    bool s[SIGNALS] = {};
    void set(const Ctrl &c) { valid = c.valid; if (c.valid) std::copy(c.s, c.s + SIGNALS, s); }
  };
  struct Pe {
    Float reg;
    bool exp2_done = false;
    Pipe l, u, d;  // l_input, u_input, d_input
    Ctrl ctrl;     // in_ctrl of a PE past column 0
  };
  struct Cmp {
    Float old_max, new_max;
    int counter = 0;
    Pipe d;  // d_input
    bool ctrl_valid = false;
    CmpCmd cmd = UPDATE;
  };
  struct Command { Kind kind; Matrix rows; bool accumulate; };
  struct Fsm {
    const Plan *plan = nullptr;
    int64_t fired = -1;
    Command command;
    uint32_t sp_next = 0, acc_next = 0;
    int64_t t(int64_t now) const { return now - fired - 1; }
    bool busy(int64_t now) const { return plan && t(now) >= 0 && t(now) < static_cast<int64_t>(plan->busy()) - 1; }
    bool conflict_free(int64_t now) const { return !plan || t(now) >= plan->conflict_free; }
  };

 public:
  void configure(uint32_t n, uint32_t reciprocal_cycles) {
    n_ = n;
    for (int k = LOAD_STATIONARY; k <= ATTN_LSE_NORM; k++) plans_[k] = plan(static_cast<Kind>(k), static_cast<int>(n), reciprocal_cycles);
    pe_.assign(n, std::vector<Pe>(n));
    for (auto &row : pe_)
      for (Pe &p : row) p.reg = mul_of(0), p.l.w.f = p.u.w.f = p.d.w.f = acc_of(0);
    cmp_.assign(n, Cmp{});
    for (Cmp &c : cmp_) c.old_max = c.new_max = acc_of(-INFINITY), c.d.w.f = acc_of(0);
    acc_out_.assign(n, Word{acc_of(0)});
    out_shift_.assign(n, std::deque<Word>());
    for (uint32_t j = 0; j < n; j++) out_shift_[j].assign(n - 1 - j, Word{acc_of(0)});
    in_shift_.assign(n, std::deque<Word>());
    for (uint32_t i = 0; i < n; i++) in_shift_[i].assign(i, Word{mul_of(0)});
    scale_.assign(n, acc_of(0));
    sram_data_.assign(n, Word{mul_of(0)});
    ram_.clear();
  }
  const Plan &plan_of(Kind k) const { return plans_[k]; }
  // An instruction to the controller, in program order. LOAD_STATIONARY brings Q [query][dim], ATTN_SCORE K [key][dim],
  // ATTN_VALUE V [key][column]; they reach the scratchpad as FSA's kernel (python/main.py) lays them out: Q's rows
  // reversed, K as it is, V transposed, scratchpad row t a vector over the array's rows.
  void issue(Kind k, const Matrix &m, bool accumulate) {
    Matrix rows;
    if (k == LOAD_STATIONARY)
      for (uint32_t t = 0; t < n_; t++) rows.push_back(m[n_ - 1 - t]);
    else if (k == ATTN_SCORE) rows = m;
    else if (k == ATTN_VALUE) {
      rows = matrix(n_, n_, mul_of(0));
      for (uint32_t t = 0; t < n_; t++)
        for (uint32_t key = 0; key < n_; key++) rows[t][key] = m[key][t];
    }
    queue_.push_back({k, std::move(rows), accumulate});
  }
  // Runs until every issued instruction has fired and its FSM is no longer busy.
  void run() {
    while (!queue_.empty() || fsm_[0].busy(now_) || fsm_[1].busy(now_)) cycle();
  }
  const std::vector<int64_t> &fires() const { return fires_; }
  // The O tile the last attn_lse_norm wrote, [query][column].
  Matrix output() const {
    Matrix o = matrix(n_, n_, acc_of(0));
    for (uint32_t t = 0; t < n_; t++) {
      auto it = ram_.find(O_ROW + t);
      if (it == ram_.end()) continue;
      for (uint32_t j = 0; j < n_; j++) o[j][t] = it->second[j];
    }
    return o;
  }

 private:
  static constexpr uint32_t L_ROW = 0, O_ROW = 1;

  // One cycle: the controller may fire the next instruction; both FSMs drive the PEs, scratchpad, comparators and
  // accumulator; every block computes from its registers as the cycle began; then every register takes its next value.
  void cycle() {
    const int64_t c = now_;
    if (!queue_.empty()) {
      Fsm &e = fsm_[enq_], &d = fsm_[1 - enq_];
      const Plan &p = plans_[queue_.front().kind];
      const bool can = !e.busy(c) && (p.wait_prev_acc ? !d.busy(c) : (!d.busy(c) || d.conflict_free(c)));
      if (can) {
        e.plan = &p, e.fired = c, e.command = std::move(queue_.front()), e.sp_next = 0, e.acc_next = 0;
        queue_.pop_front();
        fires_.push_back(c);
        enq_ = 1 - enq_;
      }
    }
    std::vector<Ctrl> row_ctrl(n_);
    bool sp_valid = false, sp_const = false, cmp_valid = false, acc_rd = false, acc_rmw = false, acc_ctl = false;
    SpadConst sp_which = NOT_CONST;
    Word sp_word;
    std::vector<Word> sp_row;
    bool sp_rev_in = false, sp_rev_out = false, sp_delay = false;
    CmpCmd cmd = UPDATE;
    AccCmd acmd = ACC;
    uint32_t acc_addr = 0;
    bool acc_zero = false;
    for (Fsm &f : fsm_) {
      if (!f.plan || f.t(c) < 0) continue;
      const Plan &p = *f.plan;
      const int64_t t = f.t(c);
      for (const Flow &fl : p.flows)
        for (uint32_t i = 0; i < n_; i++) {
          const int64_t at = t - p.delay(fl.direction, static_cast<int>(i));
          if (at >= fl.start && at < fl.start + fl.repeat) row_ctrl[i].s[fl.signal] = true;
        }
      for (const SpRead &r : p.sp_read)
        if (t >= r.cycle && t < r.cycle + r.repeat) {
          sp_valid = true, sp_const = r.constant != NOT_CONST, sp_which = r.constant;
          if (sp_const) sp_rev_in = false, sp_rev_out = false, sp_delay = true;
          else {
            spad_flags(p.kind, sp_rev_in, sp_rev_out, sp_delay);
            sp_row.clear();
            for (const Float &x : f.command.rows[f.sp_next]) sp_row.push_back(Word{x});
            f.sp_next++;
          }
        }
      for (const CmpCtrl &x : p.cmp)
        if (t >= x.cycle && t < x.cycle + x.repeat) cmp_valid = true, cmd = x.cmd;
      for (const AccRead &x : p.acc_read)
        if (t >= x.cycle && t < x.cycle + x.repeat) {
          acc_rd = true, acc_rmw = x.rmw;
          const uint32_t base = p.kind == ATTN_VALUE || p.kind == ATTN_LSE_NORM ? O_ROW : L_ROW;
          acc_addr = base + f.acc_next++;
          acc_zero = (p.kind == ATTN_SCORE || p.kind == ATTN_VALUE) && !f.command.accumulate;
        }
      for (const AccCtrl &x : p.acc)
        if (t >= x.cycle && t < x.cycle + x.repeat) acc_ctl = true, acmd = x.cmd;
    }
    for (Ctrl &r : row_ctrl)
      for (bool b : r.s) r.valid = r.valid || b;

    std::vector<Word> in_data(n_);
    {
      Word konst;
      switch (sp_sel_) {
        case ONE: konst = Word{mul_of(1)}; break;
        case ATTENTION_SCALE: konst = Word{mul_of(std::log2(M_E) / std::sqrt(static_cast<double>(n_)))}; break;
        case EXP2_SLOPES: konst = Word{slope(slope_counter_), slope_counter_}; break;
        default: konst = Word{mul_of(0)}; break;
      }
      const bool in_valid = last_sp_valid_;
      if (in_valid) delay_r_ = last_delay_, rev_out_r_ = last_rev_out_;
      for (uint32_t i = 0; i < n_; i++) {
        const uint32_t from = last_rev_in_ ? n_ - 1 - i : i;
        in_data[i] = last_sp_const_ ? konst : sram_data_[from];
      }
    }
    std::vector<Word> delayed(n_), pe_data(n_);
    for (uint32_t i = 0; i < n_; i++) delayed[i] = i == 0 ? in_data[0] : in_shift_[i].front();
    for (uint32_t i = 0; i < n_; i++) {
      const Word &x = delay_r_ ? delayed[i] : in_data[i];
      pe_data[rev_out_r_ ? n_ - 1 - i : i] = x;
    }

    std::vector<std::vector<Pipe>> r_out(n_, std::vector<Pipe>(n_)), d_out = r_out, u_out = r_out;
    std::vector<std::vector<Float>> reg_next(n_, std::vector<Float>(n_));
    std::vector<std::vector<bool>> done_next(n_, std::vector<bool>(n_));
    for (uint32_t i = 0; i < n_; i++)
      for (uint32_t j = 0; j < n_; j++) {
        Pe &p = pe_[i][j];
        const Ctrl &k = j == 0 ? row_ctrl[i] : p.ctrl;
        const Word l = j == 0 ? pe_data[i] : p.l.w;
        const Word u = p.u.w, d = i + 1 == n_ ? Word{acc_of(0)} : p.d.w;
        const bool fire = k.valid;
        const Word &c_in = k.s[ACC_UI] ? u : d;
        reg_next[i][j] = p.reg, done_next[i][j] = p.exp2_done;
        Float out_acc, out_elem;
        bool out_exp2 = false;
        if (k.s[EXP2]) {
          const Split s = easyfloat::split_float(p.reg.raw());
          out_exp2 = static_cast<int>(easyfloat::PIECES - 1 - s.frac_msb) == c_in.piece;
          out_elem = easyfloat::pow2(p.reg, MUL_EW, MUL_MW, MUL_EW, MUL_MW, ACC_EW, ACC_MW);
        } else {
          out_acc = easyfloat::fma(p.reg, l.f, c_in.f, ACC_EW, ACC_MW);
          out_elem = easyfloat::fma(p.reg, l.f, c_in.f, MUL_EW, MUL_MW);
        }
        if (fire) {
          if (k.s[EXP2]) done_next[i][j] = p.exp2_done || out_exp2;
          else done_next[i][j] = false;
          if (k.s[LOAD_REG_LI]) reg_next[i][j] = l.f;
          else if (k.s[LOAD_REG_UI]) reg_next[i][j] = viewed_as_elem(u.f);
          else if (k.s[UPDATE_REG] || (out_exp2 && !p.exp2_done)) reg_next[i][j] = out_elem;
        }
        r_out[i][j].set(fire && (k.s[LOAD_REG_LI] || k.s[FLOW_LR]), k.s[LOAD_REG_LI] ? Word{p.reg} : l);
        d_out[i][j].set(fire && ((k.s[MAC] && k.s[ACC_UI]) || k.s[FLOW_UD]), k.s[MAC] && k.s[ACC_UI] ? Word{out_acc} : u);
        u_out[i][j].set(fire && ((k.s[MAC] && !k.s[ACC_UI]) || k.s[FLOW_DU]), k.s[MAC] && !k.s[ACC_UI] ? Word{out_acc} : d);
      }
    std::vector<Pipe> cmp_out(n_);
    std::vector<Cmp> cmp_next = cmp_;
    for (uint32_t j = 0; j < n_; j++) {
      const Cmp &x = cmp_[j];
      const bool valid = j == 0 ? cmp_valid : x.ctrl_valid;
      const CmpCmd k = j == 0 ? cmd : x.cmd;
      const Float a = k == UPDATE ? x.d.w.f : k == PROP_MAX ? acc_of(0) : x.old_max;
      const Float diff = sub(a, x.new_max);
      const Float max = diff.sign ? x.new_max : a;
      Cmp &y = cmp_next[j];
      if (valid) {
        if (k == RESET) y.old_max = y.new_max = acc_of(-INFINITY);
        else if (k == PROP_ZERO || k == PROP_EXP2_INTERCEPTS || k == PROP_MAX) {
        } else {
          y.new_max = max;
          if (k == PROP_MAX_DIFF) y.old_max = max;
        }
        if (k == PROP_EXP2_INTERCEPTS) y.counter = (x.counter + 1) % PWL_PIECES;
      }
      Word w;
      if (k == PROP_ZERO) w = Word{acc_of(0)};
      else if (k == PROP_EXP2_INTERCEPTS) w = Word{intercept(x.counter), x.counter};
      else if (k == UPDATE) w = Word{easyfloat::round_to(x.d.w.f, MUL_EW, MUL_MW)};
      else w = Word{diff};
      cmp_out[j].set(valid && k != RESET, w);
    }
    std::vector<Word> sa_in(n_);
    for (uint32_t j = 0; j < n_; j++) sa_in[j] = j + 1 == n_ ? acc_out_[j] : out_shift_[j].front();
    std::vector<Float> sram_in(n_), acc_res(n_);
    for (uint32_t j = 0; j < n_; j++) {
      if (last_acc_zero_) sram_in[j] = acc_of(0);
      else {
        auto it = ram_.find(last_acc_addr_);
        sram_in[j] = it == ram_.end() ? acc_of(0) : it->second[j];
      }
    }
    std::vector<Float> scale_next = scale_;
    for (uint32_t j = 0; j < n_; j++) {
      const Float &s = scale_[j];
      switch (acc_ctl ? acmd : ACC) {
        case EXP_S1: acc_res[j] = easyfloat::fma(sa_in[j].f, acc_of(std::log2(M_E) / std::sqrt(static_cast<double>(n_))), acc_of(0), ACC_EW, ACC_MW); break;
        case EXP_S2: acc_res[j] = easyfloat::pow2(s, ACC_EW, ACC_MW, ACC_EW, ACC_MW, ACC_EW, ACC_MW); break;
        case ACC_SA: acc_res[j] = easyfloat::fma(s, sram_in[j], sa_in[j].f, ACC_EW, ACC_MW); break;
        default: acc_res[j] = easyfloat::fma(s, sram_in[j], acc_of(0), ACC_EW, ACC_MW); break;
      }
      if (acc_ctl) {
        if (acmd == EXP_S1 || acmd == EXP_S2) scale_next[j] = acc_res[j];
        else if (acmd == SET_SCALE) scale_next[j] = sram_in[j];
      }
    }
    if (acc_ctl && acmd == ACC_RECIPROCAL) {
      if (!dividing_) dividing_ = true, divisor_ = scale_;
    } else if (dividing_) {
      dividing_ = false;
    }
    const bool last_reciprocal = acc_ctl && acmd == ACC_RECIPROCAL && !reciprocal_next(c);
    if (last_reciprocal)
      for (uint32_t j = 0; j < n_; j++) scale_next[j] = easyfloat::div(acc_of(1), divisor_[j]);
    if (last_acc_rd_ && last_acc_rmw_) ram_[last_acc_addr_] = acc_res;

    for (uint32_t i = 0; i < n_; i++)
      for (uint32_t j = n_; j-- > 0;) {
        Pe &p = pe_[i][j];
        p.reg = reg_next[i][j], p.exp2_done = done_next[i][j];
        if (j + 1 < n_) {
          pe_[i][j + 1].l.set(r_out[i][j].valid, r_out[i][j].w);
          pe_[i][j + 1].ctrl.set(j == 0 ? row_ctrl[i] : p.ctrl);
        }
      }
    for (uint32_t i = 0; i < n_; i++)
      for (uint32_t j = 0; j < n_; j++) {
        if (i + 1 < n_) pe_[i + 1][j].u.set(d_out[i][j].valid, d_out[i][j].w);
        if (i > 0) pe_[i - 1][j].d.set(u_out[i][j].valid, u_out[i][j].w);
      }
    for (uint32_t j = 0; j < n_; j++) {
      cmp_next[j].d.set(u_out[0][j].valid, u_out[0][j].w);
      pe_[0][j].u.set(cmp_out[j].valid, cmp_out[j].w);
      if (d_out[n_ - 1][j].valid) acc_out_new_[j] = d_out[n_ - 1][j].w;
    }
    for (uint32_t j = n_; j-- > 1;) {
      cmp_next[j].ctrl_valid = j == 1 ? cmp_valid : cmp_[j - 1].ctrl_valid;
      const bool v = cmp_next[j].ctrl_valid;
      if (v) cmp_next[j].cmd = j == 1 ? cmd : cmp_[j - 1].cmd;
    }
    cmp_ = std::move(cmp_next);
    for (uint32_t j = 0; j + 1 < n_; j++) {
      out_shift_[j].push_back(acc_out_[j]);
      out_shift_[j].pop_front();
    }
    for (uint32_t j = 0; j < n_; j++) acc_out_[j] = acc_out_new_.count(j) ? acc_out_new_[j] : acc_out_[j];
    acc_out_new_.clear();
    for (uint32_t i = 1; i < n_; i++) {
      in_shift_[i].push_back(in_data[i]);
      in_shift_[i].pop_front();
    }
    scale_ = std::move(scale_next);
    if (last_sp_const_ && sp_sel_ == EXP2_SLOPES) slope_counter_ = (slope_counter_ + 1) % PWL_PIECES;
    if (sp_valid && sp_const) sp_sel_ = sp_which;
    if (sp_valid && !sp_const) sram_data_ = sp_row;
    last_sp_valid_ = sp_valid, last_sp_const_ = sp_valid && sp_const;
    last_rev_in_ = sp_valid && sp_rev_in, last_rev_out_ = sp_rev_out, last_delay_ = sp_delay;
    last_acc_rd_ = acc_rd, last_acc_rmw_ = acc_rmw;
    if (acc_rd) last_acc_addr_ = acc_addr;
    last_acc_zero_ = acc_rd && acc_zero;
    now_++;
  }

  bool reciprocal_next(int64_t c) const {
    for (const Fsm &f : fsm_) {
      if (!f.plan || f.t(c + 1) < 0) continue;
      for (const AccCtrl &x : f.plan->acc)
        if (x.cmd == ACC_RECIPROCAL && f.t(c + 1) >= x.cycle && f.t(c + 1) < x.cycle + x.repeat) return true;
    }
    return false;
  }
  // The flags FSA's kernel (python/fsa/kernel.py) gives each matrix instruction's scratchpad read.
  static void spad_flags(Kind k, bool &rev_in, bool &rev_out, bool &delay) {
    rev_in = k == ATTN_SCORE || k == ATTN_VALUE, rev_out = k == ATTN_SCORE, delay = k == ATTN_SCORE || k == ATTN_VALUE;
  }
  static Float viewed_as_elem(const Float &x) {
    if (x.ew != MUL_EW || x.mw != MUL_MW) throw std::logic_error("fsa array: load_reg_ui of a value that is no half");
    return x;
  }
  static Float slope(int piece) { return easyfloat::round_to(Float::of_bits(easyfloat::SLOPES[piece], 11, 52), MUL_EW, MUL_MW); }
  static Float intercept(int piece) { return easyfloat::round_to(Float::of_bits(easyfloat::INTERCEPTS[piece], 11, 52), ACC_EW, ACC_MW); }
  using Split = easyfloat::Split;

  uint32_t n_ = 0;
  Plan plans_[ATTN_LSE_NORM + 1];
  std::deque<Command> queue_;
  Fsm fsm_[2];
  uint32_t enq_ = 0;
  int64_t now_ = 0;
  std::vector<int64_t> fires_;
  std::vector<std::vector<Pe>> pe_;
  std::vector<Cmp> cmp_;
  std::vector<Word> acc_out_;
  std::map<uint32_t, Word> acc_out_new_;
  std::vector<std::deque<Word>> out_shift_, in_shift_;
  std::vector<Float> scale_, divisor_;
  bool dividing_ = false;
  std::vector<Word> sram_data_;
  std::map<uint32_t, std::vector<Float>> ram_;
  SpadConst sp_sel_ = NOT_CONST;
  int slope_counter_ = 0;
  bool last_sp_valid_ = false, last_sp_const_ = false, last_rev_in_ = false, last_rev_out_ = false, last_delay_ = false;
  bool delay_r_ = false, rev_out_r_ = false;
  bool last_acc_rd_ = false, last_acc_rmw_ = false, last_acc_zero_ = false;
  uint32_t last_acc_addr_ = 0;
};

}  // namespace fsa

#endif
