#!/usr/bin/env python3
"""Generate FMPy validation data for libcosim's FMI 3 data-type tests.

FMPy is the independent reference implementation. Each case is defined once,
in tests/data/fmi3/validation/cases/<case>.json, and read by both this script
and the C++ harness (tests/fmi3_validation_test.cpp). This script runs the
case through FMPy and writes:

    tests/data/fmi3/validation/results/<case>.csv    FMPy samples (format below)

The harness replays the same case with libcosim, writes the same CSV format
and diffs against results/<case>.csv.

Case file:
    fmu           file name in the FMI 3 Reference-FMUs directory
    start, stop, step, event_mode
    structural    {name: uint64}            set in Configuration Mode before initialization
    start_values  {name: {type, value}}     set in Initialization Mode
    stimuli       [{variable, type, changes: [{time, value}]}]   zero-order hold
    outputs       [{name, type}]            compared columns, in order
    description   free text
  Literals: Int64/UInt64 as strings (JSON numbers are doubles), Float32 as text
  (keeps the float32 literal exact), Binary as lowercase hex, arrays as lists.
  Every declared type is checked against the FMU's modelDescription.xml.

CSV format (derived from fmpy.util.write_csv, extended for String/Binary):
    header            "time","name",...
    Float64 / time    repr(float(x)) (shortest round-trip)
    Float32           str(numpy.float32(x)) of the *float32* value
    integers / Enum   decimal
    Boolean           True / False
    arrays            space-separated %.16g, row-major, in one column
    String            raw text (test strings contain no ',' or newline)
    Binary            lowercase hex, empty -> empty field

Rows: one at t=start after initialization (before the first step), then one
after every communication step at start + k*h.

Alignment rules (observed; see docs/fmi3-validation-plan.md):
  * Inputs are zero-order hold. FMPy interpolates 'continuous' inputs, so each
    change at t_k is emitted as two input rows (t_k, old) (t_k, new).
  * A value applied at t_k takes effect in the step starting at t_k and is
    first visible in the sample at t_k + h.
  * All cases run with Event Mode OFF (use_event_mode=False) on both sides.

Usage:
    gen_validation.py --fmu-dir <dir> [--validation-dir <dir>] [--case T3 ...]
    gen_validation.py --list
"""
from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

import numpy as np

import fmpy
from fmpy import extract, read_model_description, simulate_fmu
from fmpy.fmi3 import FMU3Slave

# ----------------------------------------------------------------------------
# Case model
# ----------------------------------------------------------------------------

NP_DTYPE = {
    'Float32': np.float32, 'Float64': np.float64,
    'Int8': np.int8, 'UInt8': np.uint8, 'Int16': np.int16, 'UInt16': np.uint16,
    'Int32': np.int32, 'UInt32': np.uint32, 'Int64': np.int64, 'UInt64': np.uint64,
    'Boolean': np.bool_, 'Enumeration': np.int64,
}


@dataclass
class Stimulus:
    """Zero-order-hold changes of one input variable: {time: value}."""
    variable: str
    changes: dict[float, Any]


@dataclass
class Case:
    name: str
    fmu: str                                  # file name inside the FMI 3 FMU dir
    step: float
    stop: float
    outputs: list[str]
    start: float = 0.0
    start_values: dict[str, Any] = field(default_factory=dict)
    structural: dict[str, int] = field(default_factory=dict)   # set in Configuration Mode
    stimuli: list[Stimulus] = field(default_factory=list)
    event_mode: bool = False
    types: dict[str, str] = field(default_factory=dict)        # variable -> FMI type, as declared in the case


