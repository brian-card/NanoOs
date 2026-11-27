#!/usr/bin/env python3
################################################################################
#
#                       Copyright (c) 2026 Brian Card
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#
#                                 Brian Card
#                       https://github.com/brian-card
#
################################################################################


"""Mutation check of Nook's widgets (plan 5) and their tests.

Each mutation breaks one thing in sysnook, rebuilds the e2e image, and runs
the tests it names, each of which must go red.  A test named e2e/<name> is
the end-to-end test of that name in the nano-os-sim configuration.  Sources
are restored and touched after every mutation so no later build reuses a
mutated object.  Don't run it while another e2e run uses the same temporary
directory.

Usage:  mutate_widgets.py [name...]
"""
import os, re, subprocess, sys, time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SYSNOOK = 'userspace/usr/src/commands/sysnook/'
TEMPLATE = SYSNOOK + 'Template/Template.c'
WIDGETS = SYSNOOK + 'Widgets/Widgets.c'
WM = SYSNOOK + 'include/Wm.h'
CONTROLS = SYSNOOK + 'Controls/Controls.c'
PROPERTY = SYSNOOK + 'Property/Property.c'
LISTS = SYSNOOK + 'Lists/Lists.c'
INPUT = SYSNOOK + 'Input/Input.c'
MENUS = SYSNOOK + 'Menus/Menus.c'
CLIENT = SYSNOOK + 'Client/Client.c'
MENU_PAINT = SYSNOOK + 'MenuPaint/MenuPaint.c'
PAINT = SYSNOOK + 'Paint/Paint.c'
MAIN = SYSNOOK + 'main/main.c'
RESTACK = SYSNOOK + 'Restack/Restack.c'
CANVAS = SYSNOOK + 'Canvas/Canvas.c'

