import { useEffect, useRef } from "react";

interface GaugeProps {
  value: number;
  min?: number;
  max: number;
  label: string;
  unit?: string;
  color?: string;
  size?: number;
  isError?: boolean;
}

/**
 * Circular arc gauge using Canvas 2D.
 * Renders a 240° arc from min to max, colored by value level.
 */
export function Gauge({
  value,
  min = 0,
  max,
  label,
  unit = "",
  color = "#00D4FF",
  size = 90,
  isError = false,
}: GaugeProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const dpr = window.devicePixelRatio || 1;
    canvas.width  = size * dpr;
    canvas.height = size * dpr;
    canvas.style.width  = `${size}px`;
    canvas.style.height = `${size}px`;
    ctx.scale(dpr, dpr);

    const cx = size / 2, cy = size / 2;
    const radius = size * 0.38;
    const startAngle = (135 * Math.PI) / 180;   // 225° on clock → bottom-left
    const totalAngle = (270 * Math.PI) / 180;    // Full sweep = 270°

    const isBidirectional = min < 0;
    const hasError = isError;

    let arcStart = startAngle;
    let arcEnd = startAngle;
    let fillColor = color;

    if (isBidirectional) {
      const midAngle = startAngle + totalAngle * 0.5;
      const absVal = Math.abs(value);
      const absMax = Math.max(Math.abs(min), Math.abs(max));
      const ratio = Math.min(1, absVal / (absMax || 1));

      if (value < 0) {
        arcStart = midAngle - (totalAngle * 0.5) * ratio;
        arcEnd = midAngle;
      } else {
        arcStart = midAngle;
        arcEnd = midAngle + (totalAngle * 0.5) * ratio;
      }

      fillColor = hasError
        ? "#FFB347"
        : ratio > 0.75
        ? "#FF3B3B"
        : ratio > 0.45
        ? "#FFB347"
        : color;
    } else {
      const pct = hasError ? 1.0 : Math.max(0, Math.min((value - min) / (max - min), 1));
      arcStart = startAngle;
      arcEnd = startAngle + totalAngle * pct;

      fillColor = hasError
        ? "#FFB347"
        : pct < 0.6
        ? color
        : pct < 0.85
        ? "#FFB347"
        : "#FF3B3B";
    }

    ctx.clearRect(0, 0, size, size);

    // Track background
    ctx.beginPath();
    ctx.arc(cx, cy, radius, startAngle, startAngle + totalAngle);
    ctx.strokeStyle = "rgba(255,255,255,0.07)";
    ctx.lineWidth = size * 0.1;
    ctx.lineCap = "round";
    ctx.stroke();

    // Center zero indicator notch for bidirectional gauges
    if (isBidirectional) {
      const midAngle = startAngle + totalAngle * 0.5;
      const tx1 = cx + (radius - size * 0.06) * Math.cos(midAngle);
      const ty1 = cy + (radius - size * 0.06) * Math.sin(midAngle);
      const tx2 = cx + (radius + size * 0.06) * Math.cos(midAngle);
      const ty2 = cy + (radius + size * 0.06) * Math.sin(midAngle);
      ctx.beginPath();
      ctx.moveTo(tx1, ty1);
      ctx.lineTo(tx2, ty2);
      ctx.strokeStyle = "rgba(255,255,255,0.25)";
      ctx.lineWidth = 1.5;
      ctx.stroke();
    }

    // Value arc
    if (Math.abs(arcEnd - arcStart) > 0.01) {
      ctx.beginPath();
      ctx.arc(cx, cy, radius, arcStart, arcEnd);
      ctx.strokeStyle = fillColor;
      ctx.lineWidth = size * 0.1;
      ctx.lineCap = "round";
      ctx.stroke();

      // Glow effect
      ctx.beginPath();
      ctx.arc(cx, cy, radius, arcStart, arcEnd);
      ctx.strokeStyle = fillColor;
      ctx.lineWidth = size * 0.18;
      ctx.lineCap = "round";
      ctx.globalAlpha = 0.25;
      ctx.stroke();
      ctx.globalAlpha = 1;
    }

    // Center value text
    ctx.fillStyle = hasError ? "#FFB347" : "#E8EAF0";
    ctx.font = `bold ${size * 0.18}px JetBrains Mono, monospace`;
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    const displayVal = hasError
      ? "ERR"
      : Number.isFinite(value)
      ? Math.abs(value) >= 100
        ? value.toFixed(0)
        : value.toFixed(1)
      : "--";
    ctx.fillText(displayVal, cx, cy - size * 0.03);

    // Unit
    ctx.fillStyle = hasError ? "rgba(255, 179, 71, 0.7)" : "rgba(255,255,255,0.3)";
    ctx.font = `${size * 0.1}px Inter, sans-serif`;
    ctx.fillText(hasError ? "FAULT" : unit, cx, cy + size * 0.14);

    // Label below
    ctx.fillStyle = "rgba(255,255,255,0.35)";
    ctx.font = `600 ${size * 0.1}px Inter, sans-serif`;
    ctx.fillText(label.toUpperCase(), cx, cy + size * 0.38);

  }, [value, min, max, label, unit, color, size, isError]);

  return (
    <canvas
      ref={canvasRef}
      style={{ display: "block" }}
    />
  );
}
