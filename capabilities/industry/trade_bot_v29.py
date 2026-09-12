#!/usr/bin/env python3
"""
trade_bot_v29.py — 自动下单交易机器人（变体29：布林带 + RSI 双确认均值回归 + 凯利分数仓位 + 分层风控 + 纸面撮合）

设计边界（务必阅读）：
  * 本脚本 **只做纸面交易（paper trading）**，不连接任何真实券商/交易所。
  * 不发起任何网络请求；行情由本地 CSV 或内置合成序列提供。
  * 所有"下单"写入本地账本文件，供复盘与回测，不产生真实资金风险。
  * 不构成投资建议，不承诺收益。

变体29 策略要点（与变体4/9/24 的差异）：
  1. 布林带（Bollinger Bands）：以 N 日均线 ± k 倍标准差构造上下轨。
  2. RSI 双确认：仅在价格触带 **且** RSI 处于超买/超卖区时才入场，过滤假突破。
  3. 凯利分数仓位（fractional Kelly）：按历史胜率与盈亏比估算最优下注比例，
     再乘以 kelly_fraction（默认 0.5，半凯利）保守缩放，并受单笔风险上限约束。
  4. 分层风控：单笔风险上限、最大持仓、日内最大回撤熔断（触发即清仓并停手）。
  5. 纸面撮合：市价单按当前价成交，含手续费与滑点。

用法：
  python3 capabilities/industry/trade_bot_v29.py --prices prices.csv --out ledger_v29.csv
  python3 capabilities/industry/trade_bot_v29.py --demo        # 用内置合成行情跑一遍

CSV 格式（无表头或带表头均可，两列）：
  date,close
  2026-01-01,100.0
  ...
"""

import argparse
import csv
import math
import sys
from dataclasses import dataclass, field
from typing import List, Optional, Tuple


# ----------------------------- 配置 -----------------------------

@dataclass
class Config:
    # 策略参数
    lookback: int = 20            # 布林带 / RSI 窗口
    band_k: float = 2.0           # 布林带标准差倍数
    rsi_period: int = 14          # RSI 周期
    rsi_oversold: float = 30.0    # 超卖阈值（做多入场确认）
    rsi_overbought: float = 70.0  # 超买阈值（平多出场确认）
    exit_mid: bool = True         # 价格回到中轨即平仓
    # 仓位（凯利）
    kelly_fraction: float = 0.5   # 半凯利缩放
    kelly_lookback: int = 60      # 估算胜率/盈亏比的滚动窗口
    max_position: float = 1.0     # 最大持仓（单位：股/手）
    risk_per_trade: float = 0.02  # 单笔风险占权益比例上限
    # 风控
    max_drawdown: float = 0.15    # 日内最大回撤熔断线
    # 成本
    fee_rate: float = 0.0003      # 手续费率
    slippage: float = 0.0005      # 滑点（价格比例）
    # 初始资金
    initial_cash: float = 100_000.0


# ----------------------------- 数据结构 -----------------------------

@dataclass
class Bar:
    date: str
    close: float


@dataclass
class LedgerRow:
    date: str
    action: str          # BUY / SELL / HOLD / HALT
    price: float
    qty: float
    cash: float
    position: float
    equity: float
    note: str = ""


@dataclass
class State:
    cash: float
    position: float = 0.0
    peak_equity: float = 0.0
    halted: bool = False
    ledger: List[LedgerRow] = field(default_factory=list)


# ----------------------------- 行情加载 -----------------------------

def load_prices(path: str) -> List[Bar]:
    bars: List[Bar] = []
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.reader(f)
        for row in reader:
            if not row or len(row) < 2:
                continue
            d, c = row[0].strip(), row[1].strip()
            try:
                price = float(c)
            except ValueError:
                continue  # 跳过表头
            bars.append(Bar(d, price))
    if not bars:
        raise ValueError(f"未从 {path} 读到任何有效行情")
    return bars


def synth_prices(n: int = 240, seed: int = 29) -> List[Bar]:
    """内置合成行情：带均值回复 + 趋势 + 噪声，便于 --demo 自检。"""
    import random
    rng = random.Random(seed)
    price = 100.0
    anchor = 100.0
    bars = []
    for i in range(n):
        # 向锚点缓慢回复 + 周期性摆动 + 日噪声
        revert = 0.02 * (anchor - price) / anchor
        drift = 0.0004 * math.sin(i / 25.0)
        shock = rng.gauss(0, 0.011)
        price *= (1 + revert + drift + shock)
        bars.append(Bar(f"T{i:04d}", round(price, 4)))
    return bars


# ----------------------------- 指标 -----------------------------

