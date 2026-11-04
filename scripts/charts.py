# Draws the README's benchmark charts (docs/charts/*.svg) from scripts\bench.ps1 and compare.exe results.
#   python scripts/charts.py --game <bench dir with chipmunk-1.csv, rapier-1.csv, original-1.csv>
#                            [--compare <compare.txt>] [--dxvk <D3D9 rapier-1.csv> <DXVK rapier-1.csv>] [--out docs/charts]
# compare.txt: lines "N=<bodies> <engine>: orig <ms> new <ms>" and "T=<threads> <engine>: orig <ms> new <ms>".
# Plain SVG, no dependencies. Light and dark colors follow prefers-color-scheme; every point has a <title> tooltip.
import argparse, csv, math, os, re, sys

ENGINES = [('original', '원작'), ('chipmunk', 'Chipmunk 다중 코어'), ('rapier', 'Rapier')]
# categorical slots 1-3 (validated all-pairs, light and dark); color follows the engine in every chart
COLOR = {'original': ('#2a78d6', '#3987e5'), 'chipmunk': ('#eb6834', '#d95926'), 'rapier': ('#1baf7a', '#199e70'),
         'd3d9': ('#898781', '#898781'), 'dxvk': ('#2a78d6', '#3987e5')}
FONT = 'system-ui, -apple-system, "Segoe UI", "Malgun Gothic", sans-serif'
W, H = 720, 380
ML, MR, MT, MB = 64, 150, 84, 52   # right margin holds the direct labels


def style(keys):
    light = ''.join(f'.s-{k}{{stroke:{COLOR[k][0]}}}.f-{k}{{fill:{COLOR[k][0]}}}' for k in keys)
    dark = ''.join(f'.s-{k}{{stroke:{COLOR[k][1]}}}.f-{k}{{fill:{COLOR[k][1]}}}' for k in keys)
    return (f'<style>svg{{font-family:{FONT}}}.bg{{fill:#fcfcfb}}.t1{{fill:#0b0b0b}}.t2{{fill:#52514e}}.tm{{fill:#898781}}'
            f'.grid{{stroke:#e1e0d9}}.axis{{stroke:#c3c2b7}}.ring{{stroke:#fcfcfb}}.ref{{stroke:#898781}}{light}'
            f'@media (prefers-color-scheme: dark){{.bg{{fill:#1a1a19}}.t1{{fill:#f0efec}}.t2{{fill:#c3c2b7}}'
            f'.grid{{stroke:#2c2c2a}}.axis{{stroke:#383835}}.ring{{stroke:#1a1a19}}{dark}}}</style>')


def esc(s): return str(s).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


