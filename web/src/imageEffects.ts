import type { ImageEffects } from "./types";

export function processImageData(image: ImageData, effects: ImageEffects): ImageData {
  const output = new ImageData(
    new Uint8ClampedArray(image.data),
    image.width,
    image.height,
  );
  const pixels = output.data;
  const contrast = effects.contrast / 100;
  const saturation = effects.grayscale ? 0 : effects.saturation / 100;
  const warmth = effects.grayscale ? 0 : effects.warmth;
  const brightness = effects.brightness * 2.55;

  for (let index = 0; index < pixels.length; index += 4) {
    const red = pixels[index] ?? 0;
    const green = pixels[index + 1] ?? 0;
    const blue = pixels[index + 2] ?? 0;
    const luminance = red * 0.2126 + green * 0.7152 + blue * 0.0722;
    const adjustedRed = luminance + (red - luminance) * saturation;
    const adjustedGreen = luminance + (green - luminance) * saturation;
    const adjustedBlue = luminance + (blue - luminance) * saturation;

    pixels[index] = (adjustedRed - 127.5) * contrast + 127.5 + brightness + warmth;
    pixels[index + 1] = (adjustedGreen - 127.5) * contrast + 127.5 + brightness;
    pixels[index + 2] = (adjustedBlue - 127.5) * contrast + 127.5 + brightness - warmth;
  }
  return output;
}
