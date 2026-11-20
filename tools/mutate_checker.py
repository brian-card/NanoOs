"""Mutation check of the message-argument checker and its tests.

Each mutation disables one detector, hook, or behavior, rebuilds the harness
through its own makefile, and runs the message_args suites.  A mutation
passes when every test it names goes red; any named test that stays green
means that test doesn't cover what it claims.  Sources are restored and
touched after every mutation so no later build reuses a mutated object.

Usage:  mutate_checker.py [mutation-name...]
"""
import os, re, subprocess, sys, time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
CHECKER = 'src/kernel/MessageArgs.c'
HEADER = 'src/kernel/MessageArgs.h'
OVERLAY_API = 'src/user/NanoOsApi.c'
OVERLAY_HEADER = 'userspace/usr/include/ExecutiveProcesses.h'

# (name, file, old, new, header?, tests that must fail); old and new may be
# tuples of replacements made together.
DISABLE_KIND = ('static void record(uint8_t error, const void *address, ProcessId pid) {\n',
                'static void record(uint8_t error, const void *address, ProcessId pid) {\n'
                '  if (error == {kind}) return;\n')
KINDS = [
    ('DOUBLE_FREE', ['double_free_is_counted']),
    ('BAD_POINTER', ['free_of_a_foreign_pointer_is_counted',
                     'unshare_with_the_wrong_original_is_counted']),
    ('WRITE_AFTER_FREE', ['write_after_free_is_counted',
                          'a_real_send_freed_before_done_is_counted']),
    ('FREE_IN_FLIGHT', ['free_while_in_flight_is_counted',
                        'a_real_send_freed_before_done_is_counted']),
    ('WRONG_OWNER', ['a_free_by_another_process_is_counted']),
    ('WRONG_RECIPIENT', ['one_way_freed_by_its_sender_is_counted']),
    ('FIRE_AND_FORGET', ['round_trip_sent_without_waiting_is_counted',
                         'a_real_send_without_waiting_is_counted']),
    ('MEMORY_MANAGER', ['created_storage_sent_to_the_memory_manager_is_counted']),
    ('IN_MODIFIED', ['in_buffer_written_by_the_recipient_is_counted']),
    ('FREE_AFTER_ADOPT', ['a_sender_free_after_adopt_is_counted']),
    ('SEND_AFTER_FREE', ['sending_freed_storage_is_counted']),
    ('LEAK', ['storage_left_at_exit_is_a_leak']),
    ('ARENA_FULL', ['a_full_arena_returns_null_and_recovers']),
    ('OVERLAY_POINTER', ['a_buffer_in_the_overlay_window_is_counted']),
    ('SHARE_RECIPIENT', ['a_buffer_shared_with_another_process_is_counted',
                         'a_buffer_shared_with_no_process_is_counted']),
]
MUTATIONS = [
    ('kind-' + kind, CHECKER, DISABLE_KIND[0],
     DISABLE_KIND[1].replace('{kind}', 'MESSAGE_ARGS_ERROR_' + kind), False, tests)
    for kind, tests in KINDS
] + [
    ('no-poison', CHECKER,
     '  memset(blockPayload(block), MESSAGE_ARGS_POISON, block->size);\n', '', False,
     ['freed_storage_is_poisoned', 'a_retained_buffer_reads_poison']),
    ('no-zero', CHECKER, '  memset(payload, 0, found->size);\n', '', False,
     ['allocate_is_zeroed_after_reuse']),
    ('no-coalesce', CHECKER,
     '    while ((next < end) && (next->state == BLOCK_FREE)) {',
     '    while (0) {', False,
     ['a_full_arena_returns_null_and_recovers']),
    ('no-copy-back', CHECKER,
     '      memcpy(original, payload, block->requested);\n', '', False,
     ['out_buffer_is_copied_back_at_unshare']),
    ('no-in-copy', CHECKER,
     '    memcpy(payload, ptr, size);\n    memcpy(payload + size, ptr, size);\n', '',
     False, ['in_buffer_is_a_copy']),
    ('no-reclaim-count', CHECKER, '        _counts.reclaimed++;\n', '', False,
     ['a_killed_process_storage_is_reclaimed']),
    ('adopt-keeps-block', CHECKER, '  blockRelease(block, BLOCK_ADOPTED);\n', '', False,
     ['adopt_moves_the_block_and_frees_the_arena',
      'a_sender_free_after_adopt_is_counted']),
    ('adopt-ignores-copy-fn', CHECKER, '  if (copyFn != NULL) {', '  if (0) {', False,
     ['adopt_rebuilds_with_the_copy_function']),
    ('share-recipient-inverted', CHECKER,
     '        && (shared->sharedWith != recipient)\n',
     '        && (shared->sharedWith == recipient)\n', False,
     ['a_buffer_shared_with_the_recipient_is_clean',
      'a_buffer_shared_with_another_process_is_counted',
      'a_buffer_shared_with_no_process_is_counted']),
    ('share-scan-cut-short', CHECKER,
     '    for (size_t ii = 0; ii + sizeof(void*) <= block->requested;\n',
     '    for (size_t ii = 0; ii + 2 * sizeof(void*) <= block->requested;\n',
     False,
     ['a_buffer_shared_with_another_process_is_counted',
      'a_buffer_shared_with_no_process_is_counted']),
    ('share-recipient-not-stored', CHECKER,
     '  block->sharedWith = recipient;\n', '', False,
     ['a_buffer_shared_with_the_recipient_is_clean']),
    ('no-send-hook', 'src/kernel/Processes.c',
     '  MESSAGE_ARGS_SENDING(processMessage, processDescriptor->processId);\n'
     '  returnValue\n', '  returnValue\n', False,
     ['a_real_send_without_waiting_is_counted',
      'a_real_send_freed_before_done_is_counted']),
    ('exit-hook-silent', 'src/kernel/Scheduler.c',
     '  MESSAGE_ARGS_PROCESS_ENDED(processDescriptor->processId,\n'
     '    processDescriptor->exitSignal == 0);\n',
     '  MESSAGE_ARGS_PROCESS_ENDED(processDescriptor->processId,\n'
     '    false);\n', False,
     ['storage_left_at_exit_is_a_leak']),
    # The cleanup handler's hook also reclaims a killed process, so both go.
    ('no-kill-or-cleanup-hook', 'src/kernel/Scheduler.c',
     ('        MESSAGE_ARGS_PROCESS_ENDED(pid, false);\n',
      '  MESSAGE_ARGS_PROCESS_ENDED(processDescriptor->processId,\n'
      '    processDescriptor->exitSignal == 0);\n'), ('', ''), False,
     ['a_killed_process_storage_is_reclaimed',
      'a_stack_overflow_storage_is_reclaimed']),
    ('harness-check-off', 'test/suites/test_kernel_message_args.c',
     '  NANO_OS_ASSERT_EQ_INT(count, messageArgsErrorTotal());\n  messageArgsResetCounts();\n',
     '  NANO_OS_ASSERT_EQ_INT(count, messageArgsErrorTotal());\n', False,
     ['double_free_is_counted', 'storage_left_at_exit_is_a_leak']),
    ('production-not-zeroed', HEADER,
     '  type name##Storage = {0}; \\\n', '  type name##Storage; \\\n', True,
     ['allocate_zeroes_dirty_stack_storage']),
    ('production-share-evaluates-size', HEADER,
     '#define MESSAGE_BUFFER_SHARE(ptr, size, direction, recipient) ((void*) (ptr))',
     '#define MESSAGE_BUFFER_SHARE(ptr, size, direction, recipient) '
     '((void) (size), (void*) (ptr))',
     True, ['share_returns_the_buffer_unevaluated']),
    ('production-share-evaluates-recipient', HEADER,
     '#define MESSAGE_BUFFER_SHARE(ptr, size, direction, recipient) ((void*) (ptr))',
     '#define MESSAGE_BUFFER_SHARE(ptr, size, direction, recipient) '
     '((void) (recipient), (void*) (ptr))',
     True, ['share_returns_the_buffer_unevaluated']),
    ('overlay-api-share-is-identity', OVERLAY_API,
     '  return MESSAGE_BUFFER_SHARE(ptr, size, direction, recipient);\n',
     '  return ptr;\n', False,
     ['overlay_in_buffer_is_a_copy',
      'overlay_in_buffer_written_by_the_recipient_is_counted',
      'overlay_out_buffer_is_copied_back_at_unshare']),
    ('overlay-api-reports-production', OVERLAY_API,
     '  .messageArgsChecked = true,\n', '  .messageArgsChecked = false,\n', False,
     ['overlay_api_reports_the_checked_configuration']),
    ('overlay-share-ignores-flag', OVERLAY_HEADER,
     '#define MESSAGE_BUFFER_SHARE(ptr, size, direction, recipient) \\\n'
     '  (messageArgsChecked() \\\n',
     '#define MESSAGE_BUFFER_SHARE(ptr, size, direction, recipient) \\\n'
     '  (1 \\\n', True,
     ['production_share_returns_the_buffer_unevaluated']),
    ('overlay-allocate-ignores-flag', OVERLAY_HEADER,
     '  type *name = messageArgsChecked() \\\n', '  type *name = 1 \\\n', True,
     ['production_allocate_uses_its_own_storage',
      'no_executive_api_means_production']),
    ('overlay-free-skipped', OVERLAY_HEADER,
     '      messageArgsFree(name); \\\n', '', True,
     ['checked_allocate_and_free_go_through_the_api']),
    ('overlay-unshare-skipped', OVERLAY_HEADER,
     '      messageBufferUnshare((void*) (shared), (void*) (original), \\\n'
     '        (direction)); \\\n', '', True,
     ['checked_share_and_unshare_go_through_the_api']),
]


