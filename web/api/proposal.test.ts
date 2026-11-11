import { describe, expect, it } from "vitest";
import { validateEffectProposal } from "./proposal";

describe("AI proposal validation", () => {
  it("accepts bounded effect calls", () => {
    expect(validateEffectProposal({ grayscale: true, warmth: 24, contrast: 115 })).toEqual({
      valid: true,
      effects: { grayscale: true, warmth: 24, contrast: 115 },
    });
  });

  it("rejects unknown, malformed, and out-of-range tool arguments", () => {
    expect(validateEffectProposal({ command: "capture" }).valid).toBe(false);
    expect(validateEffectProposal({ grayscale: "yes" }).valid).toBe(false);
    expect(validateEffectProposal({ brightness: 101 }).valid).toBe(false);
    expect(validateEffectProposal({ warmth: Number.NaN }).valid).toBe(false);
  });

  it("does not accept an empty adjustment", () => {
    expect(validateEffectProposal({}).valid).toBe(false);
  });
});
