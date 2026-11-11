import { afterEach, describe, expect, it, vi } from "vitest";
import type { ApiRequest, ApiResponse } from "./handler-types";
import handler from "./director";
import groqTestHandler from "./groq-test";

const fakeKey = "dummy-unit-test-key-not-a-real-credential";
const model = "openai/gpt-oss-20b";

function createRequest(pathBody: Record<string, unknown>): ApiRequest {
  return {
    method: "POST",
    headers: {
      origin: "https://camdoctor.example",
      host: "camdoctor.example",
    },
    body: pathBody,
  } as unknown as ApiRequest;
}

function createResponse(): ApiResponse & { statusCode: number; body: unknown } {
  const result = {
    statusCode: 200,
    body: undefined as unknown,
    setHeader: vi.fn(),
    status(code: number) {
      this.statusCode = code;
      return this;
    },
    json(value: unknown) {
      this.body = value;
      return this;
    },
  };
  return result as unknown as ApiResponse & { statusCode: number; body: unknown };
}

function groqToolResponse(args: unknown): Response {
  return new Response(JSON.stringify({
    choices: [{
      message: {
        content: "A gentle monochrome treatment is an option.",
        tool_calls: [{
          type: "function",
          function: {
            name: "propose_adjustments",
            arguments: JSON.stringify(args),
          },
        }],
      },
    }],
  }), { status: 200 });
}

afterEach(() => vi.unstubAllGlobals());

describe("Groq server forwarding", () => {
  it("reports authentication rejection without returning or logging the key", async () => {
    const fetchMock = vi.fn().mockResolvedValue(
      new Response(JSON.stringify({ error: "unauthorized" }), { status: 401 }),
    );
    vi.stubGlobal("fetch", fetchMock);
    const response = createResponse();
    await handler(createRequest({ apiKey: fakeKey, model, prompt: "Make it monochrome." }), response);
    expect(response.statusCode).toBe(401);
    expect(JSON.stringify(response.body)).not.toContain(fakeKey);
    expect(fetchMock.mock.calls[0]?.[1]).toMatchObject({
      headers: { Authorization: `Bearer ${fakeKey}` },
      cache: "no-store",
    });
  });

  it("validates tool-call proposals and forwards text only", async () => {
    const fetchMock = vi.fn().mockResolvedValue(groqToolResponse({ grayscale: true, warmth: 101 }));
    vi.stubGlobal("fetch", fetchMock);
    const response = createResponse();
    await handler(createRequest({
      apiKey: fakeKey,
      model,
      prompt: "Make it black and white.",
      image: "should-not-be-forwarded",
      file: "private-local-path",
    }), response);
    expect(response.statusCode).toBe(502);
    expect(JSON.stringify(response.body)).not.toContain(fakeKey);

    fetchMock.mockResolvedValueOnce(groqToolResponse({ grayscale: true, warmth: 20 }));
    const validResponse = createResponse();
    await handler(createRequest({
      apiKey: fakeKey,
      model,
      prompt: "Make it black and white.",
      image: "should-not-be-forwarded",
    }), validResponse);
    expect(validResponse.statusCode).toBe(200);
    expect(validResponse.body).toMatchObject({
      proposal: { effects: { grayscale: true, warmth: 20 } },
    });
    const forwarded = JSON.parse(String(fetchMock.mock.calls[1]?.[1]?.body));
    expect(JSON.stringify(forwarded)).not.toContain("should-not-be-forwarded");
    expect(JSON.stringify(forwarded)).not.toContain(fakeKey);
    expect(forwarded.messages).toEqual([
      expect.objectContaining({ role: "system" }),
      { role: "user", content: "Make it black and white." },
    ]);
  });

  it("verifies model access and tool calling before reporting a connected key", async () => {
    const fetchMock = vi.fn()
      .mockResolvedValueOnce(new Response(JSON.stringify({
        data: [{ id: model }],
      }), { status: 200 }))
      .mockResolvedValueOnce(new Response(JSON.stringify({
        choices: [{
          message: {
            tool_calls: [{
              function: {
                name: "camdoctor_connection_probe",
                arguments: "{}",
              },
            }],
          },
        }],
      }), { status: 200 }));
    vi.stubGlobal("fetch", fetchMock);
    const response = createResponse();
    await groqTestHandler(createRequest({ apiKey: fakeKey, model }), response);
    expect(response.statusCode).toBe(200);
    expect(response.body).toEqual({
      message: "Groq authentication and tool calling verified.",
    });
    expect(fetchMock).toHaveBeenCalledTimes(2);
    expect(JSON.stringify(response.body)).not.toContain(fakeKey);
  });
});
