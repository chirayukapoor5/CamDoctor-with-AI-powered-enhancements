import { defineConfig } from "vitest/config";
import react from "@vitejs/plugin-react";
import type { ApiRequest, ApiResponse } from "./api/handler-types";
import directorHandler from "./api/director";
import groqTestHandler from "./api/groq-test";

export default defineConfig({
  plugins: [
    react(),
    {
      name: "camdoctor-vercel-function-dev",
      configureServer(server) {
        const handlers = new Map([
          ["/api/director", directorHandler],
          ["/api/groq-test", groqTestHandler],
        ]);
        server.middlewares.use(async (request, response, next) => {
          const url = new URL(request.url ?? "/", "http://localhost");
          const handler = handlers.get(url.pathname);
          if (!handler) return next();
          const chunks: Buffer[] = [];
          let total = 0;
          for await (const chunk of request) {
            const buffer = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk);
            total += buffer.byteLength;
            if (total > 16 * 1024) {
              response.statusCode = 413;
              response.end(JSON.stringify({ error: "Request body is too large." }));
              return;
            }
            chunks.push(buffer);
          }
          const rawBody = Buffer.concat(chunks).toString("utf8");
          let body: unknown;
          try {
            body = rawBody ? JSON.parse(rawBody) : undefined;
          } catch {
            response.statusCode = 400;
            response.end(JSON.stringify({ error: "Request body is invalid." }));
            return;
          }
          Object.assign(request, { body });
          response.setHeader("Content-Type", "application/json");
          await handler(request as ApiRequest, response as ApiResponse);
        });
      },
    },
  ],
  server: {
    host: "127.0.0.1",
  },
  test: {
    environment: "node",
    include: ["api/**/*.test.ts", "src/**/*.test.ts"],
  },
});
