"""Exercise the full 3v3 CUDA batch and resume without starting a long run."""
import json
import math
import subprocess
import uuid
from pathlib import Path

root = Path(__file__).resolve().parents[1]
dest = root / 'engine/build/3v3-checks' / uuid.uuid4().hex
dest.mkdir(parents=True)
config = json.loads((root / 'configs/presets/3v3-team.json').read_text())
config.update(iterations=2, checkpointEvery=1)
path = dest / 'config.json'
path.write_text(json.dumps(config))
exe = root / 'engine/build/bin/rl-engine.exe'
batch = config['arenas'] * 6 * config['rolloutSteps']
checkpoint = None
for name, offset in [('fresh', 0), ('resumed', 2)]:
    args = [str(exe), 'train', '--config', str(path), '--run', str(dest / name)]
    if checkpoint:
        args += ['--checkpoint', str(checkpoint)]
    result = subprocess.run(args, cwd=root, capture_output=True, text=True, timeout=180)
    assert result.returncode == 0, result.stdout + result.stderr
    events = [json.loads(line) for line in result.stdout.splitlines()]
    metrics = [e for e in events if e['type'] == 'metrics']
    assert len(metrics) == 2
    assert metrics[-1]['steps'] == (offset + 2) * batch
    assert all(math.isfinite(v) for m in metrics for v in m.values() if isinstance(v, (float, int)))
    assert any(e['type'] == 'frame' and len(e['cars']) == 6 for e in events)
    assert events[-1]['status'] == 'completed'
    checkpoint = dest / name / 'checkpoints' / str(offset + 2)
    assert (checkpoint / 'optimizer.pt').is_file()
    print(name, metrics[-1]['steps'], 'steps;', round(metrics[-1]['stepsPerSecond']), 'steps/s', flush=True)
print('PASS: full 3v3 batch, six cars, finite PPO metrics, CUDA, checkpoint resume')
