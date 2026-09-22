export const HEADER_SIZE = 16;
export const TRAINER_SIZE = 512;
export const PRG_BANK_SIZE = 16 * 1024;
export const CHR_BANK_SIZE = 8 * 1024;
export const MAX_INES_SIZE = HEADER_SIZE + TRAINER_SIZE + 32 * 1024 + CHR_BANK_SIZE;

export type InesMetadata = {
  mapper: number;
  prgKib: 16 | 32;
  chrKib: 8;
  mirroring: "horizontal" | "vertical";
  trainer: boolean;
};

export class InesError extends Error {
  constructor(message: string, readonly code: string) {
    super(message);
    this.name = "InesError";
  }
}

export function parseSupportedInes(bytes: Uint8Array): InesMetadata {
  if (bytes.byteLength < HEADER_SIZE) {
    throw new InesError("File is shorter than the 16-byte iNES header.", "short_header");
  }
  if (bytes[0] !== 0x4e || bytes[1] !== 0x45 || bytes[2] !== 0x53 || bytes[3] !== 0x1a) {
    throw new InesError("Missing iNES NES 1A signature.", "bad_magic");
  }

  const prgBanks = bytes[4];
  const chrBanks = bytes[5];
  const flags6 = bytes[6];
  const flags7 = bytes[7];
  const mapper = (flags6 >>> 4) | (flags7 & 0xf0);
  const trainer = (flags6 & 0x04) !== 0;

  if ((flags7 & 0x0c) === 0x08) {
    throw new InesError("NES 2.0 images are not supported by Rev A.", "nes2_unsupported");
  }
  if (mapper !== 0) {
    throw new InesError(`Mapper ${mapper} is not supported by the public Rev A firmware.`, "mapper_unsupported");
  }
  if (prgBanks !== 1 && prgBanks !== 2) {
    throw new InesError("PRG must be 16 KiB or 32 KiB.", "prg_geometry");
  }
  if (chrBanks !== 1) {
    throw new InesError("CHR must be exactly 8 KiB.", "chr_geometry");
  }
  if ((flags6 & 0x08) !== 0) {
    throw new InesError("Four-screen mirroring is not supported.", "four_screen_unsupported");
  }

  const expected = HEADER_SIZE + (trainer ? TRAINER_SIZE : 0) + prgBanks * PRG_BANK_SIZE + CHR_BANK_SIZE;
  if (bytes.byteLength !== expected) {
    throw new InesError(`Unexpected file length: expected ${expected}, got ${bytes.byteLength}.`, "length_mismatch");
  }

  return {
    mapper,
    prgKib: (prgBanks * 16) as 16 | 32,
    chrKib: 8,
    mirroring: (flags6 & 0x01) !== 0 ? "vertical" : "horizontal",
    trainer,
  };
}

export function crc32Hex(bytes: Uint8Array): string {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
    }
  }
  return ((crc ^ 0xffffffff) >>> 0).toString(16).padStart(8, "0");
}

export async function sha256Hex(bytes: Uint8Array): Promise<string> {
  const digest = await crypto.subtle.digest("SHA-256", Uint8Array.from(bytes).buffer);
  return [...new Uint8Array(digest)].map((value) => value.toString(16).padStart(2, "0")).join("");
}
