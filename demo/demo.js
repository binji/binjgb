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
const CPU_CYCLES_PER_SECOND = 4194304;
const EVENT_NEW_FRAME = 1;
const EVENT_AUDIO_BUFFER_FULL = 2;
const EVENT_UNTIL_CYCLES = 4;
const REWIND_FRAMES_PER_BASE_STATE = 45;
const REWIND_BUFFER_CAPACITY = 4 * 1024 * 1024;

var $ = document.querySelector.bind(document);
var canvasEl = $('canvas');
var pauseEl = $('#pause');
var rewindEl = $('#rewind');
var rewindBarEl = $('#rewindBar');
var rewindTimeEl = $('#rewindTime');
var emulator = null;

function setScale(scale) {
  canvasEl.style.width = canvasEl.width * scale + 'px';
  canvasEl.style.height = canvasEl.height * scale + 'px';
}

$('#_1x').addEventListener('click', (event) => setScale(1));
$('#_2x').addEventListener('click', (event) => setScale(2));
$('#_3x').addEventListener('click', (event) => setScale(3));
$('#_4x').addEventListener('click', (event) => setScale(4));

$('#file').addEventListener('change', (event) => {
  var reader = new FileReader();
  reader.onloadend = () => startEmulator(reader.result);
  reader.readAsArrayBuffer(event.target.files[0]);
});

function startEmulator(romArrayBuffer) {
  if (emulator) {
    emulator.cleanup();
  }

  emulator = new Emulator(romArrayBuffer);
  emulator.bindPauseButton(pauseEl);
  emulator.bindFpsCounter($('#fps'), 500);
  emulator.bindKeyInput(window);
  emulator.bindRewindSlider($('#rewind'));
  emulator.showRewindBar(false);
  emulator.run();
}

