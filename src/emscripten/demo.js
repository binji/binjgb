"use strict";

const AUDIO_FRAMES = 4096;
const MAX_UPDATE_SEC = 0.1;
const CPU_CYCLES_PER_SEC= 4194304;
const EVENT_NEW_FRAME = 1;
const EVENT_AUDIO_BUFFER_FULL = 2;

var $ = document.querySelector.bind(document);
var canvasEl = $('canvas');
var canvas = canvasEl.getContext('2d');
var imageData = canvas.createImageData(canvasEl.width, canvasEl.height);
var audio = new AudioContext();

var e = _new_emulator();
var frameBuffer;
var audioBuffer;
var romData = 0;
var lastSec;
var startAudioSec;
var audioLatencySec;
var leftoverCycles;
var stop = false;
var fps = 0;

$('button').addEventListener('click', function() { stop = true; });

setInterval(function() {
  $('#fps').textContent = fps.toFixed(1);
}, 500);

var keyFuncs = {
  8: _set_joyp_select,
  13: _set_joyp_start,
  37: _set_joyp_left,
  38: _set_joyp_up,
  39: _set_joyp_right,
  40: _set_joyp_down,
  88: _set_joyp_a,
  90: _set_joyp_b,
};

function nop() {}

window.addEventListener('keydown', function(event) {
  (keyFuncs[event.keyCode] || nop)(e, true);
});

window.addEventListener('keyup', function(event) {
  (keyFuncs[event.keyCode] || nop)(e, false);
});

$('#file').addEventListener('change', function(event) {
  var reader = new FileReader();
  reader.onloadend = function() { start(reader.result); }
  reader.readAsArrayBuffer(this.files[0]);
});

function start(data) {
  _free(romData);
  romData = _malloc(data.byteLength);
  HEAPU8.set(new Uint8Array(data), romData, data.byteLength);

  _clear_emulator(e);
  _init_rom_data(e, romData, data.byteLength);
  _init_emulator(e);
  _init_audio_buffer(e, audio.sampleRate, AUDIO_FRAMES);

  frameBuffer = new Uint8Array(
      Module.buffer, _get_frame_buffer_ptr(e), _get_frame_buffer_size(e));
  audioBuffer = new Uint8Array(
      Module.buffer, _get_audio_buffer_ptr(e), _get_audio_buffer_capacity(e));

  stop = false;
  fps = 0;
  lastSec = performance.now() / 1000;
  startAudioSec = audio.currentTime + audioLatencySec;
  audioLatencySec = AUDIO_FRAMES / audio.sampleRate;
  leftoverCycles = 0;
  requestAnimationFrame(renderVideo);
}

function lerp(from, to, alpha) {
  return (alpha * from) + (1 - alpha) * to;
}

function renderVideo(startMs) {
  canvas.putImageData(imageData, 0, 0);
  if (!stop) requestAnimationFrame(renderVideo);

  var startSec = startMs / 1000;
  var deltaSec = Math.max(startSec - lastSec, 0);
  var startCycles = _get_cycles(e);
  var deltaCycles = Math.min(deltaSec, MAX_UPDATE_SEC) * CPU_CYCLES_PER_SEC;
  var runUntilCycles = (startCycles + deltaCycles + leftoverCycles) >>> 0;
  while (((runUntilCycles - _get_cycles(e)) | 0) >= 0) {
    var event = _run_emulator(e, AUDIO_FRAMES);
    if (event & EVENT_NEW_FRAME) {
      imageData.data.set(frameBuffer);
    }
    if (event & EVENT_AUDIO_BUFFER_FULL) {
      var nowAudioSec = audio.currentTime;
      var bufferSec = AUDIO_FRAMES / audio.sampleRate;
      var endAudioSec = startAudioSec + bufferSec;
      if (endAudioSec >= nowAudioSec) {
        var buffer = audio.createBuffer(2, AUDIO_FRAMES, audio.sampleRate);
        var channel0 = buffer.getChannelData(0);
        var channel1 = buffer.getChannelData(1);
        var outPos = 0;
        var inPos = 0;
        for (var i = 0; i < AUDIO_FRAMES; i++) {
          channel0[outPos] = (audioBuffer[inPos] - 128) / 128;
          channel1[outPos] = (audioBuffer[inPos + 1] - 128) / 128;
          outPos++;
          inPos += 2;
        }
        var bufferSource = audio.createBufferSource();
        bufferSource.buffer = buffer;
        bufferSource.connect(audio.destination);
        bufferSource.start(startAudioSec);
        startAudioSec = endAudioSec;
      } else {
        console.log('resetting');
        startAudioSec = nowAudioSec + 2 * audioLatencySec;
      }
    }
  }
  leftoverCycles = runUntilCycles - _get_cycles(e);
  lastSec = startSec;
  fps = lerp(fps, Math.min(1 / deltaSec, 10000), 0.3);
}
