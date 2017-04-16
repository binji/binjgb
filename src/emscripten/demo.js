/*
 * Copyright (C) 2017 Ben Smith
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */
"use strict";

const SCREEN_WIDTH = 160;
const SCREEN_HEIGHT = 144;
const AUDIO_FRAMES = 4096;
const AUDIO_LATENCY_SEC = 0.1;
const MAX_UPDATE_SEC = 5 / 60;
const CPU_CYCLES_PER_SECOND = 4194304;
const EVENT_NEW_FRAME = 1;
const EVENT_AUDIO_BUFFER_FULL = 2;
const EVENT_UNTIL_CYCLES = 4;

var $ = document.querySelector.bind(document);
var canvasEl = $('canvas');
var emulator = null;

function setScale(scale) {
  canvasEl.style.width = canvasEl.width * scale + 'px';
  canvasEl.style.height = canvasEl.height * scale + 'px';
}

$('#_1x').addEventListener('click', function(event) { setScale(1); });
$('#_2x').addEventListener('click', function(event) { setScale(2); });
$('#_3x').addEventListener('click', function(event) { setScale(3); });
$('#_4x').addEventListener('click', function(event) { setScale(4); });

$('#file').addEventListener('change', function(event) {
  var reader = new FileReader();
  reader.onloadend = function() { startEmulator(reader.result); }
  reader.readAsArrayBuffer(this.files[0]);
});

function startEmulator(romArrayBuffer) {
  if (emulator) {
    emulator.cleanup();
  }

  emulator = new Emulator(romArrayBuffer);
  emulator.bindStopButton($('#stop'));
  emulator.bindFpsCounter($('#fps'), 500);
  emulator.bindKeyInput(window);
  emulator.run();
}

function Emulator(romArrayBuffer) {
  this.cleanupFuncs = [];
  this.renderer = new WebGLRenderer(canvasEl);
  // this.renderer = new Canvas2DRenderer(canvasEl);
  this.audio = new AudioContext();
  this.defer(function() { this.audio.close(); });

  var romData = _malloc(romArrayBuffer.byteLength);
  HEAPU8.set(
      new Uint8Array(romArrayBuffer), romData, romArrayBuffer.byteLength);
  this.e = _emulator_new_simple(
      romData, romArrayBuffer.byteLength, this.audio.sampleRate, AUDIO_FRAMES);
  if (this.e == 0) {
    throw Error('Invalid ROM.');
  }
  this.defer(function() { _emulator_delete(this.e); });

  this.frameBuffer = new Uint8Array(
      Module.buffer, _get_frame_buffer_ptr(this.e),
      _get_frame_buffer_size(this.e));
  this.audioBuffer = new Uint8Array(
      Module.buffer, _get_audio_buffer_ptr(this.e),
      _get_audio_buffer_capacity(this.e));

  this.lastSec = null;
  this.startAudioSec = null;
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

Emulator.prototype.stop = function() {
  cancelAnimationFrame(this.rafCancelToken);
  this.audio.suspend();
};

Emulator.prototype.bindStopButton = function(el) {
  var func = this.stop.bind(this);
  el.addEventListener('click', func);
  this.defer(function() { el.removeEventListener('click', func); });
};

Emulator.prototype.bindFpsCounter = function(el, updateMs) {
  var func = (function() { el.textContent = this.fps.toFixed(1); }).bind(this);
  var intervalId = setInterval(func, updateMs);
  this.defer(function() { clearInterval(intervalId); });
};

Emulator.prototype.bindKeyInput = function(el) {
  var keyFuncs = {
    8: _set_joyp_select,
    13: _set_joyp_start,
    37: _set_joyp_left,
    38: _set_joyp_up,
    39: _set_joyp_right,
    40: _set_joyp_down,
    88: _set_joyp_A,
    90: _set_joyp_B,
  };

  var nop = function() {};
  var makeKeyFunc = function(isKeyDown) {
    return (function(event) {
      (keyFuncs[event.keyCode] || nop)(this.e, isKeyDown);
      event.preventDefault();
    });
  };

  var keyDown = makeKeyFunc(true).bind(this);
  var keyUp = makeKeyFunc(false).bind(this);
  el.addEventListener('keydown', keyDown);
  el.addEventListener('keyup', keyUp);

  this.defer(function() {
    el.removeEventListener('keydown', keyDown);
    el.removeEventListener('keyup', keyUp);
  });
};

Emulator.prototype.run = function() {
  this.rafCancelToken = requestAnimationFrame(this.renderVideo.bind(this));
  this.defer(function() { cancelAnimationFrame(this.rafCancelToken); });
};

Emulator.prototype.getCycles = function() {
  return _emulator_get_cycles(this.e) >>> 0;
};

function lerp(from, to, alpha) {
  return (alpha * from) + (1 - alpha) * to;
}

Emulator.prototype.renderVideo = function(startMs) {
  this.rafCancelToken = requestAnimationFrame(this.renderVideo.bind(this));

  var startSec = startMs / 1000;
  var deltaSec = Math.max(startSec - (this.lastSec || startSec), 0);
  var startCycles = this.getCycles();
  var deltaCycles = Math.min(deltaSec, MAX_UPDATE_SEC) * CPU_CYCLES_PER_SECOND;
  var runUntilCycles = (startCycles + deltaCycles - this.leftoverCycles) >>> 0;
  while (true) {
    var event = _emulator_run_until(this.e, runUntilCycles);
    if (event & EVENT_NEW_FRAME) {
      this.renderer.uploadTexture(this.frameBuffer);
    }
    if (event & EVENT_AUDIO_BUFFER_FULL) {
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
  this.leftoverCycles = (this.getCycles() - runUntilCycles) | 0;
  this.lastSec = startSec;
  this.fps = lerp(this.fps, Math.min(1 / deltaSec, 10000), 0.3);
  this.renderer.renderTexture();
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
