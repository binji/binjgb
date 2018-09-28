/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
"use strict";

const RESULT_OK = 0;
const RESULT_ERROR = 1;
const SCREEN_WIDTH = 160;
const SCREEN_HEIGHT = 144;
const AUDIO_FRAMES = 4096;
const AUDIO_LATENCY_SEC = 0.1;
const MAX_UPDATE_SEC = 5 / 60;
const CPU_TICKS_PER_SECOND = 4194304;
const EVENT_NEW_FRAME = 1;
const EVENT_AUDIO_BUFFER_FULL = 2;
const EVENT_UNTIL_TICKS = 4;
const REWIND_FRAMES_PER_BASE_STATE = 45;
const REWIND_BUFFER_CAPACITY = 4 * 1024 * 1024;

const $ = document.querySelector.bind(document);
let emulator = null;

const dbPromise = idb.open('db', 1, upgradeDb => {
  const objectStore = upgradeDb.createObjectStore('games', {keyPath : 'sha1'});
  objectStore.createIndex('sha1', 'sha1', {unique : true});
});

function readFile(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onerror = event => reject(event.error);
    reader.onloadend = event => resolve(event.target.result);
    reader.readAsArrayBuffer(file);
  });
}

let data = {
  fps: 60,
  ticks: 0,
  loaded: false,
  loadedFile: null,
  paused: false,
  extRamUpdated: false,
  canvas: {
    show: false,
    scale: 3,
  },
  rewind: {
    minTicks: 0,
    maxTicks: 0,
  },
  files: {
    show: true,
    selected: 0,
    list: []
  }
};

