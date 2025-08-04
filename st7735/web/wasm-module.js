export default class WasmModule {
    imports = {};
    _wasmImports = {
        wasi_snapshot_preview1: {
            fd_write: (fd, iovs, iovs_len, nwritten) => {
                const textDecoder = new TextDecoder("utf-8");
                let written = 0;
                const mem = this.ram;
                for (let i = 0; i < iovs_len; i++) {
                    const ptr = iovs + i * 8;
                    const offset = mem[ptr] | (mem[ptr+1]<<8) | (mem[ptr+2]<<16) | (mem[ptr+3]<<24);
                    const len = mem[ptr+4] | (mem[ptr+5]<<8) | (mem[ptr+6]<<16) | (mem[ptr+7]<<24);
                    const chunk = mem.subarray(offset, offset + len);
                    const string = textDecoder.decode(chunk);
                    written += len;
                    if (string && string !== '\n') {
                        if (fd === 1) {
                            console.log(string);
                        }
                        else if (fd === 2) {
                            console.warn(string);
                        }
                    }
                }
                const nwrittenView = new DataView(mem.buffer);
                nwrittenView.setUint32(nwritten, written, true);
                return 0; // errno success
            },
            fd_seek: (fd, offset_low, offset_high, whence, newOffsetPtr) => {
                // Just pretend seeking succeeded and the new offset is zero
                // WASI expects errno and the new offset (64-bit)
                const mem = new DataView(this.ram);
                // Set *newOffsetPtr = 0 (64-bit little endian)
                mem.setUint32(newOffsetPtr, 0, true);           // lower 32 bits
                mem.setUint32(newOffsetPtr + 4, 0, true);       // upper 32 bits
                return 0; // errno = 0 success
            },
            fd_close: () => 0,
            fd_fdstat_get: () => 0,
            proc_exit: (code) => { throw new Error("WASM exited with code " + code); },
            environ_sizes_get: () => 0,
            environ_get: () => 0,
        },
    }
    async load(wasmFile, memPages) {
        const wasmBuffer = await fetch(wasmFile).then(res => res.arrayBuffer());
        this.imports.memory = new WebAssembly.Memory({ initial: memPages });
        this._wasmImports.env = this.imports;
        const result = await WebAssembly.instantiate(wasmBuffer, this._wasmImports);
        this.instance = result.instance;
        this.module = result.module;
        this.exports = this.instance.exports;
        this.ram = new Uint8Array(this.exports.memory.buffer);
        this.exports.__wasm_call_ctors(); // run C/C++ static initialization
    }
}
