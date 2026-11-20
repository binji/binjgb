/* Shared native-style keyboard configuration for the browser frontends. */
"use strict";

// Physical key name: [KeyboardEvent.code, PS/2 set 2 code, extended prefix].
const BINJGB_KEY_CODES = {
  A: ['KeyA', 0x1c, false],
  B: ['KeyB', 0x32, false],
  C: ['KeyC', 0x21, false],
  D: ['KeyD', 0x23, false],
  E: ['KeyE', 0x24, false],
  F: ['KeyF', 0x2b, false],
  G: ['KeyG', 0x34, false],
  H: ['KeyH', 0x33, false],
  I: ['KeyI', 0x43, false],
  J: ['KeyJ', 0x3b, false],
  K: ['KeyK', 0x42, false],
  L: ['KeyL', 0x4b, false],
  M: ['KeyM', 0x3a, false],
  N: ['KeyN', 0x31, false],
  O: ['KeyO', 0x44, false],
  P: ['KeyP', 0x4d, false],
  Q: ['KeyQ', 0x15, false],
  R: ['KeyR', 0x2d, false],
  S: ['KeyS', 0x1b, false],
  T: ['KeyT', 0x2c, false],
  U: ['KeyU', 0x3c, false],
  V: ['KeyV', 0x2a, false],
  W: ['KeyW', 0x1d, false],
  X: ['KeyX', 0x22, false],
  Y: ['KeyY', 0x35, false],
  Z: ['KeyZ', 0x1a, false],
  1: ['Digit1', 0x16, false],
  2: ['Digit2', 0x1e, false],
  3: ['Digit3', 0x26, false],
  4: ['Digit4', 0x25, false],
  5: ['Digit5', 0x2e, false],
  6: ['Digit6', 0x36, false],
  7: ['Digit7', 0x3d, false],
  8: ['Digit8', 0x3e, false],
  9: ['Digit9', 0x46, false],
  0: ['Digit0', 0x45, false],
  ESCAPE: ['Escape', 0x76, false],
  GRAVE: ['Backquote', 0x0e, false],
  MINUS: ['Minus', 0x4e, false],
  EQUALS: ['Equal', 0x55, false],
  BACKSPACE: ['Backspace', 0x66, false],
  TAB: ['Tab', 0x0d, false],
  LEFTBRACKET: ['BracketLeft', 0x54, false],
  RIGHTBRACKET: ['BracketRight', 0x5b, false],
  BACKSLASH: ['Backslash', 0x5d, false],
  SEMICOLON: ['Semicolon', 0x4c, false],
  APOSTROPHE: ['Quote', 0x52, false],
  RETURN: ['Enter', 0x5a, false],
  LSHIFT: ['ShiftLeft', 0x12, false],
  RSHIFT: ['ShiftRight', 0x59, false],
  LCTRL: ['ControlLeft', 0x14, false],
  LALT: ['AltLeft', 0x11, false],
  SPACE: ['Space', 0x29, false],
  COMMA: ['Comma', 0x41, false],
  PERIOD: ['Period', 0x49, false],
  SLASH: ['Slash', 0x4a, false],
  F1: ['F1', 0x05, false],
  F2: ['F2', 0x06, false],
  F3: ['F3', 0x04, false],
  F4: ['F4', 0x0c, false],
  F5: ['F5', 0x03, false],
  F6: ['F6', 0x0b, false],
  F7: ['F7', 0x83, false],
  F8: ['F8', 0x0a, false],
  F9: ['F9', 0x01, false],
  F10: ['F10', 0x09, false],
  F11: ['F11', 0x78, false],
  F12: ['F12', 0x07, false],
  CAPSLOCK: ['CapsLock', 0x58, false],
  SCROLLLOCK: ['ScrollLock', 0x7e, false],
  NUMLOCKCLEAR: ['NumLock', 0x77, false],
  KP_0: ['Numpad0', 0x70, false],
  KP_1: ['Numpad1', 0x69, false],
  KP_2: ['Numpad2', 0x72, false],
  KP_3: ['Numpad3', 0x7a, false],
  KP_4: ['Numpad4', 0x6b, false],
  KP_5: ['Numpad5', 0x73, false],
  KP_6: ['Numpad6', 0x74, false],
  KP_7: ['Numpad7', 0x6c, false],
  KP_8: ['Numpad8', 0x75, false],
  KP_9: ['Numpad9', 0x7d, false],
  KP_PERIOD: ['NumpadDecimal', 0x71, false],
  KP_MULTIPLY: ['NumpadMultiply', 0x7c, false],
  KP_MINUS: ['NumpadSubtract', 0x7b, false],
  KP_PLUS: ['NumpadAdd', 0x79, false],
  UP: ['ArrowUp', 0x75, true],
  DOWN: ['ArrowDown', 0x72, true],
  LEFT: ['ArrowLeft', 0x6b, true],
  RIGHT: ['ArrowRight', 0x74, true],
  KP_DIVIDE: ['NumpadDivide', 0x4a, true],
  KP_ENTER: ['NumpadEnter', 0x5a, true],
  INSERT: ['Insert', 0x70, true],
  DELETE: ['Delete', 0x71, true],
  HOME: ['Home', 0x6c, true],
  END: ['End', 0x69, true],
  PAGEUP: ['PageUp', 0x7d, true],
  PAGEDOWN: ['PageDown', 0x7a, true],
  RCTRL: ['ControlRight', 0x14, true],
  RALT: ['AltRight', 0x11, true],
  LGUI: ['MetaLeft', 0x1f, true],
  RGUI: ['MetaRight', 0x27, true],
  APPLICATION: ['ContextMenu', 0x2f, true],
};

