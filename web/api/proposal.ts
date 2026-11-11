import { DEFAULT_EFFECTS, type ImageEffects } from "../src/types";

type ValidationResult =
  | { valid: true; effects: Partial<ImageEffects> }
  | { valid: false; error: string };

export function validateEffectProposal(value: unknown): ValidationResult {
  if (typeof value !== "object" || value === null || Array.isArray(value)) {
    return { valid: false, error: "Tool arguments must be a JSON object." };
  }
  const record = value as Record<string, unknown>;
  const allowed = new Set(["grayscale", "brightness", "contrast", "saturation", "warmth"]);
  if (Object.keys(record).some((key) => !allowed.has(key)) || Object.keys(record).length === 0) {
    return { valid: false, error: "The proposal contains unknown or empty effect arguments." };
  }

  const effects: Partial<ImageEffects> = {};
  if ("grayscale" in record) {
    if (typeof record.grayscale !== "boolean") {
      return { valid: false, error: "Grayscale must be true or false." };
    }
    effects.grayscale = record.grayscale;
  }
  const limits: Record<"brightness" | "contrast" | "saturation" | "warmth", [number, number]> = {
    brightness: [-100, 100],
    contrast: [50, 150],
    saturation: [0, 200],
    warmth: [-100, 100],
  };
  for (const [name, range] of Object.entries(limits) as Array<
    [keyof typeof limits, [number, number]]
  >) {
    if (!(name in record)) continue;
    const numeric = record[name];
    if (typeof numeric !== "number" || !Number.isFinite(numeric)
      || numeric < range[0] || numeric > range[1]) {
      return { valid: false, error: `${name} is outside its supported range.` };
    }
    effects[name] = numeric;
  }
  return { valid: true, effects };
}

export const initialEffectValues = DEFAULT_EFFECTS;