class Chart:
    def __init__(self, title, sub, xlab, ylab, xr, yr, ylog=False, xlog=False):
        self.title, self.sub, self.xlab, self.ylab = title, sub, xlab, ylab
        self.xr, self.yr, self.ylog, self.xlog = xr, yr, ylog, xlog
        self.parts, self.series, self.refs = [], [], []

    def X(self, v):
        a, b = self.xr
        f = (math.log10(v) - math.log10(a)) / (math.log10(b) - math.log10(a)) if self.xlog else (v - a) / (b - a)
        return ML + f * (W - ML - MR)

    def Y(self, v):
        a, b = self.yr
        f = (math.log10(v) - math.log10(a)) / (math.log10(b) - math.log10(a)) if self.ylog else (v - a) / (b - a)
        return H - MB - f * (H - MT - MB)

    def add(self, key, label, pts, fmt, markers=False):
        self.series.append((key, label, [p for p in pts if p[1] is not None], fmt, markers))

    def ref(self, y, label): self.refs.append((y, label))

    def svg(self, xticks, yticks, xfmt, yfmt):
        o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" role="img" aria-label="{esc(self.title)}">',
             style([s[0] for s in self.series]), f'<rect class="bg" width="{W}" height="{H}" rx="8"/>',
             f'<text class="t1" x="{ML}" y="26" font-size="16" font-weight="600">{esc(self.title)}</text>',
             f'<text class="t2" x="{ML}" y="46" font-size="12">{esc(self.sub)}</text>']
        for t in yticks:
            y = self.Y(t)
            o.append(f'<line class="grid" x1="{ML}" x2="{W - MR}" y1="{y:.1f}" y2="{y:.1f}" stroke-width="1"/>')
            o.append(f'<text class="tm" x="{ML - 8}" y="{y + 4:.1f}" font-size="11" text-anchor="end">{yfmt(t)}</text>')
        o.append(f'<line class="axis" x1="{ML}" x2="{W - MR}" y1="{H - MB}" y2="{H - MB}" stroke-width="1"/>')
        for t in xticks:
            x = self.X(t)
            o.append(f'<text class="tm" x="{x:.1f}" y="{H - MB + 18}" font-size="11" text-anchor="middle">{xfmt(t)}</text>')
        o.append(f'<text class="tm" x="{(ML + W - MR) / 2:.1f}" y="{H - 10}" font-size="11" text-anchor="middle">{esc(self.xlab)}</text>')
        o.append(f'<text class="tm" x="12" y="{MT - 14}" font-size="11" text-anchor="start">{esc(self.ylab)}</text>')
        for y, label in self.refs:
            yy = self.Y(y)
            o.append(f'<line class="ref" x1="{ML}" x2="{W - MR}" y1="{yy:.1f}" y2="{yy:.1f}" stroke-width="1" stroke-dasharray="4 4"/>')
            o.append(f'<text class="tm" x="{ML + 6}" y="{yy - 5:.1f}" font-size="11" text-anchor="start">{esc(label)}</text>')
        ends = []
        for key, label, pts, fmt, markers in self.series:
            if not pts: continue
            d = ' '.join(f'{"M" if i == 0 else "L"}{self.X(x):.1f},{self.Y(y):.1f}' for i, (x, y, _) in enumerate(pts))
            o.append(f'<path class="s-{key}" d="{d}" fill="none" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"/>')
            for x, y, tip in pts:
                cx, cy = self.X(x), self.Y(y)
                if markers: o.append(f'<circle class="f-{key} ring" cx="{cx:.1f}" cy="{cy:.1f}" r="4.5" stroke-width="2"/>')
                o.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="10" fill="transparent"><title>{esc(label)} · {esc(tip)}: {esc(fmt(y))}</title></circle>')
            x, y, _ = pts[-1]
            ends.append([self.Y(y), self.X(x), key, label, fmt(y)])
        # direct labels at the right edge, spread so they do not overlap; a leader line joins label and line end
        # a line that stops well before the right edge is labeled beside its end; the rest at the right edge, spread
        # apart, with a leader line from the line end
        edge = [e for e in ends if e[1] > W - MR - 60]; inner = [e for e in ends if e[1] <= W - MR - 60]
        edge.sort()
        for i in range(1, len(edge)):
            if edge[i][0] - edge[i - 1][0] < 30: edge[i][0] = edge[i - 1][0] + 30
        for ly, ex, key, label, val in edge + inner:
            ey = self._endy(key); lx = W - MR + 14 if [ly, ex, key, label, val] in edge else ex + 10
            if [ly, ex, key, label, val] in inner: ly = ey
            o.append(f'<circle class="f-{key} ring" cx="{ex:.1f}" cy="{ey:.1f}" r="4.5" stroke-width="2"/>')
            if lx > ex + 10: o.append(f'<line class="s-{key}" x1="{ex + 6:.1f}" y1="{ey:.1f}" x2="{lx - 4}" y2="{ly:.1f}" stroke-width="1"/>')
            o.append(f'<text class="t1" x="{lx}" y="{ly - 2:.1f}" font-size="12" font-weight="600">{esc(label)}</text>')
            o.append(f'<text class="t2" x="{lx}" y="{ly + 13:.1f}" font-size="11">{esc(val)}</text>')
        o.append('</svg>')
        return '\n'.join(o)

    def _endy(self, key):
        pts = [s for s in self.series if s[0] == key][0][2]
        return self.Y(pts[-1][1])


def load_run(path):
    rows = list(csv.DictReader(open(path, encoding='utf-8-sig')))
    out, started = [], False
    for r in rows:
        if 'swarm' in (r.get('note') or ''): started = True; continue
        if started and r.get('paused') != '1' and not r.get('note'): out.append(r)
    return out


