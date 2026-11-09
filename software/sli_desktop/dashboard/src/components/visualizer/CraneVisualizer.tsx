import { useRef, useEffect, useState } from "react";
import type { TelemetryFrame } from "../../types";
import { loadStatus } from "../telemetry/loadStatus";
import { useTheme } from "../../theme";
import { BOOM_BASE_MM, BOOM_HEAD_MM, hookArmMm } from "../../geometry";

/** "#RRGGBB" + alpha -> rgba() (canvas needs real colours, not CSS variables). */
function alpha(hex: string, a: number): string {
  const m = /^#?([0-9a-f]{6})$/i.exec(hex.trim());
  if (!m) return hex;
  const n = parseInt(m[1], 16);
  return `rgba(${(n >> 16) & 255}, ${(n >> 8) & 255}, ${n & 255}, ${a})`;
}

/** Theme tokens read at draw time (index.css). */
function palette() {
  const css = getComputedStyle(document.documentElement);
  const t = (n: string) => css.getPropertyValue(n).trim();
  return {
    bg: t("--surface"), s2: t("--surface-2"), s3: t("--surface-3"),
    grid: t("--chart-grid"), line: t("--line"), lineStrong: t("--line-strong"),
    text: t("--text"), text2: t("--text-2"), text3: t("--text-3"),
    warn: t("--warn"), crit: t("--crit"),
  };
}

interface CraneVisualizerProps {
  frame: TelemetryFrame | null;
}

/**
 * Industrial-grade 2D Crane Boom & Telemetry Visualizer
 *
 * Features:
 * - Dynamic container observation (ResizeObserver) for crisp, non-blurry, uncropped rendering
 * - High-DPI (Retina) scaling support
 * - Mathematically bounded coordinate system: boom & envelopes NEVER clip
 * - Turntable base with counterweight & slew pivot
 * - Hydraulic luffing cylinder connecting superstructure to boom
 * - Telescoping boom sections with extension readout
 * - Wire rope with industrial crane hook block
 * - Angle arc with bold readout
 * - Operating radius indicator line
 * - Safe load envelope arc (color-coded by alarm status)
 */
