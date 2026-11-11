/**
 * Generated on-device photography sample. No network, webcam or external assets.
 * Color gradients and high-frequency details make image effects easy to inspect.
 */
export function createSampleScene(): HTMLCanvasElement {
  const canvas = document.createElement("canvas");
  canvas.width = 960;
  canvas.height = 540;
  const ctx = canvas.getContext("2d");
  if (!ctx) return canvas;

  const sky = ctx.createLinearGradient(0, 0, 0, 540);
  sky.addColorStop(0, "#223f70");
  sky.addColorStop(0.45, "#efa878");
  sky.addColorStop(1, "#f9e3ba");
  ctx.fillStyle = sky;
  ctx.fillRect(0, 0, canvas.width, canvas.height);

  // Distant glow provides a useful exposure/highlight reference.
  const glow = ctx.createRadialGradient(735, 158, 20, 735, 158, 230);
  glow.addColorStop(0, "rgba(255,248,208,0.95)");
  glow.addColorStop(0.45, "rgba(255,218,162,0.28)");
  glow.addColorStop(1, "rgba(255,218,162,0)");
  ctx.fillStyle = glow;
  ctx.fillRect(480, 0, 480, 390);
  ctx.fillStyle = "#fff0b8";
  ctx.beginPath();
  ctx.arc(735, 158, 50, 0, Math.PI * 2);
  ctx.fill();

  function mountain(context: CanvasRenderingContext2D, points: number[], color: string) {
    context.fillStyle = color;
    context.beginPath();
    context.moveTo(points[0], points[1]);
    for (let i = 2; i < points.length; i += 2) context.lineTo(points[i], points[i + 1]);
    context.closePath();
    context.fill();
  }
  mountain(ctx, [0, 355, 95, 235, 205, 345, 315, 198, 440, 344, 600, 240, 760, 355, 960, 270, 960, 540, 0, 540], "#48647c");
  mountain(ctx, [0, 425, 165, 328, 310, 414, 500, 295, 700, 405, 840, 310, 960, 390, 960, 540, 0, 540], "#244c5b");
  mountain(ctx, [0, 483, 195, 432, 405, 475, 620, 396, 780, 470, 960, 408, 960, 540, 0, 540], "#18333f");

  // Color patches help visually verify saturation, grayscale and warmth.
  const patches = ["#df5657", "#eebd60", "#55b7a4", "#627ed3", "#e7dae2"];
  patches.forEach((color, index) => {
    ctx.fillStyle = color;
    ctx.fillRect(42 + index * 68, 445, 53, 53);
  });
  ctx.strokeStyle = "rgba(255,255,255,0.45)";
  ctx.lineWidth = 2;
  ctx.strokeRect(30, 432, 368, 79);

  // Fine details provide a reference for perceived clarity.
  ctx.strokeStyle = "rgba(255,255,255,0.70)";
  ctx.lineWidth = 1;
  for (let i = 0; i < 17; i++) {
    ctx.beginPath();
    ctx.moveTo(500 + i * 6, 485);
    ctx.lineTo(500 + i * 6, 512);
    ctx.stroke();
  }
  ctx.font = "600 18px Segoe UI, Arial, sans-serif";
  ctx.fillStyle = "#ffffff";
  ctx.fillText("CAMDOCTOR / GENERATED SAMPLE", 36, 44);
  ctx.font = "14px Segoe UI, Arial, sans-serif";
  ctx.fillStyle = "rgba(255,255,255,0.83)";
  ctx.fillText("Try filters without camera permission", 36, 68);

  return canvas;
}