let vm = new Vue({
  el: '.main',
  data: data,
  created: function() {
    setInterval(() => {
      this.fps = emulator ? emulator.fps : 60;
    }, 500);
    setInterval(() => {
      if (this.extRamUpdated) {
        this.updateExtRam();
        this.extRamUpdated = false;
      }
    }, 1000);
    this.readFiles();
  },
  mounted: function() {
    $('.main').classList.add('ready');
  },
  computed: {
    canvasWidthPx: function() {
      return (160 * this.canvas.scale) + 'px';
    },
    canvasHeightPx: function() {
      return (144 * this.canvas.scale) + 'px';
    },
    rewindTime: function() {
      const zeroPadLeft = (num, width) => ('' + (num | 0)).padStart(width, '0');
      const ticks = this.ticks;
      const hr = (ticks / (60 * 60 * CPU_TICKS_PER_SECOND)) | 0;
      const min = zeroPadLeft((ticks / (60 * CPU_TICKS_PER_SECOND)) % 60, 2);
      const sec = zeroPadLeft((ticks / CPU_TICKS_PER_SECOND) % 60, 2);
      const ms = zeroPadLeft((ticks / (CPU_TICKS_PER_SECOND / 1000)) % 1000, 3);
      return `${hr}:${min}:${sec}.${ms}`;
    },
    pauseLabel: function() {
      return this.paused ? 'resume' : 'pause';
    },
    isFilesListEmpty: function() {
      return this.files.list.length == 0;
    },
    loadedFileName: function() {
      return this.loadedFile ? this.loadedFile.name : '';
    },
    selectedFile: function() {
      return this.files.list[this.files.selected];
    },
    selectedFileHasImage: function() {
      const file = this.selectedFile;
      return file && file.image;
    },
    selectedFileImageSrc: function() {
      if (!this.selectedFileHasImage) return '';
      return this.selectedFile.image;
    },
  },
  watch: {
    paused: function(newPaused, oldPaused) {
      if (!emulator) return;
      if (newPaused == oldPaused) return;
      if (newPaused) {
        emulator.pause();
        this.updateTicks();
        this.rewind.minTicks =
            _rewind_get_oldest_ticks_f64(emulator.rewindBuffer);
        this.rewind.maxTicks =
            _rewind_get_newest_ticks_f64(emulator.rewindBuffer);
      } else {
        emulator.resume();
      }
    },
  },
  methods: {
    updateTicks: function() {
      this.ticks = _emulator_get_ticks_f64(emulator.e);
    },
    togglePause: function() {
      if (!this.loaded) return;
      this.paused = !this.paused;
    },
    rewindTo: function(event) {
      if (!emulator) return;
      emulator.rewindToTicks(+event.target.value);
      this.updateTicks();
    },
    selectFile: function(index) {
      this.files.selected = index;
    },
    playFile: async function(file) {
      const [romBuffer, extRamBuffer] = await Promise.all([
        readFile(file.rom),
        file.extRam ? readFile(file.extRam) : Promise.resolve(null)
      ]);
      this.paused = false;
      this.loaded = true;
      this.canvas.show = true;
      this.files.show = false;
      this.loadedFile = file;
      Emulator.start(romBuffer, extRamBuffer);
    },
    deleteFile: async function(file) {
      const db = await dbPromise;
      const tx = db.transaction('games', 'readwrite');
      const cursor = await tx.objectStore('games').openCursor(file.sha1);
      if (!cursor) return;
      cursor.delete();
      await tx.complete;
      const index = this.files.list.findIndex(x => x.sha1 === file.sha1);
      if (index < 0) return;
      this.files.list.splice(index, 1);
      if (this.loadedFile && this.loadedFile.sha1 === file.sha1) {
        this.loaded = false;
        this.loadedFile = null;
        this.paused = true;
        this.canvas.show = false;
        Emulator.stop();
      }
    },
    uploadClicked: function() {
      $('#upload').click();
    },
    uploadFile: async function(event) {
      const file = event.target.files[0];
      const [db, buffer] = await Promise.all([dbPromise, readFile(file)]);
      const sha1 = SHA1Digest(buffer);
      const name = file.name;
      const rom = new Blob([buffer]);
      const data = {sha1, name, rom, modified: new Date};
      const tx = db.transaction('games', 'readwrite');
      tx.objectStore('games').add(data)
      await tx.complete;
      this.files.list.push(data);
    },
    updateExtRam: async function() {
      if (!emulator) return;
      const extRamBlob = new Blob([emulator.getExtRam()]);
      const imageDataURL = $('canvas').toDataURL();
      const db = await dbPromise;
      const tx = db.transaction('games', 'readwrite');
      const cursor = await tx.objectStore('games').openCursor(
          this.loadedFile.sha1);
      if (!cursor) return;
      Object.assign(this.loadedFile, cursor.value);
      this.loadedFile.extRam = extRamBlob;
      this.loadedFile.image = imageDataURL;
      this.loadedFile.modified = new Date;
      cursor.update(this.loadedFile);
      return tx.complete;
    },
    toggleOpenDialog: function() {
      this.files.show = !this.files.show;
      if (this.files.show) {
        this.paused = true;
      }
    },
    readFiles: async function() {
      this.files.list.length = 0;
      const db = await dbPromise;
      const tx = db.transaction('games');
      tx.objectStore('games').iterateCursor(cursor => {
        if (!cursor) return;
        this.files.list.push(cursor.value);
        cursor.continue();
      });
      return tx.complete;
    },
    prettySize: function(size) {
      if (size >= 1024 * 1024) {
        return `${(size / (1024 * 1024)).toFixed(1)}Mib`;
      } else if (size >= 1024) {
        return `${(size / 1024).toFixed(1)}Kib`;
      } else {
        return `${size}b`;
      }
    },
    prettyDate: function(date) {
      const options = {
        year: 'numeric',
        month: 'numeric',
        day: 'numeric',
        hour: 'numeric',
        minute: 'numeric'
      };
      return date.toLocaleDateString(undefined, options);
    },
  }
});