def rolling_stats(series: List[float], idx: int, window: int) -> Tuple[Optional[float], Optional[float]]:
    """返回 (均值, 标准差)；数据不足返回 (None, None)。"""
    if idx < window:
        return None, None
    seg = series[idx - window:idx]
    mean = sum(seg) / window
    var = sum((x - mean) ** 2 for x in seg) / window
    return mean, math.sqrt(var)


def rsi(series: List[float], idx: int, period: int) -> Optional[float]:
    """Wilder 简化版 RSI：基于最近 period 根的平均涨/跌幅。"""
    if idx < period:
        return None
    gains = 0.0
    losses = 0.0
    for j in range(idx - period + 1, idx + 1):
        change = series[j] - series[j - 1]
        if change >= 0:
            gains += change
        else:
            losses -= change
    avg_gain = gains / period
    avg_loss = losses / period
    if avg_loss == 0:
        return 100.0
    rs = avg_gain / avg_loss
    return 100.0 - (100.0 / (1.0 + rs))


# ----------------------------- 凯利仓位 -----------------------------

def kelly_fraction_size(closes: List[float], idx: int, cfg: Config) -> float:
    """
    用滚动窗口内的"上涨日/下跌日"估算胜率 p 与盈亏比 b，返回凯利比例 f*。
    f* = p - (1-p)/b ；再乘 kelly_fraction 缩放。数据不足或 b<=0 时返回保守默认。
    """
    start = max(1, idx - cfg.kelly_lookback)
    wins = 0
    losses = 0
    gain_sum = 0.0
    loss_sum = 0.0
    for j in range(start, idx):
        change = closes[j] - closes[j - 1]
        if change > 0:
            wins += 1
            gain_sum += change
        elif change < 0:
            losses += 1
            loss_sum += -change
    total = wins + losses
    if total < 10 or losses == 0 or wins == 0:
        return 0.1 * cfg.kelly_fraction  # 保守默认
    p = wins / total
    avg_win = gain_sum / wins
    avg_loss = loss_sum / losses
    b = avg_win / avg_loss if avg_loss > 0 else 0.0
    if b <= 0:
        return 0.0
    f_star = p - (1.0 - p) / b
    f_star = max(0.0, min(f_star, 1.0))
    return f_star * cfg.kelly_fraction


# ----------------------------- 撮合 -----------------------------

def fill_price(price: float, side: str, cfg: Config) -> float:
    """市价单含滑点：买入略高、卖出略低。"""
    if side == "BUY":
        return price * (1 + cfg.slippage)
    return price * (1 - cfg.slippage)


def apply_fee(notional: float, cfg: Config) -> float:
    return abs(notional) * cfg.fee_rate


# ----------------------------- 主循环 -----------------------------

def run(bars: List[Bar], cfg: Config) -> State:
    closes = [b.close for b in bars]
    st = State(cash=cfg.initial_cash)
    st.peak_equity = cfg.initial_cash

    for i, bar in enumerate(bars):
        price = bar.close
        equity = st.cash + st.position * price

        # --- 风控：回撤熔断 ---
        st.peak_equity = max(st.peak_equity, equity)
        dd = (st.peak_equity - equity) / st.peak_equity if st.peak_equity else 0.0
        if dd >= cfg.max_drawdown and not st.halted:
            st.halted = True
            if st.position > 0:
                px = fill_price(price, "SELL", cfg)
                notional = px * st.position
                st.cash += notional - apply_fee(notional, cfg)
                st.position = 0.0
                st.ledger.append(LedgerRow(bar.date, "HALT", px, 0, st.cash,
                                           0.0, st.cash, f"回撤 {dd:.1%} 熔断，清仓"))
            else:
                st.ledger.append(LedgerRow(bar.date, "HALT", price, 0, st.cash,
                                           0.0, st.cash, f"回撤 {dd:.1%} 熔断"))
            continue

        if st.halted:
            st.ledger.append(LedgerRow(bar.date, "HOLD", price, 0, st.cash,
                                       st.position, equity, "熔断中，不再开仓"))
            continue

        mean, sd = rolling_stats(closes, i, cfg.lookback)
        r = rsi(closes, i, cfg.rsi_period)
        if mean is None or sd == 0 or r is None:
            st.ledger.append(LedgerRow(bar.date, "HOLD", price, 0, st.cash,
                                       st.position, equity, "数据不足"))
            continue

        upper = mean + cfg.band_k * sd
        lower = mean - cfg.band_k * sd

        # --- 入场：布林带触带 + RSI 双确认（仅做多） ---
        if st.position == 0 and price <= lower and r <= cfg.rsi_oversold:
            f = kelly_fraction_size(closes, i, cfg)
            # 凯利比例 -> 目标名义仓位，再受单笔风险上限约束
            target_notional = min(f * st.cash,
                                  cfg.risk_per_trade * st.cash / max(cfg.slippage + cfg.fee_rate, 1e-6))
            qty = min(cfg.max_position, target_notional / price if price > 0 else 0.0)
            qty = max(0.0, round(qty, 4))
            if qty > 0:
                px = fill_price(price, "BUY", cfg)
                cost = px * qty
                if cost + apply_fee(cost, cfg) <= st.cash:
                    st.cash -= cost + apply_fee(cost, cfg)
                    st.position += qty
                    st.ledger.append(LedgerRow(bar.date, "BUY", px, qty, st.cash,
                                               st.position,
                                               st.cash + st.position * price,
                                               f"触下轨 z={(price-mean)/sd:.2f} RSI={r:.1f} kelly={f:.2f}"))
                    continue

        # --- 出场：回到中轨 或 RSI 超买 ---
        elif st.position > 0 and ((cfg.exit_mid and price >= mean) or r >= cfg.rsi_overbought):
            px = fill_price(price, "SELL", cfg)
            notional = px * st.position
            st.cash += notional - apply_fee(notional, cfg)
            qty = st.position
            st.position = 0.0
            reason = "回归中轨" if price >= mean else f"RSI={r:.1f} 超买"
            st.ledger.append(LedgerRow(bar.date, "SELL", px, qty, st.cash,
                                       0.0, st.cash, reason))
            continue

        st.ledger.append(LedgerRow(bar.date, "HOLD", price, 0, st.cash,
                                   st.position, st.cash + st.position * price, ""))

    return st


