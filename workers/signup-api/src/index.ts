interface Env {
  SERVICE_NAME: string;
  STRIPE_SECRET_KEY: string;
  STRIPE_MONTHLY_PRICE_ID: string;
  STRIPE_WEBHOOK_SECRET: string;
  SUCCESS_URL: string;
  CANCEL_URL: string;
  PREMIUM_PROXY_BASE_URL: string;
  ACTIVATION_CODES: KVNamespace;
}

const CORS_HEADERS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "Content-Type",
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
  "X-Content-Type-Options": "nosniff"
};

const PREMIUM_SESSION_TTL_SECONDS = 6 * 60 * 60;
const ACTIVE_SUBSCRIPTION_STATUSES = new Set(["active", "trialing"]);
const INACTIVE_SUBSCRIPTION_STATUSES = new Set(["canceled", "incomplete_expired", "unpaid", "past_due", "paused"]);

interface HttpError {
  status: number;
  message: string;
}

function httpError(status: number, message: string): HttpError {
  return { status, message };
}

function isHttpError(error: unknown): error is HttpError {
  return typeof error === "object"
    && error !== null
    && "status" in error
    && typeof (error as { status?: unknown }).status === "number"
    && "message" in error
    && typeof (error as { message?: unknown }).message === "string";
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    try {
      if (request.method === "OPTIONS") return new Response(null, { headers: CORS_HEADERS });

      const url = new URL(request.url);
      if (request.method === "GET" && (url.pathname === "/" || url.pathname === "/health")) {
        return json({ ok: true, service: env.SERVICE_NAME });
      }

      if (request.method === "POST" && url.pathname === "/api/create-checkout-session") {
        return await createCheckoutSession(request, env);
      }
      if (request.method === "POST" && url.pathname === "/api/stripe-webhook") {
        return await stripeWebhook(request, env);
      }
      if (request.method === "GET" && url.pathname === "/api/checkout-code") {
        return await checkoutCode(url, env);
      }
      if (request.method === "POST" && url.pathname === "/api/redeem-code") {
        return await redeemCode(request, env);
      }

      throw httpError(404, "Not found");
    } catch (error) {
      if (isHttpError(error)) return json({ error: error.message }, error.status);
      console.error(error);
      return json({ error: "Internal server error" }, 500);
    }
  }
};

async function createCheckoutSession(request: Request, env: Env): Promise<Response> {
  const body = await parseJson(request);
  if (body.plan !== "pockettv-premium-monthly") throw httpError(400, "Unknown plan");
  requireEnv(env.STRIPE_SECRET_KEY, "STRIPE_SECRET_KEY");
  requireEnv(env.STRIPE_MONTHLY_PRICE_ID, "STRIPE_MONTHLY_PRICE_ID");
  requireEnv(env.SUCCESS_URL, "SUCCESS_URL");
  requireEnv(env.CANCEL_URL, "CANCEL_URL");

  const params = new URLSearchParams();
  params.set("mode", "subscription");
  params.set("line_items[0][price]", env.STRIPE_MONTHLY_PRICE_ID);
  params.set("line_items[0][quantity]", "1");
  params.set("subscription_data[trial_period_days]", "7");
  params.set("allow_promotion_codes", "true");
  params.set("billing_address_collection", "auto");
  params.set("success_url", appendCheckoutSessionPlaceholder(env.SUCCESS_URL));
  params.set("cancel_url", env.CANCEL_URL);
  params.set("metadata[plan]", "pockettv-premium-monthly");

  const stripeResponse = await fetch("https://api.stripe.com/v1/checkout/sessions", {
    method: "POST",
    headers: {
      Authorization: `Bearer ${env.STRIPE_SECRET_KEY}`,
      "Content-Type": "application/x-www-form-urlencoded"
    },
    body: params
  });

  const payload = (await stripeResponse.json()) as StripeCheckoutSession | StripeError;
  if (!stripeResponse.ok || !("url" in payload) || !payload.url) {
    const message = "error" in payload ? payload.error.message : "Stripe checkout failed";
    throw httpError(502, message);
  }

  return json({ url: payload.url });
}

async function stripeWebhook(request: Request, env: Env): Promise<Response> {
  requireEnv(env.STRIPE_WEBHOOK_SECRET, "STRIPE_WEBHOOK_SECRET");
  const signature = request.headers.get("Stripe-Signature");
  if (!signature) throw httpError(400, "Missing Stripe signature");

  const rawBody = await request.arrayBuffer();
  const verified = await verifyStripeSignature(signature, rawBody, env.STRIPE_WEBHOOK_SECRET);
  if (!verified) throw httpError(401, "Invalid Stripe signature");

  const event = JSON.parse(new TextDecoder().decode(rawBody)) as StripeEvent;
  if (event.type === "checkout.session.completed") {
    await handleCheckoutCompleted(event.data.object as StripeCheckoutCompletedSession, env);
  } else if (event.type === "customer.subscription.updated" || event.type === "customer.subscription.deleted") {
    await handleSubscriptionChanged(event.data.object as StripeSubscription, env);
  }

  return json({ received: true });
}