(function bindKeyInput() {
  function keyRewind(e, isKeyDown) {
    if (emulator.isRewinding() !== isKeyDown) {
      if (isKeyDown) {
        vm.paused = true;
        emulator.autoRewind = true;
      } else {
        emulator.autoRewind = false;
        vm.paused = false;
      }
    }
  }

  const keyFuncs = {
    'ArrowDown': _set_joyp_down,
    'ArrowLeft': _set_joyp_left,
    'ArrowRight': _set_joyp_right,
    'ArrowUp': _set_joyp_up,
    'KeyZ': _set_joyp_B,
    'KeyX': _set_joyp_A,
    'Enter': _set_joyp_start,
    'Tab': _set_joyp_select,
    'Backspace': keyRewind,
    'Space': (e, isKeyDown) => { if (isKeyDown) vm.togglePause(); },
  };

  const makeKeyFunc = isKeyDown => {
    return event => {
      if (!emulator) return;
      if (event.code in keyFuncs) {
        keyFuncs[event.code](emulator.e, isKeyDown);
        event.preventDefault();
      }
    };
  };

  window.addEventListener('keydown', makeKeyFunc(true));
  window.addEventListener('keyup', makeKeyFunc(false));
})();

function copyInto(buffer, ptr) {
  HEAPU8.set(new Uint8Array(buffer), ptr, buffer.byteLength);
}

class Emulator {
  static start(romBuffer, extRamBuffer) {
    Emulator.stop();
    emulator = new Emulator(romBuffer, extRamBuffer);
    emulator.run();
  }

  static stop() {
    if (emulator) {
      emulator.cleanup();
      emulator = null;
    }
  }

  constructor(romBuffer, extRamBuffer) {
    const canvasEl = $('canvas');
    this.cleanupFuncs = [];
    // this.renderer = new WebGLRenderer(canvasEl);
    this.renderer = new Canvas2DRenderer(canvasEl);
    this.audio = new AudioContext;
    this.defer(() => this.audio.close());

    this.joypadBuffer = _joypad_new();
    this.defer(() => _joypad_delete(this.joypadBuffer));

    const romData = _malloc(romBuffer.byteLength);
    this.defer(() => _free(romData));
    copyInto(romBuffer, romData);
    this.e = _emulator_new_simple(
        romData, romBuffer.byteLength, this.audio.sampleRate, AUDIO_FRAMES,
        this.joypadBuffer);
    if (this.e == 0) {
      throw Error('Invalid ROM.');
    }
    this.defer(() => _emulator_delete(this.e));

    _emulator_set_default_joypad_callback(this.e, this.joypadBuffer);

    this.rewindState = null;
    this.rewindBuffer = _rewind_new_simple(
        this.e, REWIND_FRAMES_PER_BASE_STATE, REWIND_BUFFER_CAPACITY);
    this.defer(() => _rewind_delete(this.rewindBuffer));

    this.rewindIntervalId = 0;

    this.frameBuffer = new Uint8Array(
        Module.buffer, _get_frame_buffer_ptr(this.e),
        _get_frame_buffer_size(this.e));
    this.audioBuffer = new Uint8Array(
        Module.buffer, _get_audio_buffer_ptr(this.e),
        _get_audio_buffer_capacity(this.e));

    this.lastSec = 0;
    this.startAudioSec = 0;
    this.leftoverTicks = 0;
    this.fps = 60;

    if (extRamBuffer) {
      this.readExtRam(extRamBuffer);
    }
  }

  readExtRam(extRamBuffer) {
    const file_data = _ext_ram_file_data_new(this.e);
    if (_get_file_data_size(file_data) == extRamBuffer.byteLength) {
      copyInto(extRamBuffer, _get_file_data_ptr(file_data));
      _emulator_read_ext_ram(this.e, file_data);
    }
    _file_data_delete(file_data);
  }

  getExtRam() {
    const file_data = _ext_ram_file_data_new(this.e);
    _emulator_write_ext_ram(this.e, file_data);
    const buffer = new Uint8Array(Module.buffer, _get_file_data_ptr(file_data),
                                  _get_file_data_size(file_data));
    _file_data_delete(file_data);
    return buffer;
  }

  defer(f) {
    this.cleanupFuncs.push(f);
  }

  cleanup() {
    for (let func of this.cleanupFuncs) {
      func.call(this);
    }
  }

  get isPaused() {
    return this.rafCancelToken === null;
  }

  pause() {
    if (!this.isPaused) {
      this.cancelAnimationFrame();
      this.audio.suspend();
      this.beginRewind();
    }
  }

  resume() {
    if (this.isPaused) {
      this.endRewind();
      this.requestAnimationFrame();
      this.audio.resume();
    }
  }