def binned(rows, key, width=5000, least=3):
    bins = {}
    for r in rows:
        try: v = float(r[key])
        except (TypeError, ValueError): continue
        bins.setdefault(int(r['zombies']) // width, []).append(v)
    pts = []
    for b in sorted(bins):
        if len(bins[b]) < least: continue
        lo = b * width
        pts.append((lo + width / 2, sum(bins[b]) / len(bins[b]), f'좀비 {lo // 1000}~{(lo + width) // 1000}천 ({len(bins[b])}초)'))
    return pts


def man(v): return f'{v / 10000:g}만'


def write(out, name, text):
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, name), 'w', encoding='utf-8', newline='\n') as f: f.write(text + '\n')
    print('wrote', os.path.join(out, name))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game'); ap.add_argument('--compare'); ap.add_argument('--dxvk', nargs=2); ap.add_argument('--out', default='docs/charts')
    a = ap.parse_args()
    xt = [0, 20000, 40000, 60000, 80000, 100000]
    if a.game:
        runs = {e: load_run(os.path.join(a.game, f'{e}-1.csv')) for e, _ in ENGINES if os.path.exists(os.path.join(a.game, f'{e}-1.csv'))}
        c = Chart('게임 진행 속도', '실제 1초에 게임 시계가 가는 비율 (1.0 = 제 속도). 좀비 5천 마리 구간 평균', '좀비 수', '게임 시간 배율', (0, 105000), (0, 1.1))
        for e, n in ENGINES:
            if e in runs: c.add(e, n, binned(runs[e], 'g_clock_rate'), lambda v: f'{v:.2f}배')
        c.ref(1.0, '제 속도')
        write(a.out, 'game-speed.svg', c.svg(xt, [0, 0.25, 0.5, 0.75, 1.0], man, lambda v: f'{v:g}'))
        c = Chart('물리 단계 시간', 'Space_Step 한 번 (로그 눈금). 1/12초를 넘으면 게임이 제 속도로 못 돈다', '좀비 수', 'ms', (0, 105000), (5, 800), ylog=True)
        for e, n in ENGINES:
            if e in runs: c.add(e, n, binned(runs[e], 'step_avg_ms'), lambda v: f'{v:.0f} ms')
        c.ref(1000 / 12, '1/12초 (83 ms)')
        write(a.out, 'step-time.svg', c.svg(xt, [10, 30, 100, 300], man, lambda v: f'{v:g}'))
        c = Chart('프레임 시간', '한 프레임을 그리는 데 걸린 평균 시간 (카메라 최대 줌)', '좀비 수', 'ms', (0, 105000), (0, 160))
        for e, n in ENGINES:
            if e in runs: c.add(e, n, binned(runs[e], 'frame_avg_ms'), lambda v: f'{v:.0f} ms')
        write(a.out, 'frame-time.svg', c.svg(xt, [0, 40, 80, 120, 160], man, lambda v: f'{v:g}'))
    if a.compare:
        res = {}
        for line in open(a.compare, encoding='utf-8'):
            m = re.match(r'([NT])=(\d+) (\w+):\s+orig ([\d.]+) new ([\d.]+)', line)
            if m: res.setdefault(m.group(1), {}).setdefault(m.group(3), []).append((int(m.group(2)), float(m.group(4)), float(m.group(5))))
        n = res.get('N', {})
        if n:
            c = Chart('비교 도구: 몸체 수별 단계 시간', 'compare.exe 세 장면 × 30단계 평균 (가로·세로 로그 눈금)', '몸체 수', 'ms', (1700, 30000), (1, 2000), ylog=True, xlog=True)
            orig = {}
            for e in n:
                for b, o, _ in n[e]: orig.setdefault(b, []).append(o)
            c.add('original', '원작', [(b, sum(v) / len(v), f'몸체 {b:,}') for b, v in sorted(orig.items())], lambda v: f'{v:,.1f} ms', True)
            for e, nm in ENGINES[1:]:
                if e in n: c.add(e, nm, [(b, nw, f'몸체 {b:,}') for b, _, nw in sorted(n[e])], lambda v: f'{v:,.1f} ms', True)
            write(a.out, 'compare-bodies.svg', c.svg([2000, 6000, 12000, 25000], [1, 10, 100, 1000], lambda v: f'{v:,}', lambda v: f'{v:,g}'))
        t = res.get('T', {})
        if t:
            auto = {e: [nw for b, _, nw in n.get(e, []) if b == 12000] for e in t}
            c = Chart('비교 도구: 작업 스레드 수별 단계 시간', '몸체 12,000개, 세 장면 × 30단계 평균', '작업 스레드 (0 = 단일 스레드)', 'ms', (0, 16), (0, 200))
            for e, nm in ENGINES[1:]:
                if e in t: c.add(e, nm, [(b, nw, f'스레드 {b}') for b, _, nw in sorted(t[e])], lambda v: f'{v:.1f} ms', True)
            write(a.out, 'compare-threads.svg', c.svg([0, 3, 7, 15], [0, 50, 100, 150, 200], lambda v: f'{v:g}', lambda v: f'{v:g}'))
    if a.dxvk:
        base, dx = load_run(a.dxvk[0]), load_run(a.dxvk[1])
        c = Chart('DXVK: 프레임 시간', 'Rapier, 같은 시험 조건. 회색 = 시스템 Direct3D 9, 파랑 = DXVK (Vulkan)', '좀비 수', 'ms', (0, 105000), (0, 140))
        c.add('d3d9', 'Direct3D 9', binned(base, 'frame_avg_ms'), lambda v: f'{v:.0f} ms')
        c.add('dxvk', 'DXVK', binned(dx, 'frame_avg_ms'), lambda v: f'{v:.0f} ms')
        write(a.out, 'dxvk-frame.svg', c.svg(xt, [0, 35, 70, 105, 140], man, lambda v: f'{v:g}'))
        c = Chart('DXVK: 프로세스 CPU 사용량', '논리 코어 수 기준. 좀비가 적을 때는 DXVK가 프레임을 더 많이 그려 CPU를 더 쓴다', '좀비 수', '코어', (0, 105000), (0, 24))
        c.add('d3d9', 'Direct3D 9', binned(base, 'g_cpu_cores'), lambda v: f'{v:.1f}코어')
        c.add('dxvk', 'DXVK', binned(dx, 'g_cpu_cores'), lambda v: f'{v:.1f}코어')
        write(a.out, 'dxvk-cpu.svg', c.svg(xt, [0, 6, 12, 18, 24], man, lambda v: f'{v:g}'))


if __name__ == '__main__':
    main()