async function checkoutCode(url: URL, env: Env): Promise<Response> {
  const sessionId = url.searchParams.get("session_id") ?? "";
  if (!/^cs_(test|live)_[A-Za-z0-9]+/.test(sessionId)) throw httpError(400, "Missing checkout session");

  let code = await env.ACTIVATION_CODES.get(`checkout:${sessionId}`);
  if (!code) {
    const session = await fetchStripeCheckoutSession(sessionId, env);
    if (session.payment_status === "paid" || session.status === "complete") {
      await handleCheckoutCompleted(session, env);
      code = await env.ACTIVATION_CODES.get(`checkout:${sessionId}`);
    }
  }
  if (!code) throw httpError(404, "Activation code is not ready yet");

  const record = await env.ACTIVATION_CODES.get<ActivationCodeRecord>(`code:${code}`, "json");
  if (!record) throw httpError(404, "Activation code is not ready yet");

  return json({
    code,
    status: record.status,
    stripeCustomerId: record.stripeCustomerId,
    stripeSubscriptionId: record.stripeSubscriptionId
  });
}

async function redeemCode(request: Request, env: Env): Promise<Response> {
  const body = await parseJson(request);
  const code = normalizeActivationCode(body.code);
  if (!code) throw httpError(400, "Enter an 8-character activation code");
  requireEnv(env.PREMIUM_PROXY_BASE_URL, "PREMIUM_PROXY_BASE_URL");

  const record = await env.ACTIVATION_CODES.get<ActivationCodeRecord>(`code:${code}`, "json");
  if (!record || record.status !== "active") throw httpError(404, "Activation code not found");

  const sessionToken = cryptoRandom(32);
  const now = Math.floor(Date.now() / 1000);
  const session: PremiumSessionRecord = {
    status: "active",
    activationCode: code,
    stripeCustomerId: record.stripeCustomerId,
    stripeSubscriptionId: record.stripeSubscriptionId,
    createdAt: now,
    expiresAt: now + PREMIUM_SESSION_TTL_SECONDS
  };
  await env.ACTIVATION_CODES.put(`session:${sessionToken}`, JSON.stringify(session), {
    expirationTtl: PREMIUM_SESSION_TTL_SECONDS
  });

  const proxyBase = env.PREMIUM_PROXY_BASE_URL.replace(/\/+$/g, "");
  return json({
    status: "active",
    expiresAt: session.expiresAt,
    m3uUrl: `${proxyBase}/premium/m3u?session=${encodeURIComponent(sessionToken)}`,
    epgUrl: `${proxyBase}/premium/epg?session=${encodeURIComponent(sessionToken)}`
  });
}

async function handleCheckoutCompleted(session: StripeCheckoutCompletedSession, env: Env): Promise<void> {
  const existing = await env.ACTIVATION_CODES.get(`checkout:${session.id}`);
  if (existing) return;

  const subscriptionId = stringValue(session.subscription);
  const customerId = stringValue(session.customer);
  const code = await uniqueActivationCode(env);
  const now = Math.floor(Date.now() / 1000);
  const record: ActivationCodeRecord = {
    status: "active",
    stripeCustomerId: customerId,
    stripeSubscriptionId: subscriptionId,
    createdAt: now,
    updatedAt: now
  };

  await env.ACTIVATION_CODES.put(`code:${code}`, JSON.stringify(record));
  await env.ACTIVATION_CODES.put(`checkout:${session.id}`, code);
  if (subscriptionId) await env.ACTIVATION_CODES.put(`subscription:${subscriptionId}`, code);
  if (customerId) await env.ACTIVATION_CODES.put(`customer:${customerId}`, code);
}

async function fetchStripeCheckoutSession(sessionId: string, env: Env): Promise<StripeCheckoutCompletedSession> {
  requireEnv(env.STRIPE_SECRET_KEY, "STRIPE_SECRET_KEY");
  const response = await fetch(`https://api.stripe.com/v1/checkout/sessions/${encodeURIComponent(sessionId)}`, {
    headers: {
      Authorization: `Bearer ${env.STRIPE_SECRET_KEY}`
    }
  });
  const payload = (await response.json()) as StripeCheckoutCompletedSession | StripeError;
  if (!response.ok || "error" in payload) {
    const message = "error" in payload ? payload.error.message : "Stripe checkout lookup failed";
    throw httpError(502, message);
  }
  return payload;
}

async function handleSubscriptionChanged(subscription: StripeSubscription, env: Env): Promise<void> {
  const code = await env.ACTIVATION_CODES.get(`subscription:${subscription.id}`);
  if (!code) return;

  const record = await env.ACTIVATION_CODES.get<ActivationCodeRecord>(`code:${code}`, "json");
  if (!record) return;

  const status = ACTIVE_SUBSCRIPTION_STATUSES.has(subscription.status)
    ? "active"
    : INACTIVE_SUBSCRIPTION_STATUSES.has(subscription.status)
      ? subscription.status === "past_due" ? "past_due" : "canceled"
      : "past_due";

  const next: ActivationCodeRecord = {
    ...record,
    status,
    stripeCustomerId: stringValue(subscription.customer) || record.stripeCustomerId,
    stripeSubscriptionId: subscription.id,
    updatedAt: Math.floor(Date.now() / 1000)
  };

  await env.ACTIVATION_CODES.put(`code:${code}`, JSON.stringify(next));
}

