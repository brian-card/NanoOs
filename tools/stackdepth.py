"""Static stack depth of every kernel function that sends a message.

Frames come from the -fstack-usage (.su) files of a SAMD21 build and the call
graph from that build's disassembly, so both are re-derived from the binary
on every run.  The roots are re-derived from the source on every run too:
every function whose body calls one of the message-send entry points below.

For each root this prints its own frame, the deepest path below it, the
number of reachable functions that make an indirect call (blx), whose
callees the disassembly can't show, and the path itself.  Paths into the
functions matched by CUT (logging and immediate prints) are not followed,
since every sender reaches them the same way and they would hide the
differences this measures.

Usage:  stackdepth.py <su-dir> <disassembly> <src-dir>
"""
import glob, os, re, sys

SEND_ENTRY_POINTS = (
    'initSendProcessMessageToPid', 'initSendProcessMessageToProcess',
    'processMessageInit', 'processMessageInitInline', 'msg_init',
    'msg_init_inline', 'sendProcessMessageToPid', 'sendProcessMessageToProcess',
    'schedulerInitSendMessageToPid', 'schedulerInitSendMessageToProcess',
    'schedulerSendProcessMessageToPid', 'schedulerSendProcessMessageToProcess',
)
CUT = re.compile(r'^(log[A-Z]\w*|logMessage|print(String|Int|Hex)\w*|'
                 r'coroutineAllocateStack\w*)$')
FUNC_START = re.compile(r'^(?!typedef|return|else|if|for|while|switch)'
                        r'[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*\(')
SEND_CALL = re.compile(r'\b(' + '|'.join(SEND_ENTRY_POINTS) + r')\s*\(')


def base(name):
    """Strip gcc's clone suffixes (.constprop.0, .isra.0, .part.0)."""
    return name.split('.')[0]


def load_frames(su_dir):
    frames = {}
    for path in glob.glob(os.path.join(su_dir, '**', '*.su'), recursive=True):
        for line in open(path):
            parts = line.rstrip('\n').split('\t')
            if len(parts) >= 2:
                name = base(parts[0].split(':')[-1])
                frames[name] = max(frames.get(name, 0), int(parts[1]))
    return frames


def load_calls(dis_path):
    calls, indirect = {}, set()
    cur = None
    for line in open(dis_path):
        m = re.match(r'^[0-9a-f]+ <([^>]+)>:', line)
        if m:
            cur = base(m.group(1))
            calls.setdefault(cur, set())
            continue
        if cur is None:
            continue
        m = re.search(r'\t(bl|b|b\.n|b\.w)\s+[0-9a-f]+ <([^>+]+)>', line)
        if m and base(m.group(2)) != cur:
            calls[cur].add(base(m.group(2)))
        elif re.search(r'\tblx\s+r\d+', line):
            indirect.add(cur)
    return calls, indirect


def scan_source(src_dir):
    """Map each function defined under src_dir to the names its body calls."""
    bodies = {}
    for path in glob.glob(os.path.join(src_dir, '**', '*.c'), recursive=True):
        current = None
        depth = 0
        for line in open(path, errors='replace'):
            line = re.sub(r'__attribute__\s*\(\(.*?\)\)', '', line)
            line = re.sub(r'"(\\.|[^"\\])*"|\'(\\.|[^\'\\])*\'', '""', line)
            line = re.sub(r'//.*', '', line)
            if depth == 0:
                m = FUNC_START.match(line)
                if m and not line.rstrip().endswith(';'):
                    current = m.group(1)
            depth += line.count('{') - line.count('}')
            if depth > 0 and current:
                bodies.setdefault(current, set()).update(
                    re.findall(r'\b([A-Za-z_]\w*)\s*\(', line))
            if depth <= 0:
                depth = 0
    return bodies


def find_roots(src_dir, in_binary):
    """Return the senders, with each one the compiler inlined replaced by its callers."""
    bodies = scan_source(src_dir)
    roots = {f for f, called in bodies.items()
             if f not in SEND_ENTRY_POINTS and called & set(SEND_ENTRY_POINTS)}
    inlined = set()
    todo = [r for r in roots if r not in in_binary]
    while todo:
        r = todo.pop()
        if r in inlined:
            continue
        inlined.add(r)
        for f, called in bodies.items():
            if r in called and f not in roots:
                roots.add(f)
                if f not in in_binary:
                    todo.append(f)
    return sorted(roots - inlined), sorted(inlined)


def deepest(fn, frames, calls, memo, stack):
    """Return (depth, path, cut) for fn; cut means a recursion edge was dropped."""
    if fn in memo:
        return memo[fn]
    if fn in stack:
        return 0, [], True
    stack.add(fn)
    best, best_path, cut = 0, [], False
    for callee in sorted(calls.get(fn, ())):
        if CUT.match(callee):
            continue
        d, p, c = deepest(callee, frames, calls, memo, stack)
        cut = cut or c
        if d > best:
            best, best_path = d, p
    stack.discard(fn)
    result = (frames.get(fn, 0) + best, [fn] + best_path, cut)
    if not cut:
        memo[fn] = result
    return result


def reachable(fn, calls):
    seen, todo = set(), [fn]
    while todo:
        f = todo.pop()
        if f in seen or CUT.match(f):
            continue
        seen.add(f)
        todo.extend(calls.get(f, ()))
    return seen


def main(su_dir, dis_path, src_dir):
    frames = load_frames(su_dir)
    calls, indirect = load_calls(dis_path)
    roots, inlined = find_roots(src_dir, set(calls))
    memo = {}
    print('# cut: ' + CUT.pattern)
    print('# inlined senders, measured through their callers: '
          + ' '.join(inlined))
    print('# root\tframe\tdepth\tindirect\tpath')
    for root in roots:
        d, path, _ = deepest(root, frames, calls, memo, set())
        n_ind = len(reachable(root, calls) & indirect)
        print(f'{root}\t{frames.get(root, 0)}\t{d}\t{n_ind}\t'
              + ' > '.join(f'{p}({frames.get(p, 0)})' for p in path))


if __name__ == '__main__':
    main(*sys.argv[1:4])