class BinjgbJoypad {
  constructor(module, emulator) {
    this.module = module;
    this.emulator = emulator;
    this.sources = new Map();
    // The WASM wrapper stores joypad state globally across ROM instances.
    for (const button of ['up', 'down', 'left', 'right', 'A', 'B', 'start', 'select']) {
      this.module[`_set_joyp_${button}`](this.emulator, false);
    }
  }

  set(button, source, down) {
    if (!this.sources.has(button)) this.sources.set(button, new Set());
    const sources = this.sources.get(button);
    const wasDown = sources.size > 0;
    if (down) sources.add(source);
    else sources.delete(source);
    if (wasDown !== (sources.size > 0)) {
      this.module[`_set_joyp_${button}`](this.emulator, sources.size > 0);
    }
  }

  clearSource(prefix) {
    for (const [button, sources] of this.sources) {
      for (const source of [...sources]) {
        if (source.startsWith(prefix)) this.set(button, source, false);
      }
    }
  }
}

class BinjgbKeyboard {
  static parseConfig(text) {
    const entries = [];
    for (const [index, line] of text.split(/\r?\n/).entries()) {
      const trimmed = line.trim();
      if (!trimmed || /^[#;]/.test(trimmed)) continue;
      const equals = trimmed.indexOf('=');
      if (equals < 0) throw new Error(`Invalid binjgb.ini line ${index + 1}`);
      const name = trimmed.slice(0, equals).trim();
      const value = trimmed.slice(equals + 1).trim();
      // Other native emulator settings do not configure the browser frontend.
      if (['joypad', 'shortcuts', 'ps2-keyboard', 'ps2-keys',
           'capture-browser-shortcuts'].includes(name) ||
          /^(joypad|shortcut)-/.test(name)) entries.push([name, value]);
    }
    return entries;
  }

  static async loadConfig(url) {
    const response = await fetch(url);
    if (response.status === 404) return [];
    if (!response.ok) throw new Error(`Could not load ${url}: ${response.status}`);
    return BinjgbKeyboard.parseConfig(await response.text());
  }

  constructor(module, emulator, config = [], handlers, options = {}) {
    this.module = module;
    this.emulator = emulator;
    this.options = options;
    this.held = new Map();
    this.counts = new Map();
    this.pending = [];
    this.ps2Connected = true;
    this.captureBrowserShortcuts = false;
    this.ps2Keys = this.parseKeys('all', true);
    this.actions = new Map(Object.entries(handlers).map(([name, action]) => [
      name, {...action, keys: this.parseKeys(action.keys), defaultKeys: action.keys},
    ]));

    for (const [name, value] of config) {
      if (name === 'ps2-keyboard') this.ps2Connected = this.parseBool(value);
      else if (name === 'capture-browser-shortcuts') {
        this.captureBrowserShortcuts = this.parseBool(value);
      } else if (name === 'ps2-keys') this.ps2Keys = this.parseKeys(value, true);
      else if (name === 'joypad' || name === 'shortcuts') {
        const mode = value.toLowerCase();
        if (!['none', 'default'].includes(mode)) throw new Error(`Invalid ${name}: ${value}`);
        const prefix = name === 'joypad' ? 'joypad-' : 'shortcut-';
        for (const [actionName, action] of this.actions) {
          if (actionName.startsWith(prefix)) {
            action.keys = this.parseKeys(mode === 'none' ? '' : action.defaultKeys);
          }
        }
      } else {
        const action = this.actions.get(name);
        if (!action) throw new Error(`Unsupported browser input setting: ${name}`);
        action.keys = this.parseKeys(value);
      }
    }

    this.routes = new Map();
    for (const prefix of ['shortcut-', 'joypad-']) {
      const group = new Map();
      for (const [name, action] of this.actions) {
        if (!name.startsWith(prefix)) continue;
        for (const key of action.keys) {
          if (group.has(key)) throw new Error(`Conflicting input bindings: ${name} and ${group.get(key)}`);
          group.set(key, name);
          if (!this.routes.has(key)) this.routes.set(key, {name, action});
        }
      }
    }
    this.keyNames = new Map(Object.entries(BINJGB_KEY_CODES).map(([name, [code]]) => [code, name]));
    this.window = options.window || window;
    this.document = options.document || document;
    this.module._emulator_set_ps2_keyboard_connected(this.emulator, this.ps2Connected);
    this.onKeyDown = event => this.keyDown(event);
    this.onKeyUp = event => this.keyUp(event);
    this.onBlur = () => this.releaseAll();
    this.onFocus = event => { if (this.isEditable(event.target)) this.releaseAll(); };
    this.onVisibility = () => { if (this.document.hidden) this.releaseAll(); };
    this.window.addEventListener('keydown', this.onKeyDown);
    this.window.addEventListener('keyup', this.onKeyUp);
    this.window.addEventListener('blur', this.onBlur);
    this.document.addEventListener('focusin', this.onFocus);
    this.document.addEventListener('visibilitychange', this.onVisibility);
  }

  parseBool(value) {
    if (value !== '0' && value !== '1') throw new Error(`Expected 0 or 1: ${value}`);
    return value === '1';
  }

  parseKeys(value, allowAll = false) {
    value = value.trim().toUpperCase();
    if (!value || value === 'NONE') return new Set();
    if (allowAll && value === 'ALL') return new Set(Object.keys(BINJGB_KEY_CODES));
    const keys = value.split(',').map(key => key.trim());
    for (const key of keys) {
      if (!Object.hasOwn(BINJGB_KEY_CODES, key)) throw new Error(`Invalid keyboard key: ${key}`);
    }
    return new Set(keys);
  }

  isEditable(target) {
    return !!(target && (target.isContentEditable || target.closest &&
      target.closest('input, textarea, select, button, a[href], [contenteditable], [role="textbox"]')));
  }

  keyDown(event) {
    if (event.defaultPrevented || event.isComposing || this.isEditable(event.target) ||
        this.options.isCaptured && this.options.isCaptured()) return;
    // Leave browser/OS chords available unless the host explicitly opts in.
    if (!this.captureBrowserShortcuts && (event.ctrlKey || event.metaKey || event.altKey)) {
      this.releaseAll();
      return;
    }
    if (this.held.has(event.code)) {
      event.preventDefault();
      return;
    }
    if (event.repeat) return;
    const key = this.keyNames.get(event.code);
    if (!key) return;
    let route = this.routes.get(key);
    if (!route && this.ps2Connected && this.ps2Keys.has(key)) route = {ps2: key};
    if (!route) return;
    this.held.set(event.code, route);
    event.preventDefault();
    if (this.options.onInput) this.options.onInput();
    if (route.ps2) this.sendScanCode(route.ps2, true);
    else {
      const count = this.counts.get(route.name) || 0;
      this.counts.set(route.name, count + 1);
      if (!count) route.action.callback(true);
    }
  }

  keyUp(event) {
    if (!this.held.has(event.code)) return;
    event.preventDefault();
    this.release(event.code);
  }

  release(code) {
    const route = this.held.get(code);
    this.held.delete(code);
    if (route.ps2) this.sendScanCode(route.ps2, false);
    else {
      const count = this.counts.get(route.name) - 1;
      this.counts.set(route.name, count);
      if (!count && route.action.held) route.action.callback(false);
    }
  }

  releaseAll() {
    for (const code of [...this.held.keys()]) this.release(code);
  }

  sendScanCode(key, down) {
    const [, scan, extended] = BINJGB_KEY_CODES[key];
    if (extended) this.pending.push(0xe0);
    if (!down) this.pending.push(0xf0);
    this.pending.push(scan);
    this.flush();
  }

  // The core queue is finite. Retry bytes in order as emulation advances,
  // retaining prefixes and releases instead of dropping partial sequences.
  flush() {
    let sent = 0;
    while (sent < this.pending.length &&
           this.module._emulator_queue_ps2_scan_code(this.emulator, this.pending[sent])) ++sent;
    if (sent) this.pending.splice(0, sent);
  }

  destroy() {
    this.releaseAll();
    this.window.removeEventListener('keydown', this.onKeyDown);
    this.window.removeEventListener('keyup', this.onKeyUp);
    this.window.removeEventListener('blur', this.onBlur);
    this.document.removeEventListener('focusin', this.onFocus);
    this.document.removeEventListener('visibilitychange', this.onVisibility);
    this.module._emulator_set_ps2_keyboard_connected(this.emulator, false);
    this.pending.length = 0;
  }
}