function Emulator(romArrayBuffer) {
  this.cleanupFuncs = [];
  this.renderer = new WebGLRenderer(canvasEl);
  // this.renderer = new Canvas2DRenderer(canvasEl);
  this.audio = new AudioContext();
  this.defer(() => this.audio.close());

  this.joypadBuffer = _joypad_new();
  this.defer(() => _joypad_delete(this.joypadBuffer));

  var romData = _malloc(romArrayBuffer.byteLength);
  HEAPU8.set(
      new Uint8Array(romArrayBuffer), romData, romArrayBuffer.byteLength);
  this.e = _emulator_new_simple(
      romData, romArrayBuffer.byteLength, this.audio.sampleRate, AUDIO_FRAMES,
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
  this.leftoverCycles = 0;
  this.fps = 60;
}

Emulator.prototype.defer = function(f) {
  this.cleanupFuncs.push(f);
};

Emulator.prototype.cleanup = function() {
  for (var i = 0; i < this.cleanupFuncs.length; ++i) {
    this.cleanupFuncs[i].call(this);
  }
};

Emulator.prototype.addEventListener = function(el, name, func) {
  el.addEventListener(name, func);
  this.defer(() => el.removeEventListener(name, func));
};

Emulator.prototype.showRewindBar = function(show) {
  if (show) {
    rewindBarEl.removeAttribute('hidden');
    rewindEl.setAttribute('min', _rewind_get_oldest_cycles(this.rewindBuffer));
    rewindEl.setAttribute('max', _rewind_get_newest_cycles(this.rewindBuffer));
    rewindEl.setAttribute('step', 1);
    rewindEl.value = this.getCycles();
    this.updateRewindTime();
  } else {
    rewindBarEl.setAttribute('hidden', '');
  }
};

Emulator.prototype.isPaused = function() {
  return this.rafCancelToken === null;
};

Emulator.prototype.setPaused = function(isPaused) {
  var wasPaused = this.isPaused();
  if (wasPaused === isPaused) {
    return;
  }

  if (isPaused) {
    pauseEl.textContent = "resume";
    this.cancelAnimationFrame();
    this.audio.suspend();
    this.beginRewind();
  } else {
    this.endRewind();
    pauseEl.textContent = "pause";
    this.requestAnimationFrame();
    this.audio.resume();
  }
};

Emulator.prototype.bindPauseButton = function(el) {
  pauseEl.textContent = 'pause';
  this.addEventListener(el, 'click', (event) => {
    this.setPaused(!this.isPaused());
    this.showRewindBar(this.isPaused());
  });
};

Emulator.prototype.bindFpsCounter = function(el, updateMs) {
  var func = () => el.textContent = this.fps.toFixed(1);
  var intervalId = setInterval(func, updateMs);
  this.defer(() => clearInterval(intervalId));
};

Emulator.prototype.keyTogglePaused = function(e, isKeyDown) {
  if (isKeyDown) {
    this.setPaused(!this.isPaused());
    this.showRewindBar(this.isPaused());
  }
};

Emulator.prototype.keyRewind = function(e, isKeyDown) {
  if (this.isRewinding() !== isKeyDown) {
    if (isKeyDown) {
      const rewindFactor = 1.5;
      const updateMs = 16;

      this.setPaused(true);
      this.rewindIntervalId = setInterval(() => {
        var oldest = _rewind_get_oldest_cycles(this.rewindBuffer);
        var start = this.getCycles();
        var delta = rewindFactor * updateMs / 1000 * CPU_CYCLES_PER_SECOND;
        var rewindTo = Math.max(oldest, start - delta);
        this.rewindToCycles(rewindTo);
      }, updateMs);
    } else {
      clearInterval(this.rewindIntervalId);
      this.rewindIntervalId = 0;
      this.setPaused(false);
    }
  }
};

Emulator.prototype.bindKeyInput = function(el) {
  var keyFuncs = {
    'ArrowDown': _set_joyp_down,
    'ArrowLeft': _set_joyp_left,
    'ArrowRight': _set_joyp_right,
    'ArrowUp': _set_joyp_up,
    'KeyX': _set_joyp_B,
    'KeyZ': _set_joyp_A,
    'Enter': _set_joyp_start,
    'Tab': _set_joyp_select,
    'Backspace': this.keyRewind.bind(this),
    'Space': this.keyTogglePaused.bind(this),
  };

  var makeKeyFunc = (isKeyDown) => {
    return (event) => {
      if (event.code in keyFuncs) {
        keyFuncs[event.code](this.e, isKeyDown);
        event.preventDefault();
      }
    };
  };

  this.addEventListener(el, 'keydown', makeKeyFunc(true));
  this.addEventListener(el, 'keyup', makeKeyFunc(false));
};

Emulator.prototype.bindRewindSlider = function(el) {
  this.addEventListener(el, 'input', (event) => {
    this.rewindToCycles(+event.target.value);
    this.updateRewindTime();
  });
};

function zeroPadLeft(num, width) {
  num = '' + (num | 0);
  while (num.length < width) {
    num = '0' + num;
  }
  return num;
}

function cyclesToTime(cycles) {
  var hr = (cycles / (60 * 60 * CPU_CYCLES_PER_SECOND)) | 0;
  var min = zeroPadLeft((cycles / (60 * CPU_CYCLES_PER_SECOND)) % 60, 2);
  var sec = zeroPadLeft((cycles / CPU_CYCLES_PER_SECOND) % 60, 2);
  var ms = zeroPadLeft((cycles / (CPU_CYCLES_PER_SECOND / 1000)) % 1000, 3);
  return `${hr}:${min}:${sec}.${ms}`;
}

Emulator.prototype.updateRewindTime = function() {
  rewindTimeEl.textContent = cyclesToTime(rewindEl.value);
};

Emulator.prototype.requestAnimationFrame = function() {
  this.rafCancelToken = requestAnimationFrame(this.renderVideo.bind(this));
};

Emulator.prototype.cancelAnimationFrame = function() {
  cancelAnimationFrame(this.rafCancelToken);
  this.rafCancelToken = null;
};

Emulator.prototype.run = function() {
  this.requestAnimationFrame();
  this.defer(() => this.cancelAnimationFrame());
};

Emulator.prototype.getCycles = function() {
  return _emulator_get_cycles(this.e) >>> 0;
};

Emulator.prototype.isRewinding = function() {
  return this.rewindState !== null;
};

Emulator.prototype.runUntil = function(cycles) {
  while (true) {
    var event = _emulator_run_until(this.e, cycles);
    if (event & EVENT_NEW_FRAME) {
      if (!this.isRewinding()) {
        _rewind_append(this.rewindBuffer, this.e);
      }
      this.renderer.uploadTexture(this.frameBuffer);
    }
    if ((event & EVENT_AUDIO_BUFFER_FULL) && !this.isRewinding()) {
      var nowAudioSec = this.audio.currentTime;
      var nowPlusLatency = nowAudioSec + AUDIO_LATENCY_SEC;
      this.startAudioSec = (this.startAudioSec || nowPlusLatency);
      if (this.startAudioSec >= nowAudioSec) {
        var buffer =
            this.audio.createBuffer(2, AUDIO_FRAMES, this.audio.sampleRate);
        var channel0 = buffer.getChannelData(0);
        var channel1 = buffer.getChannelData(1);
        var outPos = 0;
        var inPos = 0;
        for (var i = 0; i < AUDIO_FRAMES; i++) {
          channel0[outPos] = (this.audioBuffer[inPos] - 128) / 128;
          channel1[outPos] = (this.audioBuffer[inPos + 1] - 128) / 128;
          outPos++;
          inPos += 2;
        }
        var bufferSource = this.audio.createBufferSource();
        bufferSource.buffer = buffer;
        bufferSource.connect(this.audio.destination);
        bufferSource.start(this.startAudioSec);
        var bufferSec = AUDIO_FRAMES / this.audio.sampleRate;
        this.startAudioSec += bufferSec;
      } else {
        console.log(
            'Resetting audio (' + this.startAudioSec.toFixed(2) + ' < ' +
            nowAudioSec.toFixed(2) + ')');
        this.startAudioSec = nowPlusLatency;
      }
    }
    if (event & EVENT_UNTIL_CYCLES) {
      break;
    }
  }
};

function lerp(from, to, alpha) {
  return (alpha * from) + (1 - alpha) * to;
}

Emulator.prototype.renderVideo = function(startMs) {
  this.requestAnimationFrame();

  if (!this.isRewinding()) {
    var startSec = startMs / 1000;
    var deltaSec = Math.max(startSec - (this.lastSec || startSec), 0);
    var startCycles = this.getCycles();
    var deltaCycles =
        Math.min(deltaSec, MAX_UPDATE_SEC) * CPU_CYCLES_PER_SECOND;
    var runUntilCycles =
        (startCycles + deltaCycles - this.leftoverCycles) >>> 0;

    this.runUntil(runUntilCycles);

    this.leftoverCycles = (this.getCycles() - runUntilCycles) | 0;
    this.lastSec = startSec;
  }

  this.fps = lerp(this.fps, Math.min(1 / deltaSec, 10000), 0.3);
  this.renderer.renderTexture();
};

Emulator.prototype.beginRewind = function() {
  if (this.isRewinding()) {
    throw Error('Already rewinding!');
  }
  this.rewindState =
      _rewind_begin(this.e, this.rewindBuffer, this.joypadBuffer);
};

Emulator.prototype.rewindToCycles = function(cycles) {
  if (!this.isRewinding()) {
    throw Error('Not rewinding!');
  }
  var result = _rewind_to_cycles_wrapper(this.rewindState, cycles);
  if (result === RESULT_OK) {
    _emulator_set_rewind_joypad_callback(this.rewindState);
    this.runUntil(cycles);
    this.renderer.renderTexture();
  }
};

Emulator.prototype.endRewind = function() {
  if (!this.isRewinding()) {
    throw Error('Not rewinding!');
  }
  _emulator_set_default_joypad_callback(this.e, this.joypadBuffer);
  _rewind_end(this.rewindState);
  this.rewindState = null;
  this.lastSec = 0;
  this.startAudioSec = 0;
  this.leftoverCycles = 0;
};


function Canvas2DRenderer(el) {
  this.ctx = el.getContext('2d');
  this.imageData = this.ctx.createImageData(el.width, el.height);
}

Canvas2DRenderer.prototype.renderTexture = function() {
  this.ctx.putImageData(this.imageData, 0, 0);
};

Canvas2DRenderer.prototype.uploadTexture = function(buffer) {
  this.imageData.data.set(buffer);
};

function WebGLRenderer(el) {
  var gl = this.gl = el.getContext('webgl');

  var w = SCREEN_WIDTH / 256;
  var h = SCREEN_HEIGHT / 256;
  var buffer = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
    -1, -1,  0, h,
    +1, -1,  w, h,
    -1, +1,  0, 0,
    +1, +1,  w, 0,
  ]), gl.STATIC_DRAW);

  var texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.texImage2D(
      gl.TEXTURE_2D, 0, gl.RGBA, 256, 256, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);

  var compileShader = function(type, source) {
    var shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
      console.log('compileShader failed: ' + gl.getShaderInfoLog(shader));
    }
    return shader;
  };

  var vertexShader = compileShader(gl.VERTEX_SHADER,
    'attribute vec2 aPos;' +
    'attribute vec2 aTexCoord;' +
    'varying highp vec2 vTexCoord;' +
    'void main(void) {' +
    '  gl_Position = vec4(aPos, 0.0, 1.0);' +
    '  vTexCoord = aTexCoord;' +
    '}'
  );
  var fragmentShader = compileShader(gl.FRAGMENT_SHADER,
      'varying highp vec2 vTexCoord;' +
      'uniform sampler2D uSampler;' +
      'void main(void) {' +
      '  gl_FragColor = texture2D(uSampler, vTexCoord);' +
      '}'
  );

  var program = gl.createProgram();
  gl.attachShader(program, vertexShader);
  gl.attachShader(program, fragmentShader);
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
    console.log('program link failed: ' + gl.getProgramInfoLog(program));
  }
  gl.useProgram(program);

  var aPos = gl.getAttribLocation(program, 'aPos');
  var aTexCoord = gl.getAttribLocation(program, 'aTexCoord');
  var uSampler = gl.getUniformLocation(program, 'uSampler');

  gl.enableVertexAttribArray(aPos);
  gl.enableVertexAttribArray(aTexCoord);
  gl.vertexAttribPointer(aPos, 2, gl.FLOAT, gl.FALSE, 16, 0);
  gl.vertexAttribPointer(aTexCoord, 2, gl.FLOAT, gl.FALSE, 16, 8);
  gl.uniform1i(uSampler, 0);
}

WebGLRenderer.prototype.renderTexture = function() {
  this.gl.clearColor(0.5, 0.5, 0.5, 1.0);
  this.gl.clear(this.gl.COLOR_BUFFER_BIT);
  this.gl.drawArrays(this.gl.TRIANGLE_STRIP, 0, 4);
};

WebGLRenderer.prototype.uploadTexture = function(buffer) {
  this.gl.texSubImage2D(
      this.gl.TEXTURE_2D, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, this.gl.RGBA,
      this.gl.UNSIGNED_BYTE, buffer);
};
