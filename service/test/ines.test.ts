import assert from "node:assert/strict";
import test from "node:test";
import { crc32Hex, InesError, parseSupportedInes, sha256Hex } from "../src/ines";

function rom(prgBanks = 2, chrBanks = 1, mapper = 0): Uint8Array {
  const result = new Uint8Array(16 + prgBanks * 16 * 1024 + chrBanks * 8 * 1024);
  result.set([0x4e, 0x45, 0x53, 0x1a, prgBanks, chrBanks, (mapper & 0x0f) << 4, mapper & 0xf0]);
  for (let index = 16; index < result.length; index += 1) result[index] = index & 0xff;
  return result;
}

test("accepts the same mapper-0 geometry as firmware", () => {
  assert.deepEqual(parseSupportedInes(rom(1)), { mapper: 0, prgKib: 16, chrKib: 8, mirroring: "horizontal", trainer: false });
  assert.equal(parseSupportedInes(rom(2)).prgKib, 32);
});

test("rejects unsupported mapper and CHR-RAM", () => {
  assert.throws(() => parseSupportedInes(rom(2, 1, 3)), (error: unknown) => error instanceof InesError && error.code === "mapper_unsupported");
  assert.throws(() => parseSupportedInes(rom(2, 0)), (error: unknown) => error instanceof InesError && error.code === "chr_geometry");
});

test("rejects truncated payloads", () => {
  const bytes = rom(2).slice(0, -1);
  assert.throws(() => parseSupportedInes(bytes), (error: unknown) => error instanceof InesError && error.code === "length_mismatch");
});

test("hash helpers are deterministic", async () => {
  const bytes = new TextEncoder().encode("123456789");
  assert.equal(crc32Hex(bytes), "cbf43926");
  assert.equal(await sha256Hex(bytes), "15e2b0d3c33891ebb0f1ef609ec419420c20e320ce94c65fbc8c3312448eb225");
});
