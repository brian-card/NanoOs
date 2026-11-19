"""Whole-process worst-case stack depth for vi on the SAMD21.

Run measure-ovl.sh first.  Frames come from -fstack-usage (.su) files and the
call graph from the disassembly of the kernel and of each linked overlay.
Each overlay's functions are named <overlay>__<function>, since the same
static inline helper compiles differently in each overlay.  Edges that the
disassembly can't show are added by hand below:  the exec path into vi's
main, each overlay call (callOverlayFunctionFromFile's 56-byte frame under
the callee), the program-side API wrappers into the kernel, and 80 bytes for
the IPC send's tail through function pointers.  Paths that only error
handling or stdin/console I/O reach are cut, since the deep chains read and
write the scratch files.

Usage:  WORK=<dir> [REPO=<repo>] [LAYOUT=nested] vistack.py [root...]
The default root, execOverlayCommand, is the whole process.  LAYOUT=nested
models vi before step 8, when Normal called the other overlays itself.
"""
import glob, os, re, subprocess, sys

WORK = os.environ['WORK']
REPO = os.environ.get('REPO') or os.path.abspath(
    os.path.join(os.path.dirname(__file__), '..'))
OBJDUMP = os.path.expanduser(
    '~/.arduino15/packages/adafruit/tools/arm-none-eabi-gcc/9-2019q4/bin/'
    'arm-none-eabi-objdump')
frames, calls = {}, {}


def add_su(path, prefix=''):
    for line in open(path):
        parts = line.rstrip('\n').split('\t')
        if len(parts) >= 2:
            name = prefix + parts[0].split(':')[-1].split('.')[0]
            frames[name] = max(frames.get(name, 0), int(parts[1]))


def add_dis(text, prefix=''):
    cur = None
    for line in text.splitlines():
        m = re.match(r'^[0-9a-f]+ <([^>]+)>:', line)
        if m:
            cur = prefix + m.group(1).split('.')[0]
            calls.setdefault(cur, set())
        elif cur is not None:
            m = re.search(r'\tbl\s+[0-9a-f]+ <([^>+]+)>', line)
            if m and prefix + m.group(1).split('.')[0] != cur:
                calls[cur].add(prefix + m.group(1).split('.')[0])


for su in glob.glob(os.path.join(REPO, 'build/su/**/*.su'), recursive=True):
    add_su(su)
add_dis(open(os.path.join(WORK, 'kernel.dis')).read())
add_su(os.path.join(WORK, 'start.su'), 'vimain__')
overlays = []
for elf in sorted(glob.glob(os.path.join(REPO, 'userspace/usr/obj/vi/*/overlay.elf'))
                  + glob.glob(os.path.join(REPO, 'userspace/usr/obj/ex/*/overlay.elf'))):
    o = os.path.basename(os.path.dirname(elf))
    o = 'vimain' if o == 'main' else o
    overlays.append(o)
    add_dis(subprocess.run([OBJDUMP, '-d', elf], capture_output=True,
                           text=True).stdout, o + '__')
for su in glob.glob(os.path.join(WORK, 'su', '*.su')):
    o = os.path.basename(su).split('_')[0]
    add_su(su, ('vimain' if o == 'main' else o) + '__')


def edge(a, *bs):
    calls.setdefault(a, set()).update(bs)


def pseudo(name, size):
    frames[name] = size
    calls.setdefault(name, set())


pseudo('ipcHiddenTail', 80)
edge('initSendProcessMessageToProcess', 'ipcHiddenTail')
api = {'fseek': 'filesystemFSeek', 'fwrite': 'filesystemFWrite',
       'fread': 'filesystemFRead', 'fgets': 'nanoOsFgets',
       'vfprintf': 'nanoOsVfprintf', 'printf': 'nanoOsVfprintf',
       'fputs': 'filesystemFWrite', 'malloc': 'memoryManagerMalloc',
       'free': 'memoryManagerFree', 'realloc': 'memoryManagerRealloc',
       'fopen': 'filesystemFopen', 'fclose': 'filesystemFclose',
       'tcsetattr': 'nanoOsTcsetattr', 'tcgetattr': 'nanoOsTcgetattr',
       'remove': 'filesystemRemove', 'rename': 'filesystemRename'}
for o in overlays:
    for wrapper, kernel in api.items():
        if o + '__' + wrapper in calls:
            edge(o + '__' + wrapper, kernel)
edge('execOverlayCommand', 'vimain___start')
edge('vimain___start', 'vimain__main')
steps = ['Motion', 'Operator', 'Change', 'Put', 'Insert', 'Command', 'History',
         'Window', 'Range']
pseudo('callFromMain', 56)
edge('vimain__main', 'callFromMain')
edge('callFromMain', 'Normal__viNormal', 'Session__viStart',
     'Session__viReport', 'Session__viFinish', 'Show__viRun', 'Window__viCommit')
if os.environ.get('LAYOUT') == 'nested':
    names = {'Motion': 'viMotion', 'Operator': 'viOperator',
             'Change': 'viChange', 'Put': 'viPut', 'Insert': 'viInsert',
             'Command': 'viCommand', 'History': 'viUndo', 'Window': 'viRun',
             'Range': 'viRun'}
    pseudo('callFromNormal', 56)
    edge('Normal__viNormal', 'callFromNormal')
    edge('callFromNormal', *[s + '__' + names[s] for s in steps])
    entry = {s: s + '__' + names[s] for s in steps}
else:
    edge('callFromMain', *[s + '__viRun' for s in steps])
    entry = {s: s + '__viRun' for s in steps}
pseudo('callFromCommand', 56)
edge(entry['Command'], 'callFromCommand')
edge('callFromCommand', 'Edit__exEdit', 'Move__exMove', 'File__exFile',
     'Write__exWrite')
pseudo('callFromHistory', 56)
edge(entry['History'], 'callFromHistory')
edge('callFromHistory', 'Undo__exUndo')
pseudo('callFromSession', 56)
edge('Session__viStart', 'callFromSession')
edge('callFromSession', 'File__exStart')
exclude = re.compile(
    r'^(nanoOsFreadStdin|nanoOsFlushAndFreadStdin|nanoOsFwriteConsole|nanoOsWaitForInput|'
    r'callOverlayFunctionFromFile|log|printString|printInt|printHex|'
    r'printDebug|halPanic|nanoOsExitProcess|coroutineAllocateStack)')
memo = {}


def depth(fn, stack):
    if fn in memo:
        return memo[fn]
    best, chain, cut = 0, [], False
    for c in calls.get(fn, ()):
        if exclude.search(c):
            continue
        if c in stack:
            cut = True
            continue
        d, ch, cc = depth(c, stack | {c})
        cut = cut or cc
        if d > best:
            best, chain = d, ch
    result = (frames.get(fn, 0) + best, [fn] + chain, cut)
    if not cut:
        memo[fn] = result
    return result


for spec in os.environ.get('ADJUST', '').split(','):
    if spec:
        suffix, delta = spec.split(':')
        for name in list(frames):
            if name.endswith('__' + suffix) or name == suffix:
                frames[name] += int(delta)
roots = sys.argv[1:] or ['execOverlayCommand']
for root in roots:
    total, chain, _ = depth(root, {root})
    print(f'{root}: {total} bytes')
    if len(roots) == 1:
        for fn in chain:
            print(f'    {frames.get(fn, 0):5d}  {fn}'
                  + ('' if fn in frames else '  (no frame)'))