MUTATIONS = [
 ('no-widget-limit', TEMPLATE,
  '  if (oldCount + count > WIN_MAX_WIDGETS) {',
  '  if (false) {',
  ['e2e/test_widgets_a_33rd_widget_is_refused_and_nothing_of_it_is_kept']),
 ('no-budget', WM,
  '    if ((bytes > 0) && (used > state->budget)) {',
  '    if (false) {',
  ['e2e/test_widgets_a_template_past_the_budget_is_refused_and_nothing_kept']),
 ('text-kept-in-the-template', TEMPLATE,
  '      memcpy(widget->text, source, copy + widget->textLength);',
  '      free(widget->text);\n      widget->text = (char*) source;',
  ['e2e/test_widgets_a_template_overwritten_once_given_repaints_the_same']),
 ('center-drawn-left', WIDGETS,
  '  if (widget->align == WIN_ALIGN_CENTER) {',
  '  if (false) {',
  ['e2e/test_widgets_labels_in_each_alignment_and_a_group_box_are_drawn_'
   'exactly']),
 ('group-caption-unbroken', WIDGETS,
  '  wmFill(cmd, window->face, &gap);',
  '  (void) gap;',
  ['e2e/test_widgets_labels_in_each_alignment_and_a_group_box_are_drawn_'
   'exactly']),
 ('command-on-press', CONTROLS,
  '  widgets->pressed = index;\n  widgets->pressInside = true;',
  '  widgets->pressed = index;\n  widgets->pressInside = true;\n'
  '  wmClick(state, job->window, widgets, &widgets->widget[index]);',
  ['e2e/test_widgets_buttons_and_boxes_are_drawn_exactly_in_each_state']),
 ('disabled-clickable', CONTROLS,
  '    && ((widget->flags & (WIN_WIDGET_DISABLED | WIN_WIDGET_HIDDEN)) == 0);',
  '    && ((widget->flags & WIN_WIDGET_HIDDEN) == 0);',
  ['e2e/test_widgets_buttons_and_boxes_send_exactly_their_commands']),
 ('radio-group-kept', CONTROLS,
  '        other->value = 0;\n        wmDamageWidget(state, window, other);\n'
  '      }\n    }\n  }\n  wmDamageWidget(state, window, widget);',
  '        wmDamageWidget(state, window, other);\n'
  '      }\n    }\n  }\n  wmDamageWidget(state, window, widget);',
  ['e2e/test_widgets_buttons_and_boxes_send_exactly_their_commands']),
 ('no-space', CONTROLS,
  "  } else if ((event->keycode != ' ')\n",
  "  } else if ((event->keycode != 0x7f)\n",
  ['e2e/test_widgets_buttons_and_boxes_send_exactly_their_commands']),
 ('no-focus-outline', WIDGETS,
  '      bool focused = active && (widgets->focus == ii)',
  '      bool focused = false && (widgets->focus == ii)',
  ['e2e/test_widgets_buttons_and_boxes_are_drawn_exactly_in_each_state']),
 ('no-default-border', WIDGETS,
  '  if ((widget->flags & WIN_WIDGET_DEFAULT) != 0) {',
  '  if (false) {',
  ['e2e/test_widgets_buttons_and_boxes_are_drawn_exactly_in_each_state']),
 ('changed-for-every-key', WM,
  '  if (event == NULL) {\n    event = wmSend(state, window->owner, '
  'WIN_EVENT_CHANGED, window->id);',
  '  if (true) {\n    event = wmSend(state, window->owner, '
  'WIN_EVENT_CHANGED, window->id);',
  ['e2e/test_widgets_keys_in_one_input_message_give_one_change']),
 ('no-most', CONTROLS,
  '  if (printable && editable && (length < widget->maxLength)) {',
  '  if (printable && editable && (length < UINT8_MAX)) {',
  ['e2e/test_widgets_typing_past_the_most_leaves_exactly_the_most']),
 ('password-shown', WIDGETS,
  '  if ((widget->flags & WIN_WIDGET_PASSWORD) != 0) {',
  '  if (false) {',
  ['e2e/test_widgets_a_password_field_shows_stars_and_gives_the_text']),
 ('backspace-deletes-at-the-caret', CONTROLS,
  '    memmove(&widget->text[caret - 1], &widget->text[caret], length - caret);'
  '\n    widget->textLength--;\n    widget->caret--;',
  '    memmove(&widget->text[caret], &widget->text[caret + 1],'
  ' length - caret - 1);\n    widget->textLength--;\n    widget->caret--;',
  ['e2e/test_widgets_a_text_field_edits_exactly_as_typed']),
 ('no-field-scroll', WM,
  '  } else if (widget->caret - scroll > shown) {',
  '  } else if (false) {',
  ['e2e/test_widgets_a_text_field_scrolls_to_keep_its_caret_in_it']),
 ('scissor-for-every-widget', WIDGETS,
  '    bool spills = wmSpills(state, widget, &rect);',
  '    bool spills = true || wmSpills(state, widget, &rect);',
  ['e2e/test_widgets_widgets_that_fit_change_the_viewport_no_more_than_one']),
# Step 4:  lists.
 ('page-moves-only-the-selection', LISTS,
  '    wmScrollTo(job, (int32_t) list->top + page);\n',
  '',
  ['e2e/test_widgets_page_down_shows_the_next_ten_items_exactly']),
 ('thumb-not-proportional', LISTS,
  '  if (layout->thumbLength < WM_THUMB_MIN) {',
  '  layout->thumbLength = WM_THUMB_MIN;\n  if (layout->thumbLength < WM_THUMB_MIN) {',
  ['e2e/test_widgets_page_down_shows_the_next_ten_items_exactly']),
 ('no-double-click-command', LISTS,
  '  if (again) {',
  '  if (false && again) {',
  ['e2e/test_widgets_a_double_click_on_an_item_chooses_it']),
 ('enter-not-command', LISTS,
  '  } else if (key == GFX_KEY_ENTER) {\n    wmCommand(job);\n',
  '  } else if (key == GFX_KEY_ENTER) {\n',
  ['e2e/test_widgets_list_keys_move_the_selection_and_enter_chooses_it']),
 ('track-press-scrolls-one', LISTS,
  '      top += layout.rows;',
  '      top += 1;',
  ['e2e/test_widgets_the_wheel_and_the_scroll_bar_scroll_a_list']),
 ('wheel-ignored', INPUT,
  '      controls(state, window, "wmControlsWheel");\n',
  '',
  ['e2e/test_widgets_the_wheel_and_the_scroll_bar_scroll_a_list']),
 ('selection-not-clamped', PROPERTY,
  '  if ((list->value != WIN_NO_ITEM) && (list->value >= list->numItems)) {',
  '  if (false) {',
  ['e2e/test_widgets_items_set_to_a_shorter_list_clamp_the_selection']),
 ('template-selection-not-shown', TEMPLATE,
  '      wmShowSelection(window, widget);\n',
  '',
  ['e2e/test_widgets_items_set_to_a_shorter_list_clamp_the_selection']),
# Step 5:  menus.
 ('no-underline', MENU_PAINT,
  '        wmFill(cmd, ink, left, top + glyphH, glyphW, 1);\n',
  '',
  ['e2e/test_widgets_a_menu_bar_and_its_dropdown_are_drawn_exactly']),
 ('dropdown-not-painted', PAINT,
  '  if (state->menu.open && meets(&state->menu.rect, &clip)) {',
  '  if (false) {',
  ['e2e/test_widgets_a_menu_bar_and_its_dropdown_are_drawn_exactly']),
 ('choose-on-hover', MENUS,
  '  if (released) {\n    wmChoose(job);',
  '  if (true) {\n    wmChoose(job);',
  ['e2e/test_widgets_a_release_on_a_menu_item_chooses_it_once']),
 ('disabled-choosable', MENUS,
  '  return (item[1] & (WIN_MENU_DISABLED | WIN_MENU_SEPARATOR)) == 0;',
  '  return (item[1] & WIN_MENU_SEPARATOR) == 0;',
  ['e2e/test_widgets_a_release_on_a_menu_item_chooses_it_once']),
 ('menu-item-not-set', PROPERTY,
  '        item[1] = (uint8_t) ((item[1] & ~bits) | (value & bits));\n',
  '',
  ['e2e/test_widgets_a_release_on_a_menu_item_chooses_it_once']),
 ('hover-does-not-switch', MENUS,
  '    } else if (title != open->menu) {',
  '    } else if (pressed && (title != open->menu)) {',
  ['e2e/test_widgets_moving_over_a_title_opens_it_and_a_press_outside_closes']),
 ('press-outside-keeps-menu', MENUS,
  '  } else if (pressed) {\n    wmClose(state);\n    return;\n  }',
  '  }',
  ['e2e/test_widgets_moving_over_a_title_opens_it_and_a_press_outside_closes']),
 ('escape-release-leaks', MENUS,
  '  open->swallow = key;\n',
  '',
  ['e2e/test_widgets_menu_keys_choose_and_escape_gives_keys_back']),
 ('alt-letter-ignored', MENUS,
  '      index = wmMarked(job->widget, event->ascii);',
  '      index = WM_NO_WIDGET;',
  ['e2e/test_widgets_menu_keys_choose_and_escape_gives_keys_back']),
# Step 6:  focus, default and cancel, anchors.
 ('tab-takes-disabled', CONTROLS,
  '  return wmTakesPress(widget) && (widget->kind != WIN_WIDGET_MENU_BAR);',
  '  return (widget->kind != WIN_WIDGET_MENU_BAR);',
  ['e2e/test_widgets_tab_follows_the_template_skips_disabled_and_wraps']),
 ('tab-no-wrap', CONTROLS,
  '    } else if (at >= count) {\n      at = 0;\n    }\n    if (wmFocusable(',
  '    } else if (at >= count) {\n      return;\n    }\n    if (wmFocusable(',
  ['e2e/test_widgets_tab_follows_the_template_skips_disabled_and_wraps']),
 ('shift-tab-forward', CONTROLS,
  '        ((event->modifiers & GFX_MOD_SHIFT) != 0) ? -1 : 1);',
  '        1);',
  ['e2e/test_widgets_tab_follows_the_template_skips_disabled_and_wraps']),
 ('enter-not-default', CONTROLS,
  '      && (wmFlagged(widgets, WIN_WIDGET_DEFAULT) != NULL)',
  '      && false',
  ['e2e/test_widgets_enter_and_escape_press_the_default_and_cancel_buttons']),
 ('no-cancel', CONTROLS,
  '    button = wmFlagged(widgets, WIN_WIDGET_CANCEL);',
  '    button = NULL;',
  ['e2e/test_widgets_enter_and_escape_press_the_default_and_cancel_buttons']),
 ('no-stretch', WM,
  '      rect->w = right - rect->x;',
  '      rect->w = rect->w;',
  ['e2e/test_widgets_maximizing_applies_each_widgets_anchors_exactly']),
 ('right-anchor-ignored', WM,
  '      rect->x = right - rect->w;',
  '      rect->x = rect->x;',
  ['e2e/test_widgets_maximizing_applies_each_widgets_anchors_exactly']),
# Step 7:  the canvas.
 ('canvas-pointer-not-captured', CONTROLS,
  '    wmCanvasPointer(state, job->window, widget, job->event);\n'
  '    inside = widgets->pressInside;',
  '    inside = widgets->pressInside;',
  ['e2e/test_widgets_a_canvas_reports_the_pointer_in_its_own_coordinates']),
 ('canvas-coordinates-absolute', CONTROLS,
  '    sent->x = event->x - rect.x;',
  '    sent->x = event->x;',
  ['e2e/test_widgets_a_canvas_reports_the_pointer_in_its_own_coordinates']),
 ('canvas-call-accepted', CANVAS,
  '  [GFX_CMD_DRAW_TEXT] = WM_TEXT_RECORD,\n};',
  '  [GFX_CMD_DRAW_TEXT] = WM_TEXT_RECORD,\n  [GFX_CMD_CALL] = GFX_CMD_CALL_SIZE,\n};',
  ['e2e/test_widgets_a_canvas_refuses_records_it_may_not_hold']),
 ('canvas-cut-record-accepted', CANVAS,
  '  return (size > left) ? 0 : size;',
  '  return size;',
  ['e2e/test_widgets_a_canvas_refuses_records_it_may_not_hold']),
 ('timing-any-grab', MAIN,
  '        && ((grab == WM_GRAB_MOVE) || (grab == WM_GRAB_RESIZE))',
  '        && (grab != WM_GRAB_NONE)',
  ['e2e/test_widgets_timing_counts_no_frame_of_a_held_button_or_widget']),
# Found with wintest demo:  activation repainted the whole window.
 ('raise-repaints-the-window', WM,
  '    state->raised = raised.id;\n',
  '    state->raised = raised.id;\n    wmDamage(state, &raised.frame);\n',
  ['e2e/test_widgets_activating_a_window_repaints_only_what_changed']),
 ('focus-kept-when-inactive', RESTACK,
  '    wmDamageFocused(state, state->refocus[ii]);\n',
  '',
  ['e2e/test_widgets_a_window_losing_activation_hides_its_focus']),
# Found with term under wintest demo:  a covered change repainted the
# windows over it.
 ('occluded-repaints-everything', SYSNOOK + 'Grid/Grid.c',
  '        wmDamageOwned(state, &damage, window->id);',
  '        wmDamage(state, &damage);',
  ['e2e/test_widgets_a_covered_grid_change_repaints_nothing_over_it']),
 ('occlusion-skipped', PAINT,
  '      if (hidden) {',
  '      if (false) {',
  ['e2e/test_widgets_a_covered_grid_change_repaints_nothing_over_it']),
 ('menu-repaints-under-it', PAINT,
  '  bool inMenu = state->menu.open && (clip.x >= menu->x)',
  '  bool inMenu = false && (clip.x >= menu->x)',
  ['e2e/test_widgets_moving_the_highlight_redraws_only_its_two_items']),
 ('highlight-repaints-the-dropdown', MENUS,
  '    wmDamageItem(state, menu, state->menu.item);\n    state->menu.item = item;\n    wmDamageItem(state, menu, item);',
  '    state->menu.item = item;\n    wmDamage(state, &state->menu.rect);',
  ['e2e/test_widgets_moving_the_highlight_redraws_only_its_two_items']),
 ('right-alt-ignored', WM,
  '#define WM_MOD_ALT (GFX_MOD_ALT | GFX_MOD_ALT_GR)',
  '#define WM_MOD_ALT (GFX_MOD_ALT)',
  ['e2e/test_widgets_the_right_alt_opens_menus_as_the_left_does',
   'e2e/test_nook_the_right_alt_tabs_between_windows_as_the_left_does']),
# A text field's CHANGED waits for it to go quiet.
 ('changed-every-key', CONTROLS,
  '    state->heldWindow = window->id;\n',
  '    wmChanged(state, window, widget, widget->textLength);\n    state->heldWindow = window->id;\n',
  ['e2e/test_widgets_a_field_typed_into_quickly_reports_one_change']),
 ('click-before-change', CONTROLS,
  '  WmWidgets *widgets, WmWidget *widget\n) {\n  wmReleaseChanged(state);',
  '  WmWidgets *widgets, WmWidget *widget\n) {',
  ['e2e/test_widgets_a_held_change_goes_out_before_the_command_that_follows']),
 ('change-held-forever', CLIENT,
  '    if (quiet >= WM_CHANGED_QUIET_MS) {',
  '    if (false && (quiet >= WM_CHANGED_QUIET_MS)) {',
  ['e2e/test_widgets_a_field_typed_into_quickly_reports_one_change']),
 ('set-text-not-kept', PROPERTY,
  '  free(widget->text);\n  widget->text = text;',
  '  free(text);\n  text = NULL;',
  ['e2e/test_widgets_text_set_on_a_label_is_drawn_and_read_back']),
]


