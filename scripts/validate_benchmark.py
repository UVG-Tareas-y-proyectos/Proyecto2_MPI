"""Comprueba la integridad de la bitácora pública de distribuciones."""
from pathlib import Path
import csv
import math
import statistics


ROOT = Path(__file__).resolve().parents[1]
DETAIL = ROOT / 'evidence' / 'distribution_benchmark.csv'
SUMMARY = ROOT / 'evidence' / 'distribution_summary.csv'

with DETAIL.open(newline='', encoding='utf-8') as stream:
    rows = list(csv.DictReader(stream))
with SUMMARY.open(newline='', encoding='utf-8') as stream:
    summaries = list(csv.DictReader(stream))

cases = {'facil', 'media', 'dificil'}
configurations = {('secuencial', 'none', 1)} | {
    ('mpi', distribution, processes)
    for distribution in ('block', 'cyclic')
    for processes in (2, 4, 6)
}
assert len(rows) == 210
assert len(summaries) == 21
assert {row['caso'] for row in rows} == cases
assert all(row['correcto'] == 'true' for row in rows)
assert all(float(row['segundos']) > 0 for row in rows)
assert len({row['commit_codigo'] for row in rows}) == 1

for case in cases:
    for algorithm, distribution, processes in configurations:
        group = [row for row in rows if row['caso'] == case
                 and row['algoritmo'] == algorithm
                 and row['distribucion'] == distribution
                 and int(row['procesos']) == processes]
        assert len(group) == 10
        assert sorted(int(row['repeticion']) for row in group) == list(range(1, 11))
        summary = next(row for row in summaries if row['caso'] == case
                       and row['algoritmo'] == algorithm
                       and row['distribucion'] == distribution
                       and int(row['procesos']) == processes)
        mean = statistics.mean(float(row['segundos']) for row in group)
        assert math.isclose(mean, float(summary['promedio_segundos']), abs_tol=5e-10)
        sequential = next(row for row in summaries if row['caso'] == case
                          and row['algoritmo'] == 'secuencial')
        expected_speedup = float(sequential['promedio_segundos']) / mean
        assert math.isclose(expected_speedup, float(summary['speedup']), abs_tol=1e-5)
        assert math.isclose(expected_speedup / processes,
                            float(summary['eficiencia']), abs_tol=1e-5)

print('Bitácora verificada: 210 mediciones correctas y 21 resúmenes consistentes.')
