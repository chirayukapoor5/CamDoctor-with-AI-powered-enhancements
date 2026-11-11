export interface ImageEffects {
  grayscale: boolean;
  brightness: number;
  contrast: number;
  saturation: number;
  warmth: number;
}

export interface ProposedAdjustments {
  explanation: string;
  effects: Partial<ImageEffects>;
}

export interface ChatResult {
  explanation: string;
  proposal: ProposedAdjustments | null;
}

export const DEFAULT_EFFECTS: ImageEffects = {
  grayscale: false,
  brightness: 0,
  contrast: 100,
  saturation: 100,
  warmth: 0,
};

export const GROQ_MODELS = [
  { id: "openai/gpt-oss-20b", label: "GPT-OSS 20B (recommended)" },
  { id: "llama-3.3-70b-versatile", label: "Llama 3.3 70B" },
  { id: "llama-3.1-8b-instant", label: "Llama 3.1 8B Instant" },
] as const;
