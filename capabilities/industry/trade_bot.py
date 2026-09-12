#!/usr/bin/env python3
"""
trade_bot.py — 自动下单交易机器人（变体4：均值回归 + 风控 + 纸面撮合）

设计边界（务必阅读）：
  * 本脚本 **只做纸面交易（paper trading）**，不连接任何真实券商/交易所。
  * 不发起任何网络请求；行情由本地 CSV 或内置合成序列提供。
  * 所有"下单"写入本地账本文件，供复盘与回测，不产生真实资金风险。

变体4 策略要点：
  1. 均值回归（mean reversion）：价格偏离 N 日均线超过阈值时反向入场。
  2. 分层风控：单笔风险上限、最大持仓、日内最大回撤熔断。
  3. 仓位按波动率缩放（简化版 vol targeting）。
  4. 纸面撮合：市价单按当前价成交，含手续费与滑点。

用法：
  python3 capabilities/industry/trade_bot.py --prices prices.csv --out ledger.csv
  python3 capabilities/industry/trade_bot.py --demo        # 用内置合成行情跑一遍

CSV 格式（无表头或带表头均可，两列）：
  date,close
  2026-01-01,100.0
  ...
"""

import argparse
import csv
import math
import os
import sys
from dataclasses import dataclass, field
from typing import List, Optional


# ----------------------------- 配置 -----------------------------

@dataclass
class Config:
    # 策略参数
    lookback: int = 20            # 均线窗口
    entry_z: float = 1.5          # 入场偏离阈值（以标准差为单位）
    exit_z: float = 0.3           # 平仓回归阈值
    # 风控参数
    max_position: float = 1.0     # 最大持仓（单位：股/手）
    risk_per_trade: float = 0.02  # 单笔风险占权益比例
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


def synth_prices(n: int = 240, seed: int = 42) -> List[Bar]:
    """内置合成行情：带趋势 + 噪声，便于 --demo 自检。"""
    import random
    rng = random.Random(seed)
    price = 100.0
    bars = []
    for i in range(n):
        drift = 0.0002 * math.sin(i / 30.0)      # 缓慢摆动
        shock = rng.gauss(0, 0.012)              # 日波动
        price *= (1 + drift + shock)
        bars.append(Bar(f"T{i:04d}", round(price, 4)))
    return bars


# ----------------------------- 指标 -----------------------------

def rolling_stats(series: List[float], idx: int, window: int):
    """返回 (均值, 标准差)；数据不足返回 (None, None)。"""
    if idx < window:
        return None, None
    seg = series[idx - window:idx]
    mean = sum(seg) / window
    var = sum((x - mean) ** 2 for x in seg) / window
    return mean, math.sqrt(var)


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
        if mean is None or sd == 0:
            st.ledger.append(LedgerRow(bar.date, "HOLD", price, 0, st.cash,
                                       st.position, equity, "数据不足"))
            continue

        z = (price - mean) / sd

        # --- 入场：均值回归 ---
        if st.position == 0 and abs(z) >= cfg.entry_z:
            side = "BUY" if z < 0 else "SELL"   # 低估买、高估卖（此处仅做多）
            if side == "BUY":
                # 波动率缩放仓位：波动越大，仓位越小
                target = min(cfg.max_position,
                             cfg.risk_per_trade * st.cash / (sd * price) if sd else 0)
                qty = max(0.0, round(target, 4))
                if qty > 0:
                    px = fill_price(price, "BUY", cfg)
                    cost = px * qty
                    if cost + apply_fee(cost, cfg) <= st.cash:
                        st.cash -= cost + apply_fee(cost, cfg)
                        st.position += qty
                        st.ledger.append(LedgerRow(bar.date, "BUY", px, qty, st.cash,
                                                   st.position,
                                                   st.cash + st.position * price,
                                                   f"z={z:.2f} 低估入场"))
                        continue

        # --- 出场：回归均值 ---
        elif st.position > 0 and abs(z) <= cfg.exit_z:
            px = fill_price(price, "SELL", cfg)
            notional = px * st.position
            st.cash += notional - apply_fee(notional, cfg)
            qty = st.position
            st.position = 0.0
            st.ledger.append(LedgerRow(bar.date, "SELL", px, qty, st.cash,
                                       0.0, st.cash, f"z={z:.2f} 回归平仓"))
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
    p = argparse.ArgumentParser(description="自动下单交易机器人（变体4，纸面交易）")
    p.add_argument("--prices", help="行情 CSV 路径（date,close）")
    p.add_argument("--out", default="ledger.csv", help="账本输出路径")
    p.add_argument("--demo", action="store_true", help="使用内置合成行情")
    p.add_argument("--lookback", type=int, default=20)
    p.add_argument("--entry-z", type=float, default=1.5)
    p.add_argument("--exit-z", type=float, default=0.3)
    p.add_argument("--max-drawdown", type=float, default=0.15)
    args = p.parse_args(argv)

    cfg = Config(lookback=args.lookback, entry_z=args.entry_z,
                 exit_z=args.exit_z, max_drawdown=args.max_drawdown)

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
