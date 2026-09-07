"""Integration checks against the compiled engine. Run from any directory."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import queue
import shutil
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

def train(name, device='cpu', checkpoint=None, **overrides):
    run = DEST / name
    path = config_file(name + '-config', device=device, **overrides)
    config = json.loads(path.read_text())
    initial = json.loads((checkpoint / 'metadata.json').read_text()) if checkpoint else {'iteration': 0, 'steps': 0}
    batch = config['arenas'] * config['teamSize'] * 2 * config['rolloutSteps']
    args = ['train', '--config', path, '--run', run]
    if checkpoint: args += ['--checkpoint', checkpoint]
    rows = execute(*args)
    started = next(row for row in rows if row['type'] == 'started')
    assert started['steps'] == started['initialSteps'] == initial['steps']
    assert started['iteration'] == started['initialIteration'] == initial['iteration']
    assert started['targetIteration'] == initial['iteration'] + config['iterations']
    assert started['resumed'] == bool(checkpoint)
    assert rows.index(started) < next(i for i, row in enumerate(rows) if row['type'] == 'metrics')
    metadata = json.loads((run / 'metadata.json').read_text())
    assert metadata['initialSteps'] == initial['steps']
    metrics = [row for row in rows if row['type'] == 'metrics']
    assert len(metrics) == 2
    assert all(math.isfinite(value) for row in metrics for value in row.values() if isinstance(value, (float, int)))
    for index, metric in enumerate(metrics, 1):
        assert metric['steps'] == initial['steps'] + index * batch
        assert metric['iteration'] == initial['iteration'] + index
        assert metric['sessionSteps'] == index * batch
        assert metric['sessionIteration'] == index
    assert rows[-1]['status'] == 'completed'
    assert rows[-1]['steps'] == metrics[-1]['steps']
    assert json.loads((run / 'summary.json').read_text())['steps'] == metrics[-1]['steps']
    checkpoints = sorted((run / 'checkpoints').iterdir(), key=lambda p: int(p.name))
    hashes = [hashlib.sha256((cp / 'model.pt').read_bytes()).hexdigest() for cp in checkpoints]
    assert hashes[0] != hashes[1], 'Model archive did not change after optimization'
    assert json.loads((checkpoints[-1] / 'metadata.json').read_text())['steps'] == metrics[-1]['steps']
    return checkpoints[-1]

def stop_partial(checkpoint):
    """Stopping in the first rollout preserves the checkpoint's trained counters."""
    run = DEST / 'stop-partial'
    path = config_file('stop-partial-config', rolloutSteps=4096)
    process = subprocess.Popen([str(EXE), 'train', '--config', str(path), '--run', str(run),
        '--checkpoint', str(checkpoint), '--interactive'], cwd=ROOT, stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    rows = []
    def reader():
        for line in process.stdout:
            row = json.loads(line)
            rows.append(row)
            if row['type'] == 'frame':
                process.stdin.write('{"type":"stop"}\n')
                process.stdin.flush()
                break
        rows.extend(json.loads(line) for line in process.stdout if line.strip())
    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    try:
        assert process.wait(timeout=60) == 0
        thread.join(timeout=10)
        assert not thread.is_alive()
        assert any(r['type'] == 'frame' for r in rows)
        assert not any(r['type'] == 'metrics' for r in rows), 'Stop must occur before first optimization'
        initial = json.loads((checkpoint / 'metadata.json').read_text())
        saved = json.loads((run / 'checkpoints' / str(initial['iteration']) / 'metadata.json').read_text())
        summary = json.loads((run / 'summary.json').read_text())
        assert saved['steps'] == summary['steps'] == initial['steps']
        assert summary['sessionSteps'] == summary['sessionIteration'] == 0
        assert rows[-1]['status'] == 'stopped'
    finally:
        if process.poll() is None: process.kill()
        process.communicate(timeout=10)

def progress_metadata(checkpoint):
    """Long histories remain 64-bit; invalid counters fail before loading weights."""
    copied = DEST / 'large-counter-checkpoint'
    shutil.copytree(checkpoint, copied)
    original = json.loads((copied / 'metadata.json').read_text())
    (copied / 'metadata.json').write_text(json.dumps({**original, 'steps': 5_000_000_000}))
    train('large-counter-resume', checkpoint=copied)
    for index, invalid in enumerate([
        {'steps': -1}, {'steps': 2**63}, {'steps': 1.5},
        {'iteration': -1}, {'iteration': 2**31 - 1}, {'iteration': '2'},
    ]):
        (copied / 'metadata.json').write_text(json.dumps({**original, **invalid}))
        result = execute('train', '--config', config_file('bad-progress-config'),
            '--run', DEST / ('bad-progress-' + str(index)), '--checkpoint', copied, expected=1)
        assert any('Invalid checkpoint progress counters' in row.get('message', '') for row in result)

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
    train('resumed-again', checkpoint=resumed, rolloutSteps=16)
    stop_partial(resumed)
    progress_metadata(resumed)
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
    print('PASS: model updates, finite metrics, initial and cumulative resume counters, changed rollout resume, partial-stop counters, evaluation, validation, pause/rewards/resume/stop' + (', CUDA' if args.cuda else ''))
    print('Artifacts:', DEST)