def run(command, timeout=None):
  try:
    return subprocess.run(command, cwd=REPO, shell=True, capture_output=True,
                          text=True, timeout=timeout)
  except subprocess.TimeoutExpired:
    return None


def passes(test):
  kind, name = test.split('/', 1)
  if kind == 'e2e':
    process = run(f'python3 test/e2e/run.py -j 1 nano-os-sim/{name}', 900)
    pattern = r'^ok \d+ - nano-os-sim/' + re.escape(name) + '$'
    return (process is not None) and re.search(pattern, process.stdout,
                                               re.M) is not None
  elif kind == 'harness':
    process = run('setsid --wait timeout -s KILL 60 '
                  f'./test/bin/nano-os-test {name}', 90)
    pattern = r'^ok \d+ - ' + re.escape(name) + '$'
    return (process is not None) and re.search(pattern, process.stdout,
                                               re.M) is not None
  process = run(f'cd test/e2e && python3 test_models.py {name}', 120)
  return (process is not None) and (process.returncode == 0)


def main(chosen):
  fails = 0
  for name, path, old, new, expect in MUTATIONS:
    if chosen and name not in chosen:
      continue
    full = os.path.join(REPO, path)
    original = open(full).read()
    assert original.count(old) == 1, name
    try:
      open(full, 'w').write(original.replace(old, new))
      if any(test.startswith('harness/') for test in expect):
        build = run('make -C test build')
        if build.returncode:
          print('FAIL', name, 'build')
          fails += 1
          continue
      green = [test for test in expect if passes(test)]
      print(('FAIL ' if green else 'ok   ') + name,
            'still green:' if green else 'red:', ' '.join(green or expect),
            flush=True)
      fails += bool(green)
    finally:
      open(full, 'w').write(original)
      now = time.time() + 2
      os.utime(full, (now, now))
  run('make -C test build')
  return 1 if fails else 0


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
