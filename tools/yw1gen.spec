# PyInstaller spec: the game code generator (tools/gen.py and its modules) as a standalone onedir helper, so the
# start screen's setup can build the game code on the player's machine without Python (runtime/gamebuild.c).
# Frozen output: yw1gen/ (yw1gen.exe + _internal/), shipped under helper/yw1gen/ next to YokaiWatch1Recomp.exe.
import glob
import importlib.util
import os
import sys

from PyInstaller.utils.hooks import collect_dynamic_libs, collect_submodules

TOOLS = os.path.normpath(SPECPATH)
_venv_sp = os.path.join(TOOLS, 'venv', 'Lib', 'site-packages')
if os.path.isdir(_venv_sp) and _venv_sp not in sys.path:
    sys.path.insert(0, _venv_sp)

# collect_dynamic_libs only picks unversioned names (libcapstone.so, ...): unicorn's library is versioned
# (libunicorn.so.2 on Linux, libunicorn.2.dylib on macOS, unicorn.dll on Windows), so add it by hand where
# importlib.resources files('unicorn')/'lib' looks for it.
_uc_lib_dir = os.path.join(os.path.dirname(importlib.util.find_spec('unicorn').origin), 'lib')
_uc_versioned = (glob.glob(os.path.join(_uc_lib_dir, 'libunicorn.so*'))
                 + glob.glob(os.path.join(_uc_lib_dir, 'libunicorn.*.dylib'))
                 + glob.glob(os.path.join(_uc_lib_dir, 'unicorn.dll')))

a = Analysis(
    [os.path.join(TOOLS, 'gen.py')],
    pathex=[TOOLS, _venv_sp] if os.path.isdir(_venv_sp) else [TOOLS],
    binaries=collect_dynamic_libs('capstone') + collect_dynamic_libs('unicorn')
             + [(p, 'unicorn/lib') for p in _uc_versioned],
    datas=[(os.path.join(TOOLS, '..', 'runtime', 'game_addresses', 'versions.h'), 'runtime/game_addresses')],
    hiddenimports=['game_addresses'] + collect_submodules('unicorn'),
    excludes=['tkinter', 'pydoc', 'doctest', 'unittest', 'pdb', 'IPython', 'numpy', 'setuptools', '_distutils_hack',
              'pkg_resources', 'ssl', '_ssl', '_hashlib', '_multibytecodec', '_codecs_cn', '_codecs_hk',
              '_codecs_iso2022', '_codecs_jp', '_codecs_kr', '_codecs_tw', '_decimal'],
    noarchive=False,
)
pyz = PYZ(a.pure)
exe = EXE(
    pyz,
    a.scripts,
    [('hash_seed=0', None, 'OPTION')],
    exclude_binaries=True,
    name='yw1gen',
    console=True,  # stderr = progress lines read by runtime/gamebuild.c (launched with CREATE_NO_WINDOW)
    strip=sys.platform == 'darwin',
    upx=False,
)
coll = COLLECT(exe, a.binaries, a.datas, strip=sys.platform == 'darwin', upx=False, name='yw1gen')