def from_json_literal(value: Any, vtype: str) -> Any:
    """Decodes a case-file literal. Int64/UInt64 are strings (JSON numbers are doubles),
    Float32 is text (to keep the float32 literal exact), Binary is hex."""
    if isinstance(value, list):
        return np.array([from_json_literal(v, vtype) for v in value], dtype=NP_DTYPE.get(vtype))
    if vtype == 'Binary':
        return bytes.fromhex(value)
    if vtype == 'String':
        return str(value)
    if vtype == 'Boolean':
        return bool(value)
    if vtype == 'Float32':
        return np.float32(value)
    if vtype == 'Float64':
        return float(value)
    return int(value)


def load_case(path: Path) -> Case:
    doc = json.loads(path.read_text(encoding='utf-8'))
    case = Case(
        name=path.stem,
        fmu=doc['fmu'],
        start=float(doc.get('start', 0.0)),
        stop=float(doc['stop']),
        step=float(doc['step']),
        event_mode=bool(doc.get('event_mode', False)),
        outputs=[o['name'] for o in doc['outputs']],
        structural={k: int(v) for k, v in doc.get('structural', {}).items()},
    )
    for o in doc['outputs']:
        case.types[o['name']] = o['type']
    for name, entry in doc.get('start_values', {}).items():
        case.types[name] = entry['type']
        case.start_values[name] = from_json_literal(entry['value'], entry['type'])
    for s in doc.get('stimuli', []):
        case.types[s['variable']] = s['type']
        case.stimuli.append(Stimulus(
            s['variable'],
            {float(ch['time']): from_json_literal(ch['value'], s['type']) for ch in s['changes']}))
    return case


def check_against_model(case: Case, md) -> None:
    """The case file declares each variable's FMI type; it must match the FMU."""
    actual = {v.name: v.type for v in md.modelVariables}
    for name, declared in case.types.items():
        if name not in actual:
            raise RuntimeError(f'{case.name}: {case.fmu} has no variable {name!r}')
        if actual[name] != declared:
            raise RuntimeError(f'{case.name}: {name} is {actual[name]} in {case.fmu}, case says {declared}')
    for name in case.structural:
        if actual.get(name) != 'UInt64':
            raise RuntimeError(f'{case.name}: structural parameter {name} must be UInt64')


def needs_loop(case: Case, md) -> bool:
    """simulate_fmu cannot record String/Binary, enter Configuration Mode, or take array inputs."""
    if case.structural or case.event_mode:
        return True
    if any(case.types[n] in ('String', 'Binary') for n in case.types):
        return True
    dims = {v.name: bool(v.dimensions) for v in md.modelVariables}
    return any(dims.get(s.variable, False) for s in case.stimuli)


# ----------------------------------------------------------------------------
# Literal formatting (shared contract with the C++ side)
# ----------------------------------------------------------------------------

def fmt(value: Any, vtype: str) -> str:
    if vtype == 'Binary':
        return bytes(value).hex()
    if vtype == 'String':
        return value.decode() if isinstance(value, bytes) else str(value)
    if vtype == 'Boolean':
        return 'True' if bool(value) else 'False'
    if isinstance(value, np.ndarray) and value.ndim > 0:
        return ' '.join(f'{v:.16g}' for v in value.flatten())
    if vtype == 'Float32':
        return str(np.float32(value))        # shortest round-trip repr of the float32 value
    if vtype == 'Float64':
        return repr(float(value))
    return str(int(value))


# ----------------------------------------------------------------------------
# Back-end 1: fmpy.simulate_fmu (fast path; no String/Binary, no structural params)
# ----------------------------------------------------------------------------

