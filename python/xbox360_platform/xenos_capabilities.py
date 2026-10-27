"""Shared compiler definitions for sources/xenos/core.cpp.

Release builds enable every validated renderer saving and compile out counters
and trials. SHIPYARD_XENOS_DIAGNOSTICS=1 selects an instrumented build whose
native counters and detailed timing feed consumer profile logs.
"""

import os


def compiler_options(abi, shader_pack=False, diagnostics=None):
    if diagnostics is None:
        diagnostics = os.environ.get('SHIPYARD_XENOS_DIAGNOSTICS', '0')
    if diagnostics not in ('0', '1'):
        raise ValueError('SHIPYARD_XENOS_DIAGNOSTICS must be 0 or 1')
    if abi not in (2, 3):
        raise ValueError('Unsupported Xenos shader ABI')
    capabilities = {
        'SHADER_ABI': str(abi),
        'SHADER_PACK': '1' if shader_pack else '0',
        'DEFERRED_FRAMEBUFFERS': '1',
        'BINDING_REUSE': '1',
        'TILING_BUDGET': '1',
        'TILING_TRIALS': '0',
        'DIAGNOSTICS': diagnostics,
        'DETAILED_TIMING': diagnostics,
        'ALPHA_HISTOGRAM': diagnostics,
    }
    return ['/DSHIPYARD_XENOS_' + name + '=' + value for name, value in capabilities.items()]
