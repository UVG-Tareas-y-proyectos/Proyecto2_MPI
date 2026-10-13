"""Mide reparto por bloques y cíclico con tres posiciones de clave controladas."""
from __future__ import annotations

from fractions import Fraction
from pathlib import Path
import argparse
import csv
import math
import os
import re
import statistics
import subprocess


ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)


def parse_processes(value: str) -> list[int]:
    try:
        result = [int(item) for item in value.split(',')]
    except ValueError as exc:
        raise argparse.ArgumentTypeError('Use enteros separados por comas') from exc
    if not result or any(item < 2 for item in result) or len(set(result)) != len(result):
        raise argparse.ArgumentTypeError('Los procesos deben ser únicos y mayores o iguales que 2')
    return result


parser = argparse.ArgumentParser()
parser.add_argument('--keyspace', type=int, default=1 << 20)
parser.add_argument('--repetitions', type=int, default=10)
parser.add_argument('--processes', type=parse_processes, default=parse_processes('2,4,6'))
parser.add_argument('--output', default='evidence/distribution_benchmark.csv')
parser.add_argument('--summary', default='evidence/distribution_summary.csv')
args = parser.parse_args()
if args.keyspace < 64 or args.keyspace > 1 << 56:
    parser.error('--keyspace debe estar entre 64 y 2^56')
if args.repetitions < 1:
    parser.error('--repetitions debe ser positivo')

commit = subprocess.check_output(['git', '-c', 'safe.directory=/workspace',
                                  'rev-parse', 'HEAD'], text=True).strip()
case_dir = ROOT / 'private' / 'distribution_benchmark'
case_dir.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, OMPI_ALLOW_RUN_AS_ROOT='1', OMPI_ALLOW_RUN_AS_ROOT_CONFIRM='1')

positions = {
    'facil': (args.keyspace // 2) + 1,
    'media': (args.keyspace // 2) + (args.keyspace // 8),
    'dificil': math.ceil(args.keyspace * (Fraction(1, 7) + Fraction(1, 13))),
}
if any(key >= args.keyspace for key in positions.values()):
    parser.error('El espacio escalado no admite las posiciones solicitadas')


def run(command: list[str], expected: int = 0) -> str:
    result = subprocess.run(command, capture_output=True, text=True, env=env,
                            timeout=180)
    if result.returncode != expected:
        raise RuntimeError(
            f"Falló ({result.returncode}) {' '.join(command)}\n"
            f"{result.stdout}{result.stderr}"
        )
    return result.stdout


def fields(output: str) -> dict[str, str]:
    return dict(re.findall(r'^([a-z_]+)=(.*)$', output, flags=re.MULTILINE))


rows: list[dict[str, object]] = []
for case_name, key_index in positions.items():
    keyword = f'PISTA_DISTRIBUCION_{case_name.upper()}_MPI'
    original = case_dir / f'{case_name}.txt'
    key_file = case_dir / f'{case_name}.key'
    cipher = case_dir / f'{case_name}.bin'
    recovered = case_dir / f'{case_name}.recovered'
    original.write_text(
        f'Caso controlado {case_name} para comparar repartos MPI. {keyword}.\n',
        encoding='utf-8',
    )
    key_file.write_text(f'{key_index}\n', encoding='ascii')
    run(['./build/des_tool', 'encrypt', '--input', str(original), '--output',
         str(cipher), '--key-file', str(key_file)])

    configurations: list[tuple[str, str, int, list[str]]] = [
        ('secuencial', 'none', 1, ['./build/des_tool', 'search'])
    ]
    for processes in args.processes:
        for distribution in ('block', 'cyclic'):
            configurations.append(
                ('mpi', distribution, processes,
                 ['mpirun', '--oversubscribe', '-np', str(processes),
                  './build/bruteforce_mpi'])
            )

    for algorithm, distribution, processes, prefix in configurations:
        command = prefix + [
            '--input', str(cipher), '--keyword', keyword,
            '--start', '0', '--end', str(args.keyspace),
            '--output', str(recovered),
        ]
        if algorithm == 'mpi':
            command += ['--distribution', distribution]
        warmup = fields(run(command))
        if warmup.get('status') != 'found' or recovered.read_bytes() != original.read_bytes():
            raise RuntimeError(f'Calentamiento incorrecto en {case_name}/{distribution}/{processes}')
        for repetition in range(1, args.repetitions + 1):
            measured = fields(run(command))
            if measured.get('status') != 'found' or recovered.read_bytes() != original.read_bytes():
                raise RuntimeError(f'Recuperación incorrecta en {case_name}/{distribution}/{processes}')
            rows.append({
                'caso': case_name,
                'fraccion_posicion': f'{key_index / args.keyspace:.9f}',
                'espacio': args.keyspace,
                'algoritmo': algorithm,
                'distribucion': distribution,
                'procesos': processes,
                'repeticion': repetition,
                'segundos': measured['seconds'],
                'intentos': measured['attempts'],
                'correcto': 'true',
                'commit_codigo': commit,
            })
            print(f'{case_name} {distribution} p={processes} r={repetition} '
                  f's={measured["seconds"]}', flush=True)

output = ROOT / args.output
output.parent.mkdir(parents=True, exist_ok=True)
fieldnames = list(rows[0])
with output.open('w', newline='', encoding='utf-8') as stream:
    writer = csv.DictWriter(stream, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)

summary_rows: list[dict[str, object]] = []
for case_name in positions:
    sequential = [float(row['segundos']) for row in rows
                  if row['caso'] == case_name and row['algoritmo'] == 'secuencial']
    sequential_mean = statistics.mean(sequential)
    groups = sorted({(str(row['algoritmo']), str(row['distribucion']), int(row['procesos']))
                     for row in rows if row['caso'] == case_name})
    for algorithm, distribution, processes in groups:
        samples = [float(row['segundos']) for row in rows
                   if row['caso'] == case_name and row['algoritmo'] == algorithm
                   and row['distribucion'] == distribution
                   and int(row['procesos']) == processes]
        mean = statistics.mean(samples)
        speedup = sequential_mean / mean
        summary_rows.append({
            'caso': case_name,
            'fraccion_posicion': f'{positions[case_name] / args.keyspace:.9f}',
            'algoritmo': algorithm,
            'distribucion': distribution,
            'procesos': processes,
            'repeticiones': len(samples),
            'promedio_segundos': f'{mean:.9f}',
            'speedup': f'{speedup:.6f}',
            'eficiencia': f'{speedup / processes:.6f}',
        })

summary = ROOT / args.summary
with summary.open('w', newline='', encoding='utf-8') as stream:
    writer = csv.DictWriter(stream, fieldnames=list(summary_rows[0]))
    writer.writeheader()
    writer.writerows(summary_rows)

print(f'Benchmark completo: {len(rows)} mediciones en {output}')
print(f'Resumen: {summary}')
