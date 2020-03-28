/*
 * Copyright (C) 2019 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
var Binjgb = (async function() {
  // Must match values defined in generated binjgb.js
  const TABLE_SIZE = 23;
  const TOTAL_MEMORY = 16777216;
  const DYNAMICTOP_PTR = 16080;
  const DYNAMIC_BASE = 5259120;

  const pageSize = 65536;
  const totalPages = TOTAL_MEMORY / pageSize;
  const wasmFile = 'binjgb.wasm';
  const memory =
      new WebAssembly.Memory({initial: totalPages, maximum: totalPages});
  const buffer = memory.buffer;
  const u8a = new Uint8Array(buffer);
  const u32a = new Uint32Array(buffer);

  u32a[DYNAMICTOP_PTR >> 2] = DYNAMIC_BASE;

  const abort = what => { throw `abort(${what}).`; };
  const abortOnCannotGrowMemory = () => { abort('Cannot enlarge memory.'); };
  const fd_close = fd => { return 0; };
  const fd_seek = (fd, offLow, offHigh, whence, newOff) => { return 0; };

  const streams = {
    1: {buffer: [], out: console.log.bind(console)},
    2: {buffer: [], out: console.error.bind(console)},
  };
  const decoder = new TextDecoder('utf8');
  const fd_write = (fd, iov, iovcnt, pnum) => {
    const {buffer, out} = streams[fd];
    const printChar = c => {
      if (c === 0 || c === 10) {
        out(decoder.decode(new Uint8Array(buffer)));
        buffer.length = 0;
      } else {
        buffer.push(c);
      }
    };
    let ret = 0;
    for (let i = 0; i < iovcnt; ++i) {
      const ptr = u32a[iov + i * 8 >> 2];
      const len = u32a[iov + (i * 8 + 4) >> 2];
      for (let j = 0; j < len; ++j) {
        printChar(u8a[ptr + j]);
      }
      ret += len;
    }
    HEAP32[pnum >> 2] = ret;
    return 0;
  };
  const emscripten_resize_heap = size => { abortOnCannotGrowMemory(size); }
  const emscripten_memcpy_big = (dest, src, num) => {
    u8a.set(u8a.subarray(src, src + num), dest);
    return dest;
  };
  const exit = status => {};
  const table = new WebAssembly.Table(
      {initial: TABLE_SIZE, maximum: TABLE_SIZE, element: 'anyfunc'});
  const setTempRet0 = () => {};

  const funcs = {
    emscripten_memcpy_big,
    emscripten_resize_heap,
    exit,
    fd_close,
    fd_seek,
    fd_write,
    memory,
    setTempRet0,
    table,
  };
  const importObject = {
    env: funcs,
    wasi_snapshot_preview1: funcs,
  };

  const response = fetch(wasmFile);
  var {instance} = await WebAssembly.instantiate(
      await (await response).arrayBuffer(), importObject);

  const ret = {};
  for (let name in instance.exports) {
    ret[name] = instance.exports[name];
  }
  ret.buffer = memory.buffer;
  return ret;
});