  set autoRewind(enabled) {
    if (enabled) {
      const rewindFactor = 1.5;
      const updateMs = 16;

      this.rewindIntervalId = setInterval(() => {
        const oldest = _rewind_get_oldest_ticks_f64(emulator.rewindBuffer);
        const start = emulator.ticks;
        const delta = rewindFactor * updateMs / 1000 * CPU_TICKS_PER_SECOND;
        const rewindTo = Math.max(oldest, start - delta);
        this.rewindToTicks(rewindTo);
        vm.ticks = emulator.ticks;
      }, updateMs);
      this.defer(() => clearInterval(this.rewindIntervalId));
    } else {
      clearInterval(this.rewindIntervalId);
      this.rewindIntervalId = 0;
    }
  }

  requestAnimationFrame() {
    this.rafCancelToken = requestAnimationFrame(this.renderVideo.bind(this));
  }

  cancelAnimationFrame() {
    cancelAnimationFrame(this.rafCancelToken);
    this.rafCancelToken = null;
  }

  run() {
    this.requestAnimationFrame();
    this.defer(() => this.cancelAnimationFrame());
  }

  get ticks() {
    return _emulator_get_ticks_f64(this.e);
  }

  isRewinding() {
    return this.rewindState !== null;
  }

  runUntil(ticks) {
    while (true) {
      const event = _emulator_run_until_f64(this.e, ticks);
      if (event & EVENT_NEW_FRAME) {
        if (!this.isRewinding()) {
          _rewind_append(this.rewindBuffer, this.e);
        }
        this.renderer.uploadTexture(this.frameBuffer);
      }
      if ((event & EVENT_AUDIO_BUFFER_FULL) && !this.isRewinding()) {
        const nowAudioSec = this.audio.currentTime;
        const nowPlusLatency = nowAudioSec + AUDIO_LATENCY_SEC;
        this.startAudioSec = (this.startAudioSec || nowPlusLatency);
        if (this.startAudioSec >= nowAudioSec) {
          const buffer =
              this.audio.createBuffer(2, AUDIO_FRAMES, this.audio.sampleRate);
          const channel0 = buffer.getChannelData(0);
          const channel1 = buffer.getChannelData(1);
          let outPos = 0;
          let inPos = 0;
          for (let i = 0; i < AUDIO_FRAMES; i++) {
            channel0[outPos] = (this.audioBuffer[inPos] - 128) / 128;
            channel1[outPos] = (this.audioBuffer[inPos + 1] - 128) / 128;
            outPos++;
            inPos += 2;
          }
          const bufferSource = this.audio.createBufferSource();
          bufferSource.buffer = buffer;
          bufferSource.connect(this.audio.destination);
          bufferSource.start(this.startAudioSec);
          const bufferSec = AUDIO_FRAMES / this.audio.sampleRate;
          this.startAudioSec += bufferSec;
        } else {
          console.log(
              'Resetting audio (' + this.startAudioSec.toFixed(2) + ' < ' +
              nowAudioSec.toFixed(2) + ')');
          this.startAudioSec = nowPlusLatency;
        }
      }
      if (event & EVENT_UNTIL_TICKS) {
        break;
      }
    }
    if (_emulator_was_ext_ram_updated(this.e)) {
      vm.extRamUpdated = true;
    }
  }

  renderVideo(startMs) {
    this.requestAnimationFrame();

    let deltaSec = 0;

    if (!this.isRewinding()) {
      const startSec = startMs / 1000;
      deltaSec = Math.max(startSec - (this.lastSec || startSec), 0);
      const startTicks = this.ticks;
      const deltaTicks =
          Math.min(deltaSec, MAX_UPDATE_SEC) * CPU_TICKS_PER_SECOND;
      const runUntilTicks = (startTicks + deltaTicks - this.leftoverTicks);

      this.runUntil(runUntilTicks);

      this.leftoverTicks = (this.ticks - runUntilTicks) | 0;
      this.lastSec = startSec;
    }

    function lerp(from, to, alpha) {
      return (alpha * from) + (1 - alpha) * to;
    }

    this.fps = lerp(this.fps, Math.min(1 / deltaSec, 10000), 0.3);
    this.renderer.renderTexture();
  }