async function parseJson(request: Request): Promise<{ plan?: string; code?: string }> {
  try {
    return await request.json();
  } catch {
    throw httpError(400, "Invalid JSON");
  }
}

function normalizeActivationCode(value: string | undefined): string {
  if (!value) return "";
  const code = value.toUpperCase().replace(/[^A-Z0-9]/g, "");
  return /^[A-Z0-9]{8}$/.test(code) ? code : "";
}

async function uniqueActivationCode(env: Env): Promise<string> {
  for (let attempt = 0; attempt < 12; attempt++) {
    const code = activationCode();
    const existing = await env.ACTIVATION_CODES.get(`code:${code}`);
    if (!existing) return code;
  }
  throw httpError(500, "Could not allocate activation code");
}

function activationCode(): string {
  const alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  const data = new Uint8Array(8);
  crypto.getRandomValues(data);
  return Array.from(data, (byte) => alphabet[byte % alphabet.length]).join("");
}

function appendCheckoutSessionPlaceholder(successUrl: string): string {
  const separator = successUrl.includes("?") ? "&" : "?";
  return `${successUrl}${separator}session_id={CHECKOUT_SESSION_ID}`;
}

function requireEnv(value: string | undefined, name: string): void {
  if (!value) throw httpError(500, `${name} is not configured`);
}

async function verifyStripeSignature(signature: string, body: ArrayBuffer, secret: string): Promise<boolean> {
  const parts = new Map(signature.split(",").map((part) => {
    const [key, value] = part.split("=", 2);
    return [key, value] as const;
  }));
  const timestamp = parts.get("t");
  const expected = parts.get("v1");
  if (!timestamp || !expected) return false;

  const signedPayload = concatUtf8(`${timestamp}.`, body);
  const key = await crypto.subtle.importKey("raw", new TextEncoder().encode(secret), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
  const mac = await crypto.subtle.sign("HMAC", key, exactBuffer(signedPayload));
  return timingSafeEqual(hex(new Uint8Array(mac)), expected);
}

function concatUtf8(prefix: string, body: ArrayBuffer): Uint8Array {
  const prefixBytes = new TextEncoder().encode(prefix);
  const bodyBytes = new Uint8Array(body);
  const out = new Uint8Array(prefixBytes.length + bodyBytes.length);
  out.set(prefixBytes);
  out.set(bodyBytes, prefixBytes.length);
  return out;
}

function exactBuffer(data: Uint8Array): ArrayBuffer {
  return data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) as ArrayBuffer;
}

function hex(data: Uint8Array): string {
  return Array.from(data, (byte) => byte.toString(16).padStart(2, "0")).join("");
}

function timingSafeEqual(a: string, b: string): boolean {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

function stringValue(value: string | { id?: string } | null | undefined): string | undefined {
  if (!value) return undefined;
  return typeof value === "string" ? value : value.id;
}

function cryptoRandom(bytes: number): string {
  const data = new Uint8Array(bytes);
  crypto.getRandomValues(data);
  return base64Url(data);
}

function base64Url(data: Uint8Array): string {
  let binary = "";
  data.forEach((byte) => {
    binary += String.fromCharCode(byte);
  });
  return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/g, "");
}

function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      ...CORS_HEADERS,
      "Content-Type": "application/json; charset=utf-8"
    }
  });
}

interface StripeCheckoutSession {
  id: string;
  url: string | null;
}

interface StripeError {
  error: {
    message: string;
  };
}

interface StripeEvent {
  id: string;
  type: string;
  data: {
    object: unknown;
  };
}

interface StripeCheckoutCompletedSession {
  id: string;
  customer?: string | { id?: string } | null;
  subscription?: string | { id?: string } | null;
  metadata?: Record<string, string>;
  payment_status?: string;
  status?: string;
}

interface StripeSubscription {
  id: string;
  customer?: string | { id?: string } | null;
  status: string;
}

interface ActivationCodeRecord {
  status: "active" | "canceled" | "past_due";
  proxyToken?: string;
  stripeCustomerId?: string;
  stripeSubscriptionId?: string;
  createdAt?: number;
  updatedAt?: number;
}

interface PremiumSessionRecord {
  status: "active" | "canceled" | "past_due";
  activationCode: string;
  stripeCustomerId?: string;
  stripeSubscriptionId?: string;
  createdAt: number;
  expiresAt: number;
}

interface KVNamespace {
  get<T = string>(key: string, type: "json"): Promise<T | null>;
  get(key: string): Promise<string | null>;
  put(key: string, value: string, options?: { expirationTtl?: number }): Promise<void>;
}
