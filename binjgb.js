var Module=typeof Module!=="undefined"?Module:{};var moduleOverrides={};var key;for(key in Module){if(Module.hasOwnProperty(key)){moduleOverrides[key]=Module[key]}}Module["arguments"]=[];Module["thisProgram"]="./this.program";Module["quit"]=(function(status,toThrow){throw toThrow});Module["preRun"]=[];Module["postRun"]=[];var ENVIRONMENT_IS_WEB=false;var ENVIRONMENT_IS_WORKER=false;var ENVIRONMENT_IS_NODE=false;var ENVIRONMENT_IS_SHELL=false;if(Module["ENVIRONMENT"]){if(Module["ENVIRONMENT"]==="WEB"){ENVIRONMENT_IS_WEB=true}else if(Module["ENVIRONMENT"]==="WORKER"){ENVIRONMENT_IS_WORKER=true}else if(Module["ENVIRONMENT"]==="NODE"){ENVIRONMENT_IS_NODE=true}else if(Module["ENVIRONMENT"]==="SHELL"){ENVIRONMENT_IS_SHELL=true}else{throw new Error("Module['ENVIRONMENT'] value is not valid. must be one of: WEB|WORKER|NODE|SHELL.")}}else{ENVIRONMENT_IS_WEB=typeof window==="object";ENVIRONMENT_IS_WORKER=typeof importScripts==="function";ENVIRONMENT_IS_NODE=typeof process==="object"&&typeof require==="function"&&!ENVIRONMENT_IS_WEB&&!ENVIRONMENT_IS_WORKER;ENVIRONMENT_IS_SHELL=!ENVIRONMENT_IS_WEB&&!ENVIRONMENT_IS_NODE&&!ENVIRONMENT_IS_WORKER}if(ENVIRONMENT_IS_NODE){var nodeFS;var nodePath;Module["read"]=function shell_read(filename,binary){var ret;if(!nodeFS)nodeFS=require("fs");if(!nodePath)nodePath=require("path");filename=nodePath["normalize"](filename);ret=nodeFS["readFileSync"](filename);return binary?ret:ret.toString()};Module["readBinary"]=function readBinary(filename){var ret=Module["read"](filename,true);if(!ret.buffer){ret=new Uint8Array(ret)}assert(ret.buffer);return ret};if(process["argv"].length>1){Module["thisProgram"]=process["argv"][1].replace(/\\/g,"/")}Module["arguments"]=process["argv"].slice(2);if(typeof module!=="undefined"){module["exports"]=Module}process["on"]("uncaughtException",(function(ex){if(!(ex instanceof ExitStatus)){throw ex}}));process["on"]("unhandledRejection",(function(reason,p){process["exit"](1)}));Module["inspect"]=(function(){return"[Emscripten Module object]"})}else if(ENVIRONMENT_IS_SHELL){if(typeof read!="undefined"){Module["read"]=function shell_read(f){return read(f)}}Module["readBinary"]=function readBinary(f){var data;if(typeof readbuffer==="function"){return new Uint8Array(readbuffer(f))}data=read(f,"binary");assert(typeof data==="object");return data};if(typeof scriptArgs!="undefined"){Module["arguments"]=scriptArgs}else if(typeof arguments!="undefined"){Module["arguments"]=arguments}if(typeof quit==="function"){Module["quit"]=(function(status,toThrow){quit(status)})}}else if(ENVIRONMENT_IS_WEB||ENVIRONMENT_IS_WORKER){Module["read"]=function shell_read(url){var xhr=new XMLHttpRequest;xhr.open("GET",url,false);xhr.send(null);return xhr.responseText};if(ENVIRONMENT_IS_WORKER){Module["readBinary"]=function readBinary(url){var xhr=new XMLHttpRequest;xhr.open("GET",url,false);xhr.responseType="arraybuffer";xhr.send(null);return new Uint8Array(xhr.response)}}Module["readAsync"]=function readAsync(url,onload,onerror){var xhr=new XMLHttpRequest;xhr.open("GET",url,true);xhr.responseType="arraybuffer";xhr.onload=function xhr_onload(){if(xhr.status==200||xhr.status==0&&xhr.response){onload(xhr.response);return}onerror()};xhr.onerror=onerror;xhr.send(null)};Module["setWindowTitle"]=(function(title){document.title=title})}Module["print"]=typeof console!=="undefined"?console.log.bind(console):typeof print!=="undefined"?print:null;Module["printErr"]=typeof printErr!=="undefined"?printErr:typeof console!=="undefined"&&console.warn.bind(console)||Module["print"];Module.print=Module["print"];Module.printErr=Module["printErr"];for(key in moduleOverrides){if(moduleOverrides.hasOwnProperty(key)){Module[key]=moduleOverrides[key]}}moduleOverrides=undefined;var STACK_ALIGN=16;function staticAlloc(size){assert(!staticSealed);var ret=STATICTOP;STATICTOP=STATICTOP+size+15&-16;return ret}function dynamicAlloc(size){assert(DYNAMICTOP_PTR);var ret=HEAP32[DYNAMICTOP_PTR>>2];var end=ret+size+15&-16;HEAP32[DYNAMICTOP_PTR>>2]=end;if(end>=TOTAL_MEMORY){var success=enlargeMemory();if(!success){HEAP32[DYNAMICTOP_PTR>>2]=ret;return 0}}return ret}function alignMemory(size,factor){if(!factor)factor=STACK_ALIGN;var ret=size=Math.ceil(size/factor)*factor;return ret}function getNativeTypeSize(type){switch(type){case"i1":case"i8":return 1;case"i16":return 2;case"i32":return 4;case"i64":return 8;case"float":return 4;case"double":return 8;default:{if(type[type.length-1]==="*"){return 4}else if(type[0]==="i"){var bits=parseInt(type.substr(1));assert(bits%8===0);return bits/8}else{return 0}}}}function warnOnce(text){if(!warnOnce.shown)warnOnce.shown={};if(!warnOnce.shown[text]){warnOnce.shown[text]=1;Module.printErr(text)}}var jsCallStartIndex=1;var functionPointers=new Array(0);var funcWrappers={};function dynCall(sig,ptr,args){if(args&&args.length){return Module["dynCall_"+sig].apply(null,[ptr].concat(args))}else{return Module["dynCall_"+sig].call(null,ptr)}}var GLOBAL_BASE=1024;var ABORT=0;var EXITSTATUS=0;function assert(condition,text){if(!condition){abort("Assertion failed: "+text)}}function getCFunc(ident){var func=Module["_"+ident];assert(func,"Cannot call unknown function "+ident+", make sure it is exported");return func}var JSfuncs={"stackSave":(function(){stackSave()}),"stackRestore":(function(){stackRestore()}),"arrayToC":(function(arr){var ret=stackAlloc(arr.length);writeArrayToMemory(arr,ret);return ret}),"stringToC":(function(str){var ret=0;if(str!==null&&str!==undefined&&str!==0){var len=(str.length<<2)+1;ret=stackAlloc(len);stringToUTF8(str,ret,len)}return ret})};var toC={"string":JSfuncs["stringToC"],"array":JSfuncs["arrayToC"]};function ccall(ident,returnType,argTypes,args,opts){var func=getCFunc(ident);var cArgs=[];var stack=0;if(args){for(var i=0;i<args.length;i++){var converter=toC[argTypes[i]];if(converter){if(stack===0)stack=stackSave();cArgs[i]=converter(args[i])}else{cArgs[i]=args[i]}}}var ret=func.apply(null,cArgs);if(returnType==="string")ret=Pointer_stringify(ret);else if(returnType==="boolean")ret=Boolean(ret);if(stack!==0){stackRestore(stack)}return ret}function setValue(ptr,value,type,noSafe){type=type||"i8";if(type.charAt(type.length-1)==="*")type="i32";switch(type){case"i1":HEAP8[ptr>>0]=value;break;case"i8":HEAP8[ptr>>0]=value;break;case"i16":HEAP16[ptr>>1]=value;break;case"i32":HEAP32[ptr>>2]=value;break;case"i64":tempI64=[value>>>0,(tempDouble=value,+Math_abs(tempDouble)>=1?tempDouble>0?(Math_min(+Math_floor(tempDouble/4294967296),4294967295)|0)>>>0:~~+Math_ceil((tempDouble- +(~~tempDouble>>>0))/4294967296)>>>0:0)],HEAP32[ptr>>2]=tempI64[0],HEAP32[ptr+4>>2]=tempI64[1];break;case"float":HEAPF32[ptr>>2]=value;break;case"double":HEAPF64[ptr>>3]=value;break;default:abort("invalid type for setValue: "+type)}}var ALLOC_STATIC=2;var ALLOC_NONE=4;function Pointer_stringify(ptr,length){if(length===0||!ptr)return"";var hasUtf=0;var t;var i=0;while(1){t=HEAPU8[ptr+i>>0];hasUtf|=t;if(t==0&&!length)break;i++;if(length&&i==length)break}if(!length)length=i;var ret="";if(hasUtf<128){var MAX_CHUNK=1024;var curr;while(length>0){curr=String.fromCharCode.apply(String,HEAPU8.subarray(ptr,ptr+Math.min(length,MAX_CHUNK)));ret=ret?ret+curr:curr;ptr+=MAX_CHUNK;length-=MAX_CHUNK}return ret}return UTF8ToString(ptr)}var UTF8Decoder=typeof TextDecoder!=="undefined"?new TextDecoder("utf8"):undefined;function UTF8ArrayToString(u8Array,idx){var endPtr=idx;while(u8Array[endPtr])++endPtr;if(endPtr-idx>16&&u8Array.subarray&&UTF8Decoder){return UTF8Decoder.decode(u8Array.subarray(idx,endPtr))}else{var u0,u1,u2,u3,u4,u5;var str="";while(1){u0=u8Array[idx++];if(!u0)return str;if(!(u0&128)){str+=String.fromCharCode(u0);continue}u1=u8Array[idx++]&63;if((u0&224)==192){str+=String.fromCharCode((u0&31)<<6|u1);continue}u2=u8Array[idx++]&63;if((u0&240)==224){u0=(u0&15)<<12|u1<<6|u2}else{u3=u8Array[idx++]&63;if((u0&248)==240){u0=(u0&7)<<18|u1<<12|u2<<6|u3}else{u4=u8Array[idx++]&63;if((u0&252)==248){u0=(u0&3)<<24|u1<<18|u2<<12|u3<<6|u4}else{u5=u8Array[idx++]&63;u0=(u0&1)<<30|u1<<24|u2<<18|u3<<12|u4<<6|u5}}}if(u0<65536){str+=String.fromCharCode(u0)}else{var ch=u0-65536;str+=String.fromCharCode(55296|ch>>10,56320|ch&1023)}}}}function UTF8ToString(ptr){return UTF8ArrayToString(HEAPU8,ptr)}function stringToUTF8Array(str,outU8Array,outIdx,maxBytesToWrite){if(!(maxBytesToWrite>0))return 0;var startIdx=outIdx;var endIdx=outIdx+maxBytesToWrite-1;for(var i=0;i<str.length;++i){var u=str.charCodeAt(i);if(u>=55296&&u<=57343)u=65536+((u&1023)<<10)|str.charCodeAt(++i)&1023;if(u<=127){if(outIdx>=endIdx)break;outU8Array[outIdx++]=u}else if(u<=2047){if(outIdx+1>=endIdx)break;outU8Array[outIdx++]=192|u>>6;outU8Array[outIdx++]=128|u&63}else if(u<=65535){if(outIdx+2>=endIdx)break;outU8Array[outIdx++]=224|u>>12;outU8Array[outIdx++]=128|u>>6&63;outU8Array[outIdx++]=128|u&63}else if(u<=2097151){if(outIdx+3>=endIdx)break;outU8Array[outIdx++]=240|u>>18;outU8Array[outIdx++]=128|u>>12&63;outU8Array[outIdx++]=128|u>>6&63;outU8Array[outIdx++]=128|u&63}else if(u<=67108863){if(outIdx+4>=endIdx)break;outU8Array[outIdx++]=248|u>>24;outU8Array[outIdx++]=128|u>>18&63;outU8Array[outIdx++]=128|u>>12&63;outU8Array[outIdx++]=128|u>>6&63;outU8Array[outIdx++]=128|u&63}else{if(outIdx+5>=endIdx)break;outU8Array[outIdx++]=252|u>>30;outU8Array[outIdx++]=128|u>>24&63;outU8Array[outIdx++]=128|u>>18&63;outU8Array[outIdx++]=128|u>>12&63;outU8Array[outIdx++]=128|u>>6&63;outU8Array[outIdx++]=128|u&63}}outU8Array[outIdx]=0;return outIdx-startIdx}function stringToUTF8(str,outPtr,maxBytesToWrite){return stringToUTF8Array(str,HEAPU8,outPtr,maxBytesToWrite)}function lengthBytesUTF8(str){var len=0;for(var i=0;i<str.length;++i){var u=str.charCodeAt(i);if(u>=55296&&u<=57343)u=65536+((u&1023)<<10)|str.charCodeAt(++i)&1023;if(u<=127){++len}else if(u<=2047){len+=2}else if(u<=65535){len+=3}else if(u<=2097151){len+=4}else if(u<=67108863){len+=5}else{len+=6}}return len}var UTF16Decoder=typeof TextDecoder!=="undefined"?new TextDecoder("utf-16le"):undefined;function demangle(func){return func}function demangleAll(text){var regex=/_Z[\w\d_]+/g;return text.replace(regex,(function(x){var y=demangle(x);return x===y?x:x+" ["+y+"]"}))}function jsStackTrace(){var err=new Error;if(!err.stack){try{throw new Error(0)}catch(e){err=e}if(!err.stack){return"(no stack trace available)"}}return err.stack.toString()}var WASM_PAGE_SIZE=65536;var ASMJS_PAGE_SIZE=16777216;function alignUp(x,multiple){if(x%multiple>0){x+=multiple-x%multiple}return x}var buffer,HEAP8,HEAPU8,HEAP16,HEAPU16,HEAP32,HEAPU32,HEAPF32,HEAPF64;function updateGlobalBuffer(buf){Module["buffer"]=buffer=buf}function updateGlobalBufferViews(){Module["HEAP8"]=HEAP8=new Int8Array(buffer);Module["HEAP16"]=HEAP16=new Int16Array(buffer);Module["HEAP32"]=HEAP32=new Int32Array(buffer);Module["HEAPU8"]=HEAPU8=new Uint8Array(buffer);Module["HEAPU16"]=HEAPU16=new Uint16Array(buffer);Module["HEAPU32"]=HEAPU32=new Uint32Array(buffer);Module["HEAPF32"]=HEAPF32=new Float32Array(buffer);Module["HEAPF64"]=HEAPF64=new Float64Array(buffer)}var STATIC_BASE,STATICTOP,staticSealed;var STACK_BASE,STACKTOP,STACK_MAX;var DYNAMIC_BASE,DYNAMICTOP_PTR;STATIC_BASE=STATICTOP=STACK_BASE=STACKTOP=STACK_MAX=DYNAMIC_BASE=DYNAMICTOP_PTR=0;staticSealed=false;function abortOnCannotGrowMemory(){abort("Cannot enlarge memory arrays. Either (1) compile with  -s TOTAL_MEMORY=X  with X higher than the current value "+TOTAL_MEMORY+", (2) compile with  -s ALLOW_MEMORY_GROWTH=1  which allows increasing the size at runtime, or (3) if you want malloc to return NULL (0) instead of this abort, compile with  -s ABORTING_MALLOC=0 ")}function enlargeMemory(){abortOnCannotGrowMemory()}var TOTAL_STACK=Module["TOTAL_STACK"]||5242880;var TOTAL_MEMORY=Module["TOTAL_MEMORY"]||16777216;if(TOTAL_MEMORY<TOTAL_STACK)Module.printErr("TOTAL_MEMORY should be larger than TOTAL_STACK, was "+TOTAL_MEMORY+"! (TOTAL_STACK="+TOTAL_STACK+")");if(Module["buffer"]){buffer=Module["buffer"]}else{if(typeof WebAssembly==="object"&&typeof WebAssembly.Memory==="function"){Module["wasmMemory"]=new WebAssembly.Memory({"initial":TOTAL_MEMORY/WASM_PAGE_SIZE,"maximum":TOTAL_MEMORY/WASM_PAGE_SIZE});buffer=Module["wasmMemory"].buffer}else{buffer=new ArrayBuffer(TOTAL_MEMORY)}Module["buffer"]=buffer}updateGlobalBufferViews();function getTotalMemory(){return TOTAL_MEMORY}HEAP32[0]=1668509029;HEAP16[1]=25459;if(HEAPU8[2]!==115||HEAPU8[3]!==99)throw"Runtime error: expected the system to be little-endian!";function callRuntimeCallbacks(callbacks){while(callbacks.length>0){var callback=callbacks.shift();if(typeof callback=="function"){callback();continue}var func=callback.func;if(typeof func==="number"){if(callback.arg===undefined){Module["dynCall_v"](func)}else{Module["dynCall_vi"](func,callback.arg)}}else{func(callback.arg===undefined?null:callback.arg)}}}var __ATPRERUN__=[];var __ATINIT__=[];var __ATMAIN__=[];var __ATEXIT__=[];var __ATPOSTRUN__=[];var runtimeInitialized=false;var runtimeExited=false;function preRun(){if(Module["preRun"]){if(typeof Module["preRun"]=="function")Module["preRun"]=[Module["preRun"]];while(Module["preRun"].length){addOnPreRun(Module["preRun"].shift())}}callRuntimeCallbacks(__ATPRERUN__)}function ensureInitRuntime(){if(runtimeInitialized)return;runtimeInitialized=true;callRuntimeCallbacks(__ATINIT__)}function preMain(){callRuntimeCallbacks(__ATMAIN__)}function exitRuntime(){callRuntimeCallbacks(__ATEXIT__);runtimeExited=true}function postRun(){if(Module["postRun"]){if(typeof Module["postRun"]=="function")Module["postRun"]=[Module["postRun"]];while(Module["postRun"].length){addOnPostRun(Module["postRun"].shift())}}callRuntimeCallbacks(__ATPOSTRUN__)}function addOnPreRun(cb){__ATPRERUN__.unshift(cb)}function addOnPostRun(cb){__ATPOSTRUN__.unshift(cb)}function writeArrayToMemory(array,buffer){HEAP8.set(array,buffer)}function writeAsciiToMemory(str,buffer,dontAddNull){for(var i=0;i<str.length;++i){HEAP8[buffer++>>0]=str.charCodeAt(i)}if(!dontAddNull)HEAP8[buffer>>0]=0}var Math_abs=Math.abs;var Math_cos=Math.cos;var Math_sin=Math.sin;var Math_tan=Math.tan;var Math_acos=Math.acos;var Math_asin=Math.asin;var Math_atan=Math.atan;var Math_atan2=Math.atan2;var Math_exp=Math.exp;var Math_log=Math.log;var Math_sqrt=Math.sqrt;var Math_ceil=Math.ceil;var Math_floor=Math.floor;var Math_pow=Math.pow;var Math_imul=Math.imul;var Math_fround=Math.fround;var Math_round=Math.round;var Math_min=Math.min;var Math_max=Math.max;var Math_clz32=Math.clz32;var Math_trunc=Math.trunc;var runDependencies=0;var runDependencyWatcher=null;var dependenciesFulfilled=null;function addRunDependency(id){runDependencies++;if(Module["monitorRunDependencies"]){Module["monitorRunDependencies"](runDependencies)}}function removeRunDependency(id){runDependencies--;if(Module["monitorRunDependencies"]){Module["monitorRunDependencies"](runDependencies)}if(runDependencies==0){if(runDependencyWatcher!==null){clearInterval(runDependencyWatcher);runDependencyWatcher=null}if(dependenciesFulfilled){var callback=dependenciesFulfilled;dependenciesFulfilled=null;callback()}}}Module["preloadedImages"]={};Module["preloadedAudios"]={};var dataURIPrefix="data:application/octet-stream;base64,";function isDataURI(filename){return String.prototype.startsWith?filename.startsWith(dataURIPrefix):filename.indexOf(dataURIPrefix)===0}function integrateWasmJS(){var wasmTextFile="binjgb.wast";var wasmBinaryFile="binjgb.wasm";var asmjsCodeFile="binjgb.temp.asm.js";if(typeof Module["locateFile"]==="function"){if(!isDataURI(wasmTextFile)){wasmTextFile=Module["locateFile"](wasmTextFile)}if(!isDataURI(wasmBinaryFile)){wasmBinaryFile=Module["locateFile"](wasmBinaryFile)}if(!isDataURI(asmjsCodeFile)){asmjsCodeFile=Module["locateFile"](asmjsCodeFile)}}var wasmPageSize=64*1024;var info={"global":null,"env":null,"asm2wasm":{"f64-rem":(function(x,y){return x%y}),"debugger":(function(){debugger})},"parent":Module};var exports=null;function mergeMemory(newBuffer){var oldBuffer=Module["buffer"];if(newBuffer.byteLength<oldBuffer.byteLength){Module["printErr"]("the new buffer in mergeMemory is smaller than the previous one. in native wasm, we should grow memory here")}var oldView=new Int8Array(oldBuffer);var newView=new Int8Array(newBuffer);newView.set(oldView);updateGlobalBuffer(newBuffer);updateGlobalBufferViews()}function fixImports(imports){var ret={};for(var i in imports){var fixed=i;if(fixed[0]=="_")fixed=fixed.substr(1);ret[fixed]=imports[i]}return ret}function getBinary(){try{if(Module["wasmBinary"]){return new Uint8Array(Module["wasmBinary"])}if(Module["readBinary"]){return Module["readBinary"](wasmBinaryFile)}else{throw"on the web, we need the wasm binary to be preloaded and set on Module['wasmBinary']. emcc.py will do that for you when generating HTML (but not JS)"}}catch(err){abort(err)}}function getBinaryPromise(){if(!Module["wasmBinary"]&&(ENVIRONMENT_IS_WEB||ENVIRONMENT_IS_WORKER)&&typeof fetch==="function"){return fetch(wasmBinaryFile,{credentials:"same-origin"}).then((function(response){if(!response["ok"]){throw"failed to load wasm binary file at '"+wasmBinaryFile+"'"}return response["arrayBuffer"]()})).catch((function(){return getBinary()}))}return new Promise((function(resolve,reject){resolve(getBinary())}))}function doNativeWasm(global,env,providedBuffer){if(typeof WebAssembly!=="object"){Module["printErr"]("no native wasm support detected");return false}if(!(Module["wasmMemory"]instanceof WebAssembly.Memory)){Module["printErr"]("no native wasm Memory in use");return false}env["memory"]=Module["wasmMemory"];info["global"]={"NaN":NaN,"Infinity":Infinity};info["global.Math"]=Math;info["env"]=env;function receiveInstance(instance,module){exports=instance.exports;if(exports.memory)mergeMemory(exports.memory);Module["asm"]=exports;Module["usingWasm"]=true;STACKTOP=STACK_BASE+TOTAL_STACK;STACK_MAX=STACK_BASE;Module["asm"]["stackRestore"](STACKTOP);removeRunDependency("wasm-instantiate")}addRunDependency("wasm-instantiate");if(Module["instantiateWasm"]){try{return Module["instantiateWasm"](info,receiveInstance)}catch(e){Module["printErr"]("Module.instantiateWasm callback failed with error: "+e);return false}}function receiveInstantiatedSource(output){receiveInstance(output["instance"],output["module"])}function instantiateArrayBuffer(receiver){getBinaryPromise().then((function(binary){return WebAssembly.instantiate(binary,info)})).then(receiver).catch((function(reason){Module["printErr"]("failed to asynchronously prepare wasm: "+reason);abort(reason)}))}if(!Module["wasmBinary"]&&typeof WebAssembly.instantiateStreaming==="function"&&!isDataURI(wasmBinaryFile)&&typeof fetch==="function"){WebAssembly.instantiateStreaming(fetch(wasmBinaryFile,{credentials:"same-origin"}),info).then(receiveInstantiatedSource).catch((function(reason){Module["printErr"]("wasm streaming compile failed: "+reason);Module["printErr"]("falling back to ArrayBuffer instantiation");instantiateArrayBuffer(receiveInstantiatedSource)}))}else{instantiateArrayBuffer(receiveInstantiatedSource)}return{}}Module["asmPreload"]=Module["asm"];var asmjsReallocBuffer=Module["reallocBuffer"];var wasmReallocBuffer=(function(size){var PAGE_MULTIPLE=Module["usingWasm"]?WASM_PAGE_SIZE:ASMJS_PAGE_SIZE;size=alignUp(size,PAGE_MULTIPLE);var old=Module["buffer"];var oldSize=old.byteLength;if(Module["usingWasm"]){try{var result=Module["wasmMemory"].grow((size-oldSize)/wasmPageSize);if(result!==(-1|0)){return Module["buffer"]=Module["wasmMemory"].buffer}else{return null}}catch(e){return null}}});Module["reallocBuffer"]=(function(size){if(finalMethod==="asmjs"){return asmjsReallocBuffer(size)}else{return wasmReallocBuffer(size)}});var finalMethod="";Module["asm"]=(function(global,env,providedBuffer){env=fixImports(env);if(!env["table"]){var TABLE_SIZE=Module["wasmTableSize"];if(TABLE_SIZE===undefined)TABLE_SIZE=1024;var MAX_TABLE_SIZE=Module["wasmMaxTableSize"];if(typeof WebAssembly==="object"&&typeof WebAssembly.Table==="function"){if(MAX_TABLE_SIZE!==undefined){env["table"]=new WebAssembly.Table({"initial":TABLE_SIZE,"maximum":MAX_TABLE_SIZE,"element":"anyfunc"})}else{env["table"]=new WebAssembly.Table({"initial":TABLE_SIZE,element:"anyfunc"})}}else{env["table"]=new Array(TABLE_SIZE)}Module["wasmTable"]=env["table"]}if(!env["memoryBase"]){env["memoryBase"]=Module["STATIC_BASE"]}if(!env["tableBase"]){env["tableBase"]=0}var exports;exports=doNativeWasm(global,env,providedBuffer);if(!exports)abort("no binaryen method succeeded. consider enabling more options, like interpreting, if you want that: https://github.com/kripken/emscripten/wiki/WebAssembly#binaryen-methods");return exports});}integrateWasmJS();STATIC_BASE=GLOBAL_BASE;STATICTOP=STATIC_BASE+13232;__ATINIT__.push();var STATIC_BUMP=13232;Module["STATIC_BASE"]=STATIC_BASE;Module["STATIC_BUMP"]=STATIC_BUMP;var tempDoublePtr=STATICTOP;STATICTOP+=16;var SYSCALLS={varargs:0,get:(function(varargs){SYSCALLS.varargs+=4;var ret=HEAP32[SYSCALLS.varargs-4>>2];return ret}),getStr:(function(){var ret=Pointer_stringify(SYSCALLS.get());return ret}),get64:(function(){var low=SYSCALLS.get(),high=SYSCALLS.get();if(low>=0)assert(high===0);else assert(high===-1);return low}),getZero:(function(){assert(SYSCALLS.get()===0)})};function ___syscall140(which,varargs){SYSCALLS.varargs=varargs;try{var stream=SYSCALLS.getStreamFromFD(),offset_high=SYSCALLS.get(),offset_low=SYSCALLS.get(),result=SYSCALLS.get(),whence=SYSCALLS.get();var offset=offset_low;FS.llseek(stream,offset,whence);HEAP32[result>>2]=stream.position;if(stream.getdents&&offset===0&&whence===0)stream.getdents=null;return 0}catch(e){if(typeof FS==="undefined"||!(e instanceof FS.ErrnoError))abort(e);return-e.errno}}function flush_NO_FILESYSTEM(){var fflush=Module["_fflush"];if(fflush)fflush(0);var printChar=___syscall146.printChar;if(!printChar)return;var buffers=___syscall146.buffers;if(buffers[1].length)printChar(1,10);if(buffers[2].length)printChar(2,10)}function ___syscall146(which,varargs){SYSCALLS.varargs=varargs;try{var stream=SYSCALLS.get(),iov=SYSCALLS.get(),iovcnt=SYSCALLS.get();var ret=0;if(!___syscall146.buffers){___syscall146.buffers=[null,[],[]];___syscall146.printChar=(function(stream,curr){var buffer=___syscall146.buffers[stream];assert(buffer);if(curr===0||curr===10){(stream===1?Module["print"]:Module["printErr"])(UTF8ArrayToString(buffer,0));buffer.length=0}else{buffer.push(curr)}})}for(var i=0;i<iovcnt;i++){var ptr=HEAP32[iov+i*8>>2];var len=HEAP32[iov+(i*8+4)>>2];for(var j=0;j<len;j++){___syscall146.printChar(stream,HEAPU8[ptr+j])}ret+=len}return ret}catch(e){if(typeof FS==="undefined"||!(e instanceof FS.ErrnoError))abort(e);return-e.errno}}function ___syscall54(which,varargs){SYSCALLS.varargs=varargs;try{return 0}catch(e){if(typeof FS==="undefined"||!(e instanceof FS.ErrnoError))abort(e);return-e.errno}}function ___syscall6(which,varargs){SYSCALLS.varargs=varargs;try{var stream=SYSCALLS.getStreamFromFD();FS.close(stream);return 0}catch(e){if(typeof FS==="undefined"||!(e instanceof FS.ErrnoError))abort(e);return-e.errno}}function __exit(status){Module["exit"](status)}function _exit(status){__exit(status)}function ___setErrNo(value){if(Module["___errno_location"])HEAP32[Module["___errno_location"]()>>2]=value;return value}function _sbrk(increment){increment=increment|0;var oldDynamicTop=0;var newDynamicTop=0;var totalMemory=0;oldDynamicTop=HEAP32[DYNAMICTOP_PTR>>2]|0;newDynamicTop=oldDynamicTop+increment|0;if((increment|0)>0&(newDynamicTop|0)<(oldDynamicTop|0)|(newDynamicTop|0)<0){abortOnCannotGrowMemory()|0;___setErrNo(12);return-1}HEAP32[DYNAMICTOP_PTR>>2]=newDynamicTop;totalMemory=getTotalMemory()|0;if((newDynamicTop|0)>(totalMemory|0)){if((enlargeMemory()|0)==0){HEAP32[DYNAMICTOP_PTR>>2]=oldDynamicTop;___setErrNo(12);return-1}}return oldDynamicTop|0}DYNAMICTOP_PTR=staticAlloc(4);STACK_BASE=STACKTOP=alignMemory(STATICTOP);STACK_MAX=STACK_BASE+TOTAL_STACK;DYNAMIC_BASE=alignMemory(STACK_MAX);HEAP32[DYNAMICTOP_PTR>>2]=DYNAMIC_BASE;staticSealed=true;var ASSERTIONS=false;Module.asmGlobalArg={};Module.asmLibraryArg={"abort":abort,"assert":assert,"enlargeMemory":enlargeMemory,"getTotalMemory":getTotalMemory,"abortOnCannotGrowMemory":abortOnCannotGrowMemory,"flush_NO_FILESYSTEM":flush_NO_FILESYSTEM,"___syscall146":___syscall146,"___syscall54":___syscall54,"___syscall140":___syscall140,"___setErrNo":___setErrNo,"__exit":__exit,"_exit":_exit,"___syscall6":___syscall6,"_sbrk":_sbrk,"STACKTOP":STACKTOP,"STACK_MAX":STACK_MAX,"DYNAMICTOP_PTR":DYNAMICTOP_PTR,"ABORT":ABORT};var asm=Module["asm"](Module.asmGlobalArg,Module.asmLibraryArg,buffer);Module["asm"]=asm;var __malloc=Module["__malloc"]=(function(){return Module["asm"]["_malloc"].apply(null,arguments)});var __set_joyp_start=Module["__set_joyp_start"]=(function(){return Module["asm"]["_set_joyp_start"].apply(null,arguments)});var _memset=Module["_memset"]=(function(){return Module["asm"]["memset"].apply(null,arguments)});var _set_joyp_start=Module["_set_joyp_start"]=(function(){return Module["asm"]["set_joyp_start"].apply(null,arguments)});var ___gttf2=Module["___gttf2"]=(function(){return Module["asm"]["__gttf2"].apply(null,arguments)});var __emulator_set_rewind_joypad_callback=Module["__emulator_set_rewind_joypad_callback"]=(function(){return Module["asm"]["_emulator_set_rewind_joypad_callback"].apply(null,arguments)});var ___fixunstfsi=Module["___fixunstfsi"]=(function(){return Module["asm"]["__fixunstfsi"].apply(null,arguments)});var __get_audio_buffer_ptr=Module["__get_audio_buffer_ptr"]=(function(){return Module["asm"]["_get_audio_buffer_ptr"].apply(null,arguments)});var ___getf2=Module["___getf2"]=(function(){return Module["asm"]["__getf2"].apply(null,arguments)});var _emulator_set_default_joypad_callback=Module["_emulator_set_default_joypad_callback"]=(function(){return Module["asm"]["emulator_set_default_joypad_callback"].apply(null,arguments)});var _ext_ram_file_data_new=Module["_ext_ram_file_data_new"]=(function(){return Module["asm"]["ext_ram_file_data_new"].apply(null,arguments)});var _emulator_read_ext_ram=Module["_emulator_read_ext_ram"]=(function(){return Module["asm"]["emulator_read_ext_ram"].apply(null,arguments)});var _get_audio_buffer_capacity=Module["_get_audio_buffer_capacity"]=(function(){return Module["asm"]["get_audio_buffer_capacity"].apply(null,arguments)});var _emulator_delete=Module["_emulator_delete"]=(function(){return Module["asm"]["emulator_delete"].apply(null,arguments)});var _emulator_new_simple=Module["_emulator_new_simple"]=(function(){return Module["asm"]["emulator_new_simple"].apply(null,arguments)});var __set_joyp_B=Module["__set_joyp_B"]=(function(){return Module["asm"]["_set_joyp_B"].apply(null,arguments)});var _stackAlloc=Module["_stackAlloc"]=(function(){return Module["asm"]["stackAlloc"].apply(null,arguments)});var __set_joyp_right=Module["__set_joyp_right"]=(function(){return Module["asm"]["_set_joyp_right"].apply(null,arguments)});var __emulator_new_simple=Module["__emulator_new_simple"]=(function(){return Module["asm"]["_emulator_new_simple"].apply(null,arguments)});var ___floatsitf=Module["___floatsitf"]=(function(){return Module["asm"]["__floatsitf"].apply(null,arguments)});var __rewind_end=Module["__rewind_end"]=(function(){return Module["asm"]["_rewind_end"].apply(null,arguments)});var __set_joyp_A=Module["__set_joyp_A"]=(function(){return Module["asm"]["_set_joyp_A"].apply(null,arguments)});var __emulator_read_ext_ram=Module["__emulator_read_ext_ram"]=(function(){return Module["asm"]["_emulator_read_ext_ram"].apply(null,arguments)});var __file_data_delete=Module["__file_data_delete"]=(function(){return Module["asm"]["_file_data_delete"].apply(null,arguments)});var __joypad_new=Module["__joypad_new"]=(function(){return Module["asm"]["_joypad_new"].apply(null,arguments)});var ___floatunsitf=Module["___floatunsitf"]=(function(){return Module["asm"]["__floatunsitf"].apply(null,arguments)});var _emulator_was_ext_ram_updated=Module["_emulator_was_ext_ram_updated"]=(function(){return Module["asm"]["emulator_was_ext_ram_updated"].apply(null,arguments)});var __emulator_run_until_f64=Module["__emulator_run_until_f64"]=(function(){return Module["asm"]["_emulator_run_until_f64"].apply(null,arguments)});var _set_joyp_right=Module["_set_joyp_right"]=(function(){return Module["asm"]["set_joyp_right"].apply(null,arguments)});var __ext_ram_file_data_new=Module["__ext_ram_file_data_new"]=(function(){return Module["asm"]["_ext_ram_file_data_new"].apply(null,arguments)});var __rewind_new_simple=Module["__rewind_new_simple"]=(function(){return Module["asm"]["_rewind_new_simple"].apply(null,arguments)});var ___lttf2=Module["___lttf2"]=(function(){return Module["asm"]["__lttf2"].apply(null,arguments)});var _malloc=Module["_malloc"]=(function(){return Module["asm"]["malloc"].apply(null,arguments)});var _rewind_get_oldest_ticks_f64=Module["_rewind_get_oldest_ticks_f64"]=(function(){return Module["asm"]["rewind_get_oldest_ticks_f64"].apply(null,arguments)});var _file_data_delete=Module["_file_data_delete"]=(function(){return Module["asm"]["file_data_delete"].apply(null,arguments)});var dynCall_viii=Module["dynCall_viii"]=(function(){return Module["asm"]["dynCall_viii"].apply(null,arguments)});var ___eqtf2=Module["___eqtf2"]=(function(){return Module["asm"]["__eqtf2"].apply(null,arguments)});var _memcpy=Module["_memcpy"]=(function(){return Module["asm"]["memcpy"].apply(null,arguments)});var _set_joyp_B=Module["_set_joyp_B"]=(function(){return Module["asm"]["set_joyp_B"].apply(null,arguments)});var ___multf3=Module["___multf3"]=(function(){return Module["asm"]["__multf3"].apply(null,arguments)});var _get_audio_buffer_ptr=Module["_get_audio_buffer_ptr"]=(function(){return Module["asm"]["get_audio_buffer_ptr"].apply(null,arguments)});var __set_joyp_select=Module["__set_joyp_select"]=(function(){return Module["asm"]["_set_joyp_select"].apply(null,arguments)});var __emulator_delete=Module["__emulator_delete"]=(function(){return Module["asm"]["_emulator_delete"].apply(null,arguments)});var ___letf2=Module["___letf2"]=(function(){return Module["asm"]["__letf2"].apply(null,arguments)});var _joypad_new=Module["_joypad_new"]=(function(){return Module["asm"]["joypad_new"].apply(null,arguments)});var ___unordtf2=Module["___unordtf2"]=(function(){return Module["asm"]["__unordtf2"].apply(null,arguments)});var _get_frame_buffer_ptr=Module["_get_frame_buffer_ptr"]=(function(){return Module["asm"]["get_frame_buffer_ptr"].apply(null,arguments)});var _emulator_get_ticks_f64=Module["_emulator_get_ticks_f64"]=(function(){return Module["asm"]["emulator_get_ticks_f64"].apply(null,arguments)});var __joypad_delete=Module["__joypad_delete"]=(function(){return Module["asm"]["_joypad_delete"].apply(null,arguments)});var _emulator_run_until_f64=Module["_emulator_run_until_f64"]=(function(){return Module["asm"]["emulator_run_until_f64"].apply(null,arguments)});var __rewind_append=Module["__rewind_append"]=(function(){return Module["asm"]["_rewind_append"].apply(null,arguments)});var _rewind_to_ticks_wrapper=Module["_rewind_to_ticks_wrapper"]=(function(){return Module["asm"]["rewind_to_ticks_wrapper"].apply(null,arguments)});var __rewind_get_newest_ticks_f64=Module["__rewind_get_newest_ticks_f64"]=(function(){return Module["asm"]["_rewind_get_newest_ticks_f64"].apply(null,arguments)});var __get_frame_buffer_size=Module["__get_frame_buffer_size"]=(function(){return Module["asm"]["_get_frame_buffer_size"].apply(null,arguments)});var _set_joyp_select=Module["_set_joyp_select"]=(function(){return Module["asm"]["set_joyp_select"].apply(null,arguments)});var _get_file_data_size=Module["_get_file_data_size"]=(function(){return Module["asm"]["get_file_data_size"].apply(null,arguments)});var ___lshrti3=Module["___lshrti3"]=(function(){return Module["asm"]["__lshrti3"].apply(null,arguments)});var _emulator_write_ext_ram=Module["_emulator_write_ext_ram"]=(function(){return Module["asm"]["emulator_write_ext_ram"].apply(null,arguments)});var dynCall_iiii=Module["dynCall_iiii"]=(function(){return Module["asm"]["dynCall_iiii"].apply(null,arguments)});var ____errno_location=Module["____errno_location"]=(function(){return Module["asm"]["___errno_location"].apply(null,arguments)});var _set_joyp_up=Module["_set_joyp_up"]=(function(){return Module["asm"]["set_joyp_up"].apply(null,arguments)});var ___errno_location=Module["___errno_location"]=(function(){return Module["asm"]["__errno_location"].apply(null,arguments)});var _stackSave=Module["_stackSave"]=(function(){return Module["asm"]["stackSave"].apply(null,arguments)});var _rewind_append=Module["_rewind_append"]=(function(){return Module["asm"]["rewind_append"].apply(null,arguments)});var dynCall_ii=Module["dynCall_ii"]=(function(){return Module["asm"]["dynCall_ii"].apply(null,arguments)});var _rewind_delete=Module["_rewind_delete"]=(function(){return Module["asm"]["rewind_delete"].apply(null,arguments)});var _set_joyp_down=Module["_set_joyp_down"]=(function(){return Module["asm"]["set_joyp_down"].apply(null,arguments)});var ___netf2=Module["___netf2"]=(function(){return Module["asm"]["__netf2"].apply(null,arguments)});var ___subtf3=Module["___subtf3"]=(function(){return Module["asm"]["__subtf3"].apply(null,arguments)});var __emulator_was_ext_ram_updated=Module["__emulator_was_ext_ram_updated"]=(function(){return Module["asm"]["_emulator_was_ext_ram_updated"].apply(null,arguments)});var __set_joyp_left=Module["__set_joyp_left"]=(function(){return Module["asm"]["_set_joyp_left"].apply(null,arguments)});var _rewind_new_simple=Module["_rewind_new_simple"]=(function(){return Module["asm"]["rewind_new_simple"].apply(null,arguments)});var ___addtf3=Module["___addtf3"]=(function(){return Module["asm"]["__addtf3"].apply(null,arguments)});var ___ashlti3=Module["___ashlti3"]=(function(){return Module["asm"]["__ashlti3"].apply(null,arguments)});var ___extenddftf2=Module["___extenddftf2"]=(function(){return Module["asm"]["__extenddftf2"].apply(null,arguments)});var __get_file_data_size=Module["__get_file_data_size"]=(function(){return Module["asm"]["_get_file_data_size"].apply(null,arguments)});var _rewind_end=Module["_rewind_end"]=(function(){return Module["asm"]["rewind_end"].apply(null,arguments)});var __get_frame_buffer_ptr=Module["__get_frame_buffer_ptr"]=(function(){return Module["asm"]["_get_frame_buffer_ptr"].apply(null,arguments)});var _emulator_set_rewind_joypad_callback=Module["_emulator_set_rewind_joypad_callback"]=(function(){return Module["asm"]["emulator_set_rewind_joypad_callback"].apply(null,arguments)});var _set_joyp_A=Module["_set_joyp_A"]=(function(){return Module["asm"]["set_joyp_A"].apply(null,arguments)});var _free=Module["_free"]=(function(){return Module["asm"]["free"].apply(null,arguments)});var __emulator_set_default_joypad_callback=Module["__emulator_set_default_joypad_callback"]=(function(){return Module["asm"]["_emulator_set_default_joypad_callback"].apply(null,arguments)});var _set_joyp_left=Module["_set_joyp_left"]=(function(){return Module["asm"]["set_joyp_left"].apply(null,arguments)});var dynCall_iii=Module["dynCall_iii"]=(function(){return Module["asm"]["dynCall_iii"].apply(null,arguments)});var ___fixtfsi=Module["___fixtfsi"]=(function(){return Module["asm"]["__fixtfsi"].apply(null,arguments)});var _joypad_delete=Module["_joypad_delete"]=(function(){return Module["asm"]["joypad_delete"].apply(null,arguments)});var __rewind_to_ticks_wrapper=Module["__rewind_to_ticks_wrapper"]=(function(){return Module["asm"]["_rewind_to_ticks_wrapper"].apply(null,arguments)});var _get_frame_buffer_size=Module["_get_frame_buffer_size"]=(function(){return Module["asm"]["get_frame_buffer_size"].apply(null,arguments)});var __emulator_write_ext_ram=Module["__emulator_write_ext_ram"]=(function(){return Module["asm"]["_emulator_write_ext_ram"].apply(null,arguments)});var __emulator_get_ticks_f64=Module["__emulator_get_ticks_f64"]=(function(){return Module["asm"]["_emulator_get_ticks_f64"].apply(null,arguments)});var __set_joyp_up=Module["__set_joyp_up"]=(function(){return Module["asm"]["_set_joyp_up"].apply(null,arguments)});var _get_file_data_ptr=Module["_get_file_data_ptr"]=(function(){return Module["asm"]["get_file_data_ptr"].apply(null,arguments)});var __rewind_delete=Module["__rewind_delete"]=(function(){return Module["asm"]["_rewind_delete"].apply(null,arguments)});var __rewind_begin=Module["__rewind_begin"]=(function(){return Module["asm"]["_rewind_begin"].apply(null,arguments)});var dynCall_vii=Module["dynCall_vii"]=(function(){return Module["asm"]["dynCall_vii"].apply(null,arguments)});var __get_audio_buffer_capacity=Module["__get_audio_buffer_capacity"]=(function(){return Module["asm"]["_get_audio_buffer_capacity"].apply(null,arguments)});var __get_file_data_ptr=Module["__get_file_data_ptr"]=(function(){return Module["asm"]["_get_file_data_ptr"].apply(null,arguments)});var _stackRestore=Module["_stackRestore"]=(function(){return Module["asm"]["stackRestore"].apply(null,arguments)});var __free=Module["__free"]=(function(){return Module["asm"]["_free"].apply(null,arguments)});var __set_joyp_down=Module["__set_joyp_down"]=(function(){return Module["asm"]["_set_joyp_down"].apply(null,arguments)});var _rewind_get_newest_ticks_f64=Module["_rewind_get_newest_ticks_f64"]=(function(){return Module["asm"]["rewind_get_newest_ticks_f64"].apply(null,arguments)});var _rewind_begin=Module["_rewind_begin"]=(function(){return Module["asm"]["rewind_begin"].apply(null,arguments)});var __rewind_get_oldest_ticks_f64=Module["__rewind_get_oldest_ticks_f64"]=(function(){return Module["asm"]["_rewind_get_oldest_ticks_f64"].apply(null,arguments)});var stackAlloc=Module["_stackAlloc"];var stackSave=Module["_stackSave"];var stackRestore=Module["_stackRestore"];Module["asm"]=asm;function ExitStatus(status){this.name="ExitStatus";this.message="Program terminated with exit("+status+")";this.status=status}ExitStatus.prototype=new Error;ExitStatus.prototype.constructor=ExitStatus;var initialStackTop;dependenciesFulfilled=function runCaller(){if(!Module["calledRun"])run();if(!Module["calledRun"])dependenciesFulfilled=runCaller};function run(args){args=args||Module["arguments"];if(runDependencies>0){return}preRun();if(runDependencies>0)return;if(Module["calledRun"])return;function doRun(){if(Module["calledRun"])return;Module["calledRun"]=true;if(ABORT)return;ensureInitRuntime();preMain();if(Module["onRuntimeInitialized"])Module["onRuntimeInitialized"]();postRun()}if(Module["setStatus"]){Module["setStatus"]("Running...");setTimeout((function(){setTimeout((function(){Module["setStatus"]("")}),1);doRun()}),1)}else{doRun()}}Module["run"]=run;function exit(status,implicit){if(implicit&&Module["noExitRuntime"]&&status===0){return}if(Module["noExitRuntime"]){}else{ABORT=true;EXITSTATUS=status;STACKTOP=initialStackTop;exitRuntime();if(Module["onExit"])Module["onExit"](status)}if(ENVIRONMENT_IS_NODE){process["exit"](status)}Module["quit"](status,new ExitStatus(status))}Module["exit"]=exit;function abort(what){if(Module["onAbort"]){Module["onAbort"](what)}if(what!==undefined){Module.print(what);Module.printErr(what);what=JSON.stringify(what)}else{what=""}ABORT=true;EXITSTATUS=1;throw"abort("+what+"). Build with -s ASSERTIONS=1 for more info."}Module["abort"]=abort;if(Module["preInit"]){if(typeof Module["preInit"]=="function")Module["preInit"]=[Module["preInit"]];while(Module["preInit"].length>0){Module["preInit"].pop()()}}Module["noExitRuntime"]=true;run()


