"""Write the CMake presets: indra/CMakePresets.json and the files beside
this script that it includes.

base.json holds the hidden bases every preset is made of. Each generator
file includes it and holds that generator's configure presets, a build
preset for each of them per configuration, and a configure-and-build
workflow preset for the generator, its proprietary preset and the fullopt
presets. The root includes the generator files. A preset may inherit only
from its own file or one it includes, which is why the bases are not in
the root. Run it after editing the tables below; it is not part of the
build.

    python indra/cmake/presets/generate.py
"""
import json
import os

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
HERE = os.path.dirname(os.path.abspath(__file__))

VERSION = 10
SCHEMA = 'https://cmake.org/cmake/help/latest/_downloads/3e2d73bff478d88a7de0de736ba5e361/schema.json'

CONFIGS = {'debug': 'Debug', 'optdebug': 'OptDebug', 'relwithdebinfo': 'RelWithDebInfo', 'release': 'Release'}
ARCHS = {'arm64': 'arm64', 'x64': 'x86_64'}

# The optimizations of a shipped build, on any channel. Tracy and Release
# debug logging are options only on the Test channel, off elsewhere, so
# the preset's values are read exactly where they matter.
FULLOPT = {'AL_USE_LTO': True, 'AL_USE_TRACY': False, 'AL_ENABLE_RELEASE_DEBUG_LOGGING': False}


def host(system):
    return {'type': 'equals', 'lhs': '${hostSystemName}', 'rhs': system}


bases = [
    {'name': 'base', 'hidden': True,
     'binaryDir': '${sourceParentDir}/build-${hostSystemName}-${presetName}'},
    {'name': 'windows', 'hidden': True, 'condition': host('Windows')},
    {'name': 'macos', 'hidden': True, 'condition': host('Darwin')},
    {'name': 'macos-arm64', 'hidden': True, 'cacheVariables': {'CMAKE_OSX_ARCHITECTURES': 'arm64'}},
    {'name': 'macos-x64', 'hidden': True, 'cacheVariables': {'CMAKE_OSX_ARCHITECTURES': 'x86_64'}},
    {'name': 'proprietary', 'hidden': True, 'cacheVariables': {'AL_ENABLE_PROPRIETARY': True}},
    {'name': 'lld', 'hidden': True, 'cacheVariables': {'CMAKE_LINKER_TYPE': 'LLD'}},
    {'name': 'mold', 'hidden': True, 'cacheVariables': {'CMAKE_LINKER_TYPE': 'MOLD'}},
    {'name': 'ccache', 'hidden': True,
     'cacheVariables': {'CMAKE_C_COMPILER_LAUNCHER': 'ccache', 'CMAKE_CXX_COMPILER_LAUNCHER': 'ccache'}},
    {'name': 'sccache', 'hidden': True,
     'cacheVariables': {'CMAKE_C_COMPILER_LAUNCHER': 'sccache', 'CMAKE_CXX_COMPILER_LAUNCHER': 'sccache'}},
]
bases.append({'name': 'fullopt', 'hidden': True, 'cacheVariables': FULLOPT})

build_bases = [
    {'name': 'windows', 'hidden': True, 'condition': host('Windows')},
    {'name': 'macos', 'hidden': True, 'condition': host('Darwin')},
]
for short, name in CONFIGS.items():
    build_bases.append({'name': short, 'hidden': True, 'configuration': name})


def fullopt(generator, display, arch=False, mac_base=False, default_config=None):
    """The fullopt presets of one generator: the open-source and the
    proprietary preset plus fullopt, and the two macOS architectures
    where the generator has them."""
    out = []
    for parent, label in [(f'{generator}-os', display), (generator, f'{display} Proprietary')]:
        p = {'name': f'{parent}-fullopt', 'displayName': f'{label} (fully optimized)',
             'description': 'The optimizations of a shipped build on any channel: '
                            'LTO on, Tracy and Release debug logging off',
             'inherits': [parent, 'fullopt']}
        if default_config:
            p['cacheVariables'] = {'CMAKE_DEFAULT_BUILD_TYPE': default_config}
        out.append(p)
        if arch:
            for arch_name, arch_label in ARCHS.items():
                inherits = [f'{parent}-fullopt']
                if mac_base:
                    inherits.append('macos')
                inherits.append(f'macos-{arch_name}')
                out.append({'name': f'{parent}-fullopt-{arch_name}',
                            'displayName': f'{label} (fully optimized, {arch_label})',
                            'inherits': inherits})
    return out