  beginRewind() {
    if (this.isRewinding()) return;
    this.rewindState =
        _rewind_begin(this.e, this.rewindBuffer, this.joypadBuffer);
  }

  rewindToTicks(ticks) {
    if (!this.isRewinding()) return;
    const result = _rewind_to_ticks_wrapper(this.rewindState, ticks);
    if (result === RESULT_OK) {
      _emulator_set_rewind_joypad_callback(this.rewindState);
      this.runUntil(ticks);
      this.renderer.renderTexture();
    }
  }

  endRewind() {
    if (!this.isRewinding()) return;
    _emulator_set_default_joypad_callback(this.e, this.joypadBuffer);
    _rewind_end(this.rewindState);
    this.rewindState = null;
    this.lastSec = 0;
    this.startAudioSec = 0;
    this.leftoverTicks = 0;
  }
}

class Canvas2DRenderer {
  constructor(el) {
    this.ctx = el.getContext('2d');
    this.imageData = this.ctx.createImageData(el.width, el.height);
  }

  renderTexture() {
    this.ctx.putImageData(this.imageData, 0, 0);
  }

  uploadTexture(buffer) {
    this.imageData.data.set(buffer);
  }
}

class WebGLRenderer {
  constructor(el) {
    const gl = this.gl = el.getContext('webgl', {preserveDrawingBuffer: true});

    const w = SCREEN_WIDTH / 256;
    const h = SCREEN_HEIGHT / 256;
    const buffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
      -1, -1,  0, h,
      +1, -1,  w, h,
      -1, +1,  0, 0,
      +1, +1,  w, 0,
    ]), gl.STATIC_DRAW);

    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texImage2D(
        gl.TEXTURE_2D, 0, gl.RGBA, 256, 256, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);

    function compileShader(type, source) {
      const shader = gl.createShader(type);
      gl.shaderSource(shader, source);
      gl.compileShader(shader);
      if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
        console.log('compileShader failed: ' + gl.getShaderInfoLog(shader));
      }
      return shader;
    }

    const vertexShader = compileShader(gl.VERTEX_SHADER,
      'attribute vec2 aPos;' +
      'attribute vec2 aTexCoord;' +
      'varying highp vec2 vTexCoord;' +
      'void main(void) {' +
      '  gl_Position = vec4(aPos, 0.0, 1.0);' +
      '  vTexCoord = aTexCoord;' +
      '}'
    );
    const fragmentShader = compileShader(gl.FRAGMENT_SHADER,
        'varying highp vec2 vTexCoord;' +
        'uniform sampler2D uSampler;' +
        'void main(void) {' +
        '  gl_FragColor = texture2D(uSampler, vTexCoord);' +
        '}'
    );

    const program = gl.createProgram();
    gl.attachShader(program, vertexShader);
    gl.attachShader(program, fragmentShader);
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
      console.log('program link failed: ' + gl.getProgramInfoLog(program));
    }
    gl.useProgram(program);

    const aPos = gl.getAttribLocation(program, 'aPos');
    const aTexCoord = gl.getAttribLocation(program, 'aTexCoord');
    const uSampler = gl.getUniformLocation(program, 'uSampler');

    gl.enableVertexAttribArray(aPos);
    gl.enableVertexAttribArray(aTexCoord);
    gl.vertexAttribPointer(aPos, 2, gl.FLOAT, gl.FALSE, 16, 0);
    gl.vertexAttribPointer(aTexCoord, 2, gl.FLOAT, gl.FALSE, 16, 8);
    gl.uniform1i(uSampler, 0);
  }

  renderTexture() {
    this.gl.clearColor(0.5, 0.5, 0.5, 1.0);
    this.gl.clear(this.gl.COLOR_BUFFER_BIT);
    this.gl.drawArrays(this.gl.TRIANGLE_STRIP, 0, 4);
  }

  uploadTexture(buffer) {
    this.gl.texSubImage2D(
        this.gl.TEXTURE_2D, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, this.gl.RGBA,
        this.gl.UNSIGNED_BYTE, buffer);
  }
}
