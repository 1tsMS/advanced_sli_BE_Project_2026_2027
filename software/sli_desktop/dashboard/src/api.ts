export const API_BASE = "http://localhost:8000/api";

export interface ApiResult<T> {
  ok: boolean;
  data?: T;
  /** Human-readable error when ok is false */
  error?: string;
}

/**
 * Small fetch wrapper that always resolves (never throws) and turns
 * FastAPI error bodies into a readable message.
 */
export async function apiJson<T>(
  path: string,
  init?: { method?: string; body?: unknown },
): Promise<ApiResult<T>> {
  try {
    const res = await fetch(`${API_BASE}${path}`, {
      method: init?.method ?? (init?.body !== undefined ? "POST" : "GET"),
      headers: init?.body !== undefined ? { "Content-Type": "application/json" } : undefined,
      body: init?.body !== undefined ? JSON.stringify(init.body) : undefined,
    });
    const body = await res.json().catch(() => null);
    if (res.ok) return { ok: true, data: body as T };

    const detail = body?.detail;
    const error =
      typeof detail === "string" ? detail
      : Array.isArray(detail) ? "Invalid values: " + detail.map((d: { msg?: string }) => d.msg).join("; ")
      : `Request failed (${res.status})`;
    return { ok: false, error };
  } catch {
    return { ok: false, error: "Backend offline" };
  }
}
