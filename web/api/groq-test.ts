import type { ApiRequest, ApiResponse } from "./handler-types";

const groqModelsUrl = "https://api.groq.com/openai/v1/models";
const groqChatUrl = "https://api.groq.com/openai/v1/chat/completions";
const supportedModels = new Set([
  "openai/gpt-oss-20b",
  "llama-3.3-70b-versatile",
  "llama-3.1-8b-instant",
]);

function error(response: ApiResponse, status: number, message: string) {
  return response.status(status).json({ error: message });
}

export default async function handler(request: ApiRequest, response: ApiResponse) {
  response.setHeader("Cache-Control", "no-store, private");
  if (request.method !== "POST") {
    response.setHeader("Allow", "POST");
    return error(response, 405, "Use POST to test a Groq key.");
  }
  const origin = request.headers.origin;
  const host = request.headers.host;
  if (origin && host) {
    try {
      if (new URL(origin).host !== host) return error(response, 403, "Cross-origin requests are not accepted.");
    } catch {
      return error(response, 403, "Request origin is invalid.");
    }
  }
  const body: unknown = request.body;
  if (typeof body !== "object" || body === null || Array.isArray(body)) {
    return error(response, 400, "Request body is invalid.");
  }
  if (Buffer.byteLength(JSON.stringify(body), "utf8") > 16 * 1024) {
    return error(response, 400, "Request body is invalid or too large.");
  }
  const { apiKey, model } = body as Record<string, unknown>;
  if (typeof apiKey !== "string" || apiKey.trim().length < 20 || apiKey.length > 512) {
    return error(response, 400, "Enter a valid Groq API key.");
  }
  if (typeof model !== "string" || !supportedModels.has(model)) {
    return error(response, 400, "Select a supported Groq model.");
  }

  try {
    const modelsResponse = await fetch(groqModelsUrl, {
      headers: { Authorization: `Bearer ${apiKey}` },
      cache: "no-store",
      signal: AbortSignal.timeout(12000),
    });
    if (!modelsResponse.ok) {
      const status = modelsResponse.status;
      return error(response, status === 401 || status === 403 || status === 429 ? status : 502,
        status === 401 ? "Groq rejected this API key."
          : status === 403 ? "Groq denied access for this key."
            : status === 429 ? "Groq rate limit reached. Try again shortly."
              : "Could not verify the Groq account.");
    }
    const data: unknown = await modelsResponse.json();
    if (typeof data !== "object" || data === null || !("data" in data)
      || !Array.isArray(data.data)) {
      return error(response, 502, "Groq returned an invalid model list.");
    }
    const available = data.data.some((item) =>
      typeof item === "object" && item !== null && "id" in item && item.id === model);
    if (!available) return error(response, 400, "This model is not available to the Groq account.");

    const probe = await fetch(groqChatUrl, {
      method: "POST",
      headers: {
        Authorization: `Bearer ${apiKey}`,
        "Content-Type": "application/json",
      },
      cache: "no-store",
      signal: AbortSignal.timeout(15000),
      body: JSON.stringify({
        model,
        messages: [{ role: "user", content: "Call the supplied verification tool." }],
        tools: [{
          type: "function",
          function: {
            name: "camdoctor_connection_probe",
            description: "Confirms tool calling support.",
            parameters: {
              type: "object",
              properties: {},
              required: [],
              additionalProperties: false,
            },
          },
        }],
        tool_choice: {
          type: "function",
          function: { name: "camdoctor_connection_probe" },
        },
        max_tokens: 32,
        temperature: 0,
      }),
    });
    if (!probe.ok) {
      const status = probe.status;
      return error(response, status === 401 || status === 403 || status === 429 ? status : 502,
        status === 401 ? "Groq rejected this API key."
          : status === 403 ? "Groq denied access for this model."
            : status === 429 ? "Groq rate limit reached. Try again shortly."
              : "The model did not accept the tool-calling check.");
    }
    const probeData: unknown = await probe.json();
    if (typeof probeData !== "object" || probeData === null || !("choices" in probeData)
      || !Array.isArray(probeData.choices) || !probeData.choices[0]
      || typeof probeData.choices[0] !== "object" || !("message" in probeData.choices[0])) {
      return error(response, 502, "The model returned an invalid tool-calling response.");
    }
    const message: unknown = probeData.choices[0].message;
    if (typeof message !== "object" || message === null || !("tool_calls" in message)
      || !Array.isArray(message.tool_calls)
      || !message.tool_calls.some((call: unknown) =>
        typeof call === "object" && call !== null && "function" in call
        && typeof call.function === "object" && call.function !== null
        && "name" in call.function && call.function.name === "camdoctor_connection_probe")) {
      return error(response, 502, "This model did not verify tool-calling support.");
    }
    return response.status(200).json({ message: "Groq authentication and tool calling verified." });
  } catch (reason) {
    const timeout = reason instanceof Error && reason.name === "TimeoutError";
    return error(response, timeout ? 504 : 502,
      timeout ? "The Groq connection test timed out." : "Could not reach Groq.");
  }
}
