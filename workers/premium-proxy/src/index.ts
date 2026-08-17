interface Env {
  SERVICE_NAME: string;
  PROXY_URL_SECRET: string;
  DISPATCHARR_PROXY_SECRET: string;
  UPSTREAM_M3U_URL: string;
  UPSTREAM_EPG_URL: string;
  ACTIVATION_CODES: KVNamespace;
}

const TEXT_HEADERS = {
  "Access-Control-Allow-Origin": "*",
  "Cache-Control": "private, max-age=60",
  "X-Content-Type-Options": "nosniff"
};

const SIGNED_TARGET_TTL_SECONDS = 6 * 60 * 60;

class HttpError extends Error {
  constructor(public status: number, message: string) {
    super(message);
  }
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    try {
      if (request.method === "OPTIONS") return new Response(null, { headers: TEXT_HEADERS });
      if (request.method !== "GET") throw new HttpError(405, "Method not allowed");

      const url = new URL(request.url);
      if (url.pathname === "/" || url.pathname === "/health") {
        return json({ ok: true, service: env.SERVICE_NAME });
      }

      const sessionToken = await requirePremiumSession(request, url, env);

      if (url.pathname === "/premium/m3u") return proxyM3u(request, env, sessionToken);
      if (url.pathname === "/premium/epg") return proxyEpg(request, env, sessionToken);
      if (url.pathname === "/premium/stream") return proxyBinary(request, url, env);
      if (url.pathname === "/premium/asset") return proxyBinary(request, url, env);

      throw new HttpError(404, "Not found");
    } catch (error) {
      if (error instanceof HttpError) return json({ error: error.message }, error.status);
      console.error(error);
      return json({ error: "Internal server error" }, 500);
    }
  }
};

async function proxyM3u(request: Request, env: Env, sessionToken: string): Promise<Response> {
  const upstream = await fetch(env.UPSTREAM_M3U_URL, {
    headers: upstreamHeaders(request, env)
  });
  if (!upstream.ok) throw new HttpError(502, `Upstream M3U failed: ${upstream.status}`);

  const publicUrl = new URL(request.url);
  const playlist = await rewriteM3u(await upstream.text(), publicUrl, env, sessionToken);
  return new Response(playlist, {
    headers: {
      ...TEXT_HEADERS,
      "Content-Type": "audio/x-mpegurl; charset=utf-8"
    }
  });
}

async function proxyEpg(request: Request, env: Env, sessionToken: string): Promise<Response> {
  const upstream = await fetch(env.UPSTREAM_EPG_URL, {
    headers: upstreamHeaders(request, env)
  });
  if (!upstream.ok) throw new HttpError(502, `Upstream EPG failed: ${upstream.status}`);

  const publicUrl = new URL(request.url);
  const epg = await rewriteXmltvIcons(await upstream.text(), publicUrl, env, sessionToken);
  return new Response(epg, {
    headers: {
      ...TEXT_HEADERS,
      "Cache-Control": "private, max-age=300",
      "Content-Type": "application/xml; charset=utf-8"
    }
  });
}

async function proxyBinary(request: Request, url: URL, env: Env): Promise<Response> {
  const target = await openTarget(url.searchParams.get("u"), env);
  if (!target) throw new HttpError(400, "Missing target");

  const upstream = await fetch(target, {
    headers: upstreamHeaders(request, env),
    redirect: "follow"
  });
  if (!upstream.ok) throw new HttpError(502, `Upstream asset failed: ${upstream.status}`);

  const headers = new Headers(upstream.headers);
  headers.set("Access-Control-Allow-Origin", "*");
  headers.set("Cache-Control", "private, max-age=60");
  headers.set("X-Content-Type-Options", "nosniff");
  headers.delete("Set-Cookie");
  return new Response(upstream.body, { status: upstream.status, headers });
}

async function rewriteM3u(source: string, publicUrl: URL, env: Env, sessionToken: string): Promise<string> {
  const logoMatches = [...source.matchAll(/\btvg-logo="([^"]+)"/g)];
  const logoUrls = new Map<string, string>();
  for (const match of logoMatches) {
    logoUrls.set(match[1], await proxyUrl(publicUrl, "/premium/asset", match[1], env, sessionToken));
  }

  const lines = await Promise.all(source
    .split(/\r?\n/)
    .map(async (line) => {
      const trimmed = line.trim();
      if (!trimmed || trimmed.startsWith("#")) return line;
      if (!/^https?:\/\//i.test(trimmed)) return line;
      return proxyUrl(publicUrl, "/premium/stream", trimmed, env, sessionToken);
    }));

  return lines
    .join("\n")
    .replace(/\b(x-tvg-url|url-tvg)="https?:\/\/[^"]+"/g, (_match, attr: string) => `${attr}="${publicUrl.origin}/premium/epg?session=${encodeURIComponent(sessionToken)}"`)
    .replace(/\btvg-logo="([^"]+)"/g, (_match, logo: string) => `tvg-logo="${logoUrls.get(logo) ?? ""}"`);
}