def build_input(case: Case, md) -> np.ndarray | None:
    if not case.stimuli:
        return None
    types = {v.name: v.type for v in md.modelVariables}
    starts = {v.name: v.start for v in md.modelVariables}
    dtype = [('time', np.float64)] + [(s.variable, NP_DTYPE[types[s.variable]]) for s in case.stimuli]
    times = sorted({t for s in case.stimuli for t in s.changes} | {case.start})
    current = {}
    for s in case.stimuli:
        if case.start in s.changes:
            current[s.variable] = s.changes[case.start]
        elif s.variable in case.start_values:
            current[s.variable] = case.start_values[s.variable]
        else:
            current[s.variable] = NP_DTYPE[types[s.variable]](starts[s.variable])
    rows = []
    for t in times:
        if t != case.start:
            rows.append((t, *[current[s.variable] for s in case.stimuli]))      # (t, old)
        for s in case.stimuli:
            if t in s.changes:
                current[s.variable] = s.changes[t]
        rows.append((t, *[current[s.variable] for s in case.stimuli]))          # (t, new)
    return np.array(rows, dtype=dtype)


def run_simulate_fmu(case: Case, fmu_path: Path, md) -> list[tuple[float, dict[str, Any]]]:
    kwargs = dict(
        start_time=case.start, stop_time=case.stop,
        step_size=case.step, output_interval=case.step,
        start_values=case.start_values,
        input=build_input(case, md),
        output=case.outputs,
    )
    if md.fmiVersion == '3.0':
        kwargs.update(use_event_mode=case.event_mode, early_return_allowed=False)
    result = simulate_fmu(str(fmu_path), **kwargs)
    missing = [o for o in case.outputs if o not in result.dtype.names]
    if missing:
        raise RuntimeError(f'{case.name}: simulate_fmu did not record {missing}')
    return [(float(r['time']), {o: r[o] for o in case.outputs}) for r in result]


# ----------------------------------------------------------------------------
# Back-end 2: explicit FMI 3 CS loop (String, Binary, structural parameters)
# ----------------------------------------------------------------------------

def run_loop(case: Case, fmu_path: Path, md) -> list[tuple[float, dict[str, Any]]]:
    if md.fmiVersion != '3.0':
        raise RuntimeError('explicit loop back-end supports FMI 3 only')
    vars_ = {v.name: v for v in md.modelVariables}
    by_vr = {v.valueReference: v for v in md.modelVariables}

    def shape_size(v) -> int:
        n = 1
        for d in v.dimensions or []:
            if d.start is not None:
                n *= int(d.start)
            else:
                ref = by_vr[int(d.valueReference)]
                n *= int(case.structural.get(ref.name, ref.start))
        return n

    def api(prefix: str, v):
        t = 'Int64' if v.type == 'Enumeration' else v.type
        return getattr(fmu, prefix + t)

    def set_value(name: str, value: Any) -> None:
        v = vars_[name]
        if v.type in ('String', 'Binary'):
            api('set', v)([v.valueReference], [value])
        elif isinstance(value, (list, tuple, np.ndarray)):
            api('set', v)([v.valueReference], [x.item() if hasattr(x, 'item') else x for x in np.asarray(value).flatten()])
        else:
            api('set', v)([v.valueReference], [value.item() if hasattr(value, 'item') else value])

    def get_value(name: str) -> Any:
        v = vars_[name]
        n = shape_size(v)
        vals = api('get', v)([v.valueReference], nValues=n)
        if v.type in ('String', 'Binary'):
            return vals[0]
        if v.dimensions:
            return np.asarray(vals, dtype=NP_DTYPE[v.type])
        return NP_DTYPE[v.type](vals[0])

    def settle_events() -> bool:
        while True:
            discrete, terminate, *_ = fmu.updateDiscreteStates()
            if terminate:
                return True
            if not discrete:
                return False

    unzipdir = extract(str(fmu_path))
    fmu = FMU3Slave(guid=md.guid, unzipDirectory=unzipdir,
                    modelIdentifier=md.coSimulation.modelIdentifier, instanceName=case.name)
    fmu.instantiate(eventModeUsed=case.event_mode, earlyReturnAllowed=False)
    try:
        if case.structural:
            fmu.enterConfigurationMode()
            for name, value in case.structural.items():
                set_value(name, value)
            fmu.exitConfigurationMode()
        fmu.enterInitializationMode(startTime=case.start, stopTime=case.stop)
        for name, value in case.start_values.items():
            set_value(name, value)
        for s in case.stimuli:
            if case.start in s.changes:
                set_value(s.variable, s.changes[case.start])
        fmu.exitInitializationMode()
        if case.event_mode:
            if settle_events():
                raise RuntimeError(f'{case.name}: FMU requested termination during initialization')
            fmu.enterStepMode()

        samples = [(case.start, {o: get_value(o) for o in case.outputs})]
        n_steps = int(round((case.stop - case.start) / case.step))
        for k in range(n_steps):
            t = case.start + k * case.step
            for s in case.stimuli:
                for tc, val in s.changes.items():
                    if tc != case.start and abs(t - tc) < 1e-12:
                        set_value(s.variable, val)
            event_needed, terminate, early_return, _ = fmu.doStep(
                currentCommunicationPoint=t, communicationStepSize=case.step)
            if early_return:
                raise RuntimeError(f'{case.name}: unexpected early return at t={t}')
            if event_needed and case.event_mode:
                fmu.enterEventMode()
                if settle_events():
                    terminate = True
                fmu.enterStepMode()
            # The step completed, so its end-point values are valid; record them
            # before honouring a termination request (same as fmpy.simulateCS).
            samples.append((case.start + (k + 1) * case.step, {o: get_value(o) for o in case.outputs}))
            if terminate:
                break
        fmu.terminate()
    finally:
        fmu.freeInstance()
    return samples


