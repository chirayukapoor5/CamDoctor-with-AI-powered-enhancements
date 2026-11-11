import type { ApiRequest, ApiResponse } from "./handler-types";
import { validateEffectProposal } from "./proposal";

const allowedModels = new Set([
  "openai/gpt-oss-20b",
  "llama-3.3-70b-versatile",
  "llama-3.1-8b-instant",
]);
const groqEndpoint = "https://api.groq.com/openai/v1/chat/completions";
const maximumRequestBytes = 16 * 1024;
const allowedEffects = ["grayscale", "brightness", "contrast", "saturation", "warmth"];

function respondError(response: ApiResponse, status: number, message: string): void {
  response.status(status).json({ error: message });
}

function readBody(value: unknown): Record<string, unknown> | null {
  if (typeof value === "string") {
    if (Buffer.byteLength(value, "utf8") > maximumRequestBytes) return null;
    try {
      const parsed: unknown = JSON.parse(value);
      return typeof parsed === "object" && parsed !== null && !Array.isArray(parsed)
        ? parsed as Record<string, unknown>
        : null;
    } catch {
      return null;
    }
  }
  if (typeof value !== "object" || value === null || Array.isArray(value)) return null;
  if (Buffer.byteLength(JSON.stringify(value), "utf8") > maximumRequestBytes) return null;
  return value as Record<string, unknown>;
}

function safeGroqError(status: number): string {
  if (status === 401) return "Groq rejected this API key. Check it and try again.";
  if (status === 403) return "Groq denied access for this key or model.";
  if (status === 429) return "Groq rate limit reached. Wait briefly and try again.";
  if (status === 400) return "Groq could not accept this model or tool-calling request.";
  return "Groq could not complete the request. Please try again later.";
}

export default async function handler(request: ApiRequest, response: ApiResponse) {
  response.setHeader("Cache-Control", "no-store, private");
  response.setHeader("X-Content-Type-Options", "nosniff");
  if (request.method !== "POST") {
    response.setHeader("Allow", "POST");
    return respondError(response, 405, "Use POST for an AI request.");
  }

  const origin = request.headers.origin;
  const host = request.headers.host;
  if (origin && host) {
    try {
      if (new URL(origin).host !== host) {
        return respondError(response, 403, "Cross-origin requests are not accepted.");
      }
    } catch {
      return respondError(response, 403, "Request origin is invalid.");
    }
  }

  const body = readBody(request.body);
  if (!body) return respondError(response, 400, "Request body is invalid or too large.");
  if (Buffer.byteLength(JSON.stringify(body), "utf8") > maximumRequestBytes) {
    return respondError(response, 400, "Request body is invalid or too large.");
  }
  const apiKey = body.apiKey;
  const model = body.model;
  const prompt = body.prompt;
  if (typeof apiKey !== "string" || apiKey.trim().length < 20 || apiKey.length > 512) {
    return respondError(response, 400, "Enter a valid Groq API key.");
  }
  if (typeof model !== "string" || !allowedModels.has(model)) {
    return respondError(response, 400, "Select a supported Groq model.");
  }
  if (typeof prompt !== "string" || prompt.trim().length < 1 || prompt.length > 1200) {
    return respondError(response, 400, "Enter a request of 1–1200 characters.");
  }

  const tools = [{
    type: "function",
    function: {
      name: "propose_adjustments",
      description: "Suggest optional browser photo effects for the user's review. Never claim objective improvement.",
      parameters: {
        type: "object",
        additionalProperties: false,
        properties: {
          grayscale: { type: "boolean" },
          brightness: { type: "number", minimum: -100, maximum: 100 },
          contrast: { type: "number", minimum: 50, maximum: 150, description: "Percent; neutral is 100." },
          saturation: { type: "number", minimum: 0, maximum: 200, description: "Percent; neutral is 100." },
          warmth: { type: "number", minimum: -100, maximum: 100, description: "Negative is cool, positive is warm." },
        },
      },
    },
  }];
  const systemPrompt = [
    "You are CamDoctor, a cautious photography assistant.",
    "You cannot see the camera or any photo. Do not claim visual inspection, objective improvement, camera hardware changes, or physical diagnoses.",
    "You may use propose_adjustments only to suggest browser image effects. Always explain the suggestion and its limitations.",
    `Allowed effect fields: ${allowedEffects.join(", ")}. Effects apply only after the user approves them.`,
    "The browser reports local camera permission/device status separately; do not infer it.",
  ].join(" ");

  try {
    const groqResponse = await fetch(groqEndpoint, {
      method: "POST",
      headers: {
        Authorization: `Bearer ${apiKey}`,
        "Content-Type": "application/json",
      },
      cache: "no-store",
      signal: AbortSignal.timeout(25000),
      body: JSON.stringify({
        model,
        messages: [
          { role: "system", content: systemPrompt },
          { role: "user", content: prompt.trim() },
        ],
        tools,
        tool_choice: "auto",
        temperature: 0.2,
        max_tokens: 600,
      }),
    });

    if (!groqResponse.ok) {
      const status = groqResponse.status;
      return respondError(response, status === 401 || status === 403 || status === 429 ? status : 502,
        safeGroqError(status));
    }

    const data: unknown = await groqResponse.json();
    if (typeof data !== "object" || data === null || !("choices" in data)
      || !Array.isArray(data.choices) || !data.choices[0]
      || typeof data.choices[0] !== "object" || !("message" in data.choices[0])) {
      return respondError(response, 502, "Groq returned an invalid response.");
    }
    const message = data.choices[0].message;
    if (typeof message !== "object" || message === null) {
      return respondError(response, 502, "Groq returned an invalid response.");
    }
    const record = message as { content?: unknown; tool_calls?: unknown };
    const explanation = typeof record.content === "string" ? record.content.slice(0, 3000) : "";
    let proposal: { effects: Record<string, unknown> } | null = null;
    if (record.tool_calls !== undefined) {
      if (!Array.isArray(record.tool_calls) || record.tool_calls.length > 1) {
        return respondError(response, 502, "Groq returned an unsupported number of adjustment proposals.");
      }
      if (record.tool_calls.length === 1) {
        const call = record.tool_calls[0];
        if (typeof call !== "object" || call === null || !("function" in call)
          || typeof call.function !== "object" || call.function === null
          || !("name" in call.function) || call.function.name !== "propose_adjustments"
          || !("arguments" in call.function) || typeof call.function.arguments !== "string") {
          return respondError(response, 502, "Groq returned an unsupported tool call.");
        }
        let args: unknown;
        try {
          args = JSON.parse(call.function.arguments);
        } catch {
          return respondError(response, 502, "Groq returned invalid adjustment data.");
        }
        const validation = validateEffectProposal(args);
        if (!validation.valid) return respondError(response, 502, validation.error);
        proposal = { effects: validation.effects };
      }
    }
    if (!explanation && !proposal) {
      return respondError(response, 502, "Groq returned no explanation or adjustment proposal.");
    }
    return response.status(200).json({ explanation, proposal });
  } catch (error) {
    const timedOut = error instanceof Error && error.name === "TimeoutError";
    return respondError(response, timedOut ? 504 : 502,
      timedOut ? "The Groq request timed out." : "Could not reach Groq. Check your connection and try again.");
  }
}
