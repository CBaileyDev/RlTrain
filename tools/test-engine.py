"""Integration checks against the compiled engine. Run from any directory."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import queue
import subprocess
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / 'engine/build/bin/rl-engine.exe'
DEST = ROOT / 'engine/build/integration-tests' / uuid.uuid4().hex[:12]
DEST.mkdir(parents=True)
BASE = json.loads((ROOT / 'configs/presets/smoke.json').read_text(encoding='utf-8'))

def config_file(name, **overrides):
    path = DEST / (name + '.json')
    path.write_text(json.dumps({**BASE, **overrides}), encoding='utf-8')
    return path

def execute(*args, expected=0):
    result = subprocess.run([str(EXE), *map(str, args)], cwd=ROOT, capture_output=True, text=True, timeout=90)
    assert result.returncode == expected, (result.returncode, result.stdout, result.stderr)
    rows = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
    assert rows, 'No protocol output'
    return rows

def train(name, device='cpu', checkpoint=None):
    run = DEST / name
    args = ['train', '--config', config_file(name + '-config', device=device), '--run', run]
    if checkpoint: args += ['--checkpoint', checkpoint]
    rows = execute(*args)
    metrics = [row for row in rows if row['type'] == 'metrics']
    assert len(metrics) == 2
    assert all(math.isfinite(value) for row in metrics for value in row.values() if isinstance(value, (float, int)))
    assert metrics[1]['steps'] > metrics[0]['steps']
    assert rows[-1]['status'] == 'completed'
    checkpoints = sorted((run / 'checkpoints').iterdir(), key=lambda p: int(p.name))
    hashes = [hashlib.sha256((cp / 'model.pt').read_bytes()).hexdigest() for cp in checkpoints]
    assert hashes[0] != hashes[1], 'Model archive did not change after optimization'
    return checkpoints[-1]

def controls():
    run = DEST / 'controls'
    path = config_file('controls-config', iterations=100000, rolloutSteps=128, minibatchSize=256)
    process = subprocess.Popen([str(EXE), 'train', '--config', str(path), '--run', str(run), '--interactive'],
        cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    inbox = queue.Queue()
    def reader():
        for line in process.stdout:
            inbox.put(json.loads(line))
    threading.Thread(target=reader, daemon=True).start()
    def wait(predicate):
        end = time.monotonic() + 30
        while time.monotonic() < end:
            row = inbox.get(timeout=30)
            assert row['type'] != 'error', row
            if predicate(row): return row
        raise TimeoutError('Control acknowledgement timed out')
    def send(value):
        process.stdin.write(json.dumps(value) + '\n'); process.stdin.flush()
    try:
        wait(lambda r: r['type'] == 'started')
        send({'type': 'pause'}); wait(lambda r: r.get('status') == 'paused')
        send({'type': 'rewards', 'values': {'touch': 7}})
        assert wait(lambda r: r['type'] == 'config')['config']['touch'] == 7
        send({'type': 'resume'}); wait(lambda r: r.get('status') == 'running')
        wait(lambda r: r['type'] == 'metrics')
        send({'type': 'stop'}); wait(lambda r: r.get('status') == 'stopped')
        assert process.wait(timeout=10) == 0
        assert list((run / 'checkpoints').glob('*/model.pt'))
    finally:
        if process.poll() is None: process.kill()
        process.communicate(timeout=10)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--cuda', action='store_true'); args = parser.parse_args()
    first = train('cpu'); resumed = train('resumed', checkpoint=first)
    result = execute('eval', '--a', first, '--b', resumed, '--matches', 2)
    evaluation = [r for r in result if r['type'] == 'evaluation'][-1]
    assert evaluation['matches'] == evaluation['blueWins'] + evaluation['orangeWins'] + evaluation['draws'] == 2
    execute('unknown-command', expected=2)
    execute('train', '--config', config_file('invalid', arenas=0), expected=2)
    controls()
    if args.cuda:
        gpu = train('cuda', device='cuda')
        execute('eval', '--a', gpu, '--b', gpu, '--matches', 2)
        train('cpu-from-cuda', checkpoint=gpu)
    print('PASS: training changes model, finite metrics, checkpoint resume, evaluation, validation, pause/rewards/resume/stop' + (', CUDA' if args.cuda else ''))
    print('Artifacts:', DEST)