async function rewriteXmltvIcons(source: string, publicUrl: URL, env: Env, sessionToken: string): Promise<string> {
  const matches = [...source.matchAll(/\bsrc="(https?:\/\/[^"]+)"/g)];
  const replacements = new Map<string, string>();
  for (const match of matches) {
    replacements.set(match[1], await proxyUrl(publicUrl, "/premium/asset", match[1], env, sessionToken));
  }
  return source.replace(/\bsrc="(https?:\/\/[^"]+)"/g, (_match, icon: string) => `src="${replacements.get(icon) ?? ""}"`);
}

async function proxyUrl(publicUrl: URL, pathname: string, target: string, env: Env, sessionToken: string): Promise<string> {
  const next = new URL(publicUrl.origin);
  next.pathname = pathname;
  next.searchParams.set("u", await sealTarget(target, env));
  next.searchParams.set("session", sessionToken);
  return next.toString();
}

async function requirePremiumSession(request: Request, url: URL, env: Env): Promise<string> {
  const auth = request.headers.get("Authorization") ?? "";
  const bearer = auth.startsWith("Bearer ") ? auth.slice("Bearer ".length) : "";
  const sessionToken = bearer || url.searchParams.get("session") || "";
  if (!/^[A-Za-z0-9_-]{32,}$/.test(sessionToken)) throw new HttpError(401, "Unauthorized");

  const session = await env.ACTIVATION_CODES.get<PremiumSessionRecord>(`session:${sessionToken}`, "json");
  const now = Math.floor(Date.now() / 1000);
  if (!session || session.status !== "active" || session.expiresAt <= now) throw new HttpError(401, "Unauthorized");
  return sessionToken;
}

function upstreamHeaders(request: Request, env: Env): Headers {
  const headers = new Headers();
  const range = request.headers.get("Range");
  if (range) headers.set("Range", range);
  headers.set("User-Agent", "PocketTV-Premium-Proxy/0.1");
  if (env.DISPATCHARR_PROXY_SECRET) headers.set("X-PocketTV-Proxy-Secret", env.DISPATCHARR_PROXY_SECRET);
  return headers;
}

async function sealTarget(target: string, env: Env): Promise<string> {
  const now = Math.floor(Date.now() / 1000);
  const payload = JSON.stringify({ u: target, exp: now + SIGNED_TARGET_TTL_SECONDS });
  const iv = crypto.getRandomValues(new Uint8Array(12));
  const key = await urlSecretKey(env);
  const ciphertext = new Uint8Array(await crypto.subtle.encrypt({ name: "AES-GCM", iv }, key, new TextEncoder().encode(payload)));
  return `${base64Url(iv)}.${base64Url(ciphertext)}`;
}

async function openTarget(value: string | null, env: Env): Promise<string | null> {
  if (!value || !value.includes(".")) return null;
  const [ivEncoded, ciphertextEncoded] = value.split(".", 2);
  try {
    const key = await urlSecretKey(env);
    const iv = base64UrlDecode(ivEncoded);
    const ciphertext = base64UrlDecode(ciphertextEncoded);
    const plaintext = await crypto.subtle.decrypt({ name: "AES-GCM", iv: exactBuffer(iv) }, key, exactBuffer(ciphertext));
    const payload = JSON.parse(new TextDecoder().decode(plaintext)) as TargetPayload;
    const now = Math.floor(Date.now() / 1000);
    return payload.exp > now && /^https?:\/\//i.test(payload.u) ? payload.u : null;
  } catch {
    return null;
  }
}

async function urlSecretKey(env: Env): Promise<CryptoKey> {
  if (!env.PROXY_URL_SECRET) throw new HttpError(500, "Proxy URL secret is not configured");
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(env.PROXY_URL_SECRET));
  return crypto.subtle.importKey("raw", digest, "AES-GCM", false, ["encrypt", "decrypt"]);
}

function base64Url(data: Uint8Array): string {
  let binary = "";
  data.forEach((byte) => {
    binary += String.fromCharCode(byte);
  });
  return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
}

function base64UrlDecode(value: string): Uint8Array {
  const padded = value.replace(/-/g, "+").replace(/_/g, "/").padEnd(Math.ceil(value.length / 4) * 4, "=");
  const binary = atob(padded);
  const data = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) data[i] = binary.charCodeAt(i);
  return data;
}

function exactBuffer(data: Uint8Array): ArrayBuffer {
  return data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) as ArrayBuffer;
}

function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      ...TEXT_HEADERS,
      "Content-Type": "application/json; charset=utf-8"
    }
  });
}

interface PremiumSessionRecord {
  status: "active" | "canceled" | "past_due";
  activationCode: string;
  stripeCustomerId?: string;
  stripeSubscriptionId?: string;
  createdAt: number;
  expiresAt: number;
}

interface TargetPayload {
  u: string;
  exp: number;
}

interface KVNamespace {
  get<T = string>(key: string, type: "json"): Promise<T | null>;
}
