import { describe, expect, it } from "vitest";
import { processImageData } from "./imageEffects";
import { DEFAULT_EFFECTS } from "./types";

class TestImageData {
  data: Uint8ClampedArray;
  width: number;
  height: number;

  constructor(data: Uint8ClampedArray, width: number, height: number) {
    this.data = data;
    this.width = width;
    this.height = height;
  }
}

Object.defineProperty(globalThis, "ImageData", { configurable: true, value: TestImageData });

function onePixel(red: number, green: number, blue: number): ImageData {
  return new ImageData(new Uint8ClampedArray([red, green, blue, 255]), 1, 1);
}

describe("browser image effects", () => {
  it("converts a colored pixel to deterministic grayscale", () => {
    const result = processImageData(onePixel(200, 80, 20), {
      ...DEFAULT_EFFECTS,
      grayscale: true,
    });
    expect(result.data[0]).toBe(101);
    expect(result.data[1]).toBe(101);
    expect(result.data[2]).toBe(101);
  });

  it("applies brightness, contrast, saturation, and warmth within pixel bounds", () => {
    const result = processImageData(onePixel(100, 100, 100), {
      ...DEFAULT_EFFECTS,
      brightness: 20,
      contrast: 120,
      warmth: 15,
    });
    expect(result.data[0]).toBeGreaterThan(result.data[2] ?? 0);
    expect(result.data[1]).toBeGreaterThan(100);
    expect([...result.data].every((value) => value >= 0 && value <= 255)).toBe(true);
    expect(result.data[3]).toBe(255);
  });

  it("does not mutate the source image", () => {
    const source = onePixel(12, 34, 56);
    processImageData(source, { ...DEFAULT_EFFECTS, brightness: 25 });
    expect([...source.data]).toEqual([12, 34, 56, 255]);
  });
});
