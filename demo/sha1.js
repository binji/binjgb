// Direct port of the C code in https://tools.ietf.org/html/rfc3174.
function SHA1() {
  this.lengthLow = 0;
  this.lengthHigh = 0;
  this.messageBlockIndex = 0;
  this.messageBlock = new Uint8Array(64);
  this.intermediateHash = new Uint32Array(
      [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0]);
  this.computed = false;
  this.corrupted = false;
}

SHA1.prototype.result = function() {
  if (this.corrupted) return null;
  if (!this.computed) {
    this.padMessage();
    for (let i = 0; i < 64; ++i) {
      this.messageBlock[i] = 0;
    }
    this.lengthLow = 0;
    this.lengthHigh = 0;
    this.computed = true;
  }
  let messageDigest = '';
  const sha1HashSize = 20;
  for (let i = 0; i < sha1HashSize; ++i) {
    let byte = this.intermediateHash[i >> 2] >>> (8 * (3 - (i & 3)));
    messageDigest += ('00' + byte.toString(16)).slice(-2);
  }
  return messageDigest;
};

SHA1.prototype.input = function(messageArray) {
  if (!messageArray.length) return;
  if (this.computed) {
    this.corrupted = true;
    return;
  }
  if (this.corrupted) return;
  let length = messageArray.length;
  let i = 0;
  while (length-- && !this.corrupted) {
    this.messageBlock[this.messageBlockIndex++] = messageArray[i++] & 0xff;
    this.lengthLow = (this.lengthLow + 8) >>> 0;
    if (this.lengthLow == 0) {
      this.lengthHigh = (this.lengthHigh + 1) >>> 0;
      if (this.lengthHigh == 0) {
        this.corrupted = true;
      }
    }
    if (this.messageBlockIndex == 64) {
      this.processMessageBlock();
    }
  }
};

SHA1.prototype.processMessageBlock = function() {
  function circularShift(bits, word) {
    return ((word << bits) | (word >>> (32 - bits))) >>> 0;
  }
  const K = [0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6];
  let temp, A, B, C, D, E;
  let W = new Uint32Array(80);
  for (let t = 0; t < 16; ++t) {
    W[t] = this.messageBlock[t * 4] << 24;
    W[t] |= this.messageBlock[t * 4 + 1] << 16;
    W[t] |= this.messageBlock[t * 4 + 2] << 8;
    W[t] |= this.messageBlock[t * 4 + 3];
  }
  for (let t = 16; t < 80; ++t) {
    W[t] = circularShift(1, W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16]);
  }
  A = this.intermediateHash[0];
  B = this.intermediateHash[1];
  C = this.intermediateHash[2];
  D = this.intermediateHash[3];
  E = this.intermediateHash[4];
  for (let t = 0; t < 20; ++t) {
    temp = (circularShift(5, A) + ((B & C) | (~B & D)) + E + W[t] + K[0]) >>> 0;
    E = D;
    D = C;
    C = circularShift(30, B);
    B = A;
    A = temp;
  }
  for (let t = 20; t < 40; ++t) {
    temp = (circularShift(5, A) + (B ^ C ^ D) + E + W[t] + K[1]) >>> 0;
    E = D;
    D = C;
    C = circularShift(30, B);
    B = A;
    A = temp;
  }
  for (let t = 40; t < 60; ++t) {
    temp = (circularShift(5, A) + ((B & C) | (B & D) | (C & D)) + E + W[t] +
            K[2]) >>>
           0;
    E = D;
    D = C;
    C = circularShift(30, B);
    B = A;
    A = temp;
  }
  for (let t = 60; t < 80; ++t) {
    temp = (circularShift(5, A) + (B ^ C ^ D) + E + W[t] + K[3]) >>> 0;
    E = D;
    D = C;
    C = circularShift(30, B);
    B = A;
    A = temp;
  }
  this.intermediateHash[0] += A;
  this.intermediateHash[1] += B;
  this.intermediateHash[2] += C;
  this.intermediateHash[3] += D;
  this.intermediateHash[4] += E;
  this.messageBlockIndex = 0;
};

SHA1.prototype.padMessage = function() {
  if (this.messageBlockIndex > 55) {
    this.messageBlock[this.messageBlockIndex++] = 0x80;
    while (this.messageBlockIndex < 64) {
      this.messageBlock[this.messageBlockIndex++] = 0;
    }
    this.processMessageBlock();
    while (this.messageBlockIndex < 56) {
      this.messageBlock[this.messageBlockIndex++] = 0;
    }
  } else {
    this.messageBlock[this.messageBlockIndex++] = 0x80;
    while (this.messageBlockIndex < 56) {
      this.messageBlock[this.messageBlockIndex++] = 0;
    }
  }
  this.messageBlock[56] = this.lengthHigh >>> 24;
  this.messageBlock[57] = this.lengthHigh >>> 16;
  this.messageBlock[58] = this.lengthHigh >>> 8;
  this.messageBlock[59] = this.lengthHigh;
  this.messageBlock[60] = this.lengthLow >>> 24;
  this.messageBlock[61] = this.lengthLow >>> 16;
  this.messageBlock[62] = this.lengthLow >>> 8;
  this.messageBlock[63] = this.lengthLow;
  this.processMessageBlock();
};

function SHA1Digest(buffer) {
  let sha1 = new SHA1();
  sha1.input(new Uint8Array(buffer));
  return sha1.result();
}