# Per generator: the configure presets, and the hidden platform base each
# one's build presets inherit (None for a preset that runs anywhere).
GENERATORS = {
    'vs2026': ('windows', [
        {'name': 'vs2026-os', 'displayName': 'Visual Studio 2026',
         'inherits': ['base', 'windows'], 'generator': 'Visual Studio 18 2026'},
        {'name': 'vs2026', 'displayName': 'Visual Studio 2026 Proprietary',
         'inherits': ['vs2026-os', 'proprietary']},
        {'name': 'vs2026-os-sdl', 'displayName': 'Visual Studio 2026 (SDL window)',
         'inherits': ['vs2026-os'], 'cacheVariables': {'AL_USE_SDL_WINDOW': True}},
        *fullopt('vs2026', 'Visual Studio 2026'),
    ]),
    'ninja': (None, [
        {'name': 'ninja-os', 'displayName': 'Ninja Multi-Config',
         'inherits': ['base'], 'generator': 'Ninja Multi-Config',
         'cacheVariables': {'CMAKE_DEFAULT_BUILD_TYPE': 'RelWithDebInfo'}},
        {'name': 'ninja', 'displayName': 'Ninja Multi-Config Proprietary',
         'inherits': ['ninja-os', 'proprietary']},
        {'name': 'ninja-os-arm64', 'displayName': 'Ninja Multi-Config (arm64)',
         'inherits': ['ninja-os', 'macos', 'macos-arm64']},
        {'name': 'ninja-os-x64', 'displayName': 'Ninja Multi-Config (x86_64)',
         'inherits': ['ninja-os', 'macos', 'macos-x64']},
        {'name': 'ninja-arm64', 'displayName': 'Ninja Multi-Config Proprietary (arm64)',
         'inherits': ['ninja', 'macos', 'macos-arm64']},
        {'name': 'ninja-x64', 'displayName': 'Ninja Multi-Config Proprietary (x86_64)',
         'inherits': ['ninja', 'macos', 'macos-x64']},
        *fullopt('ninja', 'Ninja Multi-Config', arch=True, mac_base=True, default_config='Release'),
    ]),
    'xcode': ('macos', [
        {'name': 'xcode-os', 'displayName': 'Xcode',
         'inherits': ['base', 'macos'], 'generator': 'Xcode'},
        {'name': 'xcode', 'displayName': 'Xcode Proprietary',
         'inherits': ['xcode-os', 'proprietary']},
        {'name': 'xcode-os-arm64', 'displayName': 'Xcode (arm64)',
         'inherits': ['xcode-os', 'macos-arm64']},
        {'name': 'xcode-os-x64', 'displayName': 'Xcode (x86_64)',
         'inherits': ['xcode-os', 'macos-x64']},
        {'name': 'xcode-arm64', 'displayName': 'Xcode Proprietary (arm64)',
         'inherits': ['xcode', 'macos-arm64']},
        {'name': 'xcode-x64', 'displayName': 'Xcode Proprietary (x86_64)',
         'inherits': ['xcode', 'macos-x64']},
        *fullopt('xcode', 'Xcode', arch=True),
    ]),
}


def platform_of(preset, default):
    """The Ninja presets that name a macOS architecture run only there."""
    if 'macos' in preset.get('inherits', []):
        return 'macos'
    return default


def has_workflow(generator, name):
    """The generator, its proprietary preset and the fullopt presets."""
    return name in (f'{generator}-os', generator) or name.endswith('-fullopt')


def write(path, doc):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(json.dumps(doc, indent=2) + '\n')


counts = {}
for generator, (platform, configure) in GENERATORS.items():
    build = []
    workflow = []
    for preset in configure:
        name = preset['name']
        for short in CONFIGS:
            inherits = [short]
            base = platform_of(preset, platform)
            if base:
                inherits.insert(0, base)
            build.append({'name': f'{name}-{short}', 'configurePreset': name, 'inherits': inherits})
        if has_workflow(generator, name):
            workflow.append({'name': f'{name}-release',
                             'steps': [{'type': 'configure', 'name': name},
                                       {'type': 'build', 'name': f'{name}-release'}]})
    write(os.path.join(HERE, f'{generator}.json'),
          {'version': VERSION, '$schema': SCHEMA, 'include': ['base.json'],
           'configurePresets': configure, 'buildPresets': build, 'workflowPresets': workflow})
    counts[generator] = (len(configure), len(build), len(workflow))

write(os.path.join(HERE, 'base.json'),
      {'version': VERSION, '$schema': SCHEMA,
       'configurePresets': bases, 'buildPresets': build_bases})
write(os.path.join(ROOT, 'CMakePresets.json'),
      {'version': VERSION, '$schema': SCHEMA,
       'include': [f'cmake/presets/{generator}.json' for generator in GENERATORS]})

for generator, (c, b, w) in counts.items():
    print(f'{generator}: {c} configure, {b} build, {w} workflow')