export function CraneVisualizer({ frame }: CraneVisualizerProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [dimensions, setDimensions] = useState({ width: 480, height: 320 });
  const theme = useTheme();

  // Dynamically observe container dimensions
  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;

    const apply = (width: number, height: number) => {
      if (width > 50 && height > 50) {
        setDimensions({ width: Math.floor(width), height: Math.floor(height) });
      }
    };
    // Measure once now: the observer's first callback can be late (hidden tab)
    const r = el.getBoundingClientRect();
    apply(r.width, r.height);

    const ro = new ResizeObserver(entries => {
      for (const entry of entries) apply(entry.contentRect.width, entry.contentRect.height);
    });

    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  // Draw scene
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const { width: W, height: H } = dimensions;
    const dpr = window.devicePixelRatio || 1;

    // Adjust canvas resolution for crisp lines on high-DPI
    canvas.width = Math.round(W * dpr);
    canvas.height = Math.round(H * dpr);
    canvas.style.width = `${W}px`;
    canvas.style.height = `${H}px`;

    ctx.save();
    ctx.scale(dpr, dpr);

    // Telemetry values
    const boomAngle = Math.max(0, Math.min(85, frame?.boomAngle ?? 0)); // no data: draw it lowered, never a made-up pose
    const extMM = Math.max(0, frame?.extensionMM ?? 0);
    // Hanging length (load-cell end to hook tip); older firmware has no hookDrop
    const ropeLength = Math.max(100, frame?.hookDrop || frame?.ropeLength || 0);
    const alarmLevel = frame?.alarmLevel ?? 0;
    const actualLoad = frame?.actualLoad ?? 0;
    const safeLimit = frame?.safeLoadLimit ?? 5.0;

    // --- Palette: neutral structure; only WARN / CUT colour the boom (ISA-101) ---
    const P = palette();
    const statusColor = alarmLevel >= 2 ? P.crit : alarmLevel === 1 ? P.warn : P.text2;
    const cyanAccent = P.text2;

    // --- Layout Geometry ---
    // Pivot positioned at right-center so boom elevates and reaches toward the left
    const pivX = Math.round(W * 0.78);
    const pivY = Math.round(H * 0.82);
    const groundY = pivY + 20;

    // Physical dimensions & coordinate scaling
    const BASE_BOOM_MM = BOOM_BASE_MM;   // drawing scale: the fixed boom tube (geometry.ts)
    const maxAllowedBoom = Math.min(pivX - 32, pivY - 26);

    // Constant Base Boom = 22.5 cm (occupies ~64% of available boom space)
    const baseBoomLen = Math.round(maxAllowedBoom * 0.64);
    const pxPerMM = baseBoomLen / BASE_BOOM_MM;

    // Extension in mm (clamped positive for rendering; capped to stay inside canvas)
    const extClampedMM = Math.max(0, extMM);
    const maxExtAllowedPx = maxAllowedBoom - baseBoomLen;
    const extPx = Math.min(extClampedMM * pxPerMM, maxExtAllowedPx);
    const currentBoomLen = baseBoomLen + extPx;

    const angleRad = (boomAngle * Math.PI) / 180;

    // Base Boom endpoint (FIXED constant 22.5cm where outer sleeve ends and collar sits)
    const baseEndX = pivX - Math.cos(angleRad) * baseBoomLen;
    const baseEndY = pivY - Math.sin(angleRad) * baseBoomLen;

    // Outer Tip of extending section (where pulley sheave & wire rope attach)
    const boomEndX = pivX - Math.cos(angleRad) * currentBoomLen;
    const boomEndY = pivY - Math.sin(angleRad) * currentBoomLen;

    // Clear background
    ctx.fillStyle = P.bg;
    ctx.fillRect(0, 0, W, H);

    // --- Engineering CAD Grid ---
    ctx.strokeStyle = P.grid;
    ctx.lineWidth = 1;
    const gridSize = 28;
    for (let x = 0; x <= W; x += gridSize) {
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, H);
      ctx.stroke();
    }
    for (let y = 0; y <= H; y += gridSize) {
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(W, y);
      ctx.stroke();
    }

    // --- Ground Line & Bed ---
    ctx.strokeStyle = P.lineStrong;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(15, groundY);
    ctx.lineTo(W - 15, groundY);
    ctx.stroke();

    // Ground hatch marks
    ctx.strokeStyle = P.grid;
    ctx.lineWidth = 1;
    for (let hx = 20; hx < W - 20; hx += 16) {
      ctx.beginPath();
      ctx.moveTo(hx, groundY);
      ctx.lineTo(hx - 10, groundY + 10);
      ctx.stroke();
    }

    // --- Safe Working Envelope Arc ---
    ctx.save();
    ctx.strokeStyle = statusColor;
    ctx.lineWidth = 1.5;
    ctx.setLineDash([4, 4]);
    ctx.globalAlpha = 0.35;
    ctx.beginPath();
    ctx.arc(pivX, pivY, currentBoomLen, Math.PI, Math.PI + (85 * Math.PI) / 180);
    ctx.stroke();

    // Subtle radial sector fill
    ctx.globalAlpha = 0.04;
    ctx.fillStyle = statusColor;
    ctx.beginPath();
    ctx.moveTo(pivX, pivY);
    ctx.arc(pivX, pivY, currentBoomLen, Math.PI, Math.PI + (85 * Math.PI) / 180);
    ctx.closePath();
    ctx.fill();
    ctx.restore();

    // --- Radius Dimension Line ---
    const currentTotalM = hookArmMm(extClampedMM) / 1000;   // radius to the hook, as in the telemetry panel
    const radMeters = (currentTotalM * Math.cos(angleRad)).toFixed(2);
    const radiusPx = pivX - boomEndX;
    if (radiusPx > 30) {
      ctx.save();
      ctx.strokeStyle = alpha(P.text2, 0.5);
      ctx.lineWidth = 1;
      ctx.setLineDash([3, 3]);

      // Vertical drop line from boom tip to radius line
      ctx.beginPath();
      ctx.moveTo(boomEndX, boomEndY);
      ctx.lineTo(boomEndX, groundY + 18);
      ctx.stroke();

      // Vertical line under pivot
      ctx.beginPath();
      ctx.moveTo(pivX, groundY);
      ctx.lineTo(pivX, groundY + 18);
      ctx.stroke();

      // Horizontal dimension line with arrows
      ctx.setLineDash([]);
      ctx.strokeStyle = alpha(P.text2, 0.85);
      const dimY = groundY + 14;
      ctx.beginPath();
      ctx.moveTo(boomEndX + 4, dimY);
      ctx.lineTo(pivX - 4, dimY);
      ctx.stroke();

      // Radius text badge
      ctx.fillStyle = cyanAccent;
      ctx.font = "600 12px JetBrains Mono, monospace";
      ctx.textAlign = "center";
      ctx.fillText(`R: ${radMeters}m`, (boomEndX + pivX) / 2, dimY - 4);
      ctx.restore();
    }

    // --- Crane Pedestal & Machinery House ---
    // Outrigger/chassis beam
    ctx.fillStyle = P.s2;
    ctx.strokeStyle = P.line;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.roundRect(pivX - 48, groundY - 11, 96, 11, [2, 2, 0, 0]);
    ctx.fill();
    ctx.stroke();

    // Counterweight block (rear, to the right)
    ctx.fillStyle = P.s3;
    ctx.strokeStyle = P.lineStrong;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.roundRect(pivX + 8, pivY - 14, 38, 25, 3);
    ctx.fill();
    ctx.stroke();

    // Hazard stripes on counterweight
    ctx.strokeStyle = alpha(P.text3, 0.6);
    ctx.lineWidth = 2;
    for (let cs = pivX + 14; cs < pivX + 42; cs += 8) {
      ctx.beginPath();
      ctx.moveTo(cs, pivY + 9);
      ctx.lineTo(cs + 6, pivY - 12);
      ctx.stroke();
    }

    // Turntable Slewing Pedestal
    ctx.fillStyle = P.s2;
    ctx.strokeStyle = alpha(P.text3, 0.7);
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(pivX - 25, groundY - 11);
    ctx.lineTo(pivX + 25, groundY - 11);
    ctx.lineTo(pivX + 18, pivY - 2);
    ctx.lineTo(pivX - 18, pivY - 2);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();

    // --- Hydraulic Luffing Cylinder ---
    // Cylinder anchor on chassis
    const cylAnchorX = pivX - 26;
    const cylAnchorY = groundY - 11;
    // Cylinder rod attaches to the fixed base boom at ~38% of base length
    const cylBoomAttachDist = baseBoomLen * 0.38;
    const cylAttachX = pivX - Math.cos(angleRad) * cylBoomAttachDist;
    const cylAttachY = pivY - Math.sin(angleRad) * cylBoomAttachDist;

    const cylDx = cylAttachX - cylAnchorX;
    const cylDy = cylAttachY - cylAnchorY;

    // Barrel (lower 55%)
    const barrelRatio = 0.55;
    const barrelEndX = cylAnchorX + cylDx * barrelRatio;
    const barrelEndY = cylAnchorY + cylDy * barrelRatio;

    // Chrome rod
    ctx.strokeStyle = P.text2;
    ctx.lineWidth = 5;
    ctx.lineCap = "round";
    ctx.beginPath();
    ctx.moveTo(cylAnchorX, cylAnchorY);
    ctx.lineTo(cylAttachX, cylAttachY);
    ctx.stroke();

    // Outer barrel
    ctx.strokeStyle = P.lineStrong;
    ctx.lineWidth = 10;
    ctx.lineCap = "butt";
    ctx.beginPath();
    ctx.moveTo(cylAnchorX, cylAnchorY);
    ctx.lineTo(barrelEndX, barrelEndY);
    ctx.stroke();

    ctx.strokeStyle = alpha(P.text2, 0.6);
    ctx.lineWidth = 1.5;
    ctx.stroke();

    // Cylinder pivot pin
    ctx.fillStyle = cyanAccent;
    ctx.beginPath();
    ctx.arc(cylAnchorX, cylAnchorY, 3.5, 0, Math.PI * 2);
    ctx.fill();

    // --- Boom Structure (Base Boom 22.5cm + Extending Section) ---
    const boomWidth = 16;
    const innerWidth = 10;

    // 1. Extendable inner boom section (slides inside base boom and extends out past baseEndX)
    const innerInsideDist = baseBoomLen * 0.45;
    const inStartX = pivX - Math.cos(angleRad) * innerInsideDist;
    const inStartY = pivY - Math.sin(angleRad) * innerInsideDist;

    ctx.save();
    // Inner boom metallic body (precision chrome / alloy)
    ctx.strokeStyle = P.text;
    ctx.lineWidth = innerWidth;
    ctx.lineCap = "square";
    ctx.beginPath();
    ctx.moveTo(inStartX, inStartY);
    ctx.lineTo(boomEndX, boomEndY);
    ctx.stroke();

    // Subtle metallic chrome core
    ctx.strokeStyle = P.text;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(
      inStartX - Math.sin(angleRad) * 2,
      inStartY + Math.cos(angleRad) * 2
    );
    ctx.lineTo(
      boomEndX - Math.sin(angleRad) * 2,
      boomEndY + Math.cos(angleRad) * 2
    );
    ctx.stroke();

    // Millimeter graduation markings along the exposed extending section
    if (extPx > 6) {
      ctx.strokeStyle = alpha(P.bg, 0.65);
      ctx.lineWidth = 1;
      const exposedDist = currentBoomLen - baseBoomLen;
      const tickSpacingPx = Math.max(7, pxPerMM * 10);
      for (let s = 6; s < exposedDist - 4; s += tickSpacingPx) {
        const d = baseBoomLen + s;
        const tx = pivX - Math.cos(angleRad) * d;
        const ty = pivY - Math.sin(angleRad) * d;
        const perp = angleRad + Math.PI / 2;
        ctx.beginPath();
        ctx.moveTo(tx + Math.cos(perp) * 4, ty - Math.sin(perp) * 4);
        ctx.lineTo(tx - Math.cos(perp) * 4, ty + Math.sin(perp) * 4);
        ctx.stroke();
      }

      // Extension callout badge floating above the extended portion
      if (extClampedMM >= 5) {
        const calloutD = baseBoomLen + (exposedDist * 0.5);
        const perp = angleRad + Math.PI / 2;
        const calloutX = pivX - Math.cos(angleRad) * calloutD + Math.cos(perp) * 16;
        const calloutY = pivY - Math.sin(angleRad) * calloutD - Math.sin(perp) * 16;

        ctx.fillStyle = alpha(P.bg, 0.9);
        ctx.strokeStyle = cyanAccent;
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.roundRect(calloutX - 25, calloutY - 8, 50, 16, 3);
        ctx.fill();
        ctx.stroke();

        ctx.fillStyle = cyanAccent;
        ctx.font = "bold 11px JetBrains Mono, monospace";
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";
        ctx.fillText(`+${extClampedMM.toFixed(0)}mm`, calloutX, calloutY);
      }
    }
    ctx.restore();

    // 2. Base Boom Section (Outer tube — FIXED 22.5 cm from pivot to baseEnd)
    ctx.save();
    // Base boom shadow / glow
    ctx.shadowColor = statusColor;
    ctx.shadowBlur = 0;
    ctx.strokeStyle = statusColor;
    ctx.lineWidth = boomWidth;
    ctx.lineCap = "round";
    ctx.beginPath();
    ctx.moveTo(pivX, pivY);
    ctx.lineTo(baseEndX, baseEndY);
    ctx.stroke();
    ctx.restore();

    // Solid inner core of outer boom
    ctx.strokeStyle = P.line;
    ctx.lineWidth = boomWidth - 4;
    ctx.lineCap = "round";
    ctx.beginPath();
    ctx.moveTo(pivX, pivY);
    ctx.lineTo(baseEndX, baseEndY);
    ctx.stroke();

    // Stencil label on the fixed base boom: "22.5cm"
    ctx.save();
    ctx.fillStyle = P.text3;
    ctx.font = "bold 11px JetBrains Mono, monospace";
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    const midBaseX = pivX - Math.cos(angleRad) * (baseBoomLen * 0.62);
    const midBaseY = pivY - Math.sin(angleRad) * (baseBoomLen * 0.62);
    ctx.translate(midBaseX, midBaseY);
    ctx.rotate(angleRad > Math.PI / 2 ? angleRad - Math.PI : -angleRad);
    ctx.fillText("22.5cm BASE", 0, 0);
    ctx.restore();

    // 3. Collar Bushing / Seal at Base Boom Tip (where extending section emerges)
    ctx.save();
    ctx.strokeStyle = cyanAccent;
    ctx.lineWidth = boomWidth + 4;
    ctx.lineCap = "butt";
    ctx.beginPath();
    ctx.moveTo(baseEndX + Math.cos(angleRad) * 4, baseEndY + Math.sin(angleRad) * 4);
    ctx.lineTo(baseEndX, baseEndY);
    ctx.stroke();

    ctx.strokeStyle = P.text;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(baseEndX, baseEndY);
    ctx.lineTo(baseEndX - Math.cos(angleRad) * 1, baseEndY - Math.sin(angleRad) * 1);
    ctx.stroke();
    ctx.restore();

    // 4. Boom Point Sheave / Head (Fixed to the tip of the extending telescope!)
    ctx.fillStyle = cyanAccent;
    ctx.beginPath();
    ctx.arc(boomEndX, boomEndY, 7.5, 0, Math.PI * 2);
    ctx.fill();

    ctx.fillStyle = P.bg;
    ctx.beginPath();
    ctx.arc(boomEndX, boomEndY, 3, 0, Math.PI * 2);
    ctx.fill();

    // --- Wire Rope & Hook Block ---
    // Rope length clamped to fit visually
    const maxVisualRope = Math.max(30, groundY - boomEndY - 20);
    const ropePx = Math.min(Math.max(25, (ropeLength / 1200) * maxVisualRope), maxVisualRope);
    const hookX = boomEndX;
    const hookY = boomEndY + ropePx;

    // Wire rope
    ctx.strokeStyle = P.text2;
    ctx.lineWidth = 1.8;
    ctx.beginPath();
    ctx.moveTo(boomEndX, boomEndY + 4);
    ctx.lineTo(hookX, hookY);
    ctx.stroke();

    // Hook Block & Pulley
    ctx.fillStyle = P.s3;
    ctx.strokeStyle = P.lineStrong;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.roundRect(hookX - 6, hookY, 12, 12, 2.5);
    ctx.fill();
    ctx.stroke();

    // Hook loop & tip
    ctx.strokeStyle = P.text;
    ctx.lineWidth = 2.2;
    ctx.beginPath();
    ctx.arc(hookX, hookY + 16, 6, 0, Math.PI * 1.5);
    ctx.stroke();

    // Load indicator at hook
    const ls = loadStatus(frame);
    const isLoadError = ls !== "ok";
    if (ls === "loadErr") {
      ctx.fillStyle = P.warn;
      ctx.font = "bold 13px JetBrains Mono, monospace";
      ctx.textAlign = "left";
      ctx.fillText("LOAD ERR (>10kg)", hookX + 10, hookY + 12);
    } else if (actualLoad > 0.05) {   // also when outside the chart: the load itself is fine
      ctx.fillStyle = P.text;
      ctx.font = "bold 13px JetBrains Mono, monospace";
      ctx.textAlign = "left";
      ctx.fillText(`${actualLoad.toFixed(2)} kg`, hookX + 10, hookY + 12);
    }

    // --- Main Turntable Pivot Pin ---
    ctx.fillStyle = P.s3;
    ctx.strokeStyle = cyanAccent;
    ctx.lineWidth = 2.5;
    ctx.beginPath();
    ctx.arc(pivX, pivY, 11, 0, Math.PI * 2);
    ctx.fill();
    ctx.stroke();

    ctx.fillStyle = cyanAccent;
    ctx.beginPath();
    ctx.arc(pivX, pivY, 4.5, 0, Math.PI * 2);
    ctx.fill();

    // --- Boom Angle Sector Arc & Badge ---
    const arcRadius = 52;
    ctx.save();
    ctx.strokeStyle = cyanAccent;
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.arc(pivX, pivY, arcRadius, Math.PI, Math.PI + angleRad);
    ctx.stroke();

    // Angle text badge on arc
    const midAngle = Math.PI + angleRad * 0.5;
    const badgeX = pivX + Math.cos(midAngle) * (arcRadius + 20);
    const badgeY = pivY + Math.sin(midAngle) * (arcRadius + 20);

    ctx.fillStyle = alpha(P.bg, 0.9);
    ctx.strokeStyle = alpha(P.text2, 0.5);
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.roundRect(badgeX - 25, badgeY - 10, 50, 20, 4);
    ctx.fill();
    ctx.stroke();

    ctx.fillStyle = P.text;
    ctx.font = "bold 13px JetBrains Mono, monospace";
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillText(`${boomAngle.toFixed(1)}°`, badgeX, badgeY);
    ctx.restore();

    // --- Top Overlay Info Badges ---
    ctx.save();
    const loadPct = isLoadError ? 0 : (safeLimit > 0 ? (actualLoad / safeLimit) * 100 : 0);
    ctx.font = "600 13px Inter, sans-serif";
    ctx.fillStyle = isLoadError ? P.warn : statusColor;
    ctx.textAlign = "left";
    ctx.fillText(
      ls === "outside" ? "● OUTSIDE LOAD CHART (capacity 0)"
        : isLoadError ? "● LOAD SENSOR ERR (>10kg)" : `● SWL: ${safeLimit.toFixed(1)}kg (${loadPct.toFixed(0)}%)`,
      16, 24
    );

    ctx.fillStyle = P.text2;
    ctx.font = "600 12px JetBrains Mono, monospace";
    ctx.fillText(
      `Pivot to hook ${Math.round(hookArmMm(extClampedMM))} mm = ${BOOM_BASE_MM + BOOM_HEAD_MM} to load-cell edge + ${extClampedMM.toFixed(0)} telescope`,
      16, 40
    );
    ctx.restore();

    ctx.restore();
  }, [dimensions, frame, theme]);

  // Both the box and the canvas are absolutely positioned: the canvas's pixel size
  // must never feed back into the size the ResizeObserver measures (in normal flow
  // it could grow the panel but never shrink it, and spilled under the gauges).
  return (
    <div
      ref={containerRef}
      style={{ position: "absolute", inset: 0, overflow: "hidden" }}
    >
      <canvas
        ref={canvasRef}
        style={{ position: "absolute", top: 0, left: 0, display: "block", borderRadius: 6 }}
      />
    </div>
  );
}
