"""Exercise the game, match tool and generator for a native PGO build."""
from pathlib import Path
import argparse
import os
import subprocess


def agent_seeds(seed):
    # core/rng.h and tools/ai_match.cpp, first pair, player A in seat zero.
    mask = (1 << 64) - 1
    stream = 0x6D61746368
    state = (seed ^ (stream * 0xD1342543DE82EF95 + 0x2545F4914F6CDD1D)) & mask

    def split():
        nonlocal state
        state = (state + 0x9E3779B97F4A7C15) & mask
        z = state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & mask
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & mask
        return z ^ (z >> 31)

    def rotate(v, k):
        return ((v << k) | (v >> (64 - k))) & mask

    split()
    state ^= stream
    s = [split() for _ in range(4)]
    numbers = []
    for _ in range(3):
        numbers.append(rotate(s[1] * 5 & mask, 7) * 9 & mask)
        t = (s[1] << 17) & mask
        s[2] ^= s[0]
        s[3] ^= s[1]
        s[1] ^= s[2]
        s[0] ^= s[3]
        s[2] ^= t
        s[3] = rotate(s[3], 45)
    return numbers[0] % 1000000000, numbers[1], numbers[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('game', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--recheck', action='store_true')
    args = parser.parse_args()
    build, game, target = (p.resolve() for p in (args.build, args.game, args.output))
    if not args.recheck:
        target.mkdir(parents=True)

    def run(exe, *arguments):
        env = os.environ.copy()
        if 'LLVM_PROFILE_FILE' in env:
            env['LLVM_PROFILE_FILE'] = env['LLVM_PROFILE_FILE'].replace('{program}', exe.stem)
        subprocess.run([str(exe), *map(str, arguments)], cwd=target, env=env, check=True)

    if not args.recheck:
        config = target / 'generator.ini'
        config.write_text('[Generator]\ntarget_chain=3\ntarget_success_count=1\ninitial_seed=191\n'
                          'color_count=4\nbeam_width=24\nrestarts=500\ncandidates_per_parent=4\nthreads=1\n')
        run(build / 'random_19_chain', config)
    for level in (36, 38):
        for ex in (0, 1):
            directory = target / f'lv{level}-ex{ex}'
            if not args.recheck:
                directory.mkdir()
                ledger = directory.with_suffix('.csv')
                ledger.touch()
                seed = 857100 + 10 * level + ex
                width, depth, queues, fixed = 40, 10, 3, 2
                name = f'lv{level}' + ('ex' if ex and level == 38 else '')
                flags = ('--ex-a', '--ex-b') if ex else ()
                run(build / 'ai_match', '--record-round', 0, 0, directory, ledger, 'training',
                    '--max-frames', 1600, '--threads', 1, '--seed', seed, '--a', name, '--b', 'lv37', '--own-tsumo',
                    '--set', f'cancel={ex}', '--set', 'colors=4',
                    '--param', f'beamWidth={width}', '--param', f'beamDepth={depth}',
                    '--param', f'beamQueues={queues}', '--param', f'beamFixedQueues={fixed}',
                    '--param', 'thinkBudget=0', '--param', 'searchThreads=1', *flags)
                round_seed, first, second = agent_seeds(seed)
                tape = (directory / 'inputs.jfreplay').read_text()
                if not any(l.startswith(f'ROUND {round_seed} ') for l in tape.splitlines()):
                    raise ValueError('recorded round uses a different seed stream')
                (directory / 'ai-case.txt').write_text(f'{width} {depth} {queues} {fixed} 1\n'
                                                        f'{level} {ex} {first}\n37 {ex} {second}\n')
            receipt = directory / ('optimized.txt' if args.recheck else 'instrumented.txt')
            run(game, '--check-ai-round', directory, receipt)
            result = receipt.read_text()
            if 'matched every button of both seats' not in result:
                raise ValueError(result)
            print(directory.name, result, flush=True)


if __name__ == '__main__':
    main()
