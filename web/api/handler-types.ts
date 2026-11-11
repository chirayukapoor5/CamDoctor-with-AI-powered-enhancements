import type { IncomingMessage, ServerResponse } from "node:http";

export interface ApiRequest extends IncomingMessage {
  body: unknown;
}

export interface ApiResponse extends ServerResponse {
  status(code: number): this;
  json(body: unknown): this;
}
