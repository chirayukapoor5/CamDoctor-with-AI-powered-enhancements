import { expect, test } from "@playwright/test";

test("does not ask for camera permission until the user starts preview", async ({ page }) => {
  await page.addInitScript(() => {
    const mediaDevices = navigator.mediaDevices;
    const original = mediaDevices.getUserMedia.bind(mediaDevices);
    let calls = 0;
    Object.defineProperty(globalThis, "__camdoctorMediaCalls", { get: () => calls });
    Object.defineProperty(mediaDevices, "getUserMedia", {
      configurable: true,
      value: (constraints: MediaStreamConstraints) => {
        calls += 1;
        return original(constraints);
      },
    });
  });
  await page.goto("/");
  expect(await page.evaluate(() => (globalThis as typeof globalThis & { __camdoctorMediaCalls: number }).__camdoctorMediaCalls)).toBe(0);
  await page.getByRole("button", { name: "Try online" }).click();
  await expect(page.getByText("Camera on")).toBeVisible({ timeout: 10_000 });
  expect(await page.evaluate(() => (globalThis as typeof globalThis & { __camdoctorMediaCalls: number }).__camdoctorMediaCalls)).toBe(1);
});

test("shows camera permission denial and no-camera states", async ({ page }) => {
  await page.addInitScript(() => {
    const reason = new URLSearchParams(location.search).get("camera-test");
    Object.defineProperty(navigator.mediaDevices, "getUserMedia", {
      configurable: true,
      value: async () => {
        throw new DOMException("Test camera state", reason === "denied" ? "NotAllowedError" : "NotFoundError");
      },
    });
  });

  await page.goto("/?camera-test=denied");
  await page.getByRole("button", { name: "Try online" }).click();
  await expect(page.locator(".preview-empty p")).toContainText(/Camera permission was denied/);

  await page.goto("/?camera-test=missing");
  await page.getByRole("button", { name: "Try online" }).click();
  await expect(page.locator(".preview-empty p")).toContainText(/No connected camera was found/);
});

test("captures a local webcam frame and applies grayscale, undo, reset, and download", async ({ page }) => {
  const apiRequests: string[] = [];
  page.on("request", (request) => {
    if (request.url().includes("/api/")) apiRequests.push(request.postData() ?? "");
  });
  await page.goto("/");
  await page.getByRole("button", { name: "Try online" }).click();
  await expect(page.getByText("Camera on")).toBeVisible({ timeout: 10_000 });
  await expect(page.getByRole("button", { name: "Capture photo" })).toBeEnabled();

  const checkbox = page.getByRole("checkbox", { name: "Black & white" });
  await checkbox.check();
  await expect(checkbox).toBeChecked();
  await page.getByRole("button", { name: "Undo" }).click();
  await expect(checkbox).not.toBeChecked();

  await checkbox.check();
  await page.getByRole("button", { name: "Capture photo" }).click();
  await expect(page.getByText("CAPTURED PHOTO")).toBeVisible();
  const pixels = await page.locator("canvas").evaluate((element) => {
    const canvas = element as HTMLCanvasElement;
    const context = canvas.getContext("2d");
    return context?.getImageData(Math.floor(canvas.width / 2), Math.floor(canvas.height / 2), 1, 1).data.slice(0, 3) ?? [];
  });
  expect([...pixels]).toHaveLength(3);
  expect(Math.max(...pixels)).toBe(Math.min(...pixels));
  expect(apiRequests).toEqual([]);

  const downloadWaiter = page.waitForEvent("download");
  await page.getByRole("button", { name: "Download photo" }).click();
  const download = await downloadWaiter;
  expect(download.suggestedFilename()).toBe("camdoctor-photo.png");

  await page.getByRole("button", { name: "Reset" }).click();
  await expect(checkbox).not.toBeChecked();
});

test("reports Groq authentication failure without persisting or displaying a key", async ({ page }) => {
  const testKey = "dummy-browser-test-key-never-valid";
  await page.route("**/api/groq-test", async (route) => {
    expect(route.request().postDataJSON()).toEqual({
      apiKey: testKey,
      model: "openai/gpt-oss-20b",
    });
    await route.fulfill({
      status: 401,
      contentType: "application/json",
      body: JSON.stringify({ error: "Groq rejected this API key." }),
    });
  });
  await page.goto("/");
  const keyField = page.getByRole("textbox", { name: "Groq API key" });
  await keyField.fill(testKey);
  await expect(keyField).toHaveAttribute("type", "password");
  await page.getByRole("button", { name: "Test connection" }).click();
  await expect(page.getByRole("alert")).toContainText("Groq rejected this API key.");
  expect(await page.evaluate(() => JSON.stringify({
    local: localStorage,
    session: sessionStorage,
  }))).not.toContain(testKey);
  await keyField.fill("");
  expect(await keyField.inputValue()).toBe("");
});


test("recruiters can try generated sample filters and export without camera permission or Groq", async ({ page }) => {
  await page.addInitScript(() => {
    let calls = 0;
    Object.defineProperty(globalThis, "__sampleMediaCalls", { get: () => calls });
    Object.defineProperty(navigator.mediaDevices, "getUserMedia", {
      configurable: true,
      value: async () => {
        calls += 1;
        throw new Error("Sample mode must not access a real camera");
      },
    });
  });

  const apiRequests: string[] = [];
  page.on("request", (request) => {
    if (request.url().includes("/api/")) apiRequests.push(request.url());
  });

  await page.goto("/");
  await page.getByRole("button", { name: "Try sample scene" }).click();
  await expect(page.getByText("SAMPLE · GENERATED")).toBeVisible();
  await expect(page.getByText(/Generated sample scene/)).toBeVisible();
  await expect(page.getByRole("button", { name: "Download sample" })).toBeVisible();

  const checkbox = page.getByRole("checkbox", { name: "Black & white" });
  await checkbox.check();
  await expect.poll(async () => {
    return page.locator(".preview-stage canvas").evaluate((element) => {
      const canvas = element as HTMLCanvasElement;
      const pixels = canvas.getContext("2d")?.getImageData(100, 140, 1, 1).data;
      if (!pixels) return false;
      return Math.max(pixels[0], pixels[1], pixels[2])
        - Math.min(pixels[0], pixels[1], pixels[2]) <= 1;
    });
  }).toBe(true);

  const download = page.waitForEvent("download");
  await page.getByRole("button", { name: "Download sample" }).click();
  expect((await download).suggestedFilename()).toBe("camdoctor-photo.png");
  await page.getByRole("button", { name: "Undo" }).click();
  await expect(checkbox).not.toBeChecked();

  const calls = await page.evaluate(() =>
    (globalThis as typeof globalThis & { __sampleMediaCalls: number }).__sampleMediaCalls);
  expect(calls).toBe(0);
  expect(apiRequests).toEqual([]);
});