# ----------------------------- 输出 -----------------------------

def write_ledger(st: State, path: str) -> None:
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["date", "action", "price", "qty", "cash",
                    "position", "equity", "note"])
        for r in st.ledger:
            w.writerow([r.date, r.action, f"{r.price:.4f}", f"{r.qty:.4f}",
                        f"{r.cash:.2f}", f"{r.position:.4f}",
                        f"{r.equity:.2f}", r.note])


def summarize(st: State, cfg: Config) -> str:
    final = st.ledger[-1].equity if st.ledger else cfg.initial_cash
    ret = (final - cfg.initial_cash) / cfg.initial_cash
    trades = sum(1 for r in st.ledger if r.action in ("BUY", "SELL"))
    halts = sum(1 for r in st.ledger if r.action == "HALT")
    return (f"初始权益: {cfg.initial_cash:,.2f}\n"
            f"期末权益: {final:,.2f}\n"
            f"收益率  : {ret:+.2%}\n"
            f"成交笔数: {trades}\n"
            f"熔断次数: {halts}\n"
            f"最终持仓: {st.position:.4f}")


# ----------------------------- CLI -----------------------------

def main(argv: Optional[List[str]] = None) -> int:
    p = argparse.ArgumentParser(description="自动下单交易机器人（变体29，纸面交易）")
    p.add_argument("--prices", help="行情 CSV 路径（date,close）")
    p.add_argument("--out", default="ledger_v29.csv", help="账本输出路径")
    p.add_argument("--demo", action="store_true", help="使用内置合成行情")
    p.add_argument("--lookback", type=int, default=20, help="布林带/RSI 窗口")
    p.add_argument("--band-k", type=float, default=2.0, help="布林带标准差倍数")
    p.add_argument("--rsi-period", type=int, default=14, help="RSI 周期")
    p.add_argument("--rsi-oversold", type=float, default=30.0, help="RSI 超卖阈值")
    p.add_argument("--rsi-overbought", type=float, default=70.0, help="RSI 超买阈值")
    p.add_argument("--kelly-fraction", type=float, default=0.5, help="凯利缩放（半凯利=0.5）")
    p.add_argument("--max-drawdown", type=float, default=0.15, help="回撤熔断线")
    args = p.parse_args(argv)

    cfg = Config(lookback=args.lookback, band_k=args.band_k,
                 rsi_period=args.rsi_period, rsi_oversold=args.rsi_oversold,
                 rsi_overbought=args.rsi_overbought,
                 kelly_fraction=args.kelly_fraction,
                 max_drawdown=args.max_drawdown)

    if args.demo:
        bars = synth_prices()
        print("[demo] 使用内置合成行情（240 根）")
    elif args.prices:
        bars = load_prices(args.prices)
    else:
        print("ERROR: 需要 --prices <csv> 或 --demo", file=sys.stderr)
        return 2

    st = run(bars, cfg)
    write_ledger(st, args.out)
    print(summarize(st, cfg))
    print(f"\n账本已写入: {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