# ----------------------------------------------------------------------------
# Writers
# ----------------------------------------------------------------------------

def write_validation(path: Path, samples, outputs: list[str], md) -> None:
    types = {v.name: v.type for v in md.modelVariables}
    with path.open('w', newline='\n', encoding='utf-8') as f:
        f.write(','.join(f'"{n}"' for n in ['time', *outputs]) + '\n')
        for t, values in samples:
            cells = [repr(float(t))] + [fmt(values[o], types[o]) for o in outputs]
            f.write(','.join(cells) + '\n')


# ----------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--fmu-dir', type=Path, help='directory with FMI 3 Reference FMUs')
    ap.add_argument('--validation-dir', type=Path,
                    default=Path(__file__).resolve().parents[1] / 'data' / 'fmi3' / 'validation',
                    help='directory holding cases/ (input) and results/ (output)')
    ap.add_argument('--case', action='append', help='case name prefix; default all')
    ap.add_argument('--list', action='store_true')
    args = ap.parse_args(argv)

    case_files = sorted((args.validation_dir / 'cases').glob('*.json'))
    if args.case:
        case_files = [p for p in case_files if any(p.stem.startswith(prefix) for prefix in args.case)]
        if not case_files:
            print('no matching case', file=sys.stderr)
            return 2
    cases = [load_case(p) for p in case_files]
    if args.list:
        for c in cases:
            print(f'{c.name:<28} {c.fmu:<18} h={c.step} T={c.stop} event_mode={c.event_mode}')
        return 0
    if args.fmu_dir is None:
        ap.error('--fmu-dir is required')

    results_dir = args.validation_dir / 'results'
    results_dir.mkdir(parents=True, exist_ok=True)

    print(f'fmpy {fmpy.__version__}')
    failures = 0
    for case in cases:
        fmu_path = args.fmu_dir / case.fmu
        md = read_model_description(str(fmu_path))
        try:
            check_against_model(case, md)
            backend: Callable = run_loop if needs_loop(case, md) else run_simulate_fmu
            samples = backend(case, fmu_path, md)
        except Exception as exc:  # report and continue with the other cases
            failures += 1
            print(f'{case.name:<28} FAILED: {exc}', file=sys.stderr)
            continue
        write_validation(results_dir / f'{case.name}.csv', samples, case.outputs, md)
        print(f'{case.name:<28} {len(samples):>5} rows  ({backend.__name__})')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