def run(cmd):
    return subprocess.run(cmd, cwd=REPO, shell=True, capture_output=True, text=True)


def results():
    """Run the message_args suites.

    @return (status, crashed): each reported test's name mapped to whether it
      passed, and whether the test binary died of a signal, which leaves
      every test after the crash unreported.
    """
    proc = run('./test/bin/nano-os-test message_args')
    status = {}
    for line in proc.stdout.splitlines():
        match = re.match(r'^(not ok|ok) \d+ - \S+/(\S+)', line)
        if match:
            status[match.group(2)] = match.group(1) == 'ok'
    # run() goes through a shell, which reports a signal as 128 + signal.
    return status, (proc.returncode < 0) or (proc.returncode > 128)


def main(names):
    failures = 0
    for name, path, old, new, header, tests in MUTATIONS:
        if names and name not in names:
            continue
        full = os.path.join(REPO, path)
        original = open(full).read()
        olds = old if isinstance(old, tuple) else (old,)
        news = new if isinstance(new, tuple) else (new,)
        mutated = original
        for oldText, newText in zip(olds, news):
            assert mutated.count(oldText) == 1, f'{name}: pattern not found once in {path}'
            mutated = mutated.replace(oldText, newText)
        try:
            open(full, 'w').write(mutated)
            if header:
                run('make -C test clean')
            build = run('make -C test build')
            if build.returncode != 0:
                print(f'FAIL {name}: did not build\n{build.stderr[-2000:]}')
                failures += 1
                continue
            status, crashed = results()
            missing = [test for test in tests if test not in status]
            green = [test for test in tests if status.get(test, False)]
            if green:
                print(f'FAIL {name}: still green: {" ".join(green)}')
                failures += 1
            elif missing and not crashed:
                print(f'FAIL {name}: did not report: {" ".join(missing)}')
                failures += 1
            elif missing:
                print(f'ok   {name}: {" ".join(tests)} went red '
                      f'(the test binary crashed before {" ".join(missing)})')
            else:
                print(f'ok   {name}: {" ".join(tests)} went red')
        finally:
            open(full, 'w').write(original)
            now = time.time() + 2
            os.utime(full, (now, now))
    run('make -C test clean')
    build = run('make -C test build')
    status, crashed = results()
    red = [test for test, passed in status.items() if not passed]
    print(f'restored: {len(status)} message_args tests, red after restore: '
          f'{red or "none"}{", crashed" if crashed else ""}')
    return 1 if failures or red or crashed or build.returncode != 0 else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
